/*
 * HEIF codec.
 * Copyright (c) 2017 Dirk Farin <dirk.farin@gmail.com>
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

#include "pixelimage.h"
#include "avif.h"
#include "codecs/avif_dec.h"
#include "codecs/avif_boxes.h"
#include "bitstream.h"
#include "common_utils.h"
#include "libheif/api_structs.h"
#include "file.h"
#include <iomanip>
#include <limits>
#include <string>
#include <cstring>

// https://aomediacodec.github.io/av1-spec/av1-spec.pdf



Error ImageItem_AVIF::on_load_file()
{
  auto av1C_box = get_file()->get_property<Box_av1C>(get_id());
  if (!av1C_box) {
    return Error{heif_error_Invalid_input,
                 heif_suberror_No_av1C_box};
  }

  m_decoder = std::make_shared<Decoder_AVIF>(av1C_box);

  DataExtent extent;
  extent.set_from_image_item(get_context()->get_heif_file(), get_id());

  m_decoder->set_data_extent(std::move(extent));

  return Error::Ok;
}


Result<ImageItem::CodedImageData> ImageItem_AVIF::encode(const std::shared_ptr<HeifPixelImage>& image,
                                                         struct heif_encoder* encoder,
                                                         const struct heif_encoding_options& options,
                                                         enum heif_image_input_class input_class)
{
  CodedImageData codedImage;

  Box_av1C::configuration config;

  // Fill preliminary av1C in case we cannot parse the sequence_header() correctly in the code below.
  // TODO: maybe we can remove this later.
  fill_av1C_configuration(&config, image);

  heif_image c_api_image;
  c_api_image.image = image;

  struct heif_error err = encoder->plugin->encode_image(encoder->encoder, &c_api_image, input_class);
  if (err.code) {
    return Error(err.code,
                 err.subcode,
                 err.message);
  }

  for (;;) {
    uint8_t* data;
    int size;

    encoder->plugin->get_compressed_data(encoder->encoder, &data, &size, nullptr);

    bool found_config = fill_av1C_configuration_from_stream(&config, data, size);
    (void) found_config;

    if (data == nullptr) {
      break;
    }

    codedImage.append(data, size);
  }

  auto av1C = std::make_shared<Box_av1C>();
  av1C->set_configuration(config);
  codedImage.properties.push_back(av1C);

  return codedImage;
}


Result<std::vector<uint8_t>> ImageItem_AVIF::read_bitstream_configuration_data(heif_item_id itemId) const
{
  return m_decoder->read_bitstream_configuration_data();
}


std::shared_ptr<class Decoder> ImageItem_AVIF::get_decoder() const
{
  return m_decoder;
}
