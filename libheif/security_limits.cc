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


struct heif_security_limits global_security_limits {
    .version = 1,

    // --- version 1

    // Artificial limit to avoid allocating too much memory.
    // 32768^2 = 1.5 GB as YUV-4:2:0 or 4 GB as RGB32
    .max_image_size_pixels = 32768 * 32768,
    .max_bayer_pattern_pixels = 16*16,

    .max_iref_references = 0,
    .max_iloc_items = 0,
    .max_iloc_extents_per_item = 32,
    .max_children_per_box = 65536,
    .max_number_of_tiles = 4096 * 4096,

    .max_color_profile_size = 100 * 1024 * 1024, // 100 MB
    .max_memory_block_size = 512 * 1024 * 1024   // 512 MB
};
