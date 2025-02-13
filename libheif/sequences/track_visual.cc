/*
 * HEIF image base codec.
 * Copyright (c) 2025 Dirk Farin <dirk.farin@gmail.com>
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

#include "track_visual.h"
#include "codecs/decoder.h"
#include "codecs/encoder.h"
#include "chunk.h"
#include "pixelimage.h"
#include "context.h"
#include "libheif/api_structs.h"


Track_Visual::Track_Visual(HeifContext* ctx, const std::shared_ptr<Box_trak>& trak)
    : Track(ctx, trak)
{
  const std::vector<uint32_t>& chunk_offsets = m_stco->get_offsets();

  // Find sequence resolution

  if (!chunk_offsets.empty())  {
    auto* s2c = m_stsc->get_chunk(static_cast<uint32_t>(0));
    if (!s2c) {
      return;
    }

    Box_stsc::SampleToChunk sampleToChunk = *s2c;

    auto sample_description = m_stsd->get_sample_entry(sampleToChunk.sample_description_index - 1);
    if (!sample_description) {
      return;
    }

    m_width = sample_description->get_VisualSampleEntry_const().width;
    m_height = sample_description->get_VisualSampleEntry_const().height;
  }
}


Track_Visual::Track_Visual(HeifContext* ctx, uint32_t track_id, uint16_t width, uint16_t height,
    heif_track_info* info)
    : Track(ctx, track_id, info)
{
    m_tkhd->set_resolution(width, height);
    m_hdlr->set_handler_type(fourcc("pict"));
}


Result<std::shared_ptr<HeifPixelImage>> Track_Visual::decode_next_image_sample(const struct heif_decoding_options& options)
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

    auto resultTai = Box_itai::decode_tai_from_vector(readResult.value);
    if (resultTai.error) {
      return resultTai.error;
    }

    image->set_tai_timestamp(&resultTai.value);
  }

  m_next_sample_to_be_decoded++;

  return image;
}


Error Track_Visual::encode_image(std::shared_ptr<HeifPixelImage> image,
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
