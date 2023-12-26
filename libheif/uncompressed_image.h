/*
 * HEIF codec.
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


#ifndef LIBHEIF_UNCOMPRESSED_IMAGE_H
#define LIBHEIF_UNCOMPRESSED_IMAGE_H

#include "pixelimage.h"
#include "file.h"
#include "context.h"

#include <cstdint>
#include <string>
#include <vector>
#include <memory>


class UncompressedImageCodec
{
public:
  static int get_luma_bits_per_pixel_from_configuration_unci(const HeifFile& heif_file, heif_item_id imageID);

  static Error decode_uncompressed_image(const std::shared_ptr<const HeifFile>& heif_file,
                                         heif_item_id ID,
                                         std::shared_ptr<HeifPixelImage>& img,
                                         uint32_t maximum_image_width_limit,
                                         uint32_t maximum_image_height_limit,
                                         const std::vector<uint8_t>& uncompressed_data);

  static Error encode_uncompressed_image(const std::shared_ptr<HeifFile>& heif_file,
                                         const std::shared_ptr<HeifPixelImage>& src_image,
                                         void* encoder_struct,
                                         const struct heif_encoding_options& options,
                                         std::shared_ptr<HeifContext::Image>& out_image);
};

#endif //LIBHEIF_UNCOMPRESSED_IMAGE_H
