/*
 * HEIF JPEG 2000 codec.
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

#include "jpeg2000.h"
#include "libheif/api_structs.h"
#include "codecs/jpeg2000_dec.h"
#include "codecs/jpeg2000_boxes.h"
#include <cstdint>
#include <iostream>
#include <cstdio>
#include <utility>



Result<ImageItem::CodedImageData> ImageItem_JPEG2000::encode(const std::shared_ptr<HeifPixelImage>& image,
                                                             struct heif_encoder* encoder,
                                                             const struct heif_encoding_options& options,
                                                             enum heif_image_input_class input_class)
{
  CodedImageData codedImageData;

  heif_image c_api_image;
  c_api_image.image = image;

  encoder->plugin->encode_image(encoder->encoder, &c_api_image, input_class);

  // get compressed data
  for (;;) {
    uint8_t* data;
    int size;

    encoder->plugin->get_compressed_data(encoder->encoder, &data, &size, nullptr);

    if (data == nullptr) {
      break;
    }

    codedImageData.append(data, size);
  }

  // add 'j2kH' property
  auto j2kH = std::make_shared<Box_j2kH>();

  // add 'cdef' to 'j2kH'
  auto cdef = std::make_shared<Box_cdef>();
  cdef->set_channels(image->get_colorspace());
  j2kH->append_child_box(cdef);

  codedImageData.properties.push_back(j2kH);

  return codedImageData;
}


Result<std::vector<uint8_t>> ImageItem_JPEG2000::read_bitstream_configuration_data() const
{
  // --- get codec configuration

  std::shared_ptr<Box_j2kH> j2kH_box = get_property<Box_j2kH>();
  if (!j2kH_box)
  {
    // TODO - Correctly Find the j2kH box
    //  return Error(heif_error_Invalid_input,
    //               heif_suberror_Unspecified);
  }
  // else if (!j2kH_box->get_headers(data)) {
  //   return Error(heif_error_Invalid_input,
  //                heif_suberror_No_item_data);
  // }

  return std::vector<uint8_t>{};
}

std::shared_ptr<Decoder> ImageItem_JPEG2000::get_decoder() const
{
  return m_decoder;
}

Error ImageItem_JPEG2000::on_load_file()
{
  auto j2kH = get_property<Box_j2kH>();
  if (!j2kH) {
    return Error{heif_error_Invalid_input,
                 heif_suberror_Unspecified,
                 "No j2kH box found."};
  }

  m_decoder = std::make_shared<Decoder_JPEG2000>(j2kH);

  DataExtent extent;
  extent.set_from_image_item(get_context()->get_heif_file(), get_id());

  m_decoder->set_data_extent(std::move(extent));

  return Error::Ok;
}
