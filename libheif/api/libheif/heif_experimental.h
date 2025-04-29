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

#ifndef LIBHEIF_HEIF_EXPERIMENTAL_H
#define LIBHEIF_HEIF_EXPERIMENTAL_H

#include "libheif/heif.h"

#ifdef __cplusplus
extern "C" {
#endif

#if HEIF_ENABLE_EXPERIMENTAL_FEATURES

  /* ===================================================================================
   *   This file contains candidate APIs that did not make it into the public API yet.
   * ===================================================================================
   */


  /*
  heif_item_property_type_camera_intrinsic_matrix = heif_fourcc('c', 'm', 'i', 'n'),
  heif_item_property_type_camera_extrinsic_matrix = heif_fourcc('c', 'm', 'e', 'x')
*/

struct heif_property_camera_intrinsic_matrix;
struct heif_property_camera_extrinsic_matrix;

//LIBHEIF_API
struct heif_error heif_item_get_property_camera_intrinsic_matrix(const struct heif_context* context,
                                                                 heif_item_id itemId,
                                                                 heif_property_id propertyId,
                                                                 struct heif_property_camera_intrinsic_matrix** out_matrix);

//LIBHEIF_API
void heif_property_camera_intrinsic_matrix_release(struct heif_property_camera_intrinsic_matrix* matrix);

//LIBHEIF_API
struct heif_error heif_property_camera_intrinsic_matrix_get_focal_length(const struct heif_property_camera_intrinsic_matrix* matrix,
                                                                int image_width, int image_height,
                                                                double* out_focal_length_x,
                                                                double* out_focal_length_y);

//LIBHEIF_API
struct heif_error heif_property_camera_intrinsic_matrix_get_principal_point(const struct heif_property_camera_intrinsic_matrix* matrix,
                                                                   int image_width, int image_height,
                                                                   double* out_principal_point_x,
                                                                   double* out_principal_point_y);

//LIBHEIF_API
struct heif_error heif_property_camera_intrinsic_matrix_get_skew(const struct heif_property_camera_intrinsic_matrix* matrix,
                                                        double* out_skew);

//LIBHEIF_API
struct heif_property_camera_intrinsic_matrix* heif_property_camera_intrinsic_matrix_alloc();

//LIBHEIF_API
void heif_property_camera_intrinsic_matrix_set_simple(struct heif_property_camera_intrinsic_matrix* matrix,
                                             int image_width, int image_height,
                                             double focal_length, double principal_point_x, double principal_point_y);

//LIBHEIF_API
void heif_property_camera_intrinsic_matrix_set_full(struct heif_property_camera_intrinsic_matrix* matrix,
                                           int image_width, int image_height,
                                           double focal_length_x,
                                           double focal_length_y,
                                           double principal_point_x, double principal_point_y,
                                           double skew);

//LIBHEIF_API
struct heif_error heif_item_add_property_camera_intrinsic_matrix(const struct heif_context* context,
                                                          heif_item_id itemId,
                                                          const struct heif_property_camera_intrinsic_matrix* matrix,
                                                          heif_property_id* out_propertyId);


//LIBHEIF_API
struct heif_error heif_item_get_property_camera_extrinsic_matrix(const struct heif_context* context,
                                                                 heif_item_id itemId,
                                                                 heif_property_id propertyId,
                                                                 struct heif_property_camera_extrinsic_matrix** out_matrix);

//LIBHEIF_API
void heif_property_camera_extrinsic_matrix_release(struct heif_property_camera_extrinsic_matrix* matrix);

// `out_matrix` must point to a 9-element matrix, which will be filled in row-major order.
//LIBHEIF_API
struct heif_error heif_property_camera_extrinsic_matrix_get_rotation_matrix(const struct heif_property_camera_extrinsic_matrix* matrix,
                                                                            double* out_matrix);

// `out_vector` must point to a 3-element vector, which will be filled with the (X,Y,Z) coordinates (in micrometers).
//LIBHEIF_API
struct heif_error heif_property_camera_extrinsic_matrix_get_position_vector(const struct heif_property_camera_extrinsic_matrix* matrix,
                                                                            int32_t* out_vector);

//LIBHEIF_API
struct heif_error heif_property_camera_extrinsic_matrix_get_world_coordinate_system_id(const struct heif_property_camera_extrinsic_matrix* matrix,
                                                                                       uint32_t* out_wcs_id);
#endif

// --- Tiled images

struct heif_tiled_image_parameters {
  int version;

  // --- version 1

  uint32_t image_width;
  uint32_t image_height;

  uint32_t tile_width;
  uint32_t tile_height;

  uint32_t compression_format_fourcc;  // will be set automatically when calling heif_context_add_tiled_image()

  uint8_t offset_field_length;   // one of: 32, 40, 48, 64
  uint8_t size_field_length;     // one of:  0, 24, 32, 64

  uint8_t number_of_extra_dimensions;  // 0 for normal images, 1 for volumetric (3D), ...
  uint32_t extra_dimensions[8];        // size of extra dimensions (first 8 dimensions)

  // boolean flags
  uint8_t tiles_are_sequential;  // TODO: can we derive this automatically
};

#if HEIF_ENABLE_EXPERIMENTAL_FEATURES
LIBHEIF_API
struct heif_error heif_context_add_tiled_image(struct heif_context* ctx,
                                               const struct heif_tiled_image_parameters* parameters,
                                               const struct heif_encoding_options* options, // TODO: do we need this?
                                               const struct heif_encoder* encoder,
                                               struct heif_image_handle** out_tiled_image_handle);
#endif

// --- 'unci' images

// This is similar to heif_metadata_compression. We should try to keep the integers compatible, but each enum will just
// contain the allowed values.
enum heif_unci_compression
{
  heif_unci_compression_off = 0,
  //heif_unci_compression_auto = 1,
  //heif_unci_compression_unknown = 2, // only used when reading unknown method from input file
  heif_unci_compression_deflate = 3,
  heif_unci_compression_zlib = 4,
  heif_unci_compression_brotli = 5
};


struct heif_unci_image_parameters {
  int version;

  // --- version 1

  uint32_t image_width;
  uint32_t image_height;

  uint32_t tile_width;
  uint32_t tile_height;

  enum heif_unci_compression compression; // TODO

  // TODO: interleave type, padding
};

#if HEIF_ENABLE_EXPERIMENTAL_FEATURES
LIBHEIF_API
struct heif_error heif_context_add_unci_image(struct heif_context* ctx,
                                              const struct heif_unci_image_parameters* parameters,
                                              const struct heif_encoding_options* encoding_options,
                                              const struct heif_image* prototype,
                                              struct heif_image_handle** out_unci_image_handle);
#endif

// --- 'pymd' entity group (pyramid layers)

struct heif_pyramid_layer_info {
  heif_item_id layer_image_id;
  uint16_t layer_binning;
  uint32_t tile_rows_in_layer;
  uint32_t tile_columns_in_layer;
};

#if HEIF_ENABLE_EXPERIMENTAL_FEATURES
// The input images are automatically sorted according to resolution. You can provide them in any order.
LIBHEIF_API
struct heif_error heif_context_add_pyramid_entity_group(struct heif_context* ctx,
                                                        const heif_item_id* layer_item_ids,
                                                        size_t num_layers,
                                                        heif_item_id* out_group_id);

LIBHEIF_API
struct heif_pyramid_layer_info* heif_context_get_pyramid_entity_group_info(struct heif_context*, heif_entity_group_id id, int* out_num_layers);

LIBHEIF_API
void heif_pyramid_layer_info_release(struct heif_pyramid_layer_info*);
#endif

// --- other pixel datatype support

enum heif_channel_datatype
{
  heif_channel_datatype_undefined = 0,
  heif_channel_datatype_unsigned_integer = 1,
  heif_channel_datatype_signed_integer = 2,
  heif_channel_datatype_floating_point = 3,
  heif_channel_datatype_complex_number = 4
};

#if HEIF_ENABLE_EXPERIMENTAL_FEATURES
LIBHEIF_API
struct heif_error heif_image_add_channel(struct heif_image* image,
                                         enum heif_channel channel,
                                         int width, int height,
                                         enum heif_channel_datatype datatype, int bit_depth);


LIBHEIF_API
int heif_image_list_channels(struct heif_image*,
                             enum heif_channel** out_channels);

LIBHEIF_API
void heif_channel_release_list(enum heif_channel** channels);
#endif

struct heif_complex32 {
  float real, imaginary;
};

struct heif_complex64 {
  double real, imaginary;
};

#if HEIF_ENABLE_EXPERIMENTAL_FEATURES
LIBHEIF_API
enum heif_channel_datatype heif_image_get_datatype(const struct heif_image* img, enum heif_channel channel);


// The 'stride' in all of these functions are in units of the underlying datatype.
LIBHEIF_API
const uint16_t* heif_image_get_channel_uint16_readonly(const struct heif_image*,
                                                       enum heif_channel channel,
                                                       size_t* out_stride);

LIBHEIF_API
const uint32_t* heif_image_get_channel_uint32_readonly(const struct heif_image*,
                                                       enum heif_channel channel,
                                                       size_t* out_stride);

LIBHEIF_API
const uint64_t* heif_image_get_channel_uint64_readonly(const struct heif_image*,
                                                       enum heif_channel channel,
                                                       size_t* out_stride);

LIBHEIF_API
const int16_t* heif_image_get_channel_int16_readonly(const struct heif_image*,
                                                     enum heif_channel channel,
                                                     size_t* out_stride);

LIBHEIF_API
const int32_t* heif_image_get_channel_int32_readonly(const struct heif_image*,
                                                     enum heif_channel channel,
                                                     size_t* out_stride);

LIBHEIF_API
const int64_t* heif_image_get_channel_int64_readonly(const struct heif_image*,
                                                     enum heif_channel channel,
                                                     size_t* out_stride);

LIBHEIF_API
const float* heif_image_get_channel_float32_readonly(const struct heif_image*,
                                                     enum heif_channel channel,
                                                     size_t* out_stride);

LIBHEIF_API
const double* heif_image_get_channel_float64_readonly(const struct heif_image*,
                                                      enum heif_channel channel,
                                                      size_t* out_stride);

LIBHEIF_API
const struct heif_complex32* heif_image_get_channel_complex32_readonly(const struct heif_image*,
                                                                       enum heif_channel channel,
                                                                       size_t* out_stride);

LIBHEIF_API
const struct heif_complex64* heif_image_get_channel_complex64_readonly(const struct heif_image*,
                                                                       enum heif_channel channel,
                                                                       size_t* out_stride);

LIBHEIF_API
uint16_t* heif_image_get_channel_uint16(struct heif_image*,
                                        enum heif_channel channel,
                                        size_t* out_stride);

LIBHEIF_API
uint32_t* heif_image_get_channel_uint32(struct heif_image*,
                                        enum heif_channel channel,
                                        size_t* out_stride);

LIBHEIF_API
uint64_t* heif_image_get_channel_uint64(struct heif_image*,
                                        enum heif_channel channel,
                                        size_t* out_stride);

LIBHEIF_API
int16_t* heif_image_get_channel_int16(struct heif_image*,
                                      enum heif_channel channel,
                                      size_t* out_stride);

LIBHEIF_API
int32_t* heif_image_get_channel_int32(struct heif_image*,
                                      enum heif_channel channel,
                                      size_t* out_stride);

LIBHEIF_API
int64_t* heif_image_get_channel_int64(struct heif_image*,
                                      enum heif_channel channel,
                                      size_t* out_stride);

LIBHEIF_API
float* heif_image_get_channel_float32(struct heif_image*,
                                      enum heif_channel channel,
                                      size_t* out_stride);

LIBHEIF_API
double* heif_image_get_channel_float64(struct heif_image*,
                                       enum heif_channel channel,
                                       size_t* out_stride);

LIBHEIF_API
struct heif_complex32* heif_image_get_channel_complex32(struct heif_image*,
                                                        enum heif_channel channel,
                                                        size_t* out_stride);

LIBHEIF_API
struct heif_complex64* heif_image_get_channel_complex64(struct heif_image*,
                                                        enum heif_channel channel,
                                                        size_t* out_stride);
#endif

// ========================= Timestamps =========================

#include <libheif/heif_tai_timestamps.h>

#if HEIF_ENABLE_EXPERIMENTAL_FEATURES

LIBHEIF_API
struct heif_error heif_image_set_tai_timestamp(struct heif_image* img,
                                               const struct heif_tai_timestamp_packet* timestamp);

LIBHEIF_API
int heif_image_has_tai_timestamp(const struct heif_image* img);

LIBHEIF_API
struct heif_error heif_image_get_tai_timestamp(const struct heif_image* img,
                                               struct heif_tai_timestamp_packet* timestamp);

LIBHEIF_API
heif_tai_timestamp_packet* heif_tai_timestamp_packet_alloc();

LIBHEIF_API
void heif_tai_timestamp_packet_release(const heif_tai_timestamp_packet*);

// version field has to be set in both structs
LIBHEIF_API
void heif_tai_timestamp_packet_copy(heif_tai_timestamp_packet* dst, const heif_tai_timestamp_packet* src);

LIBHEIF_API
heif_error heif_image_extract_area(const heif_image*,
                                   uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                                   const heif_security_limits* limits,
                                   struct heif_image** out_image);

#endif


#ifdef __cplusplus
}
#endif

#endif
