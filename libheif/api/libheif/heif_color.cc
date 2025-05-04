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

#include <libheif/heif_color.h>

#include "common_utils.h"
#include <cstdint>
#include "heif.h"
#include "pixelimage.h"
#include "api_structs.h"
#include "error.h"
#include <set>
#include <limits>

#include <algorithm>
#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <cstring>
#include <array>


// TODO: duplicated in heif.h
//const struct heif_error heif_error_success = {heif_error_Ok, heif_suberror_Unspecified, Error::kSuccess};
static struct heif_error error_null_parameter = {heif_error_Usage_error,
                                                 heif_suberror_Null_pointer_argument,
                                                 "NULL passed"};

heif_color_profile_type heif_image_handle_get_color_profile_type(const struct heif_image_handle* handle)
{
  auto profile_icc = handle->image->get_color_profile_icc();
  if (profile_icc) {
    return (heif_color_profile_type) profile_icc->get_type();
  }

  auto profile_nclx = handle->image->get_color_profile_nclx();
  if (profile_nclx) {
    return (heif_color_profile_type) profile_nclx->get_type();
  }
  else {
    return heif_color_profile_type_not_present;
  }
}

size_t heif_image_handle_get_raw_color_profile_size(const struct heif_image_handle* handle)
{
  auto profile_icc = handle->image->get_color_profile_icc();
  if (profile_icc) {
    return profile_icc->get_data().size();
  }
  else {
    return 0;
  }
}


static const std::set<typename std::underlying_type<heif_color_primaries>::type> known_color_primaries{
    heif_color_primaries_ITU_R_BT_709_5,
    heif_color_primaries_unspecified,
    heif_color_primaries_ITU_R_BT_470_6_System_M,
    heif_color_primaries_ITU_R_BT_470_6_System_B_G,
    heif_color_primaries_ITU_R_BT_601_6,
    heif_color_primaries_SMPTE_240M,
    heif_color_primaries_generic_film,
    heif_color_primaries_ITU_R_BT_2020_2_and_2100_0,
    heif_color_primaries_SMPTE_ST_428_1,
    heif_color_primaries_SMPTE_RP_431_2,
    heif_color_primaries_SMPTE_EG_432_1,
    heif_color_primaries_EBU_Tech_3213_E,
};

struct heif_error heif_nclx_color_profile_set_color_primaries(heif_color_profile_nclx* nclx, uint16_t cp)
{
  if (static_cast<std::underlying_type<heif_color_primaries>::type>(cp) > std::numeric_limits<std::underlying_type<heif_color_primaries>::type>::max()) {
    return Error(heif_error_Invalid_input, heif_suberror_Unknown_NCLX_color_primaries).error_struct(nullptr);
  }

  auto n = static_cast<typename std::underlying_type<heif_color_primaries>::type>(cp);
  if (known_color_primaries.find(n) != known_color_primaries.end()) {
    nclx->color_primaries = static_cast<heif_color_primaries>(n);
  }
  else {
    nclx->color_primaries = heif_color_primaries_unspecified;
    return Error(heif_error_Invalid_input, heif_suberror_Unknown_NCLX_color_primaries).error_struct(nullptr);
  }

  return Error::Ok.error_struct(nullptr);
}


static const std::set<typename std::underlying_type<heif_transfer_characteristics>::type> known_transfer_characteristics{
    heif_transfer_characteristic_ITU_R_BT_709_5,
    heif_transfer_characteristic_unspecified,
    heif_transfer_characteristic_ITU_R_BT_470_6_System_M,
    heif_transfer_characteristic_ITU_R_BT_470_6_System_B_G,
    heif_transfer_characteristic_ITU_R_BT_601_6,
    heif_transfer_characteristic_SMPTE_240M,
    heif_transfer_characteristic_linear,
    heif_transfer_characteristic_logarithmic_100,
    heif_transfer_characteristic_logarithmic_100_sqrt10,
    heif_transfer_characteristic_IEC_61966_2_4,
    heif_transfer_characteristic_ITU_R_BT_1361,
    heif_transfer_characteristic_IEC_61966_2_1,
    heif_transfer_characteristic_ITU_R_BT_2020_2_10bit,
    heif_transfer_characteristic_ITU_R_BT_2020_2_12bit,
    heif_transfer_characteristic_ITU_R_BT_2100_0_PQ,
    heif_transfer_characteristic_SMPTE_ST_428_1,
    heif_transfer_characteristic_ITU_R_BT_2100_0_HLG
};


struct heif_error heif_nclx_color_profile_set_transfer_characteristics(struct heif_color_profile_nclx* nclx, uint16_t tc)
{
  if (static_cast<std::underlying_type<heif_color_primaries>::type>(tc) > std::numeric_limits<std::underlying_type<heif_transfer_characteristics>::type>::max()) {
    return Error(heif_error_Invalid_input, heif_suberror_Unknown_NCLX_transfer_characteristics).error_struct(nullptr);
  }

  auto n = static_cast<typename std::underlying_type<heif_transfer_characteristics>::type>(tc);
  if (known_transfer_characteristics.find(n) != known_transfer_characteristics.end()) {
    nclx->transfer_characteristics = static_cast<heif_transfer_characteristics>(n);
  }
  else {
    nclx->transfer_characteristics = heif_transfer_characteristic_unspecified;
    return Error(heif_error_Invalid_input, heif_suberror_Unknown_NCLX_transfer_characteristics).error_struct(nullptr);
  }

  return Error::Ok.error_struct(nullptr);
}


static const std::set<typename std::underlying_type<heif_matrix_coefficients>::type> known_matrix_coefficients{
    heif_matrix_coefficients_RGB_GBR,
    heif_matrix_coefficients_ITU_R_BT_709_5,
    heif_matrix_coefficients_unspecified,
    heif_matrix_coefficients_US_FCC_T47,
    heif_matrix_coefficients_ITU_R_BT_470_6_System_B_G,
    heif_matrix_coefficients_ITU_R_BT_601_6,
    heif_matrix_coefficients_SMPTE_240M,
    heif_matrix_coefficients_YCgCo,
    heif_matrix_coefficients_ITU_R_BT_2020_2_non_constant_luminance,
    heif_matrix_coefficients_ITU_R_BT_2020_2_constant_luminance,
    heif_matrix_coefficients_SMPTE_ST_2085,
    heif_matrix_coefficients_chromaticity_derived_non_constant_luminance,
    heif_matrix_coefficients_chromaticity_derived_constant_luminance,
    heif_matrix_coefficients_ICtCp
};

struct heif_error heif_nclx_color_profile_set_matrix_coefficients(struct heif_color_profile_nclx* nclx, uint16_t mc)
{
  if (static_cast<std::underlying_type<heif_color_primaries>::type>(mc) > std::numeric_limits<std::underlying_type<heif_matrix_coefficients>::type>::max()) {
    return Error(heif_error_Invalid_input, heif_suberror_Unknown_NCLX_matrix_coefficients).error_struct(nullptr);
  }

  auto n = static_cast<typename std::underlying_type<heif_matrix_coefficients>::type>(mc);
  if (known_matrix_coefficients.find(n) != known_matrix_coefficients.end()) {
    nclx->matrix_coefficients = static_cast<heif_matrix_coefficients>(n);;
  }
  else {
    nclx->matrix_coefficients = heif_matrix_coefficients_unspecified;
    return Error(heif_error_Invalid_input, heif_suberror_Unknown_NCLX_matrix_coefficients).error_struct(nullptr);
  }

  return Error::Ok.error_struct(nullptr);
}


struct heif_error heif_image_handle_get_nclx_color_profile(const struct heif_image_handle* handle,
                                                           struct heif_color_profile_nclx** out_data)
{
  if (!out_data) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(handle->image.get());
  }

  auto nclx_profile = handle->image->get_color_profile_nclx();
  if (!nclx_profile) {
    Error err(heif_error_Color_profile_does_not_exist,
              heif_suberror_Unspecified);
    return err.error_struct(handle->image.get());
  }

  Error err = nclx_profile->get_nclx_color_profile(out_data);

  return err.error_struct(handle->image.get());
}


struct heif_error heif_image_handle_get_raw_color_profile(const struct heif_image_handle* handle,
                                                          void* out_data)
{
  if (out_data == nullptr) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(handle->image.get());
  }

  auto raw_profile = handle->image->get_color_profile_icc();
  if (raw_profile) {
    memcpy(out_data,
           raw_profile->get_data().data(),
           raw_profile->get_data().size());
  }
  else {
    Error err(heif_error_Color_profile_does_not_exist,
              heif_suberror_Unspecified);
    return err.error_struct(handle->image.get());
  }

  return Error::Ok.error_struct(handle->image.get());
}


struct heif_color_profile_nclx* heif_nclx_color_profile_alloc()
{
  return color_profile_nclx::alloc_nclx_color_profile();
}


void heif_nclx_color_profile_free(struct heif_color_profile_nclx* nclx_profile)
{
  color_profile_nclx::free_nclx_color_profile(nclx_profile);
}


int heif_image_handle_get_mastering_display_colour_volume(const struct heif_image_handle* handle, struct heif_mastering_display_colour_volume* out)
{
  auto mdcv = handle->image->get_property<Box_mdcv>();
  if (out && mdcv) {
    *out = mdcv->mdcv;
  }

  return mdcv ? 1 : 0;
}

int heif_image_has_content_light_level(const struct heif_image* image)
{
  return image->image->has_clli();
}

void heif_image_get_content_light_level(const struct heif_image* image, struct heif_content_light_level* out)
{
  if (out) {
    *out = image->image->get_clli();
  }
}

int heif_image_handle_get_content_light_level(const struct heif_image_handle* handle, struct heif_content_light_level* out)
{
  auto clli = handle->image->get_property<Box_clli>();
  if (out && clli) {
    *out = clli->clli;
  }

  return clli ? 1 : 0;
}

void heif_image_set_content_light_level(const struct heif_image* image, const struct heif_content_light_level* in)
{
  if (in == nullptr) {
    return;
  }

  image->image->set_clli(*in);
}


int heif_image_has_mastering_display_colour_volume(const struct heif_image* image)
{
  return image->image->has_mdcv();
}

void heif_image_get_mastering_display_colour_volume(const struct heif_image* image, struct heif_mastering_display_colour_volume* out)
{
  *out = image->image->get_mdcv();
}


void heif_image_set_mastering_display_colour_volume(const struct heif_image* image, const struct heif_mastering_display_colour_volume* in)
{
  if (in == nullptr) {
    return;
  }

  image->image->set_mdcv(*in);
}

float mdcv_coord_decode_x(uint16_t coord)
{
  // check for unspecified value
  if (coord < 5 || coord > 37000) {
    return 0.0f;
  }

  return (float) (coord * 0.00002);
}

float mdcv_coord_decode_y(uint16_t coord)
{
  // check for unspecified value
  if (coord < 5 || coord > 42000) {
    return 0.0f;
  }

  return (float) (coord * 0.00002);
}

struct heif_error heif_mastering_display_colour_volume_decode(const struct heif_mastering_display_colour_volume* in,
                                                              struct heif_decoded_mastering_display_colour_volume* out)
{
  if (in == nullptr || out == nullptr) {
    return error_null_parameter;
  }

  for (int c = 0; c < 3; c++) {
    out->display_primaries_x[c] = mdcv_coord_decode_x(in->display_primaries_x[c]);
    out->display_primaries_y[c] = mdcv_coord_decode_y(in->display_primaries_y[c]);
  }

  out->white_point_x = mdcv_coord_decode_x(in->white_point_x);
  out->white_point_y = mdcv_coord_decode_y(in->white_point_y);

  if (in->max_display_mastering_luminance < 50000 || in->max_display_mastering_luminance > 100000000) {
    out->max_display_mastering_luminance = 0;
  }
  else {
    out->max_display_mastering_luminance = in->max_display_mastering_luminance * 0.0001;
  }

  if (in->min_display_mastering_luminance < 1 || in->min_display_mastering_luminance > 50000) {
    out->min_display_mastering_luminance = 0;
  }
  else {
    out->min_display_mastering_luminance = in->min_display_mastering_luminance * 0.0001;
  }

  return heif_error_success;
}


