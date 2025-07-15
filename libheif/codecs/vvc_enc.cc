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

#include "vvc_enc.h"
#include "vvc_boxes.h"
#include "error.h"
#include "context.h"
#include "api_structs.h"

#include <string>


Result<Encoder::CodedImageData> Encoder_VVC::encode(const std::shared_ptr<HeifPixelImage>& image,
                                                    struct heif_encoder* encoder,
                                                    const struct heif_encoding_options& options,
                                                    enum heif_image_input_class input_class)
{
  Encoder::CodedImageData codedImage;

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

  codedImage.codingConstraints.intra_pred_used = true;
  codedImage.codingConstraints.all_ref_pics_intra = true; // TODO: change when we use predicted frames
  codedImage.codingConstraints.max_ref_per_pic = 0;

  return codedImage;
}


std::shared_ptr<class Box_VisualSampleEntry> Encoder_VVC::get_sample_description_box(const CodedImageData& data) const
{
  auto vvc1 = std::make_shared<Box_vvc1>();
  vvc1->get_VisualSampleEntry().compressorname = "VVC";

  for (auto prop : data.properties) {
    if (prop->get_short_type() == fourcc("vvcC")) {
      vvc1->append_child_box(prop);
      return vvc1;
    }
  }

  assert(false); // no hvcC generated
  return nullptr;
}
