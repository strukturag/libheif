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

#include <array>
#include <cstring>
#include <memory>
#include <vector>
#include <string>
#include <algorithm>


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

  if (propertyId < 1 || propertyId - 1 >= properties.size()) {
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
  auto udes = context->context->find_property<Box_udes>(itemId, propertyId);
  if (!udes) {
    return udes.error.error_struct(context->context.get());
  }

  auto* udes_c = new heif_property_user_description();
  udes_c->version = 1;
  udes_c->lang = create_c_string_copy(udes.value->get_lang());
  udes_c->name = create_c_string_copy(udes.value->get_name());
  udes_c->description = create_c_string_copy(udes.value->get_description());
  udes_c->tags = create_c_string_copy(udes.value->get_tags());

  *out = udes_c;

  return heif_error_success;
}


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


enum heif_transform_mirror_direction heif_item_get_property_transform_mirror(const struct heif_context* context,
                                                                             heif_item_id itemId,
                                                                             heif_property_id propertyId)
{
  auto imir = context->context->find_property<Box_imir>(itemId, propertyId);
  if (!imir) {
    return heif_transform_mirror_direction_invalid;
  }

  return imir.value->get_mirror_direction();
}

enum heif_transform_mirror_direction heif_item_get_property_transform_mirror2(const struct heif_context* context,
                                                                             heif_item_id itemId)
{
  auto imir = context->context->find_property<Box_imir>(itemId);
  if (!imir) {
    return heif_transform_mirror_direction_invalid;
  }

  return imir.value->get_mirror_direction();
}


int heif_item_get_property_transform_rotation_ccw(const struct heif_context* context,
                                                  heif_item_id itemId,
                                                  heif_property_id propertyId)
{
  auto irot = context->context->find_property<Box_irot>(itemId, propertyId);
  if (!irot) {
    return -1;
  }

  return irot.value->get_rotation_ccw();
}


int heif_item_get_property_transform_rotation_ccw2(const struct heif_context* context,
                                                   heif_item_id itemId)
{
  auto irot = context->context->find_property<Box_irot>(itemId);
  if (!irot) {
    return -1;
  }

  return irot.value->get_rotation_ccw();
}

void heif_item_get_property_transform_crop_borders(const struct heif_context* context,
                                                   heif_item_id itemId,
                                                   heif_property_id propertyId,
                                                   int image_width, int image_height,
                                                   int* left, int* top, int* right, int* bottom)
{
  auto clap = context->context->find_property<Box_clap>(itemId, propertyId);
  if (!clap) {
    return;
  }

  if (left) *left = clap.value->left_rounded(image_width);
  if (right) *right = image_width - 1 - clap.value->right_rounded(image_width);
  if (top) *top = clap.value->top_rounded(image_height);
  if (bottom) *bottom = image_height - 1 - clap.value->bottom_rounded(image_height);
}


void heif_item_get_property_transform_crop_borders2(const struct heif_context* context,
                                                    heif_item_id itemId,
                                                    int image_width, int image_height,
                                                    int* left, int* top, int* right, int* bottom)
{
  auto clap = context->context->find_property<Box_clap>(itemId);
  if (!clap) {
    return;
  }

  if (left) *left = clap.value->left_rounded(image_width);
  if (right) *right = image_width - 1 - clap.value->right_rounded(image_width);
  if (top) *top = clap.value->top_rounded(image_height);
  if (bottom) *bottom = image_height - 1 - clap.value->bottom_rounded(image_height);
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
  auto box_other = context->context->find_property<Box_other>(itemId, propertyId);
  if (!box_other) {
    return box_other.error.error_struct(context->context.get());
  }

  // TODO: every Box (not just Box_other) should have a get_raw_data() method.
  if (box_other.value == nullptr) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "this property is not read as a raw box"};
  }

  const auto& data = box_other.value->get_raw_data();

  *size_out = data.size();

  return heif_error_success;
}


struct heif_error heif_item_get_property_raw_data(const struct heif_context* context,
                                                  heif_item_id itemId,
                                                  heif_property_id propertyId,
                                                  uint8_t* data_out)
{
  if (!context || !data_out) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "NULL argument passed in"};
  }

  auto box_other = context->context->find_property<Box_other>(itemId, propertyId);
  if (!box_other) {
    return box_other.error.error_struct(context->context.get());
  }

  // TODO: every Box (not just Box_other) should have a get_raw_data() method.
  if (box_other.value == nullptr) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "this property is not read as a raw box"};
  }

  auto data = box_other.value->get_raw_data();


  std::copy(data.begin(), data.end(), data_out);

  return heif_error_success;
}


struct heif_error heif_item_get_property_uuid_type(const struct heif_context* context,
                                                   heif_item_id itemId,
                                                   heif_property_id propertyId,
                                                   uint8_t extended_type[16])
{
  if (!context || !extended_type) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "NULL argument passed in"};
  }

  auto box_other = context->context->find_property<Box_other>(itemId, propertyId);
  if (!box_other) {
    return box_other.error.error_struct(context->context.get());
  }

  if (box_other.value == nullptr) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "this property is not read as a raw box"};
  }

  auto uuid = box_other.value->get_uuid_type();

  std::copy(uuid.begin(), uuid.end(), extended_type);

  return heif_error_success;
}



// ------------------------- intrinsic and extrinsic matrices -------------------------


int heif_image_handle_has_camera_intrinsic_matrix(const struct heif_image_handle* handle)
{
  if (!handle) {
    return false;
  }

  return handle->image->has_intrinsic_matrix();
}


struct heif_error heif_image_handle_get_camera_intrinsic_matrix(const struct heif_image_handle* handle,
                                                                struct heif_camera_intrinsic_matrix* out_matrix)
{
  if (handle == nullptr || out_matrix == nullptr) {
    return heif_error{heif_error_Usage_error,
                      heif_suberror_Null_pointer_argument};
  }

  if (!handle->image->has_intrinsic_matrix()) {
    Error err(heif_error_Usage_error,
              heif_suberror_Camera_intrinsic_matrix_undefined);
    return err.error_struct(handle->image.get());
  }

  const auto& m = handle->image->get_intrinsic_matrix();
  out_matrix->focal_length_x = m.focal_length_x;
  out_matrix->focal_length_y = m.focal_length_y;
  out_matrix->principal_point_x = m.principal_point_x;
  out_matrix->principal_point_y = m.principal_point_y;
  out_matrix->skew = m.skew;

  return heif_error_success;
}


int heif_image_handle_has_camera_extrinsic_matrix(const struct heif_image_handle* handle)
{
  if (!handle) {
    return false;
  }

  return handle->image->has_extrinsic_matrix();
}


struct heif_camera_extrinsic_matrix
{
  Box_cmex::ExtrinsicMatrix matrix;
};


struct heif_error heif_image_handle_get_camera_extrinsic_matrix(const struct heif_image_handle* handle,
                                                                struct heif_camera_extrinsic_matrix** out_matrix)
{
  if (handle == nullptr || out_matrix == nullptr) {
    return heif_error{heif_error_Usage_error,
                      heif_suberror_Null_pointer_argument};
  }

  if (!handle->image->has_extrinsic_matrix()) {
    Error err(heif_error_Usage_error,
              heif_suberror_Camera_extrinsic_matrix_undefined);
    return err.error_struct(handle->image.get());
  }

  *out_matrix = new heif_camera_extrinsic_matrix;
  (*out_matrix)->matrix = handle->image->get_extrinsic_matrix();

  return heif_error_success;
}


void heif_camera_extrinsic_matrix_release(struct heif_camera_extrinsic_matrix* matrix)
{
  delete matrix;
}


struct heif_error heif_camera_extrinsic_matrix_get_rotation_matrix(const struct heif_camera_extrinsic_matrix* matrix,
                                                                   double* out_matrix_row_major)
{
  if (matrix == nullptr || out_matrix_row_major == nullptr) {
    return heif_error{heif_error_Usage_error,
                      heif_suberror_Null_pointer_argument};
  }

  auto m3x3 = matrix->matrix.calculate_rotation_matrix();

  for (int i=0;i<9;i++) {
    out_matrix_row_major[i] = m3x3[i];
  }

  return heif_error_success;
}

