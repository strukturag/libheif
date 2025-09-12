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

#include "avif_enc.h"
#include "avif_boxes.h"
#include "error.h"
#include "context.h"
#include "api_structs.h"

#include <string>


Result<Encoder::CodedImageData> Encoder_AVIF::encode(const std::shared_ptr<HeifPixelImage>& image,
                                                     struct heif_encoder* encoder,
                                                     const struct heif_encoding_options& options,
                                                     enum heif_image_input_class input_class)
{
  Encoder::CodedImageData codedImage;

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

  codedImage.codingConstraints.intra_pred_used = true;
  codedImage.codingConstraints.all_ref_pics_intra = true; // TODO: change when we use predicted frames

  return codedImage;
}


std::shared_ptr<class Box_VisualSampleEntry> Encoder_AVIF::get_sample_description_box(const CodedImageData& data) const
{
  auto av01 = std::make_shared<Box_av01>();
  av01->get_VisualSampleEntry().compressorname = "AVIF";

  for (auto prop : data.properties) {
    if (prop->get_short_type() == fourcc("av1C")) {
      av01->append_child_box(prop);
      return av01;
    }
  }

  assert(false); // no av1C generated
  return nullptr;
}
