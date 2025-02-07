/*
 * HEIF image base codec.
 * Copyright (c) 2024 Dirk Farin <dirk.farin@gmail.com>
 *
 * This file is part of libheif.
 *
 * libheif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libheif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libheif.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstring>
#include "track.h"
#include "context.h"
#include "codecs/decoder.h"
#include "codecs/encoder.h"
#include "sequences/seq_boxes.h"
#include "sequences/chunk.h"
#include "libheif/api_structs.h"

heif_tai_clock_info* heif_tai_clock_info_alloc()
{
  auto* taic = new heif_tai_clock_info;
  taic->version = 1;

  taic->time_uncertainty = 0; // TODO
  taic->clock_resolution = 0; // TODO
  taic->clock_drift_rate = 0; // TODO
  taic->clock_type = 0; // TODO

  return taic;
}

void heif_tai_clock_info_copy(heif_tai_clock_info* dst, const heif_tai_clock_info* src)
{
  if (dst->version >= 1 && src->version >= 1) {
    dst->time_uncertainty = src->time_uncertainty;
    dst->clock_resolution = src->clock_resolution;
    dst->clock_drift_rate = src->clock_drift_rate;
    dst->clock_type = src->clock_type;
  }

  // in the future when copying with "src->version > dst->version",
  // the remaining dst fields have to be filled with defaults
}

void heif_tai_clock_info_release(heif_tai_clock_info* info)
{
  delete info;
}


void heif_track_info_copy(heif_track_info* dst, const heif_track_info* src)
{
  if (src->version >= 1 && dst->version >= 1) {
    dst->timescale = src->timescale;
    dst->write_aux_info_interleaved = src->write_aux_info_interleaved;
    dst->with_tai_timestamps = src->with_tai_timestamps;

    if (src->tai_clock_info) {
      dst->tai_clock_info = heif_tai_clock_info_alloc();
      heif_tai_clock_info_copy(dst->tai_clock_info, src->tai_clock_info);
    }

    dst->with_sample_contentid_uuids = src->with_sample_contentid_uuids;

    dst->with_gimi_track_contentID = src->with_gimi_track_contentID;
    if (src->with_gimi_track_contentID && src->gimi_track_contentID) {
      char* dst_id = new char[strlen(src->gimi_track_contentID) + 1];
      strcpy(dst_id, src->gimi_track_contentID);
      dst->gimi_track_contentID = dst_id;
    }
    else {
      dst->gimi_track_contentID = nullptr;
    }
  }
}


heif_track_info* heif_track_info_alloc()
{
  auto* info = new heif_track_info;
  info->version = 1;

  info->timescale = 90000;
  info->write_aux_info_interleaved = false;
  info->with_tai_timestamps = heif_sample_aux_info_presence_none;
  info->tai_clock_info = nullptr;
  info->with_sample_contentid_uuids = heif_sample_aux_info_presence_none;
  info->with_gimi_track_contentID = false;
  info->gimi_track_contentID = nullptr;

  return info;
}


void heif_track_info_release(struct heif_track_info* info)
{
  if (info) {
    heif_tai_clock_info_release(info->tai_clock_info);
    delete[] info->gimi_track_contentID;

    delete info;
  }
}


SampleAuxInfoHelper::SampleAuxInfoHelper(bool interleaved)
    : m_interleaved(interleaved)
{
  m_saiz = std::make_shared<Box_saiz>();
  m_saio = std::make_shared<Box_saio>();
}


void SampleAuxInfoHelper::set_aux_info_type(uint32_t aux_info_type, uint32_t aux_info_type_parameter)
{
  m_saiz->set_aux_info_type(aux_info_type, aux_info_type_parameter);
  m_saio->set_aux_info_type(aux_info_type, aux_info_type_parameter);
}

Error SampleAuxInfoHelper::add_sample_info(const std::vector<uint8_t>& data)
{
  if (data.size() > 0xFF) {
    return {heif_error_Encoding_error,
            heif_suberror_Unspecified,
            "Encoded sample auxiliary information exceeds maximum size"};
  }

  m_saiz->add_sample_size(static_cast<uint8_t>(data.size()));

  m_data.insert(m_data.end(), data.begin(), data.end());

  return Error::Ok;
}

void SampleAuxInfoHelper::add_nonpresent_sample()
{
  m_saiz->add_nonpresent_sample();
}


void SampleAuxInfoHelper::write_interleaved(const std::shared_ptr<class HeifFile>& file)
{
  if (m_interleaved && !m_data.empty()) {
    uint64_t pos = file->append_mdat_data(m_data);
    m_saio->add_sample_offset(pos);

    m_data.clear();
  }
}

void SampleAuxInfoHelper::write_all(const std::shared_ptr<class Box>& parent, const std::shared_ptr<class HeifFile>& file)
{
  parent->append_child_box(m_saiz);
  parent->append_child_box(m_saio);

  if (!m_data.empty()) {
    uint64_t pos = file->append_mdat_data(m_data);
    m_saio->add_sample_offset(pos);
  }
}


SampleAuxInfoReader::SampleAuxInfoReader(std::shared_ptr<Box_saiz> saiz,
                                         std::shared_ptr<Box_saio> saio)
{
  m_saiz = saiz;
  m_saio = saio;

  m_contiguous = (saio->get_num_samples() == 1);
  if (m_contiguous) {
    uint64_t offset = saio->get_sample_offset(0);
    auto nSamples = saiz->get_num_samples();

    for (uint32_t i=0;i<nSamples;i++) {
      m_contiguous_offsets.push_back(offset);
      offset += saiz->get_sample_size(i);
    }

    // TODO: we could add a special case for contiguous data with constant size
  }
}


Result<std::vector<uint8_t>> SampleAuxInfoReader::get_sample_info(const HeifFile* file, uint32_t idx)
{
  uint64_t offset;
  if (m_contiguous) {
    offset = m_contiguous_offsets[idx];
  }
  else {
    offset = m_saio->get_sample_offset(idx);
  }

  uint8_t size = m_saiz->get_sample_size(idx);

  std::vector<uint8_t> data;
  Error err = file->append_data_from_file_range(data, offset, size);
  if (err) {
    return err;
  }

  return data;
}


std::shared_ptr<class HeifFile> Track::get_file() const
{
  return m_heif_context->get_heif_file();
}


Track::Track(HeifContext* ctx, const std::shared_ptr<Box_trak>& trak_box)
{
  m_heif_context = ctx;

  auto tkhd = trak_box->get_child_box<Box_tkhd>();
  if (!tkhd) {
    return; // TODO: error or dummy error track ?
  }

  m_id = tkhd->get_track_id();

  auto mdia = trak_box->get_child_box<Box_mdia>();
  if (!mdia) {
    return;
  }

  auto hdlr = mdia->get_child_box<Box_hdlr>();
  if (!hdlr) {
    return;
  }

  m_handler_type = hdlr->get_handler_type();

  auto minf = mdia->get_child_box<Box_minf>();
  if (!minf) {
    return;
  }

  auto stbl = minf->get_child_box<Box_stbl>();
  if (!stbl) {
    return;
  }

  auto stsd = stbl->get_child_box<Box_stsd>();
  if (!stsd) {
    return;
  }

  auto stsc = stbl->get_child_box<Box_stsc>();
  if (!stsc) {
    return;
  }

  auto stco = stbl->get_child_box<Box_stco>();
  if (!stco) {
    return;
  }

  m_stsz = stbl->get_child_box<Box_stsz>();
  if (!m_stsz) {
    return;
  }

  m_stts = stbl->get_child_box<Box_stts>();

  const std::vector<uint32_t>& chunk_offsets = stco->get_offsets();
  assert(chunk_offsets.size() <= (size_t) std::numeric_limits<uint32_t>::max()); // There cannot be more than uint32_t chunks.

  uint32_t current_sample_idx = 0;

  for (size_t chunk_idx = 0; chunk_idx < chunk_offsets.size(); chunk_idx++) {
    auto* s2c = stsc->get_chunk(static_cast<uint32_t>(chunk_idx + 1));
    if (!s2c) {
      return;
    }

    Box_stsc::SampleToChunk sampleToChunk = *s2c;

    auto sample_description = stsd->get_sample_entry(sampleToChunk.sample_description_index - 1);
    if (!sample_description) {
      return;
    }

    m_width = sample_description->get_VisualSampleEntry_const().width;
    m_height = sample_description->get_VisualSampleEntry_const().height;

    auto chunk = std::make_shared<Chunk>(ctx, m_id, sample_description,
                                         current_sample_idx, sampleToChunk.samples_per_chunk,
                                         stco->get_offsets()[chunk_idx],
                                         m_stsz);

    m_chunks.push_back(chunk);

    current_sample_idx += sampleToChunk.samples_per_chunk;
  }

  // --- read sample auxiliary information boxes

  std::vector<std::shared_ptr<Box_saiz>> saiz_boxes = stbl->get_child_boxes<Box_saiz>();
  std::vector<std::shared_ptr<Box_saio>> saio_boxes = stbl->get_child_boxes<Box_saio>();

  for (const auto& saiz : saiz_boxes) {
    uint32_t aux_info_type = saiz->get_aux_info_type();
    uint32_t aux_info_type_parameter = saiz->get_aux_info_type_parameter();

    // find the corresponding saio box

    std::shared_ptr<Box_saio> saio;
    for (const auto& candidate : saio_boxes) {
      if (candidate->get_aux_info_type() == aux_info_type &&
          candidate->get_aux_info_type_parameter() == aux_info_type_parameter) {
        saio = candidate;
        break;
      }
    }

    if (saio) {
      if (aux_info_type == fourcc("suid")) {
        m_aux_reader_content_ids = std::make_unique<SampleAuxInfoReader>(saiz, saio);
      }

      if (aux_info_type == fourcc("stai")) {
        m_aux_reader_tai_timestamps = std::make_unique<SampleAuxInfoReader>(saiz, saio);
      }
    }
  }
}


Track::~Track()
{
  delete m_track_info;
}


Track::Track(HeifContext* ctx, uint32_t track_id, uint16_t width, uint16_t height,
             heif_track_info* info)
{
  m_heif_context = ctx;

  m_moov = ctx->get_heif_file()->get_moov_box();
  assert(m_moov);

  // --- find next free track ID

  if (track_id == 0) {
    track_id = 1; // minimum track ID

    for (const auto& track : m_moov->get_child_boxes<Box_trak>()) {
      auto tkhd = track->get_child_box<Box_tkhd>();

      if (tkhd->get_track_id() >= track_id) {
        track_id = tkhd->get_track_id() + 1;
      }
    }

    auto mvhd = m_moov->get_child_box<Box_mvhd>();
    mvhd->set_next_track_id(track_id + 1);
  }

  auto trak = std::make_shared<Box_trak>();
  m_moov->append_child_box(trak);

  m_tkhd = std::make_shared<Box_tkhd>();
  trak->append_child_box(m_tkhd);
  m_tkhd->set_track_id(track_id);
  m_tkhd->set_resolution(width, height);

  auto mdia = std::make_shared<Box_mdia>();
  trak->append_child_box(mdia);

  m_mdhd = std::make_shared<Box_mdhd>();
  m_mdhd->set_timescale(info->timescale);
  mdia->append_child_box(m_mdhd);

  auto hdlr = std::make_shared<Box_hdlr>();
  mdia->append_child_box(hdlr);
  hdlr->set_handler_type(fourcc("pict"));

  auto minf = std::make_shared<Box_minf>();
  mdia->append_child_box(minf);

  auto vmhd = std::make_shared<Box_vmhd>();
  minf->append_child_box(vmhd);

  m_stbl = std::make_shared<Box_stbl>();
  minf->append_child_box(m_stbl);

  m_stsd = std::make_shared<Box_stsd>();
  m_stbl->append_child_box(m_stsd);

  auto stts = std::make_shared<Box_stts>();
  m_stbl->append_child_box(stts);
  m_stts = stts;

  m_stsc = std::make_shared<Box_stsc>();
  m_stbl->append_child_box(m_stsc);

  m_stsz = std::make_shared<Box_stsz>();
  m_stbl->append_child_box(m_stsz);

  m_stco = std::make_shared<Box_stco>();
  m_stbl->append_child_box(m_stco);

  m_stss = std::make_shared<Box_stss>();
  m_stbl->append_child_box(m_stss);

  if (info) {
    m_track_info = heif_track_info_alloc();
    heif_track_info_copy(m_track_info, info);

    if (m_track_info->with_tai_timestamps != heif_sample_aux_info_presence_none) {
      m_aux_helper_tai_timestamps = std::make_unique<SampleAuxInfoHelper>(m_track_info->write_aux_info_interleaved);
      m_aux_helper_tai_timestamps->set_aux_info_type(fourcc("stai"));
    }

    if (m_track_info->with_sample_contentid_uuids != heif_sample_aux_info_presence_none) {
      m_aux_helper_content_ids = std::make_unique<SampleAuxInfoHelper>(m_track_info->write_aux_info_interleaved);
      m_aux_helper_content_ids->set_aux_info_type(fourcc("suid"));
    }

    if (info->with_gimi_track_contentID) {
      auto hdlr_box = std::make_shared<Box_hdlr>();
      hdlr_box->set_handler_type(fourcc("meta"));

      auto uuid_box = std::make_shared<Box_infe>();
      uuid_box->set_item_type_4cc(fourcc("uri "));
      uuid_box->set_item_uri_type("urn:uuid:15beb8e4-944d-5fc6-a3dd-cb5a7e655c73");
      uuid_box->set_item_ID(1);

      std::vector<uint8_t> track_uuid_vector;
      track_uuid_vector.insert(track_uuid_vector.begin(),
                               info->gimi_track_contentID,
                               info->gimi_track_contentID + strlen(info->gimi_track_contentID) + 1);

      auto iloc_box = std::make_shared<Box_iloc>();
      iloc_box->append_data(1, track_uuid_vector, 1);

      auto meta_box = std::make_shared<Box_meta>();
      meta_box->append_child_box(hdlr_box);
      meta_box->append_child_box(uuid_box);
      meta_box->append_child_box(iloc_box);

      trak->append_child_box(meta_box);
    }
  }
}


bool Track::is_visual_track() const
{
  return m_handler_type == fourcc("pict");
}


bool Track::end_of_sequence_reached() const
{
  return (m_next_sample_to_be_decoded > m_chunks.back()->last_sample_number());
}


static Result<std::string> vector_to_string(const std::vector<uint8_t>& vec)
{
  if (vec.empty()) {
    return Error{heif_error_Invalid_input,
                 heif_suberror_Unspecified,
                 "Null length string"};
  }

  if (vec.back() != 0) {
    return Error{heif_error_Invalid_input,
                 heif_suberror_Unspecified,
                 "utf8string not null-terminated"};
  }

  for (size_t i=0;i<vec.size()-1;i++) {
    if (vec[i] == 0) {
      return Error{heif_error_Invalid_input,
                   heif_suberror_Unspecified,
                   "utf8string with null character"};
    }
  }

  return std::string(vec.begin(), vec.end()-1);
}


Result<std::shared_ptr<HeifPixelImage>> Track::decode_next_image_sample(const struct heif_decoding_options& options)
{
  if (m_current_chunk > m_chunks.size()) {
    return Error{heif_error_End_of_sequence,
                 heif_suberror_Unspecified,
                 "End of sequence"};
  }

  while (m_next_sample_to_be_decoded > m_chunks[m_current_chunk]->last_sample_number()) {
    m_current_chunk++;

    if (m_current_chunk > m_chunks.size()) {
      return Error{heif_error_End_of_sequence,
                   heif_suberror_Unspecified,
                   "End of sequence"};
    }
  }

  const std::shared_ptr<Chunk>& chunk = m_chunks[m_current_chunk];

  auto decoder = chunk->get_decoder();
  assert(decoder);

  decoder->set_data_extent(chunk->get_data_extent_for_sample(m_next_sample_to_be_decoded));

  Result<std::shared_ptr<HeifPixelImage>> decodingResult = decoder->decode_single_frame_from_compressed_data(options);
  if (decodingResult.error) {
    m_next_sample_to_be_decoded++;
    return decodingResult.error;
  }

  auto image = decodingResult.value;

  if (m_stts) {
    image->set_sample_duration(m_stts->get_sample_duration(m_next_sample_to_be_decoded));
  }

  // --- read sample auxiliary data

  if (m_aux_reader_content_ids) {
    auto readResult = m_aux_reader_content_ids->get_sample_info(get_file().get(), m_next_sample_to_be_decoded);
    if (readResult.error) {
      return readResult.error;
    }

    Result<std::string> convResult = vector_to_string(readResult.value);
    if (convResult.error) {
      return convResult.error;
    }

    image->set_gimi_content_id(convResult.value);
  }

  if (m_aux_reader_tai_timestamps) {
    auto readResult = m_aux_reader_tai_timestamps->get_sample_info(get_file().get(), m_next_sample_to_be_decoded);
    if (readResult.error) {
      return readResult.error;
    }
  }

  m_next_sample_to_be_decoded++;

  return image;
}


Error Track::encode_image(std::shared_ptr<HeifPixelImage> image,
                          struct heif_encoder* h_encoder,
                          const struct heif_encoding_options& in_options,
                          heif_image_input_class input_class)
{
  if (image->get_width() > 0xFFFF ||
      image->get_height() > 0xFFFF) {
    return {heif_error_Invalid_input,
            heif_suberror_Unspecified,
            "Input image resolution too high"};
  }

  // === generate compressed image bitstream

  // generate new chunk for first image or when compression formats don't match

  bool add_sample_description = false;
  bool new_chunk = false;

  if (m_chunks.empty() || m_chunks.back()->get_compression_format() != h_encoder->plugin->compression_format) {
    auto chunk = std::make_shared<Chunk>(m_heif_context, m_id, h_encoder->plugin->compression_format);
    m_chunks.push_back(chunk);
    add_sample_description = true;
    new_chunk = true;
  }

  auto encoder = m_chunks.back()->get_encoder();

  // --- check whether we have to convert the image color space

  // The reason for doing the color conversion here is that the input might be an RGBA image and the color conversion
  // will extract the alpha plane anyway. We can reuse that plane below instead of having to do a new conversion.

  heif_encoding_options options = in_options;

  if (const auto* nclx = encoder->get_forced_output_nclx()) {
    options.output_nclx_profile = const_cast<heif_color_profile_nclx*>(nclx);
  }

  Result<std::shared_ptr<HeifPixelImage>> srcImageResult = encoder->convert_colorspace_for_encoding(image,
                                                                                                    h_encoder,
                                                                                                    options,
                                                                                                    m_heif_context->get_security_limits());
  if (srcImageResult.error) {
    return srcImageResult.error;
  }

  std::shared_ptr<HeifPixelImage> colorConvertedImage = srcImageResult.value;

  // --- encode image

  Result<Encoder::CodedImageData> encodeResult = encoder->encode(colorConvertedImage, h_encoder, options, input_class);
  if (encodeResult.error) {
    return encodeResult.error;
  }

  const Encoder::CodedImageData& data = encodeResult.value;

  if (add_sample_description) {
    auto props = data.properties;

    auto sample_description_box = encoder->get_sample_description_box(data);
    VisualSampleEntry& visualSampleEntry = sample_description_box->get_VisualSampleEntry();
    visualSampleEntry.width = static_cast<uint16_t>(colorConvertedImage->get_width());
    visualSampleEntry.height = static_cast<uint16_t>(colorConvertedImage->get_height());
    m_stsd->add_sample_entry(sample_description_box);

    auto ccst = std::make_shared<Box_ccst>();
    ccst->set_coding_constraints(data.codingConstraints);
    sample_description_box->append_child_box(ccst);

    // --- add 'taic' when we store timestamps as sample auxiliary information

    if (m_track_info->with_tai_timestamps != heif_sample_aux_info_presence_none) {
      auto taic = std::make_shared<Box_taic>();
      taic->set_from_tai_clock_info(m_track_info->tai_clock_info);
      sample_description_box->append_child_box(taic);
    }

    m_stsc->add_chunk((uint32_t) m_chunks.size());
  }

  m_stsc->increase_samples_in_chunk(1);

  size_t data_start = m_heif_context->get_heif_file()->append_mdat_data(data.bitstream);

  if (new_chunk) {
    // if auxiliary data is interleaved, write it between the chunks
    m_aux_helper_tai_timestamps->write_interleaved(get_file());
    m_aux_helper_content_ids->write_interleaved(get_file());

    // TODO
    assert(data_start < 0xFF000000); // add some headroom for header data
    m_stco->add_chunk_offset(static_cast<uint32_t>(data_start));
  }

  m_stsz->append_sample_size((uint32_t)data.bitstream.size());

  if (data.is_sync_frame) {
    m_stss->add_sync_sample(m_next_sample_to_be_decoded + 1);
  }

  m_stts->append_sample_duration(colorConvertedImage->get_sample_duration());


  // --- sample timestamp

  if (m_track_info) {
    if (m_track_info->with_tai_timestamps != heif_sample_aux_info_presence_none) {
      const auto* tai = image->get_tai_timestamp();
      if (tai) {
        std::vector<uint8_t> tai_data = Box_itai::encode_tai_to_bitstream(tai);
        auto err = m_aux_helper_tai_timestamps->add_sample_info(tai_data);
        if (err) {
          return err;
        }
      } else if (m_track_info->with_tai_timestamps == heif_sample_aux_info_presence_optional) {
        m_aux_helper_tai_timestamps->add_nonpresent_sample();
      } else {
        return {heif_error_Encoding_error,
                heif_suberror_Unspecified,
                "Mandatory TAI timestamp missing"};
      }
    }

    if (m_track_info->with_sample_contentid_uuids != heif_sample_aux_info_presence_none) {
      if (image->has_gimi_content_id()) {
        auto id = image->get_gimi_content_id();
        const char* id_str = id.c_str();
        std::vector<uint8_t> id_vector;
        id_vector.insert(id_vector.begin(), id_str, id_str + id.length() + 1);
        auto err = m_aux_helper_content_ids->add_sample_info(id_vector);
        if (err) {
          return err;
        }
      } else if (m_track_info->with_sample_contentid_uuids == heif_sample_aux_info_presence_optional) {
        m_aux_helper_content_ids->add_nonpresent_sample();
      } else {
        return {heif_error_Encoding_error,
                heif_suberror_Unspecified,
                "Mandatory ContentID missing"};
      }
    }
  }

  m_next_sample_to_be_decoded++;

  return Error::Ok;
}


void Track::finalize_track()
{
  m_aux_helper_tai_timestamps->write_all(m_stbl, get_file());
  m_aux_helper_content_ids->write_all(m_stbl, get_file());

  uint64_t duration = m_stts->get_total_duration(false);
  m_mdhd->set_duration(duration);
}


uint64_t Track::get_duration_in_media_units() const
{
  return m_mdhd->get_duration();
}


uint32_t Track::get_timescale() const
{
  return m_mdhd->get_timescale();
}


void Track::set_track_duration_in_movie_units(uint64_t total_duration)
{
  m_tkhd->set_duration(total_duration);
}
