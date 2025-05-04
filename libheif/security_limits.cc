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

#include "security_limits.h"
#include <limits>


struct heif_security_limits global_security_limits {
    .version = 2,

    // --- version 1

    // Artificial limit to avoid allocating too much memory.
    // 32768^2 = 1.5 GB as YUV-4:2:0 or 4 GB as RGB32
    .max_image_size_pixels = 32768 * 32768,
    .max_number_of_tiles = 4096 * 4096,
    .max_bayer_pattern_pixels = 16*16,
    .max_items = 1000,

    .max_color_profile_size = 100 * 1024 * 1024, // 100 MB
    .max_memory_block_size = uint64_t(4) * 1024 * 1024 * 1024,  // 4 GB

    .max_components = 256,
    .max_iloc_extents_per_item = 32,
    .max_size_entity_group = 64,

    .max_children_per_box = 100,

    // --- version 2

    .min_memory_margin = 100 * 1024*1024, // 100 MB
    .max_memory_margin = 1 * 1024*1024*1024, // 1 GB

    .max_sample_description_box_entries = 1024,
    .max_sample_group_description_box_entries = 1024
};


struct heif_security_limits disabled_security_limits{
        .version = 2
};


Error check_for_valid_image_size(const heif_security_limits* limits, uint32_t width, uint32_t height)
{
  uint64_t maximum_image_size_limit = limits->max_image_size_pixels;

  // --- check whether the image size is "too large"

  if (maximum_image_size_limit > 0) {
    auto max_width_height = static_cast<uint32_t>(std::numeric_limits<int>::max());
    if ((width > max_width_height || height > max_width_height) ||
        (height != 0 && width > maximum_image_size_limit / height)) {
      std::stringstream sstr;
      sstr << "Image size " << width << "x" << height << " exceeds the maximum image size "
           << maximum_image_size_limit << "\n";

      return {heif_error_Memory_allocation_error,
              heif_suberror_Security_limit_exceeded,
              sstr.str()};
    }
  }

  if (width == 0 || height == 0) {
    return {heif_error_Memory_allocation_error,
            heif_suberror_Invalid_image_size,
            "zero width or height"};
  }

  return Error::Ok;
}
