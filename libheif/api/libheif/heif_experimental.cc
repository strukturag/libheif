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

#include "heif_experimental.h"
#include "context.h"
#include "api_structs.h"
#include "file.h"

#include <array>
#include <cstring>
#include <memory>
#include <vector>
#include <string>


struct heif_property_camera_intrinsic_matrix
{
  Box_cmin::RelativeIntrinsicMatrix matrix;
};

struct heif_error heif_item_get_property_camera_intrinsic_matrix(const struct heif_context* context,
                                                                 heif_item_id itemId,
                                                                 heif_property_id propertyId,
                                                                 struct heif_property_camera_intrinsic_matrix** out_matrix)
{
  if (!out_matrix || !context) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed"};
  }

  auto file = context->context->get_heif_file();

  std::vector<std::shared_ptr<Box>> properties;
  Error err = file->get_properties(itemId, properties);
  if (err) {
    return err.error_struct(context->context.get());
  }

  if (propertyId < 1 || propertyId - 1 >= properties.size()) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "property index out of range"};
  }

  auto cmin = std::dynamic_pointer_cast<Box_cmin>(properties[propertyId - 1]);
  if (!cmin) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "wrong property type"};
  }

  *out_matrix = new heif_property_camera_intrinsic_matrix;
  (*out_matrix)->matrix = cmin->get_intrinsic_matrix();

  return heif_error_success;
}


void heif_property_camera_intrinsic_matrix_release(struct heif_property_camera_intrinsic_matrix* matrix)
{
  delete matrix;
}

struct heif_error heif_property_camera_intrinsic_matrix_get_focal_length(const struct heif_property_camera_intrinsic_matrix* matrix,
                                                                int image_width, int image_height,
                                                                double* out_focal_length_x,
                                                                double* out_focal_length_y)
{
  if (!matrix) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed as matrix"};
  }

  double fx, fy;
  matrix->matrix.compute_focal_length(image_width, image_height, fx, fy);

  if (out_focal_length_x) *out_focal_length_x = fx;
  if (out_focal_length_y) *out_focal_length_y = fy;

  return heif_error_success;
}


struct heif_error heif_property_camera_intrinsic_matrix_get_principal_point(const struct heif_property_camera_intrinsic_matrix* matrix,
                                                                   int image_width, int image_height,
                                                                   double* out_principal_point_x,
                                                                   double* out_principal_point_y)
{
  if (!matrix) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed as matrix"};
  }

  double px, py;
  matrix->matrix.compute_principal_point(image_width, image_height, px, py);

  if (out_principal_point_x) *out_principal_point_x = px;
  if (out_principal_point_y) *out_principal_point_y = py;

  return heif_error_success;
}


struct heif_error heif_property_camera_intrinsic_matrix_get_skew(const struct heif_property_camera_intrinsic_matrix* matrix,
                                                        double* out_skew)
{
  if (!matrix || !out_skew) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed"};
  }

  *out_skew = matrix->matrix.skew;

  return heif_error_success;
}


struct heif_property_camera_intrinsic_matrix* heif_property_camera_intrinsic_matrix_alloc()
{
  return new heif_property_camera_intrinsic_matrix;
}

void heif_property_camera_intrinsic_matrix_set_simple(struct heif_property_camera_intrinsic_matrix* matrix,
                                             int image_width, int image_height,
                                             double focal_length, double principal_point_x, double principal_point_y)
{
  if (!matrix) {
    return;
  }

  matrix->matrix.is_anisotropic = false;
  matrix->matrix.focal_length_x = focal_length / image_width;
  matrix->matrix.principal_point_x = principal_point_x / image_width;
  matrix->matrix.principal_point_y = principal_point_y / image_height;
}

void heif_property_camera_intrinsic_matrix_set_full(struct heif_property_camera_intrinsic_matrix* matrix,
                                           int image_width, int image_height,
                                           double focal_length_x,
                                           double focal_length_y,
                                           double principal_point_x, double principal_point_y,
                                           double skew)
{
  if (!matrix) {
    return;
  }

  if (focal_length_x == focal_length_y && skew == 0) {
    heif_property_camera_intrinsic_matrix_set_simple(matrix, image_width, image_height, focal_length_x, principal_point_x, principal_point_y);
    return;
  }

  matrix->matrix.is_anisotropic = true;
  matrix->matrix.focal_length_x = focal_length_x / image_width;
  matrix->matrix.focal_length_y = focal_length_y / image_width;
  matrix->matrix.principal_point_x = principal_point_x / image_width;
  matrix->matrix.principal_point_y = principal_point_y / image_height;
  matrix->matrix.skew = skew;
}


struct heif_error heif_item_add_property_camera_intrinsic_matrix(const struct heif_context* context,
                                                                 heif_item_id itemId,
                                                                 const struct heif_property_camera_intrinsic_matrix* matrix,
                                                                 heif_property_id* out_propertyId)
{
  if (!context || !matrix) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "NULL passed"};
  }

  auto cmin = std::make_shared<Box_cmin>();
  cmin->set_intrinsic_matrix(matrix->matrix);

  heif_property_id id = context->context->add_property(itemId, cmin, false);

  if (out_propertyId) {
    *out_propertyId = id;
  }

  return heif_error_success;
}


struct heif_property_camera_extrinsic_matrix
{
  Box_cmex::ExtrinsicMatrix matrix;
};


struct heif_error heif_item_get_property_camera_extrinsic_matrix(const struct heif_context* context,
                                                                 heif_item_id itemId,
                                                                 heif_property_id propertyId,
                                                                 struct heif_property_camera_extrinsic_matrix** out_matrix)
{
  if (!out_matrix || !context) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed"};
  }

  auto file = context->context->get_heif_file();

  std::vector<std::shared_ptr<Box>> properties;
  Error err = file->get_properties(itemId, properties);
  if (err) {
    return err.error_struct(context->context.get());
  }

  if (propertyId < 1 || propertyId - 1 >= properties.size()) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "property index out of range"};
  }

  auto cmex = std::dynamic_pointer_cast<Box_cmex>(properties[propertyId - 1]);
  if (!cmex) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "wrong property type"};
  }

  *out_matrix = new heif_property_camera_extrinsic_matrix;
  (*out_matrix)->matrix = cmex->get_extrinsic_matrix();

  return heif_error_success;
}


void heif_property_camera_extrinsic_matrix_release(struct heif_property_camera_extrinsic_matrix* matrix)
{
  delete matrix;
}


struct heif_error heif_property_camera_extrinsic_matrix_get_rotation_matrix(const struct heif_property_camera_extrinsic_matrix* matrix,
                                                                   double* out_matrix)
{
  if (!matrix || !out_matrix) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed"};
  }

  auto rot_matrix = matrix->matrix.calculate_rotation_matrix();
  for (int i = 0; i < 9; i++) {
    out_matrix[i] = rot_matrix[i];
  }

  return heif_error_success;
}


struct heif_error heif_property_camera_extrinsic_matrix_get_position_vector(const struct heif_property_camera_extrinsic_matrix* matrix,
                                                                   int32_t* out_vector)
{
  if (!matrix || !out_vector) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed"};
  }

  out_vector[0] = matrix->matrix.pos_x;
  out_vector[1] = matrix->matrix.pos_y;
  out_vector[2] = matrix->matrix.pos_z;

  return heif_error_success;
}


struct heif_error heif_property_camera_extrinsic_matrix_get_world_coordinate_system_id(const struct heif_property_camera_extrinsic_matrix* matrix,
                                                                              uint32_t* out_wcs_id)
{
  if (!matrix || !out_wcs_id) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed"};
  }

  *out_wcs_id = matrix->matrix.world_coordinate_system_id;

  return heif_error_success;
}
