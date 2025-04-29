/*
 * HEIF codec.
 * Copyright (c) 2025 Dirk Farin <dirk.farin@gmail.com>
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

#include <libheif/heif_tai_timestamps.h>
#include <libheif/api_structs.h>
#include "box.h"
#include "context.h"
#include "file.h"


const uint64_t heif_tai_clock_info_time_uncertainty_unknown = UINT64_C(0xFFFFFFFFFFFFFFFF);
const int32_t heif_tai_clock_info_clock_drift_rate_unknown = INT32_C(0x7FFFFFFF);
const int8_t heif_tai_clock_info_clock_type_unknown = 0;
const int8_t heif_tai_clock_info_clock_type_not_synchronized_to_atomic_source = 1;
const int8_t heif_tai_clock_info_clock_type_synchronized_to_atomic_source = 2;


struct heif_error heif_item_set_property_tai_clock_info(struct heif_context* ctx,
                                                        heif_item_id itemId,
                                                        const heif_tai_clock_info* clock,
                                                        heif_property_id* out_propertyId)
{
  if (!ctx || !clock) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "NULL passed"};
  }

  // Check if itemId exists
  auto file = ctx->context->get_heif_file();
  if (!file->item_exists(itemId)) {
    return {heif_error_Input_does_not_exist, heif_suberror_Invalid_parameter_value, "itemId does not exist"};
  }

  // Create new taic (it will be deduplicated automatically in add_property())

  auto taic = std::make_shared<Box_taic>();
  taic->set_from_tai_clock_info(clock);

  heif_property_id id = ctx->context->add_property(itemId, taic, false);

  if (out_propertyId) {
    *out_propertyId = id;
  }

  return heif_error_success;
}


struct heif_error heif_item_get_property_tai_clock_info(const struct heif_context* ctx,
                                                        heif_item_id itemId,
                                                        heif_tai_clock_info** out_clock)
{
  if (!ctx) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL heif_context passed in"};
  }
  else if (!out_clock) {
    return {heif_error_Input_does_not_exist, heif_suberror_Invalid_parameter_value, "NULL heif_tai_clock_info passed in"};
  }

  *out_clock = nullptr;

  // Check if itemId exists
  auto file = ctx->context->get_heif_file();
  if (!file->item_exists(itemId)) {
    return {heif_error_Input_does_not_exist, heif_suberror_Invalid_parameter_value, "item ID does not exist"};
  }

  // Check if taic exists for itemId
  auto taic = file->get_property_for_item<Box_taic>(itemId);
  if (!taic) {
    // return NULL heif_tai_clock_info
    return heif_error_success;
  }

  *out_clock = new heif_tai_clock_info;
  **out_clock = *taic->get_tai_clock_info();

  return heif_error_success;
}


void heif_tai_clock_info_release(struct heif_tai_clock_info* clock_info)
{
  delete clock_info;
}


void heif_tai_timestamp_packet_release(const heif_tai_timestamp_packet* tai)
{
  delete tai;
}


struct heif_error heif_item_set_property_tai_timestamp(struct heif_context* ctx,
                                                       heif_item_id itemId,
                                                       heif_tai_timestamp_packet* timestamp,
                                                       heif_property_id* out_propertyId)
{
  if (!ctx) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "NULL passed"};
  }

  // Check if itemId exists
  auto file = ctx->context->get_heif_file();
  if (!file->item_exists(itemId)) {
    return {heif_error_Input_does_not_exist, heif_suberror_Invalid_parameter_value, "item does not exist"};
  }

  // Create new itai (it will be deduplicated automatically in add_property())

  auto itai = std::make_shared<Box_itai>();
  itai->set_from_tai_timestamp_packet(timestamp);

  heif_property_id id = ctx->context->add_property(itemId, itai, false);

  if (out_propertyId) {
    *out_propertyId = id;
  }

  return heif_error_success;
}


struct heif_error heif_item_get_property_tai_timestamp(const struct heif_context* ctx,
                                                       heif_item_id itemId,
                                                       struct heif_tai_timestamp_packet** out_timestamp)
{
  if (!ctx) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed"};
  }
  else if (!out_timestamp) {
    return {heif_error_Input_does_not_exist, heif_suberror_Invalid_parameter_value, "NULL heif_tai_timestamp_packet passed in"};
  }

  *out_timestamp = nullptr;

  // Check if itemId exists
  auto file = ctx->context->get_heif_file();
  if (!file->item_exists(itemId)) {
    return {heif_error_Input_does_not_exist, heif_suberror_Invalid_parameter_value, "item does not exist"};
  }

  // Check if itai exists for itemId
  auto itai = file->get_property_for_item<Box_itai>(itemId);
  if (!itai) {
    // return NULL heif_tai_timestamp_packet;
    return heif_error_success;
  }

  *out_timestamp = new heif_tai_timestamp_packet;
  **out_timestamp = *itai->get_tai_timestamp_packet();

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


struct heif_error heif_image_get_tai_timestamp(const struct heif_image* img,
                                               struct heif_tai_timestamp_packet** out_timestamp)
{
  if (!out_timestamp) {
    return {heif_error_Input_does_not_exist, heif_suberror_Invalid_parameter_value, "NULL heif_tai_timestamp_packet passed in"};
  }

  *out_timestamp = nullptr;

  auto* tai = img->image->get_tai_timestamp();
  if (!tai) {
    return {heif_error_Usage_error,
            heif_suberror_Unspecified,
            "No timestamp attached to image"};
  }

  *out_timestamp = new heif_tai_timestamp_packet;
  **out_timestamp = *tai;

  return heif_error_success;
}

