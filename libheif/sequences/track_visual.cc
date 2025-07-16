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
#include "api_structs.h"
#include "codecs/hevc_boxes.h"


Track_Visual::Track_Visual(HeifContext* ctx, const std::shared_ptr<Box_trak>& trak)
    : Track(ctx, trak)
{
  const std::vector<uint32_t>& chunk_offsets = m_stco->get_offsets();

  // Find sequence resolution

  if (!chunk_offsets.empty())  {
    auto* s2c = m_stsc->get_chunk(static_cast<uint32_t>(1));
    if (!s2c) {
      return;
    }

    Box_stsc::SampleToChunk sampleToChunk = *s2c;

    auto sample_description = m_stsd->get_sample_entry(sampleToChunk.sample_description_index - 1);
    if (!sample_description) {
      return; // TODO
    }

    auto visual_sample_description = std::dynamic_pointer_cast<const Box_VisualSampleEntry>(sample_description);
    if (!visual_sample_description) {
      return; // TODO
    }

    m_width = visual_sample_description->get_VisualSampleEntry_const().width;
    m_height = visual_sample_description->get_VisualSampleEntry_const().height;
  }
}


Track_Visual::Track_Visual(HeifContext* ctx, uint32_t track_id, uint16_t width, uint16_t height,
                           const TrackOptions* options, uint32_t handler_type)
    : Track(ctx, track_id, options, handler_type)
{
  m_tkhd->set_resolution(width, height);
  //m_hdlr->set_handler_type(handler_type);  already done in Track()

  auto vmhd = std::make_shared<Box_vmhd>();
  m_minf->append_child_box(vmhd);
}


Result<std::shared_ptr<HeifPixelImage>> Track_Visual::decode_next_image_sample(const struct heif_decoding_options& options)
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

  auto decoder = chunk->get_decoder();
  assert(decoder);

  decoder->set_data_extent(chunk->get_data_extent_for_sample(m_next_sample_to_be_processed));

  Result<std::shared_ptr<HeifPixelImage>> decodingResult = decoder->decode_single_frame_from_compressed_data(options,
                                                                                                             m_heif_context->get_security_limits());
  if (decodingResult.error) {
    m_next_sample_to_be_processed++;
    return decodingResult.error;
  }

  auto image = decodingResult.value;

  if (m_stts) {
    image->set_sample_duration(m_stts->get_sample_duration(m_next_sample_to_be_processed));
  }

  // --- read sample auxiliary data

  if (m_aux_reader_content_ids) {
    auto readResult = m_aux_reader_content_ids->get_sample_info(get_file().get(), m_next_sample_to_be_processed);
    if (readResult.error) {
      return readResult.error;
    }

    Result<std::string> convResult = vector_to_string(readResult.value);
    if (convResult.error) {
      return convResult.error;
    }

    image->set_gimi_sample_content_id(convResult.value);
  }

  if (m_aux_reader_tai_timestamps) {
    auto readResult = m_aux_reader_tai_timestamps->get_sample_info(get_file().get(), m_next_sample_to_be_processed);
    if (readResult.error) {
      return readResult.error;
    }

    auto resultTai = Box_itai::decode_tai_from_vector(readResult.value);
    if (resultTai.error) {
      return resultTai.error;
    }

    image->set_tai_timestamp(&resultTai.value);
  }

  m_next_sample_to_be_processed++;

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

  if (m_chunks.empty() || m_chunks.back()->get_compression_format() != h_encoder->plugin->compression_format) {
    add_chunk(h_encoder->plugin->compression_format);
    add_sample_description = true;
  }

  // --- check whether we have to convert the image color space

  // The reason for doing the color conversion here is that the input might be an RGBA image and the color conversion
  // will extract the alpha plane anyway. We can reuse that plane below instead of having to do a new conversion.

  heif_encoding_options options = in_options;

  auto encoder = m_chunks.back()->get_encoder();

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


  // --- generate SampleDescriptionBox

  if (add_sample_description) {
    auto sample_description_box = encoder->get_sample_description_box(data);
    VisualSampleEntry& visualSampleEntry = sample_description_box->get_VisualSampleEntry();
    visualSampleEntry.width = static_cast<uint16_t>(colorConvertedImage->get_width());
    visualSampleEntry.height = static_cast<uint16_t>(colorConvertedImage->get_height());

    auto ccst = std::make_shared<Box_ccst>();
    ccst->set_coding_constraints(data.codingConstraints);
    sample_description_box->append_child_box(ccst);

    set_sample_description_box(sample_description_box);
  }

  Error err = write_sample_data(data.bitstream,
                                colorConvertedImage->get_sample_duration(),
                                data.is_sync_frame,
                                image->get_tai_timestamp(),
                                image->has_gimi_sample_content_id() ? image->get_gimi_sample_content_id() : std::string{});

  if (err) {
    return err;
  }

  return Error::Ok;
}


heif_brand2 Track_Visual::get_compatible_brand() const
{
  if (m_stsd->get_num_sample_entries() == 0) {
    return 0; // TODO: error ? Or can we assume at this point that there is at least one sample entry?
  }

  auto sampleEntry = m_stsd->get_sample_entry(0);

  uint32_t sample_entry_type = sampleEntry->get_short_type();
  switch (sample_entry_type) {
    case fourcc("hvc1"): {
      auto hvcC = sampleEntry->get_child_box<Box_hvcC>();
      if (!hvcC) { return 0; }

      const auto& config = hvcC->get_configuration();
      if (config.is_profile_compatibile(HEVCDecoderConfigurationRecord::Profile_Main) ||
          config.is_profile_compatibile(HEVCDecoderConfigurationRecord::Profile_MainStillPicture)) {
        return heif_brand2_hevc;
      }
      else {
        return heif_brand2_hevx;
      }
    }

    case fourcc("avc1"):
      return heif_brand2_avcs;

    case fourcc("av01"):
      return heif_brand2_avis;

    case fourcc("j2ki"):
      return heif_brand2_j2is;

    case fourcc("mjpg"):
      return heif_brand2_jpgs;

    case fourcc("vvc1"):
      return heif_brand2_vvis;

    default:
      return 0;
  }
}
