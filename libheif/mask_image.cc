/*
 * HEIF mask image codec.
 *
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
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


#include <cstdint>
#include <cstring>
#include <algorithm>
#include <map>
#include <string.h>

#include "libheif/heif.h"
#include "libheif/logging.h"
#include "mask_image.h"

namespace heif {

  Error Box_mskC::parse(BitstreamRange& range)
  {
    parse_full_box_header(range);
    m_bits_per_pixel = range.read8();
    return range.get_error();
  }

  std::string Box_mskC::dump(Indent& indent) const
  {
    std::ostringstream sstr;
    sstr << Box::dump(indent);
    sstr << indent << "bits_per_pixel: " << ((int)m_bits_per_pixel) << "\n";
    return sstr.str();
  }

  Error Box_mskC::write(StreamWriter& writer) const
  {
    size_t box_start = reserve_box_header_space(writer);
    writer.write8(m_bits_per_pixel);
    prepend_header(writer, box_start);
    return Error::Ok;
  }


  Error MaskImageCodec::decode_mask_image(const std::shared_ptr<const HeifFile>& heif_file,
                                          heif_item_id ID,
                                          std::shared_ptr<HeifPixelImage>& img,
                                          uint32_t maximum_image_width_limit,
                                          uint32_t maximum_image_height_limit,
                                          const std::vector<uint8_t>& data)
  {
    std::vector<heif::Box_ipco::Property> item_properties;
    Error error = heif_file->get_properties(ID, item_properties);
    if (error) {
      return error;
    }
    std::shared_ptr<Box_mskC> mskC;
    uint32_t width = 0;
    uint32_t height = 0;
    bool found_ispe = false;
    for (const auto& prop : item_properties) {
      auto ispe = std::dynamic_pointer_cast<Box_ispe>(prop.property);
      if (ispe) {
        width = ispe->get_width();
        height = ispe->get_height();

        if (width >= maximum_image_width_limit || height >= maximum_image_height_limit) {
          std::stringstream sstr;
          sstr << "Image size " << width << "x" << height << " exceeds the maximum image size "
               << maximum_image_width_limit << "x" << maximum_image_height_limit << "\n";

          return Error(heif_error_Memory_allocation_error,
                       heif_suberror_Security_limit_exceeded,
                       sstr.str());
        }
        found_ispe = true;
      }

      auto maybe_mskC = std::dynamic_pointer_cast<Box_mskC>(prop.property);
      if (maybe_mskC) {
        mskC = maybe_mskC;
      }
    }
    if (!found_ispe || !mskC) {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   "Missing required box for mask codec");
    }


    img = std::make_shared<HeifPixelImage>();
    img->create(width, height, heif_colorspace_monochrome, heif_chroma_monochrome);
    img->add_plane(heif_channel_Y, width, height, mskC->get_bits_per_pixel());
    int stride;
    uint8_t* dst = img->get_plane(heif_channel_Y, &stride);
    if (((uint32_t)stride) == width) {
      memcpy(dst, data.data(), data.size());
    }
    else
    {
      for (uint32_t i = 0; i < height; i++)
      {
        memcpy(dst + i * stride, data.data() + i * width, width);
      }
    }
    return Error::Ok;
  }
}
