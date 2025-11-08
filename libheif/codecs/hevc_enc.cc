/*
 * HEIF codec.
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

#include "hevc_enc.h"
#include "hevc_boxes.h"
#include "error.h"
#include "context.h"
#include "api_structs.h"

#include <string>

#include "plugins/nalu_utils.h"


Result<Encoder::CodedImageData> Encoder_HEVC::encode(const std::shared_ptr<HeifPixelImage>& image,
                                                     heif_encoder* encoder,
                                                     const heif_encoding_options& options,
                                                     heif_image_input_class input_class)
{
  CodedImageData codedImage;

  auto hvcC = std::make_shared<Box_hvcC>();

  heif_image c_api_image;
  c_api_image.image = image;

  heif_error err = encoder->plugin->encode_image(encoder->encoder, &c_api_image, input_class);
  if (err.code) {
    return Error(err.code,
                 err.subcode,
                 err.message);
  }

  int encoded_width = 0;
  int encoded_height = 0;

  for (;;) {
    uint8_t* data;
    int size;

    encoder->plugin->get_compressed_data(encoder->encoder, &data, &size, nullptr);

    if (data == nullptr) {
      break;
    }


    if ((data[0] >> 1) == NAL_UNIT_SPS_NUT) {
      parse_sps_for_hvcC_configuration(data, size, &hvcC->get_configuration(), &encoded_width, &encoded_height);

      codedImage.encoded_image_width = encoded_width;
      codedImage.encoded_image_height = encoded_height;
    }

    switch (data[0] >> 1) {
      case NAL_UNIT_VPS_NUT:
      case NAL_UNIT_SPS_NUT:
      case NAL_UNIT_PPS_NUT:
        hvcC->append_nal_data(data, size);
        break;

      default:
        codedImage.append_with_4bytes_size(data, size);
    }
  }

  if (!encoded_width || !encoded_height) {
    return Error(heif_error_Encoder_plugin_error,
                 heif_suberror_Invalid_image_size);
  }

  codedImage.properties.push_back(hvcC);


  // Make sure that the encoder plugin works correctly and the encoded image has the correct size.

  if (encoder->plugin->plugin_api_version >= 3 &&
      encoder->plugin->query_encoded_size != nullptr) {
    uint32_t check_encoded_width = image->get_width(), check_encoded_height = image->get_height();

    encoder->plugin->query_encoded_size(encoder->encoder,
                                        image->get_width(), image->get_height(),
                                        &check_encoded_width,
                                        &check_encoded_height);

    assert((int)check_encoded_width == encoded_width);
    assert((int)check_encoded_height == encoded_height);
  }

  codedImage.codingConstraints.intra_pred_used = true;
  codedImage.codingConstraints.all_ref_pics_intra = true; // TODO: change when we use predicted frames

  return codedImage;
}


Error Encoder_HEVC::encode_sequence_frame(const std::shared_ptr<HeifPixelImage>& image,
                                          heif_encoder* encoder,
                                          const heif_encoding_options& options,
                                          heif_image_input_class input_class,
                                          uintptr_t frame_number)
{
  heif_image c_api_image;
  c_api_image.image = image;

  if (!m_encoder_active) {
    heif_error err = encoder->plugin->start_sequence_encoding(encoder->encoder, &c_api_image, input_class,
                                                              nullptr);
    if (err.code) {
      return {
        err.code,
        err.subcode,
        err.message
      };
    }

    m_hvcC = std::make_shared<Box_hvcC>();
    m_encoder_active = true;
  }

  Error dataErr = get_data(encoder);
  if (dataErr) {
    return dataErr;
  }

  heif_error err = encoder->plugin->encode_sequence_frame(encoder->encoder, &c_api_image, frame_number);
  if (err.code) {
    return {
      err.code,
      err.subcode,
      err.message
    };
  }

  return get_data(encoder);
}


Error Encoder_HEVC::encode_sequence_flush(heif_encoder* encoder)
{
  encoder->plugin->end_sequence_encoding(encoder->encoder);
  m_encoder_active = false;
  m_end_of_sequence_reached = true;

  return get_data(encoder);
}


std::optional<Encoder::CodedImageData> Encoder_HEVC::encode_sequence_get_data()
{
  return std::move(m_current_output_data);
}

Error Encoder_HEVC::get_data(heif_encoder* encoder)
{
  //CodedImageData codedImage;

  bool got_some_data = false;

  for (;;) {
    uint8_t* data;
    int size;

    uintptr_t frameNr=0;
    encoder->plugin->get_compressed_data2(encoder->encoder, &data, &size, &frameNr);

    if (data == nullptr) {
      break;
    }

    got_some_data = true;

    const uint8_t nal_type = (data[0] >> 1);
    const bool is_sync = (nal_type == 19 || nal_type == 20 || nal_type == 21);

    if ((data[0] >> 1) == NAL_UNIT_SPS_NUT) {
      parse_sps_for_hvcC_configuration(data, size,
                                       &m_hvcC->get_configuration(),
                                       &m_encoded_image_width, &m_encoded_image_height);
    }

    switch (data[0] >> 1) {
      case NAL_UNIT_VPS_NUT:
        m_hvcC_has_VPS = true;
        m_hvcC->append_nal_data(data, size);
        break;

      case NAL_UNIT_SPS_NUT:
        m_hvcC_has_SPS = true;
        m_hvcC->append_nal_data(data, size);
        break;

      case NAL_UNIT_PPS_NUT:
        m_hvcC_has_PPS = true;
        m_hvcC->append_nal_data(data, size);
        break;

      default:
        if (!m_current_output_data) {
          m_current_output_data = CodedImageData{};
        }
        m_current_output_data->append_with_4bytes_size(data, size);
        m_current_output_data->is_sync_frame = is_sync;
        m_current_output_data->frame_nr = frameNr;
    }
  }

  if (!got_some_data) {
    return {};
  }

  if (!m_encoded_image_width || !m_encoded_image_height) {
    return Error(heif_error_Encoder_plugin_error,
                 heif_suberror_Invalid_image_size);
  }


  // --- return hvcC when all headers are included and it was not returned yet
  //     TODO: it's maybe better to return this at the end so that we are sure to have all headers
  //           and also complete codingConstraints.

  //if (hvcC_has_VPS && m_hvcC_has_SPS && m_hvcC_has_PPS && !m_hvcC_returned) {
  if (m_end_of_sequence_reached && m_hvcC) {
    m_current_output_data->properties.push_back(m_hvcC);
    m_hvcC = nullptr;
  }

  m_current_output_data->encoded_image_width = m_encoded_image_width;
  m_current_output_data->encoded_image_height = m_encoded_image_height;


  // Make sure that the encoder plugin works correctly and the encoded image has the correct size.
#if 0
  if (encoder->plugin->plugin_api_version >= 3 &&
      encoder->plugin->query_encoded_size != nullptr) {
    uint32_t check_encoded_width = image->get_width(), check_encoded_height = image->get_height();

    encoder->plugin->query_encoded_size(encoder->encoder,
                                        image->get_width(), image->get_height(),
                                        &check_encoded_width,
                                        &check_encoded_height);

    assert((int)check_encoded_width == encoded_width);
    assert((int)check_encoded_height == encoded_height);
      }
#endif

  m_current_output_data->codingConstraints.intra_pred_used = true;
  m_current_output_data->codingConstraints.all_ref_pics_intra = true; // TODO: change when we use predicted frames

  return {};
}


std::shared_ptr<Box_VisualSampleEntry> Encoder_HEVC::get_sample_description_box(const CodedImageData& data) const
{
  auto hvc1 = std::make_shared<Box_hvc1>();
  hvc1->get_VisualSampleEntry().compressorname = "HEVC";

  for (auto prop : data.properties) {
    if (prop->get_short_type() == fourcc("hvcC")) {
      hvc1->append_child_box(prop);
      return hvc1;
    }
  }

  assert(false); // no hvcC generated
  return nullptr;
}
