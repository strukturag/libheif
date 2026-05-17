/*6
 * HEIF codec.
 * Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>
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

#ifndef LIBHEIF_HEIF_UNCOMPRESSED_TYPES_H
#define LIBHEIF_HEIF_UNCOMPRESSED_TYPES_H

#include <stdint.h>

#include "libheif/heif_components.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- ISO 23001-17 component types (Table 1)

typedef enum heif_cmpd_component_type
{
  heif_cmpd_component_type_monochrome = 0,
  heif_cmpd_component_type_Y = 1,
  heif_cmpd_component_type_Cb = 2,
  heif_cmpd_component_type_Cr = 3,
  heif_cmpd_component_type_red = 4,
  heif_cmpd_component_type_green = 5,
  heif_cmpd_component_type_blue = 6,
  heif_cmpd_component_type_alpha = 7,
  heif_cmpd_component_type_depth = 8,
  heif_cmpd_component_type_disparity = 9,
  heif_cmpd_component_type_palette = 10,
  heif_cmpd_component_type_filter_array = 11,
  heif_cmpd_component_type_padded = 12,
  heif_cmpd_component_type_cyan = 13,
  heif_cmpd_component_type_magenta = 14,
  heif_cmpd_component_type_yellow = 15,
  heif_cmpd_component_type_key_black = 16
} heif_cmpd_component_type;


// Compression methods for 'unci' (ISO 23001-17) images.
// This is similar to heif_metadata_compression. We should try to keep the integers compatible, but each enum will just
// contain the allowed values.
typedef enum heif_unci_compression
{
  heif_unci_compression_off = 0,
  //heif_unci_compression_auto = 1,
  //heif_unci_compression_unknown = 2, // only used when reading unknown method from input file
  heif_unci_compression_deflate = 3,
  heif_unci_compression_zlib = 4,
  heif_unci_compression_brotli = 5
} heif_unci_compression;


// --- 'unci' image parameters

typedef struct heif_unci_image_parameters
{
  int version;

  // --- version 1

  uint32_t image_width;
  uint32_t image_height;

  uint32_t tile_width;
  uint32_t tile_height;

  heif_unci_compression compression;

  // TODO: interleave type, padding
} heif_unci_image_parameters;


#ifdef __cplusplus
}
#endif

#endif
