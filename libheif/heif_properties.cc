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
  if (!out) {
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


int heif_is_tai_clock_info_drift_rate_undefined(float drift_rate)
{
  float undef_clock_drift = std::numeric_limits<float>::quiet_NaN();
  return memcmp(reinterpret_cast<const void*>(&undef_clock_drift),
                reinterpret_cast<const void*>(&drift_rate),
                sizeof(float)) == 0;
}


struct heif_error heif_property_set_clock_info(struct heif_context* ctx,
                                               heif_item_id itemId,
                                               const heif_tai_clock_info* clock,
                                               heif_property_id* out_propertyId)
{
  if (!ctx) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "NULL passed"};
  }

  // Check if itemId exists
  auto file = ctx->context->get_heif_file();
  if (!file->image_exists(itemId)) {
    return {heif_error_Input_does_not_exist, heif_suberror_Invalid_parameter_value, "itemId does not exist"};
  }

  // Create new taic if one doesn't exist for the itemId.
  auto taic = ctx->context->get_heif_file()->get_property<Box_taic>(itemId);
  if (!taic) {
    taic = std::make_shared<Box_taic>();
  }

  taic->set_time_uncertainty(clock->time_uncertainty);
  taic->set_correction_offset(clock->correction_offset);
  taic->set_clock_drift_rate(clock->clock_drift_rate);
  taic->set_clock_source(clock->clock_source);

  bool essential = false;
  heif_property_id id = ctx->context->add_property(itemId, taic, essential);

  if (out_propertyId) {
    *out_propertyId = id;
  }

  return heif_error_success;
}

struct heif_error heif_property_get_clock_info(const struct heif_context* ctx,
                                               heif_item_id itemId,
                                               heif_tai_clock_info* out_clock)
{
  if (!ctx) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL heif_context passed in"};
  } else if (!out_clock) {
    return {heif_error_Input_does_not_exist, heif_suberror_Invalid_parameter_value, "NULL heif_tai_clock_info passed in"};
  }

  // Check if itemId exists
  auto file = ctx->context->get_heif_file();
  if (!file->image_exists(itemId)) {
    return {heif_error_Input_does_not_exist, heif_suberror_Invalid_parameter_value, "itemId does not exist"};
  }

  // Check if taic exists for itemId
  auto taic = file->get_property<Box_taic>(itemId);
  if (!taic) {
    out_clock = nullptr;
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "TAI Clock property not found for itemId"};

  }

  if (out_clock->version >= 1) {
    out_clock->time_uncertainty = taic->get_time_uncertainty();
    out_clock->correction_offset = taic->get_correction_offset();
    out_clock->clock_drift_rate = taic->get_clock_drift_rate();
    out_clock->clock_source = taic->get_clock_source();
  }

  return heif_error_success;
}


struct heif_error heif_property_set_tai_timestamp(struct heif_context* ctx,
                                                  heif_item_id itemId,
                                                  uint64_t tai_timestamp,
                                                  uint8_t status_bits,
                                                  heif_property_id* out_propertyId)
{
  if (!ctx) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "NULL passed"};
  }

  // Check if itemId exists
  auto file = ctx->context->get_heif_file();
  if (!file->image_exists(itemId)) {
    return {heif_error_Input_does_not_exist, heif_suberror_Invalid_parameter_value, "itemId does not exist"};
  }

  // Create new itai if one doesn't exist for the itemId.
  auto itai = file->get_property<Box_itai>(itemId);
  if (!itai) {
    itai = std::make_shared<Box_itai>();
  }

  itai->set_TAI_timestamp(tai_timestamp);
  itai->set_status_bits(status_bits);

  heif_property_id id = ctx->context->add_property(itemId, itai, false);
  
  // Create new taic if one doesn't exist for the itemId.
  auto taic = file->get_property<Box_taic>(itemId);
  if (!taic) {
    taic = std::make_shared<Box_taic>();
    ctx->context->add_property(itemId, taic, false);
    // Should we output taic_id?
  }
    

  if (out_propertyId) {
    *out_propertyId = id;
  }

  return heif_error_success;
}

struct heif_error heif_property_get_tai_timestamp(const struct heif_context* ctx,
                                                  heif_item_id itemId,
                                                  uint64_t* out_tai_timestamp,
                                                  uint8_t* out_status_bits)
{
  if (!ctx) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed"};
  }

  // Check if itemId exists
  auto file = ctx->context->get_heif_file();
  if (!file->image_exists(itemId)) {
    return {heif_error_Input_does_not_exist, heif_suberror_Invalid_parameter_value, "itemId does not exist"};
  }

  //Check if itai exists for itemId
  auto itai = file->get_property<Box_itai>(itemId);
  if (!itai) {
    out_tai_timestamp = nullptr;
    out_status_bits = nullptr;
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "Timestamp property not found for itemId"};
  }

  if (out_tai_timestamp) {
    *out_tai_timestamp = itai->get_TAI_timestamp();
  }
  if (out_status_bits) {
    *out_status_bits = itai->get_status_bits();
  }

  return heif_error_success;
}

