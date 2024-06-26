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


  /* ===================================================================================
   *   This file contains candidate APIs that did not make it into the public API yet.
   * ===================================================================================
   */


  /*
  heif_item_property_type_camera_intrinsic_matrix = heif_fourcc('c', 'm', 'i', 'n'),
  heif_item_property_type_camera_extrinsic_matrix = heif_fourcc('c', 'm', 'e', 'x')
*/

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

#ifdef __cplusplus
}
#endif

#endif
