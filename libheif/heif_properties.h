/*
 * HEIF codec.
 * Copyright (c) 2017-2023 Dirk Farin <dirk.farin@gmail.com>
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

#ifndef LIBHEIF_HEIF_PROPERTIES_H
#define LIBHEIF_HEIF_PROPERTIES_H

#include "libheif/heif.h"

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------- item properties -------------------------

enum heif_item_property_type
{
//  heif_item_property_unknown = -1,
  heif_item_property_type_invalid = 0,
  heif_item_property_type_user_description = heif_fourcc('u', 'd', 'e', 's'),
  heif_item_property_type_transform_mirror = heif_fourcc('i', 'm', 'i', 'r'),
  heif_item_property_type_transform_rotation = heif_fourcc('i', 'r', 'o', 't'),
  heif_item_property_type_transform_crop = heif_fourcc('c', 'l', 'a', 'p'),
  heif_item_property_type_image_size = heif_fourcc('i', 's', 'p', 'e'),
  heif_item_property_type_uuid = heif_fourcc('u', 'u', 'i', 'd'),
  heif_item_property_type_camera_intrinsic_matrix = heif_fourcc('c', 'm', 'i', 'n'),
  heif_item_property_type_camera_extrinsic_matrix = heif_fourcc('c', 'm', 'e', 'x')
};

// Get the heif_property_id for a heif_item_id.
// You may specify which property 'type' you want to receive.
// If you specify 'heif_item_property_type_invalid', all properties associated to that item are returned.
// The number of properties is returned, which are not more than 'count' if (out_list != nullptr).
// By setting out_list==nullptr, you can query the number of properties, 'count' is ignored.
LIBHEIF_API
int heif_item_get_properties_of_type(const struct heif_context* context,
                                     heif_item_id id,
                                     enum heif_item_property_type type,
                                     heif_property_id* out_list,
                                     int count);

// Returns all transformative properties in the correct order.
// This includes "irot", "imir", "clap".
// The number of properties is returned, which are not more than 'count' if (out_list != nullptr).
// By setting out_list==nullptr, you can query the number of properties, 'count' is ignored.
LIBHEIF_API
int heif_item_get_transformation_properties(const struct heif_context* context,
                                            heif_item_id id,
                                            heif_property_id* out_list,
                                            int count);

LIBHEIF_API
enum heif_item_property_type heif_item_get_property_type(const struct heif_context* context,
                                                         heif_item_id id,
                                                         heif_property_id property_id);

// The strings are managed by libheif. They will be deleted in heif_property_user_description_release().
struct heif_property_user_description
{
  int version;

  // version 1

  const char* lang;
  const char* name;
  const char* description;
  const char* tags;
};

// Get the "udes" user description property content.
// Undefined strings are returned as empty strings.
LIBHEIF_API
struct heif_error heif_item_get_property_user_description(const struct heif_context* context,
                                                          heif_item_id itemId,
                                                          heif_property_id propertyId,
                                                          struct heif_property_user_description** out);

// Add a "udes" user description property to the item.
// If any string pointers are NULL, an empty string will be used instead.
LIBHEIF_API
struct heif_error heif_item_add_property_user_description(const struct heif_context* context,
                                                          heif_item_id itemId,
                                                          const struct heif_property_user_description* description,
                                                          heif_property_id* out_propertyId);

// Release all strings and the object itself.
// Only call for objects that you received from heif_item_get_property_user_description().
LIBHEIF_API
void heif_property_user_description_release(struct heif_property_user_description*);

enum heif_transform_mirror_direction
{
  heif_transform_mirror_direction_invalid = -1,
  heif_transform_mirror_direction_vertical = 0,    // flip image vertically
  heif_transform_mirror_direction_horizontal = 1   // flip image horizontally
};

// Will return 'heif_transform_mirror_direction_invalid' in case of error.
LIBHEIF_API
enum heif_transform_mirror_direction heif_item_get_property_transform_mirror(const struct heif_context* context,
                                                                             heif_item_id itemId,
                                                                             heif_property_id propertyId);

// Returns only 0, 90, 180, or 270 angle values.
// Returns -1 in case of error (but it will only return an error in case of wrong usage).
LIBHEIF_API
int heif_item_get_property_transform_rotation_ccw(const struct heif_context* context,
                                                  heif_item_id itemId,
                                                  heif_property_id propertyId);

// Returns the number of pixels that should be removed from the four edges.
// Because of the way this data is stored, you have to pass the image size at the moment of the crop operation
// to compute the cropped border sizes.
LIBHEIF_API
void heif_item_get_property_transform_crop_borders(const struct heif_context* context,
                                                   heif_item_id itemId,
                                                   heif_property_id propertyId,
                                                   int image_width, int image_height,
                                                   int* left, int* top, int* right, int* bottom);


/**
 * @param context
 * @param itemId      The image item id to which this property belongs.
 * @param fourcc_type The short four-cc type of the property to add.
 * @param uuid_type   If fourcc_type=='uuid', this should point to a 16-byte UUID type. It is ignored otherwise and can be NULL.
 * @param data        Data to insert for this property (including a full-box header, if required for this box).
 * @param size        Length of data in bytes.
 * @param is_essential   Whether this property is essential (boolean).
 * @param out_propertyId Outputs the id of the inserted property. Can be NULL.
*/
LIBHEIF_API
struct heif_error heif_item_add_raw_property(const struct heif_context* context,
                                              heif_item_id itemId,
                                              uint32_t fourcc_type,
                                              const uint8_t* uuid_type,
                                              const uint8_t* data, size_t size,
                                              int is_essential,
                                              heif_property_id* out_propertyId);

LIBHEIF_API
struct heif_error heif_item_get_property_raw_size(const struct heif_context* context,
                                                  heif_item_id itemId,
                                                  heif_property_id propertyId,
                                                  size_t* size_out);

/**
 * @param data_out User-supplied array to write the property data to. The size given by heif_item_get_property_raw_size().
*/
LIBHEIF_API
struct heif_error heif_item_get_property_raw_data(const struct heif_context* context,
                                                  heif_item_id itemId,
                                                  heif_property_id propertyId,
                                                  uint8_t* data_out);


LIBHEIF_API
struct heif_error heif_item_get_property_camera_intrinsic_matrix(const struct heif_context* context,
                                                                 heif_item_id itemId,
                                                                 heif_property_id propertyId,
                                                                 struct heif_property_camera_intrinsic_matrix** out_matrix);

LIBHEIF_API
void heif_property_camera_intrinsic_matrix_release(struct heif_property_camera_intrinsic_matrix* matrix);

LIBHEIF_API
struct heif_error heif_property_camera_intrinsic_matrix_get_focal_length(const struct heif_property_camera_intrinsic_matrix* matrix,
                                                                int image_width, int image_height,
                                                                double* out_focal_length_x,
                                                                double* out_focal_length_y);

LIBHEIF_API
struct heif_error heif_property_camera_intrinsic_matrix_get_principal_point(const struct heif_property_camera_intrinsic_matrix* matrix,
                                                                   int image_width, int image_height,
                                                                   double* out_principal_point_x,
                                                                   double* out_principal_point_y);

LIBHEIF_API
struct heif_error heif_property_camera_intrinsic_matrix_get_skew(const struct heif_property_camera_intrinsic_matrix* matrix,
                                                        double* out_skew);

LIBHEIF_API
struct heif_property_camera_intrinsic_matrix* heif_property_camera_intrinsic_matrix_alloc();

LIBHEIF_API
void heif_property_camera_intrinsic_matrix_set_simple(struct heif_property_camera_intrinsic_matrix* matrix,
                                             int image_width, int image_height,
                                             double focal_length, double principal_point_x, double principal_point_y);

LIBHEIF_API
void heif_property_camera_intrinsic_matrix_set_full(struct heif_property_camera_intrinsic_matrix* matrix,
                                           int image_width, int image_height,
                                           double focal_length_x,
                                           double focal_length_y,
                                           double principal_point_x, double principal_point_y,
                                           double skew);

LIBHEIF_API
struct heif_error heif_item_add_property_camera_intrinsic_matrix(const struct heif_context* context,
                                                          heif_item_id itemId,
                                                          const struct heif_property_camera_intrinsic_matrix* matrix,
                                                          heif_property_id* out_propertyId);


LIBHEIF_API
struct heif_error heif_item_get_property_camera_extrinsic_matrix(const struct heif_context* context,
                                                                 heif_item_id itemId,
                                                                 heif_property_id propertyId,
                                                                 struct heif_camera_extrinsic_matrix** out_matrix);

LIBHEIF_API
void heif_camera_extrinsic_matrix_release(struct heif_camera_extrinsic_matrix* matrix);

// `out_matrix` must point to a 9-element matrix, which will be filled in row-major order.
LIBHEIF_API
struct heif_error heif_camera_extrinsic_matrix_get_rotation_matrix(const struct heif_camera_extrinsic_matrix* matrix,
                                                                   double* out_matrix);

// `out_vector` must point to a 3-element vector, which will be filled with the (X,Y,Z) coordinates (in micrometers).
LIBHEIF_API
struct heif_error heif_camera_extrinsic_matrix_get_position_vector(const struct heif_camera_extrinsic_matrix* matrix,
                                                                   int32_t* out_vector);

LIBHEIF_API
struct heif_error heif_camera_extrinsic_matrix_get_world_coordinate_system_id(const struct heif_camera_extrinsic_matrix* matrix,
                                                                              uint32_t* out_wcs_id);

#ifdef __cplusplus
}
#endif

#endif
