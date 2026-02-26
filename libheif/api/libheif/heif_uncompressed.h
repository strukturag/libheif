/*
 * HEIF codec.
 * Copyright (c) 2024-2025 Dirk Farin <dirk.farin@gmail.com>
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

#ifndef LIBHEIF_HEIF_UNCOMPRESSED_H
#define LIBHEIF_HEIF_UNCOMPRESSED_H

#include "libheif/heif.h"

#ifdef __cplusplus
extern "C" {
#endif

/* @file heif_uncompressed.h
 * @brief Functions for adding ISO 23001-17 (uncompressed) images to a HEIF file.
 *        Despite its name, this is not limited to uncompressed images.
 *        It is also possible to add images with lossless compression methods.
 *        See heif_metadata_compression for more information.
 */

// --- ISO 23001-17 component types (Table 1)

typedef enum heif_uncompressed_component_type
{
  heif_uncompressed_component_type_monochrome = 0,
  heif_uncompressed_component_type_Y = 1,
  heif_uncompressed_component_type_Cb = 2,
  heif_uncompressed_component_type_Cr = 3,
  heif_uncompressed_component_type_red = 4,
  heif_uncompressed_component_type_green = 5,
  heif_uncompressed_component_type_blue = 6,
  heif_uncompressed_component_type_alpha = 7,
  heif_uncompressed_component_type_depth = 8,
  heif_uncompressed_component_type_disparity = 9,
  heif_uncompressed_component_type_palette = 10,
  heif_uncompressed_component_type_filter_array = 11,
  heif_uncompressed_component_type_padded = 12,
  heif_uncompressed_component_type_cyan = 13,
  heif_uncompressed_component_type_magenta = 14,
  heif_uncompressed_component_type_yellow = 15,
  heif_uncompressed_component_type_key_black = 16
} heif_uncompressed_component_type;


// --- Bayer / filter array pattern

typedef struct heif_bayer_pattern_pixel
{
  uint16_t component_type;  // one of heif_uncompressed_component_type values
  float component_gain;
} heif_bayer_pattern_pixel;

// Set a Bayer / filter array pattern on an image.
// The pattern is a 2D array of component types with dimensions pattern_width x pattern_height.
// The number of entries in patternPixels must be pattern_width * pattern_height.
// The component_type values correspond to the ISO 23001-17 component types
// (e.g. heif_uncompressed_component_type_red=4, heif_uncompressed_component_type_green=5, heif_uncompressed_component_type_blue=6).
// The encoder resolves these component types to cmpd indices when writing the cpat box.
LIBHEIF_API
heif_error heif_image_set_bayer_pattern(heif_image*,
                                        uint16_t pattern_width,
                                        uint16_t pattern_height,
                                        const heif_bayer_pattern_pixel* patternPixels);

// Returns whether the image has a Bayer / filter array pattern.
// If the image has a pattern, out_pattern_width and out_pattern_height are set.
// Either output pointer may be NULL if the caller does not need that value.
LIBHEIF_API
int heif_image_has_bayer_pattern(const heif_image*,
                                 uint16_t* out_pattern_width,
                                 uint16_t* out_pattern_height);

// Get the Bayer / filter array pattern pixels.
// The caller must provide an array large enough for pattern_width * pattern_height entries
// (use heif_image_has_bayer_pattern() to query the dimensions first).
// Returns heif_error_Ok on success, or an error if no pattern is set.
LIBHEIF_API
heif_error heif_image_get_bayer_pattern(const heif_image*,
                                        heif_bayer_pattern_pixel* out_patternPixels);

// --- Polarization pattern (ISO 23001-17, Section 6.1.5)

// Special float value indicating "no polarization filter" at a pattern position.
// On the wire this is the IEEE 754 bit pattern 0xFFFFFFFF (a signaling NaN).
// Test with heif_polarization_angle_is_no_filter() below, or with isnan()/std::isnan().

// Returns a float with the 0xFFFFFFFF bit pattern (NaN) representing "no polarization filter".
LIBHEIF_API
float heif_polarization_angle_no_filter(void);

// Returns non-zero if the given angle has the "no filter" bit pattern (0xFFFFFFFF).
LIBHEIF_API
int heif_polarization_angle_is_no_filter(float angle);

// Add a polarization pattern to an image.
// component_indices: array of component indices this pattern applies to (may be NULL if num_component_indices == 0,
//                    meaning the pattern applies to all components).
// polarization_angles: array of pattern_width * pattern_height float values.
//                      Each is an angle in degrees [0.0, 360.0), or heif_polarization_angle_no_filter() for "no filter".
// Multiple patterns can be added (one per distinct component group).
LIBHEIF_API
heif_error heif_image_add_polarization_pattern(heif_image*,
                                               uint32_t num_component_indices,
                                               const uint32_t* component_indices,
                                               uint16_t pattern_width,
                                               uint16_t pattern_height,
                                               const float* polarization_angles);

// Returns the number of polarization patterns on this image (0 if none).
LIBHEIF_API
int heif_image_get_number_of_polarization_patterns(const heif_image*);

// Get the sizes/dimensions of a polarization pattern (to allocate arrays for the data query).
LIBHEIF_API
heif_error heif_image_get_polarization_pattern_info(const heif_image*,
                                                    int pattern_index,
                                                    uint32_t* out_num_component_indices,
                                                    uint16_t* out_pattern_width,
                                                    uint16_t* out_pattern_height);

// Get the actual data of a polarization pattern.
// Caller must provide pre-allocated arrays:
//   out_component_indices: num_component_indices entries (may be NULL if num_component_indices == 0)
//   out_polarization_angles: pattern_width * pattern_height entries
LIBHEIF_API
heif_error heif_image_get_polarization_pattern_data(const heif_image*,
                                                    int pattern_index,
                                                    uint32_t* out_component_indices,
                                                    float* out_polarization_angles);

// Find the polarization pattern index that applies to a given component index.
// Returns the pattern index (>= 0), or -1 if no pattern matches.
// A pattern with an empty component list (component_count == 0) matches all components.
LIBHEIF_API
int heif_image_get_polarization_pattern_index_for_component(const heif_image*,
                                                            uint32_t component_index);


// --- 'unci' images

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


typedef struct heif_unci_image_parameters
{
  int version;

  // --- version 1

  uint32_t image_width;
  uint32_t image_height;

  uint32_t tile_width;
  uint32_t tile_height;

  enum heif_unci_compression compression;

  // TODO: interleave type, padding
} heif_unci_image_parameters;

LIBHEIF_API
heif_unci_image_parameters* heif_unci_image_parameters_alloc(void);

LIBHEIF_API
void heif_unci_image_parameters_copy(heif_unci_image_parameters* dst,
                                     const heif_unci_image_parameters* src);

LIBHEIF_API
void heif_unci_image_parameters_release(heif_unci_image_parameters*);


/*
 * This adds an empty iso23001-17 (uncompressed) image to the HEIF file.
 * The actual image data is added later using heif_context_add_image_tile().
 * If you do not need tiling, you can use heif_context_encode_image() instead.
 * However, this will by default disable any compression and any control about
 * the data layout.
 *
 * @param ctx The file context
 * @param parameters The parameters for the image, must not be NULL.
 * @param encoding_options Optional, may be NULL.
 * @param prototype An image with the same channel configuration as the image data
 *                  that will be later inserted. The image size need not match this.
 *                  Must not be NULL.
 * @param out_unci_image_handle Returns a handle to the image. The caller is responsible for freeing it.
 *                  Must not be NULL because this is required to fill in image data.
 * @return Returns an error if the passed parameters are incorrect.
 *         If ISO23001-17 images are not supported, returns heif_error_Unsupported_feature.
 */
LIBHEIF_API
heif_error heif_context_add_empty_unci_image(heif_context* ctx,
                                             const heif_unci_image_parameters* parameters,
                                             const heif_encoding_options* encoding_options,
                                             const heif_image* prototype,
                                             heif_image_handle** out_unci_image_handle);

#ifdef __cplusplus
}
#endif

#endif
