/*
 * HEIF AVC codec.
 * Copyright (c) 2023 Brad Hards <bradh@frogmouth.net>
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

#include "avc.h"
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>
#include "file.h"
#include "context.h"
#include "codecs/avc_dec.h"
#include "codecs/avc_boxes.h"
#include <utility>


Result<Encoder::CodedImageData> ImageItem_AVC::encode(const std::shared_ptr<HeifPixelImage>& image,
                                                        struct heif_encoder* encoder,
                                                        const struct heif_encoding_options& options,
                                                        enum heif_image_input_class input_class)
{
#if 0
  CodedImageData codedImage;

  auto hvcC = std::make_shared<Box_hvcC>();

  heif_image c_api_image;
  c_api_image.image = image;

  struct heif_error err = encoder->plugin->encode_image(encoder->encoder, &c_api_image, input_class);
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


    const uint8_t NAL_SPS = 33;

    if ((data[0] >> 1) == NAL_SPS) {
      Box_hvcC::configuration config;

      parse_sps_for_hvcC_configuration(data, size, &config, &encoded_width, &encoded_height);

      hvcC->set_configuration(config);

      codedImage.encoded_image_width = encoded_width;
      codedImage.encoded_image_height = encoded_height;
    }

    switch (data[0] >> 1) {
      case 0x20:
      case 0x21:
      case 0x22:
        hvcC->append_nal_data(data, size);
        break;

      default:
        codedImage.append_with_4bytes_size(data, size);
        // m_heif_file->append_iloc_data_with_4byte_size(image_id, data, size);
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

  return codedImage;
#endif
  assert(false); // TODO
  return {};
}


Result<std::shared_ptr<Decoder>> ImageItem_AVC::get_decoder() const
{
  return {m_decoder};
}


Error ImageItem_AVC::on_load_file()
{
  auto avcC_box = get_property<Box_avcC>();
  if (!avcC_box) {
    return Error{heif_error_Invalid_input,
                 heif_suberror_No_av1C_box};
  }

  m_decoder = std::make_shared<Decoder_AVC>(avcC_box);

  DataExtent extent;
  extent.set_from_image_item(get_context()->get_heif_file(), get_id());

  m_decoder->set_data_extent(std::move(extent));

  return Error::Ok;
}
