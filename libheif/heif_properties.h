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
  heif_item_property_type_image_size = heif_fourcc('i', 's', 'p', 'e')
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

#ifdef __cplusplus
}
#endif

#endif
