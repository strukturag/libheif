/*
 * HEIF VVC codec.
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
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

#include "vvc.h"
#include "vvc_dec.h"
#include "vvc_boxes.h"
#include <cstring>
#include <string>
#include <cassert>
#include <libheif/api_structs.h>


Result<ImageItem::CodedImageData> ImageItem_VVC::encode(const std::shared_ptr<HeifPixelImage>& image,
                                                        struct heif_encoder* encoder,
                                                        const struct heif_encoding_options& options,
                                                        enum heif_image_input_class input_class)
{
  CodedImageData codedImage;

  auto vvcC = std::make_shared<Box_vvcC>();
  codedImage.properties.push_back(vvcC);


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

    encoder->plugin->get_compressed_data(encoder->encoder, &data, &size, NULL);

    if (data == NULL) {
      break;
    }


    const uint8_t NAL_SPS = 15;

    uint8_t nal_type = 0;
    if (size>=2) {
      nal_type = (data[1] >> 3) & 0x1F;
    }

    if (nal_type == NAL_SPS) {
      Box_vvcC::configuration config;

      parse_sps_for_vvcC_configuration(data, size, &config, &encoded_width, &encoded_height);

      vvcC->set_configuration(config);
    }

    switch (nal_type) {
      case 14: // VPS
      case 15: // SPS
      case 16: // PPS
        vvcC->append_nal_data(data, size);
        break;

      default:
        codedImage.append_with_4bytes_size(data, size);
    }
  }

  return codedImage;
}


Result<std::vector<uint8_t>> ImageItem_VVC::read_bitstream_configuration_data(heif_item_id itemId) const
{
  // --- get codec configuration

  std::shared_ptr<Box_vvcC> vvcC_box = get_file()->get_property<Box_vvcC>(itemId);
  if (!vvcC_box)
  {
    assert(false);
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_vvcC_box);
  }

  std::vector<uint8_t> data;
  if (!vvcC_box->get_headers(&data))
  {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_item_data);
  }

  return data;
}


std::shared_ptr<Decoder> ImageItem_VVC::get_decoder() const
{
  return m_decoder;
}

Error ImageItem_VVC::on_load_file()
{
  auto vvcC_box = get_file()->get_property<Box_vvcC>(get_id());
  if (!vvcC_box) {
    return Error{heif_error_Invalid_input,
                 heif_suberror_No_av1C_box};
  }

  m_decoder = std::make_shared<Decoder_VVC>(vvcC_box);

  DataExtent extent;
  extent.set_from_image_item(get_context()->get_heif_file(), get_id());

  m_decoder->set_data_extent(extent);

  return Error::Ok;
}
