/*
 * HEIF codec.
 * Copyright (c) 2017 Dirk Farin <dirk.farin@gmail.com>
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

#include "libheif/heif_properties.h"
#include "context.h"
#include "api_structs.h"
#include "file.h"

#include <cstring>
#include <memory>
#include <vector>
#include <string>


int heif_item_get_properties_of_type(const struct heif_context* context,
                                     heif_item_id id,
                                     heif_item_property_type type,
                                     heif_property_id* out_list,
                                     int count)
{
  auto file = context->context->get_heif_file();

  std::vector<std::shared_ptr<Box>> properties;
  Error err = file->get_properties(id, properties);
  if (err) {
    // We do not pass the error, because a missing ipco should have been detected already when reading the file.
    return 0;
  }

  int out_idx = 0;
  int property_id = 1;

  for (const auto& property : properties) {
    bool match;
    if (type == heif_item_property_type_invalid) {
      match = true;
    }
    else {
      match = (property->get_short_type() == type);
    }

    if (match) {
      if (out_list && out_idx < count) {
        out_list[out_idx] = property_id;
        out_idx++;
      }
      else if (!out_list) {
        out_idx++;
      }
    }

    property_id++;
  }

  return out_idx;
}


int heif_item_get_transformation_properties(const struct heif_context* context,
                                            heif_item_id id,
                                            heif_property_id* out_list,
                                            int count)
{
  auto file = context->context->get_heif_file();

  std::vector<std::shared_ptr<Box>> properties;
  Error err = file->get_properties(id, properties);
  if (err) {
    // We do not pass the error, because a missing ipco should have been detected already when reading the file.
    return 0;
  }

  int out_idx = 0;
  int property_id = 1;

  for (const auto& property : properties) {
    bool match = (property->get_short_type() == fourcc("imir") ||
                  property->get_short_type() == fourcc("irot") ||
                  property->get_short_type() == fourcc("clap"));

    if (match) {
      if (out_list && out_idx < count) {
        out_list[out_idx] = property_id;
        out_idx++;
      }
      else if (!out_list) {
        out_idx++;
      }
    }

    property_id++;
  }

  return out_idx;
}

enum heif_item_property_type heif_item_get_property_type(const struct heif_context* context,
                                                         heif_item_id id,
                                                         heif_property_id propertyId)
{
  auto file = context->context->get_heif_file();

  std::vector<std::shared_ptr<Box>> properties;
  Error err = file->get_properties(id, properties);
  if (err) {
    // We do not pass the error, because a missing ipco should have been detected already when reading the file.
    return heif_item_property_type_invalid;
  }

  if (propertyId - 1 < 0 || propertyId - 1 >= properties.size()) {
    return heif_item_property_type_invalid;
  }

  auto property = properties[propertyId - 1];
  return (enum heif_item_property_type) property->get_short_type();
}


static char* create_c_string_copy(const std::string s)
{
  char* copy = new char[s.length() + 1];
  strcpy(copy, s.data());
  return copy;
}


struct heif_error heif_item_get_property_user_description(const struct heif_context* context,
                                                          heif_item_id itemId,
                                                          heif_property_id propertyId,
                                                          struct heif_property_user_description** out)
{
  if (!out || !context) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed"};
  }

  auto file = context->context->get_heif_file();

  std::vector<std::shared_ptr<Box>> properties;
  Error err = file->get_properties(itemId, properties);
  if (err) {
    return err.error_struct(context->context.get());
  }

  if (propertyId - 1 < 0 || propertyId - 1 >= properties.size()) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "property index out of range"};
  }

  auto udes = std::dynamic_pointer_cast<Box_udes>(properties[propertyId - 1]);
  if (!udes) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "wrong property type"};
  }

  auto* udes_c = new heif_property_user_description();
  udes_c->version = 1;
  udes_c->lang = create_c_string_copy(udes->get_lang());
  udes_c->name = create_c_string_copy(udes->get_name());
  udes_c->description = create_c_string_copy(udes->get_description());
  udes_c->tags = create_c_string_copy(udes->get_tags());

  *out = udes_c;

  return heif_error_success;
}


LIBHEIF_API
struct heif_error heif_item_add_property_user_description(const struct heif_context* context,
                                                          heif_item_id itemId,
                                                          const struct heif_property_user_description* description,
                                                          heif_property_id* out_propertyId)
{
  if (!context || !description) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "NULL passed"};
  }

  auto udes = std::make_shared<Box_udes>();
  udes->set_lang(description->lang ? description->lang : "");
  udes->set_name(description->name ? description->name : "");
  udes->set_description(description->description ? description->description : "");
  udes->set_tags(description->tags ? description->tags : "");

  heif_property_id id = context->context->add_property(itemId, udes, false);

  if (out_propertyId) {
    *out_propertyId = id;
  }

  return heif_error_success;
}


enum heif_transform_mirror_direction heif_item_get_property_transform_mirror(const struct heif_context* context,
                                                                             heif_item_id itemId,
                                                                             heif_property_id propertyId)
{
  auto file = context->context->get_heif_file();

  std::vector<std::shared_ptr<Box>> properties;
  Error err = file->get_properties(itemId, properties);
  if (err) {
    return heif_transform_mirror_direction_invalid;
  }

  if (propertyId - 1 < 0 || propertyId - 1 >= properties.size()) {
    return heif_transform_mirror_direction_invalid;
  }

  auto imir = std::dynamic_pointer_cast<Box_imir>(properties[propertyId - 1]);
  if (!imir) {
    return heif_transform_mirror_direction_invalid;
  }

  return imir->get_mirror_direction();
}


int heif_item_get_property_transform_rotation_ccw(const struct heif_context* context,
                                                  heif_item_id itemId,
                                                  heif_property_id propertyId)
{
  auto file = context->context->get_heif_file();

  std::vector<std::shared_ptr<Box>> properties;
  Error err = file->get_properties(itemId, properties);
  if (err) {
    return -1;
  }

  if (propertyId - 1 < 0 || propertyId - 1 >= properties.size()) {
    return -1;
  }

  auto irot = std::dynamic_pointer_cast<Box_irot>(properties[propertyId - 1]);
  if (!irot) {
    return -1;
  }

  return irot->get_rotation();
}


void heif_item_get_property_transform_crop_borders(const struct heif_context* context,
                                                   heif_item_id itemId,
                                                   heif_property_id propertyId,
                                                   int image_width, int image_height,
                                                   int* left, int* top, int* right, int* bottom)
{
  auto file = context->context->get_heif_file();

  std::vector<std::shared_ptr<Box>> properties;
  Error err = file->get_properties(itemId, properties);
  if (err) {
    return;
  }

  if (propertyId - 1 < 0 || propertyId - 1 >= properties.size()) {
    return;
  }

  auto clap = std::dynamic_pointer_cast<Box_clap>(properties[propertyId - 1]);
  if (!clap) {
    return;
  }

  if (left) *left = clap->left_rounded(image_width);
  if (right) *right = image_width - 1 - clap->right_rounded(image_width);
  if (top) *top = clap->top_rounded(image_height);
  if (bottom) *bottom = image_height - 1 - clap->bottom_rounded(image_height);
}


void heif_property_user_description_release(struct heif_property_user_description* udes)
{
  if (udes == nullptr) {
    return;
  }

  delete[] udes->lang;
  delete[] udes->name;
  delete[] udes->description;
  delete[] udes->tags;

  delete udes;
}


struct heif_error heif_item_add_raw_property(const struct heif_context* context,
                                              heif_item_id itemId,
                                              uint32_t short_type,
                                              const uint8_t* uuid_type,
                                              const uint8_t* data, size_t size,
                                              int is_essential,
                                              heif_property_id* out_propertyId)
{
  if (!context || !data || (short_type == fourcc("uuid") && uuid_type==nullptr)) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "NULL argument passed in"};
  }

  auto raw_box = std::make_shared<Box_other>(short_type);

  if (short_type == fourcc("uuid")) {
    std::vector<uint8_t> uuid_type_vector(uuid_type, uuid_type + 16);
    raw_box->set_uuid_type(uuid_type_vector);
  }

  std::vector<uint8_t> data_vector(data, data + size);
  raw_box->set_raw_data(data_vector);

  heif_property_id id = context->context->add_property(itemId, raw_box, is_essential != 0);

  if (out_propertyId) {
    *out_propertyId = id;
  }

  return heif_error_success;
}


struct heif_error heif_item_get_property_raw_size(const struct heif_context* context,
                                                  heif_item_id itemId,
                                                  heif_property_id propertyId,
                                                  size_t* size_out)
{
  if (!context || !size_out) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "NULL argument passed in"};
  }

  auto file = context->context->get_heif_file();

  std::vector<std::shared_ptr<Box>> properties;
  Error err = file->get_properties(itemId, properties);
  if (err) {
    return err.error_struct(context->context.get());
  }

  if (propertyId - 1 < 0 || propertyId - 1 >= properties.size()) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "property index out of range"};
  }

  auto box = properties[propertyId - 1];
  auto box_other = std::dynamic_pointer_cast<Box_other>(box);

  // TODO: every Box (not just Box_other) should have a get_raw_data() method.
  if (box_other == nullptr) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "this property is not read as a raw box"};
  }

  auto data = box_other->get_raw_data();

  *size_out = data.size();

  return heif_error_success;
}


struct heif_error heif_item_get_property_uuid(const struct heif_context* context,
                                              heif_item_id itemId,
                                              heif_property_id propertyId,
                                              uint8_t* data_out)
{
  if (!context || !data_out) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "NULL argument passed in"};
  }

  auto file = context->context->get_heif_file();

  std::vector<std::shared_ptr<Box>> properties;
  Error err = file->get_properties(itemId, properties);
  if (err) {
    return err.error_struct(context->context.get());
  }

  if (propertyId - 1 < 0 || propertyId - 1 >= properties.size()) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "property index out of range"};
  }


  auto box = properties[propertyId - 1];
  auto box_other = std::dynamic_pointer_cast<Box_other>(box);

  // TODO: every Box (not just Box_other) should have a get_raw_data() method.
  if (box_other == nullptr) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "this property is not read as a raw box"};
  }

  auto data = box_other->get_raw_data();


  std::copy(data.begin(), data.end(), data_out);

  return heif_error_success;
}


struct heif_camera_intrinsic_matrix
{
  Box_cmin::IntrinsicMatrix matrix;
};

struct heif_error heif_item_get_property_camera_intrinsic_matrix(const struct heif_context* context,
                                                                 heif_item_id itemId,
                                                                 heif_property_id propertyId,
                                                                 struct heif_camera_intrinsic_matrix** out_matrix)
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

  if (propertyId - 1 < 0 || propertyId - 1 >= properties.size()) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "property index out of range"};
  }

  auto cmin = std::dynamic_pointer_cast<Box_cmin>(properties[propertyId - 1]);
  if (!cmin) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "wrong property type"};
  }

  *out_matrix = new heif_camera_intrinsic_matrix;
  (*out_matrix)->matrix = cmin->get_intrinsic_matrix();

  return heif_error_success;
}


void heif_intrinsic_matrix_release(struct heif_camera_intrinsic_matrix* matrix)
{
  delete matrix;
}

struct heif_error heif_intrinsic_matrix_get_focal_length(const struct heif_camera_intrinsic_matrix* matrix,
                                                         int image_width, int image_height,
                                                         double* out_focal_length_x,
                                                         double* out_focal_length_y)
{
  if (!matrix) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed as matrix"};
  }

  double fx, fy;
  matrix->matrix.compute_focal_length(image_width, image_height, fx,fy);

  if (out_focal_length_x) *out_focal_length_x = fx;
  if (out_focal_length_y) *out_focal_length_y = fy;

  return heif_error_success;
}


struct heif_error heif_intrinsic_matrix_get_principal_point(const struct heif_camera_intrinsic_matrix* matrix,
                                                            int image_width, int image_height,
                                                            double* out_principal_point_x,
                                                            double* out_principal_point_y)
{
  if (!matrix) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed as matrix"};
  }

  double px, py;
  matrix->matrix.compute_principal_point(image_width, image_height, px,py);

  if (out_principal_point_x) *out_principal_point_x = px;
  if (out_principal_point_y) *out_principal_point_y = py;

  return heif_error_success;
}


struct heif_error heif_intrinsic_matrix_get_skew(const struct heif_camera_intrinsic_matrix* matrix,
                                                 double* out_skew)
{
  if (!matrix || !out_skew) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed"};
  }

  *out_skew = matrix->matrix.skew;

  return heif_error_success;
}
