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
#include <cstring>

#include "libheif/heif.h"
#include "logging.h"
#include "mask_image.h"
#include <codecs/image_item.h>

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


Error MaskImageCodec::decode_mask_image(const HeifContext* context,
                                        heif_item_id ID,
                                        std::shared_ptr<HeifPixelImage>& img,
                                        const std::vector<uint8_t>& data)
{
  std::vector<std::shared_ptr<Box>> item_properties;
  Error error = context->get_heif_file()->get_properties(ID, item_properties);
  if (error) {
    return error;
  }
  std::shared_ptr<Box_mskC> mskC;
  uint32_t width = 0;
  uint32_t height = 0;
  bool found_ispe = false;
  for (const auto& prop : item_properties) {
    auto ispe = std::dynamic_pointer_cast<Box_ispe>(prop);
    if (ispe) {
      width = ispe->get_width();
      height = ispe->get_height();
      error = context->check_resolution(width, height);
      if (error) {
        return error;
      }

      found_ispe = true;
    }

    auto maybe_mskC = std::dynamic_pointer_cast<Box_mskC>(prop);
    if (maybe_mskC) {
      mskC = maybe_mskC;
    }
  }
  if (!found_ispe || !mskC) {
    return Error(heif_error_Unsupported_feature,
                  heif_suberror_Unsupported_data_version,
                  "Missing required box for mask codec");
  }

  if ((mskC->get_bits_per_pixel() != 8) && (mskC->get_bits_per_pixel() != 16))
  {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Unsupported bit depth for mask item");
  }

  if (data.size() < width * height) {
    return {heif_error_Invalid_input,
            heif_suberror_Unspecified,
            "Mask image data is too short"};
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

Error MaskImageCodec::encode_mask_image(const std::shared_ptr<HeifFile>& heif_file,
                                        const std::shared_ptr<HeifPixelImage>& src_image,
                                        void* encoder_struct,
                                        const struct heif_encoding_options& options,
                                        std::shared_ptr<ImageItem>& out_image)
{
  if (src_image->get_colorspace() != heif_colorspace_monochrome)
  {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Unsupported colourspace for mask region");
  }
  if (src_image->get_bits_per_pixel(heif_channel_Y) != 8)
  {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Unsupported bit depth for mask region");
  }
  // TODO: we could add an option to lossless-compress this data
  std::vector<uint8_t> data;
  int src_stride;
  uint8_t* src_data = src_image->get_plane(heif_channel_Y, &src_stride);

  int w = src_image->get_width();
  int h = src_image->get_height();

  data.resize(w * h);

  if (w == src_stride) {
    memcpy(data.data(), src_data, w * h);
  }
  else {
    for (int y = 0; y < h; y++) {
      memcpy(data.data() + y * w, src_data + y * src_stride, w);
    }
  }

  heif_file->append_iloc_data(out_image->get_id(), data, 0);

  std::shared_ptr<Box_mskC> mskC = std::make_shared<Box_mskC>();
  mskC->set_bits_per_pixel(src_image->get_bits_per_pixel(heif_channel_Y));
  heif_file->add_property(out_image->get_id(), mskC, true);

  // We need to ensure ispe is essential for the mask case
  std::shared_ptr<Box_ispe> ispe = std::make_shared<Box_ispe>();
  ispe->set_size(src_image->get_width(), src_image->get_height());
  heif_file->add_property(out_image->get_id(), ispe, true);

  return Error::Ok;
}
