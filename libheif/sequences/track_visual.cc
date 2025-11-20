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


Track_Visual::Track_Visual(HeifContext* ctx)
  : Track(ctx)
{
}


Track_Visual::~Track_Visual()
{
  for (auto& user_data : m_frame_user_data) {
    user_data.second.release();
  }
}


Error Track_Visual::load(const std::shared_ptr<Box_trak>& trak)
{
  Error parentLoadError = Track::load(trak);
  if (parentLoadError) {
    return parentLoadError;
  }

  const std::vector<uint32_t>& chunk_offsets = m_stco->get_offsets();

  // Find sequence resolution

  if (!chunk_offsets.empty()) {
    auto* s2c = m_stsc->get_chunk(1);
    if (!s2c) {
      return {
        heif_error_Invalid_input,
        heif_suberror_Unspecified,
        "Visual track has no chunk 1"
      };
    }

    Box_stsc::SampleToChunk sampleToChunk = *s2c;

    auto sample_description = m_stsd->get_sample_entry(sampleToChunk.sample_description_index - 1);
    if (!sample_description) {
      return {
        heif_error_Invalid_input,
        heif_suberror_Unspecified,
        "Visual track has sample description"
      };
    }

    auto visual_sample_description = std::dynamic_pointer_cast<const Box_VisualSampleEntry>(sample_description);
    if (!visual_sample_description) {
      return {
        heif_error_Invalid_input,
        heif_suberror_Unspecified,
        "Visual track sample description does not match visual track."
      };
    }

    m_width = visual_sample_description->get_VisualSampleEntry_const().width;
    m_height = visual_sample_description->get_VisualSampleEntry_const().height;
  }

  return {};
}


void Track_Visual::initialize_after_parsing(HeifContext* ctx, const std::vector<std::shared_ptr<Track> >& all_tracks)
{
  // --- check whether there is an auxiliary alpha track assigned to this track

  // Only assign to image-sequence tracks (TODO: are there also alpha tracks allowed for video tracks 'heif_track_type_video'?)

  if (get_handler() == heif_track_type_image_sequence) {
    for (auto track : all_tracks) {
      // skip ourselves
      if (track->get_id() != get_id()) {
        // Is this an aux alpha track?
        auto h = fourcc_to_string(track->get_handler());
        if (track->get_handler() == heif_track_type_auxiliary &&
            track->get_auxiliary_info_type() == heif_auxiliary_track_info_type_alpha) {
          // Is it assigned to the current track
          auto tref = track->get_tref_box();
          auto references = tref->get_references(fourcc("auxl"));
          if (std::any_of(references.begin(), references.end(), [this](uint32_t id) { return id == get_id(); })) {
            // Assign it

            m_aux_alpha_track = std::dynamic_pointer_cast<Track_Visual>(track);
          }
        }
      }
    }
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


Result<std::shared_ptr<HeifPixelImage> > Track_Visual::decode_next_image_sample(const heif_decoding_options& options)
{
  uint64_t num_output_samples = m_num_output_samples;
  if (options.ignore_sequence_editlist) {
    num_output_samples = m_num_samples;
  }

  if (m_next_sample_to_be_processed >= num_output_samples) {
    return Error{
      heif_error_End_of_sequence,
      heif_suberror_Unspecified,
      "End of sequence"
    };
  }


  std::shared_ptr<HeifPixelImage> image;

  uint32_t sample_idx;

  for (;;) {
    const SampleTiming& sampleTiming = m_presentation_timeline[m_next_sample_to_be_decoded % m_presentation_timeline.size()];
    /*uint32_t*/ sample_idx = sampleTiming.sampleIdx;
    uint32_t chunk_idx = sampleTiming.chunkIdx;

    const std::shared_ptr<Chunk>& chunk = m_chunks[chunk_idx];

    auto decoder = chunk->get_decoder();
    assert(decoder);

    if (m_next_sample_to_be_decoded != 0) {
      // TODO(251119): hack: avoid calling get_decoded_frame() before starting the decoder.
      Result<std::shared_ptr<HeifPixelImage> > getFrameResult = decoder->get_decoded_frame(options,
                                                                                           m_heif_context->get_security_limits());
      if (getFrameResult.error()) {
        return getFrameResult.error();
      }

      if (*getFrameResult != nullptr) {
        image = *getFrameResult;

        if ((m_next_sample_to_be_processed+1) % m_presentation_timeline.size() == 0) {
          m_is_flushed = false;
        }

        break;
      }

      if (m_is_flushed) {
        return Error(heif_error_Decoder_plugin_error,
                     heif_suberror_Unspecified,
                     "Did not decode all frames");
      }
    }

    if (m_next_sample_to_be_decoded < m_num_samples) {
      DataExtent extent = chunk->get_data_extent_for_sample(sample_idx);
      decoder->set_data_extent(extent);

      // std::cout << "PUSH chunk " << chunk_idx << " sample " << sample_idx << " (" << extent.m_size << " bytes)\n";

      // advance decoding index to next in segment
      m_next_sample_to_be_decoded = (m_next_sample_to_be_decoded + 1) % m_presentation_timeline.size();

      Error decodingError = decoder->decode_sequence_frame_from_compressed_data(options,
                                                                                m_heif_context->get_security_limits());
      if (decodingError) {
        return decodingError;
      }
    }
    else {
      // std::cout << "FLUSH\n";
      Error flushError = decoder->flush_decoder();
      if (flushError) {
        return flushError;
      }

      m_is_flushed = true;
    }
  }


  if (m_stts) {
    image->set_sample_duration(m_stts->get_sample_duration(sample_idx));
  }

  // --- assign alpha if we have an assigned alpha track

  if (m_aux_alpha_track) {
    auto alphaResult = m_aux_alpha_track->decode_next_image_sample(options);
    if (!alphaResult) {
      return alphaResult.error();
    }

    auto alphaImage = *alphaResult;
    image->transfer_plane_from_image_as(alphaImage, heif_channel_Y, heif_channel_Alpha);
  }


  // --- read sample auxiliary data

  if (m_aux_reader_content_ids) {
    auto readResult = m_aux_reader_content_ids->get_sample_info(get_file().get(), sample_idx);
    if (!readResult) {
      return readResult.error();
    }

    Result<std::string> convResult = vector_to_string(*readResult);
    if (!convResult) {
      return convResult.error();
    }

    image->set_gimi_sample_content_id(*convResult);
  }

  if (m_aux_reader_tai_timestamps) {
    auto readResult = m_aux_reader_tai_timestamps->get_sample_info(get_file().get(), sample_idx);
    if (!readResult) {
      return readResult.error();
    }

    auto resultTai = Box_itai::decode_tai_from_vector(*readResult);
    if (!resultTai) {
      return resultTai.error();
    }

    image->set_tai_timestamp(&*resultTai);
  }

  m_next_sample_to_be_processed++;

  return image;
}


Error Track_Visual::encode_end_of_sequence(heif_encoder* h_encoder)
{
  auto encoder = m_chunks.back()->get_encoder();

  for (;;) {
    Error err = encoder->encode_sequence_flush(h_encoder);
    if (err) {
      return err;
    }

    Result<bool> processingResult = process_encoded_data(h_encoder);
    if (processingResult.is_error()) {
      return processingResult.error();
    }

    if (!*processingResult) {
      break;
    }
  }

  return {};
}


Error Track_Visual::encode_image(std::shared_ptr<HeifPixelImage> image,
                                 heif_encoder* h_encoder,
                                 const heif_sequence_encoding_options* in_options,
                                 heif_image_input_class input_class)
{
  if (image->get_width() > 0xFFFF ||
      image->get_height() > 0xFFFF) {
    return {
      heif_error_Invalid_input,
      heif_suberror_Unspecified,
      "Input image resolution too high"
    };
  }

  // === generate compressed image bitstream

  // generate new chunk for first image or when compression formats don't match

  if (m_chunks.empty() || m_chunks.back()->get_compression_format() != h_encoder->plugin->compression_format) {
    add_chunk(h_encoder->plugin->compression_format);
  }

  // --- check whether we have to convert the image color space

  // The reason for doing the color conversion here is that the input might be an RGBA image and the color conversion
  // will extract the alpha plane anyway. We can reuse that plane below instead of having to do a new conversion.

  auto encoder = m_chunks.back()->get_encoder();

  const heif_color_profile_nclx* output_nclx;
  heif_color_profile_nclx nclx;

  if (const auto* image_nclx = encoder->get_forced_output_nclx()) {
    output_nclx = const_cast<heif_color_profile_nclx*>(image_nclx);
  }
  else if (in_options) {
    output_nclx = in_options->output_nclx_profile;
  }
  else {
    if (image->has_nclx_color_profile()) {
      nclx_profile input_nclx = image->get_color_profile_nclx();

      nclx.version = 1;
      nclx.color_primaries = (enum heif_color_primaries) input_nclx.get_colour_primaries();
      nclx.transfer_characteristics = (enum heif_transfer_characteristics) input_nclx.get_transfer_characteristics();
      nclx.matrix_coefficients = (enum heif_matrix_coefficients) input_nclx.get_matrix_coefficients();
      nclx.full_range_flag = input_nclx.get_full_range_flag();

      output_nclx = &nclx;
    }
  }

  Result<std::shared_ptr<HeifPixelImage> > srcImageResult = encoder->convert_colorspace_for_encoding(image,
                                                                                                     h_encoder,
                                                                                                     output_nclx,
                                                                                                     in_options ? &in_options->color_conversion_options : nullptr,
                                                                                                     m_heif_context->get_security_limits());
  if (!srcImageResult) {
    return srcImageResult.error();
  }

  std::shared_ptr<HeifPixelImage> colorConvertedImage = *srcImageResult;

  m_width = colorConvertedImage->get_width();
  m_height = colorConvertedImage->get_height();

  // --- encode image

  heif_sequence_encoding_options* local_dummy_options = nullptr;
  if (!in_options) {
    local_dummy_options = heif_sequence_encoding_options_alloc();
  }

  Error encodeError = encoder->encode_sequence_frame(colorConvertedImage, h_encoder,
                                                     in_options ? *in_options : *local_dummy_options,
                                                     input_class,
                                                     m_current_frame_nr);
  if (local_dummy_options) {
    heif_sequence_encoding_options_release(local_dummy_options);
  }

  if (encodeError) {
    return encodeError;
  }

  m_sample_duration = colorConvertedImage->get_sample_duration();
  // TODO heif_tai_timestamp_packet* tai = image->get_tai_timestamp();
  // TODO image->has_gimi_sample_content_id() ? image->get_gimi_sample_content_id() : std::string{});


  // store frame user data

  FrameUserData userData;
  userData.sample_duration = colorConvertedImage->get_sample_duration();
  if (image->has_gimi_sample_content_id()) {
    userData.gimi_content_id = image->get_gimi_sample_content_id();
  }

  if (const auto* tai = image->get_tai_timestamp()) {
    userData.tai_timestamp = heif_tai_timestamp_packet_alloc();
    heif_tai_timestamp_packet_copy(userData.tai_timestamp, tai);
  }

  m_frame_user_data[m_current_frame_nr] = userData;

  m_current_frame_nr++;

  // --- get compressed data from encoder

  Result<bool> processingResult = process_encoded_data(h_encoder);
  return processingResult.error();
}


Result<bool> Track_Visual::process_encoded_data(heif_encoder* h_encoder)
{
  auto encoder = m_chunks.back()->get_encoder();

  std::optional<Encoder::CodedImageData> encodingResult = encoder->encode_sequence_get_data();
  if (!encodingResult) {
    return {};
  }

  const Encoder::CodedImageData& data = *encodingResult;


  if (data.bitstream.empty() &&
      data.properties.empty()) {
    return {false};
  }

  // --- generate SampleDescriptionBox

  if (!data.properties.empty()) {
    auto sample_description_box = encoder->get_sample_description_box(data);
    VisualSampleEntry& visualSampleEntry = sample_description_box->get_VisualSampleEntry();
    visualSampleEntry.width = m_width;
    visualSampleEntry.height = m_height;

    auto ccst = std::make_shared<Box_ccst>();
    ccst->set_coding_constraints(data.codingConstraints);
    sample_description_box->append_child_box(ccst);

    set_sample_description_box(sample_description_box);
  }

  if (!data.bitstream.empty()) {
    uintptr_t frame_number = data.frame_nr;

    auto& user_data = m_frame_user_data[frame_number];

    Error err = write_sample_data(data.bitstream,
                                  user_data.sample_duration,
                                  data.is_sync_frame,
                                  user_data.tai_timestamp,
                                  user_data.gimi_content_id);

    user_data.release();
    m_frame_user_data.erase(frame_number);

    if (err) {
      return err;
    }
  }

  return {true};
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
