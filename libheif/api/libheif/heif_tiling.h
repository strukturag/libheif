/*
 * HEIF codec.
 * Copyright (c) 2017-2025 Dirk Farin <dirk.farin@gmail.com>
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

#ifndef LIBHEIF_HEIF_TILING_H
#define LIBHEIF_HEIF_TILING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libheif/heif_library.h>
#include <libheif/heif_error.h>
#include <libheif/heif_image.h>


struct heif_image_tiling
{
  int version;

  // --- version 1

  uint32_t num_columns;
  uint32_t num_rows;
  uint32_t tile_width;
  uint32_t tile_height;

  uint32_t image_width;
  uint32_t image_height;

  // Position of the top left tile.
  // Usually, this is (0;0), but if a tiled image is rotated or cropped, it may be that the top left tile should be placed at a negative position.
  // The offsets define this negative shift.
  uint32_t top_offset;
  uint32_t left_offset;

  uint8_t number_of_extra_dimensions;  // 0 for normal images, 1 for volumetric (3D), ...
  uint32_t extra_dimension_size[8];    // size of extra dimensions (first 8 dimensions)
};


// If 'process_image_transformations' is true, this returns modified sizes.
// If it is false, the top_offset and left_offset will always be (0;0).
LIBHEIF_API
struct heif_error heif_image_handle_get_image_tiling(const struct heif_image_handle* handle, int process_image_transformations, struct heif_image_tiling* out_tiling);


// For grid images, return the image item ID of a specific grid tile.
// If 'process_image_transformations' is true, the tile positions are given in the transformed image coordinate system and
// are internally mapped to the original image tile positions.
LIBHEIF_API
struct heif_error heif_image_handle_get_grid_image_tile_id(const struct heif_image_handle* handle,
                                                           int process_image_transformations,
                                                           uint32_t tile_x, uint32_t tile_y,
                                                           heif_item_id* out_tile_item_id);


struct heif_decoding_options;

// The tile position is given in tile indices, not in pixel coordinates.
// If the image transformations are processed (option->ignore_image_transformations==false), the tile position
// is given in the transformed coordinates.
LIBHEIF_API
struct heif_error heif_image_handle_decode_image_tile(const struct heif_image_handle* in_handle,
                                                      struct heif_image** out_img,
                                                      enum heif_colorspace colorspace,
                                                      enum heif_chroma chroma,
                                                      const struct heif_decoding_options* options,
                                                      uint32_t tile_x, uint32_t tile_y);

#ifdef __cplusplus
}
#endif

#endif
