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

  auto file = context->context->get_heif_file();

  std::vector<std::shared_ptr<Box>> properties;
  Error err = file->get_properties(itemId, properties);
  if (err) {
    return err.error_struct(context->context.get());
  }

  if (propertyId < 1 || propertyId - 1 >= properties.size()) {
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

  if (propertyId < 1 || propertyId - 1 >= properties.size()) {
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

  if (propertyId < 1 || propertyId - 1 >= properties.size()) {
    return -1;
  }

  auto irot = std::dynamic_pointer_cast<Box_irot>(properties[propertyId - 1]);
  if (!irot) {
    return -1;
  }

  return irot->get_rotation_ccw();
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

  if (propertyId < 1 || propertyId - 1 >= properties.size()) {
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


template<typename T>
struct heif_error find_property(const struct heif_context* context,
                                heif_item_id itemId,
                                heif_property_id propertyId,
                                std::shared_ptr<T>* box_casted)
{
  auto file = context->context->get_heif_file();

  std::vector<std::shared_ptr<Box>> properties;
  Error err = file->get_properties(itemId, properties);
  if (err) {
    return err.error_struct(context->context.get());
  }

  if (propertyId < 1 || propertyId - 1 >= properties.size()) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "property index out of range"};
  }

  auto box = properties[propertyId - 1];
  *box_casted = std::dynamic_pointer_cast<T>(box);
  return heif_error_success;
}


#if HEIF_ENABLE_EXPERIMENTAL_FEATURES


int heif_is_tai_clock_info_drift_rate_undefined(int32_t drift_rate)
{
  if (drift_rate == heif_tai_clock_info_clock_drift_rate_unknown) {
    return 1;
  }
  return 0;
}


struct heif_error heif_property_set_clock_info(struct heif_context* ctx,
                                               heif_item_id itemId,
                                               const heif_tai_clock_info* clock,
                                               heif_property_id* out_propertyId)
{
  if (!ctx || !clock) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "NULL passed"};
  }

  // Check if itemId exists
  auto file = ctx->context->get_heif_file();
  if (!file->image_exists(itemId)) {
    return {heif_error_Input_does_not_exist, heif_suberror_Invalid_parameter_value, "itemId does not exist"};
  }

  // Create new taic if one doesn't exist for the itemId.
  auto taic = ctx->context->get_heif_file()->get_property_for_item<Box_taic>(itemId);
  if (!taic) {
    taic = std::make_shared<Box_taic>();
  }

  taic->set_time_uncertainty(clock->time_uncertainty);
  taic->set_clock_resolution(clock->clock_resolution);
  taic->set_clock_drift_rate(clock->clock_drift_rate);
  taic->set_clock_type(clock->clock_type);

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
  auto taic = file->get_property_for_item<Box_taic>(itemId);
  if (!taic) {
    out_clock = nullptr;
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "TAI Clock property not found for itemId"};

  }

  const auto* taic_data = taic->get_tai_clock_info();
  heif_tai_clock_info_copy(out_clock, taic_data);

  return heif_error_success;
}


struct heif_error heif_property_set_tai_timestamp(struct heif_context* ctx,
                                                  heif_item_id itemId,
                                                  heif_tai_timestamp_packet* timestamp,
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
  auto itai = file->get_property_for_item<Box_itai>(itemId);
  if (!itai) {
    itai = std::make_shared<Box_itai>();
  }

  // Set timestamp values
  itai->set_tai_timestamp(timestamp->tai_timestamp);
  itai->set_synchronization_state(timestamp->synchronization_state);
  itai->set_timestamp_generation_failure(timestamp->timestamp_generation_failure);
  itai->set_timestamp_is_modified(timestamp->timestamp_is_modified);
  heif_property_id id = ctx->context->add_property(itemId, itai, false);
  
  // Create new taic if one doesn't exist for the itemId.
  auto taic = file->get_property_for_item<Box_taic>(itemId);
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
                                                  heif_tai_timestamp_packet* out_timestamp)
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
  auto itai = file->get_property_for_item<Box_itai>(itemId);
  if (!itai) {
    out_timestamp = nullptr;
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "Timestamp property not found for itemId"};
  }

  if (out_timestamp) {
    out_timestamp->version = 1;
    out_timestamp->tai_timestamp = itai->get_tai_timestamp();
    out_timestamp->synchronization_state = itai->get_synchronization_state();
    out_timestamp->timestamp_generation_failure = itai->get_timestamp_generation_failure();
    out_timestamp->timestamp_is_modified = itai->get_timestamp_is_modified();
  }

  return heif_error_success;
}


struct heif_error heif_image_set_tai_timestamp(struct heif_image* img,
                                               const struct heif_tai_timestamp_packet* timestamp)
{
  Error err = img->image->set_tai_timestamp(timestamp);
  if (err) {
    return err.error_struct(img->image.get());
  }
  else {
    return heif_error_success;
  }
}


void heif_tai_timestamp_packet_copy(heif_tai_timestamp_packet* dst, const heif_tai_timestamp_packet* src)
{
  if (dst->version >= 1 && src->version >= 1) {
    dst->tai_timestamp = src->tai_timestamp;
    dst->synchronization_state = src->synchronization_state;
    dst->timestamp_is_modified = src->timestamp_is_modified;
    dst->timestamp_generation_failure = src->timestamp_generation_failure;
  }

  // in the future when copying with "src->version > dst->version",
  // the remaining dst fields have to be filled with defaults
}


int heif_image_has_tai_timestamp(const struct heif_image* img)
{
  return img->image->get_tai_timestamp() != nullptr;
}


struct heif_error heif_image_get_tai_timestamp(const struct heif_image* img,
                                               struct heif_tai_timestamp_packet* timestamp)
{
  auto* tai = img->image->get_tai_timestamp();
  if (!tai) {
    return {heif_error_Usage_error,
            heif_suberror_Unspecified,
            "No timestamp attached to image"};
  }

  heif_tai_timestamp_packet_copy(timestamp, tai);
  return heif_error_success;
}

heif_tai_timestamp_packet* heif_tai_timestamp_packet_alloc()
{
  auto* tai = new heif_tai_timestamp_packet;
  tai->version = 1;
  tai->tai_timestamp = 0;
  tai->synchronization_state = false; // TODO: or true ?
  tai->timestamp_generation_failure = false;
  tai->timestamp_is_modified = false;

  return tai;
}

void heif_tai_timestamp_packet_release(const heif_tai_timestamp_packet* tai)
{
  delete tai;
}

#endif


struct heif_error heif_item_get_property_raw_size(const struct heif_context* context,
                                                  heif_item_id itemId,
                                                  heif_property_id propertyId,
                                                  size_t* size_out)
{
  if (!context || !size_out) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "NULL argument passed in"};
  }
  std::shared_ptr<Box_other> box_other;
  struct heif_error err = find_property<Box_other>(context, itemId, propertyId, &box_other);
  if (err.code) {
    return err;
  }

  // TODO: every Box (not just Box_other) should have a get_raw_data() method.
  if (box_other == nullptr) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "this property is not read as a raw box"};
  }

  const auto& data = box_other->get_raw_data();

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

  std::shared_ptr<Box_other> box_other;
  struct heif_error err = find_property<Box_other>(context, itemId, propertyId, &box_other);
  if (err.code) {
    return err;
  }

  // TODO: every Box (not just Box_other) should have a get_raw_data() method.
  if (box_other == nullptr) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "this property is not read as a raw box"};
  }

  auto data = box_other->get_raw_data();


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

  std::shared_ptr<Box_other> box_other;
  struct heif_error err = find_property(context, itemId, propertyId, &box_other);
  if (err.code) {
    return err;
  }

  if (box_other == nullptr) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "this property is not read as a raw box"};
  }

  auto uuid = box_other->get_uuid_type();

  std::copy(uuid.begin(), uuid.end(), extended_type);

  return heif_error_success;
}
