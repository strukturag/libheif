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
#include "sequences/seq_boxes.h"
#include "sequences/chunk.h"
#include "sequences/track_visual.h"
#include "sequences/track_metadata.h"
#include "api_structs.h"
#include <limits>


TrackOptions& TrackOptions::operator=(const TrackOptions& src)
{
  if (&src == this) {
    return *this;
  }

  this->track_timescale = src.track_timescale;
  this->write_sample_aux_infos_interleaved = src.write_sample_aux_infos_interleaved;
  this->with_sample_tai_timestamps = src.with_sample_tai_timestamps;

  if (src.tai_clock_info) {
    this->tai_clock_info = heif_tai_clock_info_alloc();
    heif_tai_clock_info_copy(this->tai_clock_info, src.tai_clock_info);
  }
  else {
    this->tai_clock_info = nullptr;
  }

  this->with_sample_content_ids = src.with_sample_content_ids;
  this->gimi_track_content_id = src.gimi_track_content_id;

  return *this;
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


heif_sample_aux_info_type SampleAuxInfoReader::get_type() const
{
  heif_sample_aux_info_type type;
  type.type = m_saiz->get_aux_info_type();
  type.parameter = m_saiz->get_aux_info_type_parameter();
  return type;
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

  m_trak = trak_box;

  auto tkhd = trak_box->get_child_box<Box_tkhd>();
  if (!tkhd) {
    return; // TODO: error or dummy error track ?
  }

  m_id = tkhd->get_track_id();

  auto mdia = trak_box->get_child_box<Box_mdia>();
  if (!mdia) {
    return;
  }

  m_tref = trak_box->get_child_box<Box_tref>();

  auto hdlr = mdia->get_child_box<Box_hdlr>();
  if (!hdlr) {
    return;
  }

  m_handler_type = hdlr->get_handler_type();

  m_minf = mdia->get_child_box<Box_minf>();
  if (!m_minf) {
    return;
  }

  m_mdhd = mdia->get_child_box<Box_mdhd>();
  if (!m_mdhd) {
    return;
  }

  auto stbl = m_minf->get_child_box<Box_stbl>();
  if (!stbl) {
    return;
  }

  m_stsd = stbl->get_child_box<Box_stsd>();
  if (!m_stsd) {
    return;
  }

  m_stsc = stbl->get_child_box<Box_stsc>();
  if (!m_stsc) {
    return;
  }

  m_stco = stbl->get_child_box<Box_stco>();
  if (!m_stco) {
    return;
  }

  m_stsz = stbl->get_child_box<Box_stsz>();
  if (!m_stsz) {
    return;
  }

  m_stts = stbl->get_child_box<Box_stts>();

  const std::vector<uint32_t>& chunk_offsets = m_stco->get_offsets();
  assert(chunk_offsets.size() <= (size_t) std::numeric_limits<uint32_t>::max()); // There cannot be more than uint32_t chunks.

  uint32_t current_sample_idx = 0;

  for (size_t chunk_idx = 0; chunk_idx < chunk_offsets.size(); chunk_idx++) {
    auto* s2c = m_stsc->get_chunk(static_cast<uint32_t>(chunk_idx + 1));
    if (!s2c) {
      return;
    }

    Box_stsc::SampleToChunk sampleToChunk = *s2c;

    auto sample_description = m_stsd->get_sample_entry(sampleToChunk.sample_description_index - 1);
    if (!sample_description) {
      return;
    }

    if (m_first_taic == nullptr) {
      auto taic = sample_description->get_child_box<Box_taic>();
      if (taic) {
        m_first_taic = taic;
      }
    }

    auto chunk = std::make_shared<Chunk>(ctx, m_id, sample_description,
                                         current_sample_idx, sampleToChunk.samples_per_chunk,
                                         m_stco->get_offsets()[chunk_idx],
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

  // --- read track properties

  if (auto meta = trak_box->get_child_box<Box_meta>()) {
    auto iloc = meta->get_child_box<Box_iloc>();
    auto idat = meta->get_child_box<Box_idat>();

    auto iinf = meta->get_child_box<Box_iinf>();
    if (iinf) {
      auto infe_boxes = iinf->get_child_boxes<Box_infe>();
      for (const auto& box : infe_boxes) {
        if (box->get_item_type_4cc() == fourcc("uri ") &&
            box->get_item_uri_type() == "urn:uuid:15beb8e4-944d-5fc6-a3dd-cb5a7e655c73") {
          heif_item_id id = box->get_item_ID();

          std::vector<uint8_t> data;
          Error err = iloc->read_data(id, ctx->get_heif_file()->get_reader(), idat, &data, ctx->get_security_limits());
          if (err) {
            // TODO
          }

          Result contentIdResult = vector_to_string(data);
          if (contentIdResult.error) {
            // TODO
          }

          m_track_info.gimi_track_content_id = contentIdResult.value;
        }
      }
    }
  }
}


Track::Track(HeifContext* ctx, uint32_t track_id, const TrackOptions* options, uint32_t handler_type)
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

    m_id = track_id;
  }

  m_trak = std::make_shared<Box_trak>();
  m_moov->append_child_box(m_trak);

  m_tkhd = std::make_shared<Box_tkhd>();
  m_trak->append_child_box(m_tkhd);
  m_tkhd->set_track_id(track_id);

  auto mdia = std::make_shared<Box_mdia>();
  m_trak->append_child_box(mdia);

  m_mdhd = std::make_shared<Box_mdhd>();
  m_mdhd->set_timescale(options ? options->track_timescale : 90000);
  mdia->append_child_box(m_mdhd);

  m_hdlr = std::make_shared<Box_hdlr>();
  mdia->append_child_box(m_hdlr);
  m_hdlr->set_handler_type(handler_type);

  m_minf = std::make_shared<Box_minf>();
  mdia->append_child_box(m_minf);

  // vmhd is added in Track_Visual

  m_stbl = std::make_shared<Box_stbl>();
  m_minf->append_child_box(m_stbl);

  m_stsd = std::make_shared<Box_stsd>();
  m_stbl->append_child_box(m_stsd);

  m_stts = std::make_shared<Box_stts>();
  m_stbl->append_child_box(m_stts);

  m_stsc = std::make_shared<Box_stsc>();
  m_stbl->append_child_box(m_stsc);

  m_stsz = std::make_shared<Box_stsz>();
  m_stbl->append_child_box(m_stsz);

  m_stco = std::make_shared<Box_stco>();
  m_stbl->append_child_box(m_stco);

  m_stss = std::make_shared<Box_stss>();
  m_stbl->append_child_box(m_stss);

  if (options) {
    m_track_info = *options;

    if (m_track_info.with_sample_tai_timestamps != heif_sample_aux_info_presence_none) {
      m_aux_helper_tai_timestamps = std::make_unique<SampleAuxInfoHelper>(m_track_info.write_sample_aux_infos_interleaved);
      m_aux_helper_tai_timestamps->set_aux_info_type(fourcc("stai"));
    }

    if (m_track_info.with_sample_content_ids != heif_sample_aux_info_presence_none) {
      m_aux_helper_content_ids = std::make_unique<SampleAuxInfoHelper>(m_track_info.write_sample_aux_infos_interleaved);
      m_aux_helper_content_ids->set_aux_info_type(fourcc("suid"));
    }

    if (!options->gimi_track_content_id.empty()) {
      auto hdlr_box = std::make_shared<Box_hdlr>();
      hdlr_box->set_handler_type(fourcc("meta"));

      auto uuid_box = std::make_shared<Box_infe>();
      uuid_box->set_item_type_4cc(fourcc("uri "));
      uuid_box->set_item_uri_type("urn:uuid:15beb8e4-944d-5fc6-a3dd-cb5a7e655c73");
      uuid_box->set_item_ID(1);

      auto iinf_box = std::make_shared<Box_iinf>();
      iinf_box->append_child_box(uuid_box);

      std::vector<uint8_t> track_uuid_vector;
      track_uuid_vector.insert(track_uuid_vector.begin(),
                               options->gimi_track_content_id.c_str(),
                               options->gimi_track_content_id.c_str() + options->gimi_track_content_id.length() + 1);

      auto iloc_box = std::make_shared<Box_iloc>();
      iloc_box->append_data(1, track_uuid_vector, 1);

      auto meta_box = std::make_shared<Box_meta>();
      meta_box->append_child_box(hdlr_box);
      meta_box->append_child_box(iinf_box);
      meta_box->append_child_box(iloc_box);

      m_trak->append_child_box(meta_box);
    }
  }
}


std::shared_ptr<Track> Track::alloc_track(HeifContext* ctx, const std::shared_ptr<Box_trak>& trak)
{
  auto mdia = trak->get_child_box<Box_mdia>();
  if (!mdia) {
    return nullptr;
  }

  auto hdlr = mdia->get_child_box<Box_hdlr>();
  if (!mdia) {
    return nullptr;
  }

  switch (hdlr->get_handler_type()) {
    case fourcc("pict"):
    case fourcc("vide"):
      return std::make_shared<Track_Visual>(ctx, trak);
    case fourcc("meta"):
      return std::make_shared<Track_Metadata>(ctx, trak);
    default:
      return nullptr;
  }
}


bool Track::is_visual_track() const
{
  return m_handler_type == fourcc("pict");
}


uint32_t Track::get_first_cluster_sample_entry_type() const
{
  if (m_stsd->get_num_sample_entries() == 0) {
    return 0; // TODO: error ? Or can we assume at this point that there is at least one sample entry?
  }

  return m_stsd->get_sample_entry(0)->get_short_type();
}


Result<std::string> Track::get_first_cluster_urim_uri() const
{
  if (m_stsd->get_num_sample_entries() == 0) {
    return Error{heif_error_Invalid_input,
                 heif_suberror_Unspecified,
                 "This track has no sample entries."};
  }

  std::shared_ptr<const Box> sampleEntry = m_stsd->get_sample_entry(0);
  auto urim = std::dynamic_pointer_cast<const Box_URIMetaSampleEntry>(sampleEntry);
  if (!urim) {
    return Error{heif_error_Usage_error,
                 heif_suberror_Unspecified,
                 "This cluster is no 'urim' sample entry."};
  }

  std::shared_ptr<const Box_uri> uri = urim->get_child_box<const Box_uri>();
  if (!uri) {
    return Error{heif_error_Invalid_input,
                 heif_suberror_Unspecified,
                 "The 'urim' box has no 'uri' child box."};
  }

  return uri->get_uri();
}


bool Track::end_of_sequence_reached() const
{
  return (m_next_sample_to_be_processed > m_chunks.back()->last_sample_number());
}


void Track::finalize_track()
{
  if (m_aux_helper_tai_timestamps) m_aux_helper_tai_timestamps->write_all(m_stbl, get_file());
  if (m_aux_helper_content_ids) m_aux_helper_content_ids->write_all(m_stbl, get_file());

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


void Track::add_chunk(heif_compression_format format)
{
  auto chunk = std::make_shared<Chunk>(m_heif_context, m_id, format);
  m_chunks.push_back(chunk);

  int chunkIdx = (uint32_t) m_chunks.size();
  m_stsc->add_chunk(chunkIdx);
}

void Track::set_sample_description_box(std::shared_ptr<Box> sample_description_box)
{
  // --- add 'taic' when we store timestamps as sample auxiliary information

  if (m_track_info.with_sample_tai_timestamps != heif_sample_aux_info_presence_none) {
    auto taic = std::make_shared<Box_taic>();
    taic->set_from_tai_clock_info(m_track_info.tai_clock_info);
    sample_description_box->append_child_box(taic);
  }

  m_stsd->add_sample_entry(sample_description_box);
}


Error Track::write_sample_data(const std::vector<uint8_t>& raw_data, uint32_t sample_duration, bool is_sync_sample,
                               const heif_tai_timestamp_packet* tai, const std::string& gimi_contentID)
{
  size_t data_start = m_heif_context->get_heif_file()->append_mdat_data(raw_data);

  // first sample in chunk? -> write chunk offset

  if (m_stsc->last_chunk_empty()) {
    // if auxiliary data is interleaved, write it between the chunks
    if (m_aux_helper_tai_timestamps) m_aux_helper_tai_timestamps->write_interleaved(get_file());
    if (m_aux_helper_content_ids) m_aux_helper_content_ids->write_interleaved(get_file());

    // TODO
    assert(data_start < 0xFF000000); // add some headroom for header data
    m_stco->add_chunk_offset(static_cast<uint32_t>(data_start));
  }

  m_stsc->increase_samples_in_chunk(1);

  m_stsz->append_sample_size((uint32_t)raw_data.size());

  if (is_sync_sample) {
    m_stss->add_sync_sample(m_next_sample_to_be_processed + 1);
  }

  if (sample_duration == 0) {
    return {heif_error_Usage_error,
            heif_suberror_Unspecified,
            "Sample duration may not be 0"};
  }

  m_stts->append_sample_duration(sample_duration);


  // --- sample timestamp

  if (m_track_info.with_sample_tai_timestamps != heif_sample_aux_info_presence_none) {
    if (tai) {
      std::vector<uint8_t> tai_data = Box_itai::encode_tai_to_bitstream(tai);
      auto err = m_aux_helper_tai_timestamps->add_sample_info(tai_data);
      if (err) {
        return err;
      }
    }
    else if (m_track_info.with_sample_tai_timestamps == heif_sample_aux_info_presence_optional) {
      m_aux_helper_tai_timestamps->add_nonpresent_sample();
    }
    else {
      return {heif_error_Encoding_error,
              heif_suberror_Unspecified,
              "Mandatory TAI timestamp missing"};
    }
  }

  // --- sample content id

  if (m_track_info.with_sample_content_ids != heif_sample_aux_info_presence_none) {
    if (!gimi_contentID.empty()) {
      auto id = gimi_contentID;
      const char* id_str = id.c_str();
      std::vector<uint8_t> id_vector;
      id_vector.insert(id_vector.begin(), id_str, id_str + id.length() + 1);
      auto err = m_aux_helper_content_ids->add_sample_info(id_vector);
      if (err) {
        return err;
      }
    } else if (m_track_info.with_sample_content_ids == heif_sample_aux_info_presence_optional) {
      m_aux_helper_content_ids->add_nonpresent_sample();
    } else {
      return {heif_error_Encoding_error,
              heif_suberror_Unspecified,
              "Mandatory ContentID missing"};
    }
  }

  m_next_sample_to_be_processed++;

  return Error::Ok;
}


void Track::add_reference_to_track(uint32_t referenceType, uint32_t to_track_id)
{
  if (!m_tref) {
    m_tref = std::make_shared<Box_tref>();
    m_trak->append_child_box(m_tref);
  }

  m_tref->add_references(to_track_id, referenceType);
}


Result<heif_raw_sequence_sample*> Track::get_next_sample_raw_data()
{
  if (m_current_chunk > m_chunks.size()) {
    return Error{heif_error_End_of_sequence,
                 heif_suberror_Unspecified,
                 "End of sequence"};
  }

  while (m_next_sample_to_be_processed > m_chunks[m_current_chunk]->last_sample_number()) {
    m_current_chunk++;

    if (m_current_chunk > m_chunks.size()) {
      return Error{heif_error_End_of_sequence,
                   heif_suberror_Unspecified,
                   "End of sequence"};
    }
  }

  const std::shared_ptr<Chunk>& chunk = m_chunks[m_current_chunk];

  DataExtent extent = chunk->get_data_extent_for_sample(m_next_sample_to_be_processed);
  auto readResult = extent.read_data();
  if (readResult.error) {
    return readResult.error;
  }

  heif_raw_sequence_sample* sample = new heif_raw_sequence_sample();
  sample->data = *readResult.value;

  // read sample duration

  if (m_stts) {
    sample->duration = m_stts->get_sample_duration(m_next_sample_to_be_processed);
  }

  // --- read sample auxiliary data

  if (m_aux_reader_content_ids) {
    auto readResult = m_aux_reader_content_ids->get_sample_info(get_file().get(), m_next_sample_to_be_processed);
    if (readResult.error) {
      return readResult.error;
    }

    if (!readResult.value.empty()) {
      Result<std::string> convResult = vector_to_string(readResult.value);
      if (convResult.error) {
        return convResult.error;
      }

      sample->gimi_sample_content_id = convResult.value;
    }
  }

  if (m_aux_reader_tai_timestamps) {
    auto readResult = m_aux_reader_tai_timestamps->get_sample_info(get_file().get(), m_next_sample_to_be_processed);
    if (readResult.error) {
      return readResult.error;
    }

    if (!readResult.value.empty()) {
      auto resultTai = Box_itai::decode_tai_from_vector(readResult.value);
      if (resultTai.error) {
        return resultTai.error;
      }

      sample->timestamp = heif_tai_timestamp_packet_alloc();
      heif_tai_timestamp_packet_copy(sample->timestamp, &resultTai.value);
    }
  }

  m_next_sample_to_be_processed++;

  return sample;
}


std::vector<heif_sample_aux_info_type> Track::get_sample_aux_info_types() const
{
  std::vector<heif_sample_aux_info_type> types;

  if (m_aux_reader_tai_timestamps) types.emplace_back(m_aux_reader_tai_timestamps->get_type());
  if (m_aux_reader_content_ids) types.emplace_back(m_aux_reader_content_ids->get_type());

  return types;
}
