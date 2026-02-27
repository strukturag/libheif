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

#include "heif_uncompressed.h"
#include "context.h"
#include "api_structs.h"
#include "pixelimage.h"
#include "image-items/unc_image.h"

#include <array>
#include <cstring>
#include <memory>
#include <algorithm>


heif_error heif_image_set_bayer_pattern(heif_image* image,
                                        uint16_t pattern_width,
                                        uint16_t pattern_height,
                                        const struct heif_bayer_pattern_pixel* patternPixels)
{
  if (image == nullptr || patternPixels == nullptr) {
    return heif_error_null_pointer_argument;
  }

  if (pattern_width == 0 || pattern_height == 0) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "Bayer pattern dimensions must be non-zero."};
  }

  BayerPattern pattern;
  pattern.pattern_width = pattern_width;
  pattern.pattern_height = pattern_height;

  size_t num_pixels = size_t{pattern_width} * pattern_height;
  pattern.pixels.assign(patternPixels, patternPixels + num_pixels);

  image->image->set_bayer_pattern(pattern);

  return heif_error_success;
}


int heif_image_has_bayer_pattern(const heif_image* image,
                                 uint16_t* out_pattern_width,
                                 uint16_t* out_pattern_height)
{
  if (image == nullptr || !image->image->has_bayer_pattern()) {
    if (out_pattern_width) {
      *out_pattern_width = 0;
    }
    if (out_pattern_height) {
      *out_pattern_height = 0;
    }
    return 0;
  }

  const BayerPattern& pattern = image->image->get_bayer_pattern();

  if (out_pattern_width) {
    *out_pattern_width = pattern.pattern_width;
  }
  if (out_pattern_height) {
    *out_pattern_height = pattern.pattern_height;
  }

  return 1;
}


heif_error heif_image_get_bayer_pattern(const heif_image* image,
                                        struct heif_bayer_pattern_pixel* out_patternPixels)
{
  if (image == nullptr || out_patternPixels == nullptr) {
    return heif_error_null_pointer_argument;
  }

  if (!image->image->has_bayer_pattern()) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "Image does not have a Bayer pattern."};
  }

  const BayerPattern& pattern = image->image->get_bayer_pattern();
  size_t num_pixels = size_t{pattern.pattern_width} * pattern.pattern_height;
  std::copy(pattern.pixels.begin(), pattern.pixels.begin() + num_pixels, out_patternPixels);

  return heif_error_success;
}


float heif_polarization_angle_no_filter()
{
  uint32_t bits = 0xFFFFFFFF;
  float f;
  memcpy(&f, &bits, sizeof(f));
  return f;
}


int heif_polarization_angle_is_no_filter(float angle)
{
  uint32_t bits;
  memcpy(&bits, &angle, sizeof(bits));
  return bits == 0xFFFFFFFF;
}


heif_error heif_image_add_polarization_pattern(heif_image* image,
                                               uint32_t num_component_indices,
                                               const uint32_t* component_indices,
                                               uint16_t pattern_width,
                                               uint16_t pattern_height,
                                               const float* polarization_angles)
{
  if (image == nullptr || polarization_angles == nullptr) {
    return heif_error_null_pointer_argument;
  }

  if (num_component_indices > 0 && component_indices == nullptr) {
    return heif_error_null_pointer_argument;
  }

  if (pattern_width == 0 || pattern_height == 0) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "Polarization pattern dimensions must be non-zero."};
  }

  PolarizationPattern pattern;
  pattern.component_indices.assign(component_indices, component_indices + num_component_indices);
  pattern.pattern_width = pattern_width;
  pattern.pattern_height = pattern_height;

  size_t num_pixels = size_t{pattern_width} * pattern_height;
  pattern.polarization_angles.assign(polarization_angles, polarization_angles + num_pixels);

  image->image->add_polarization_pattern(pattern);

  return heif_error_success;
}


int heif_image_get_number_of_polarization_patterns(const heif_image* image)
{
  if (image == nullptr) {
    return 0;
  }

  return static_cast<int>(image->image->get_polarization_patterns().size());
}


heif_error heif_image_get_polarization_pattern_info(const heif_image* image,
                                                    int pattern_index,
                                                    uint32_t* out_num_component_indices,
                                                    uint16_t* out_pattern_width,
                                                    uint16_t* out_pattern_height)
{
  if (image == nullptr) {
    return heif_error_null_pointer_argument;
  }

  const auto& patterns = image->image->get_polarization_patterns();
  if (pattern_index < 0 || static_cast<size_t>(pattern_index) >= patterns.size()) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "Polarization pattern index out of range."};
  }

  const auto& p = patterns[pattern_index];
  if (out_num_component_indices) {
    *out_num_component_indices = static_cast<uint32_t>(p.component_indices.size());
  }
  if (out_pattern_width) {
    *out_pattern_width = p.pattern_width;
  }
  if (out_pattern_height) {
    *out_pattern_height = p.pattern_height;
  }

  return heif_error_success;
}


heif_error heif_image_get_polarization_pattern_data(const heif_image* image,
                                                    int pattern_index,
                                                    uint32_t* out_component_indices,
                                                    float* out_polarization_angles)
{
  if (image == nullptr || out_polarization_angles == nullptr) {
    return heif_error_null_pointer_argument;
  }

  const auto& patterns = image->image->get_polarization_patterns();
  if (pattern_index < 0 || static_cast<size_t>(pattern_index) >= patterns.size()) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "Polarization pattern index out of range."};
  }

  const auto& p = patterns[pattern_index];

  if (out_component_indices && !p.component_indices.empty()) {
    std::copy(p.component_indices.begin(), p.component_indices.end(), out_component_indices);
  }

  size_t num_pixels = size_t{p.pattern_width} * p.pattern_height;
  std::copy(p.polarization_angles.begin(), p.polarization_angles.begin() + num_pixels, out_polarization_angles);

  return heif_error_success;
}


int heif_image_get_polarization_pattern_index_for_component(const heif_image* image,
                                                            uint32_t component_index)
{
  if (image == nullptr) {
    return -1;
  }

  const auto& patterns = image->image->get_polarization_patterns();
  for (size_t i = 0; i < patterns.size(); i++) {
    const auto& p = patterns[i];
    if (p.component_indices.empty()) {
      // Empty component list means pattern applies to all components.
      return static_cast<int>(i);
    }
    for (uint32_t idx : p.component_indices) {
      if (idx == component_index) {
        return static_cast<int>(i);
      }
    }
  }

  return -1;
}


heif_error heif_image_add_sensor_bad_pixels_map(heif_image* image,
                                                 uint32_t num_component_indices,
                                                 const uint32_t* component_indices,
                                                 int correction_applied,
                                                 uint32_t num_bad_rows,
                                                 const uint32_t* bad_rows,
                                                 uint32_t num_bad_columns,
                                                 const uint32_t* bad_columns,
                                                 uint32_t num_bad_pixels,
                                                 const struct heif_bad_pixel* bad_pixels)
{
  if (image == nullptr) {
    return heif_error_null_pointer_argument;
  }

  if (num_component_indices > 0 && component_indices == nullptr) {
    return heif_error_null_pointer_argument;
  }

  if (num_bad_rows > 0 && bad_rows == nullptr) {
    return heif_error_null_pointer_argument;
  }

  if (num_bad_columns > 0 && bad_columns == nullptr) {
    return heif_error_null_pointer_argument;
  }

  if (num_bad_pixels > 0 && bad_pixels == nullptr) {
    return heif_error_null_pointer_argument;
  }

  SensorBadPixelsMap map;
  map.component_indices.assign(component_indices, component_indices + num_component_indices);
  map.correction_applied = (correction_applied != 0);

  map.bad_rows.assign(bad_rows, bad_rows + num_bad_rows);
  map.bad_columns.assign(bad_columns, bad_columns + num_bad_columns);

  map.bad_pixels.resize(num_bad_pixels);
  for (uint32_t i = 0; i < num_bad_pixels; i++) {
    map.bad_pixels[i].row = bad_pixels[i].row;
    map.bad_pixels[i].column = bad_pixels[i].column;
  }

  image->image->add_sensor_bad_pixels_map(map);

  return heif_error_success;
}


int heif_image_get_number_of_sensor_bad_pixels_maps(const heif_image* image)
{
  if (image == nullptr) {
    return 0;
  }

  return static_cast<int>(image->image->get_sensor_bad_pixels_maps().size());
}


heif_error heif_image_get_sensor_bad_pixels_map_info(const heif_image* image,
                                                      int map_index,
                                                      uint32_t* out_num_component_indices,
                                                      int* out_correction_applied,
                                                      uint32_t* out_num_bad_rows,
                                                      uint32_t* out_num_bad_columns,
                                                      uint32_t* out_num_bad_pixels)
{
  if (image == nullptr) {
    return heif_error_null_pointer_argument;
  }

  const auto& maps = image->image->get_sensor_bad_pixels_maps();
  if (map_index < 0 || static_cast<size_t>(map_index) >= maps.size()) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "Sensor bad pixels map index out of range."};
  }

  const auto& m = maps[map_index];
  if (out_num_component_indices) {
    *out_num_component_indices = static_cast<uint32_t>(m.component_indices.size());
  }
  if (out_correction_applied) {
    *out_correction_applied = m.correction_applied ? 1 : 0;
  }
  if (out_num_bad_rows) {
    *out_num_bad_rows = static_cast<uint32_t>(m.bad_rows.size());
  }
  if (out_num_bad_columns) {
    *out_num_bad_columns = static_cast<uint32_t>(m.bad_columns.size());
  }
  if (out_num_bad_pixels) {
    *out_num_bad_pixels = static_cast<uint32_t>(m.bad_pixels.size());
  }

  return heif_error_success;
}


heif_error heif_image_get_sensor_bad_pixels_map_data(const heif_image* image,
                                                      int map_index,
                                                      uint32_t* out_component_indices,
                                                      uint32_t* out_bad_rows,
                                                      uint32_t* out_bad_columns,
                                                      struct heif_bad_pixel* out_bad_pixels)
{
  if (image == nullptr) {
    return heif_error_null_pointer_argument;
  }

  const auto& maps = image->image->get_sensor_bad_pixels_maps();
  if (map_index < 0 || static_cast<size_t>(map_index) >= maps.size()) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "Sensor bad pixels map index out of range."};
  }

  const auto& m = maps[map_index];

  if (out_component_indices && !m.component_indices.empty()) {
    std::copy(m.component_indices.begin(), m.component_indices.end(), out_component_indices);
  }

  if (out_bad_rows && !m.bad_rows.empty()) {
    std::copy(m.bad_rows.begin(), m.bad_rows.end(), out_bad_rows);
  }

  if (out_bad_columns && !m.bad_columns.empty()) {
    std::copy(m.bad_columns.begin(), m.bad_columns.end(), out_bad_columns);
  }

  if (out_bad_pixels && !m.bad_pixels.empty()) {
    for (size_t i = 0; i < m.bad_pixels.size(); i++) {
      out_bad_pixels[i].row = m.bad_pixels[i].row;
      out_bad_pixels[i].column = m.bad_pixels[i].column;
    }
  }

  return heif_error_success;
}


heif_error heif_image_add_sensor_nuc(heif_image* image,
                                      uint32_t num_component_indices,
                                      const uint32_t* component_indices,
                                      int nuc_is_applied,
                                      uint32_t image_width,
                                      uint32_t image_height,
                                      const float* nuc_gains,
                                      const float* nuc_offsets)
{
  if (image == nullptr || nuc_gains == nullptr || nuc_offsets == nullptr) {
    return heif_error_null_pointer_argument;
  }

  if (num_component_indices > 0 && component_indices == nullptr) {
    return heif_error_null_pointer_argument;
  }

  if (image_width == 0 || image_height == 0) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "NUC image dimensions must be non-zero."};
  }

  SensorNonUniformityCorrection nuc;
  nuc.component_indices.assign(component_indices, component_indices + num_component_indices);
  nuc.nuc_is_applied = (nuc_is_applied != 0);
  nuc.image_width = image_width;
  nuc.image_height = image_height;

  size_t num_pixels = size_t{image_width} * image_height;
  nuc.nuc_gains.assign(nuc_gains, nuc_gains + num_pixels);
  nuc.nuc_offsets.assign(nuc_offsets, nuc_offsets + num_pixels);

  image->image->add_sensor_nuc(nuc);

  return heif_error_success;
}


int heif_image_get_number_of_sensor_nucs(const heif_image* image)
{
  if (image == nullptr) {
    return 0;
  }

  return static_cast<int>(image->image->get_sensor_nuc().size());
}


heif_error heif_image_get_sensor_nuc_info(const heif_image* image,
                                           int nuc_index,
                                           uint32_t* out_num_component_indices,
                                           int* out_nuc_is_applied,
                                           uint32_t* out_image_width,
                                           uint32_t* out_image_height)
{
  if (image == nullptr) {
    return heif_error_null_pointer_argument;
  }

  const auto& nucs = image->image->get_sensor_nuc();
  if (nuc_index < 0 || static_cast<size_t>(nuc_index) >= nucs.size()) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "Sensor NUC index out of range."};
  }

  const auto& n = nucs[nuc_index];
  if (out_num_component_indices) {
    *out_num_component_indices = static_cast<uint32_t>(n.component_indices.size());
  }
  if (out_nuc_is_applied) {
    *out_nuc_is_applied = n.nuc_is_applied ? 1 : 0;
  }
  if (out_image_width) {
    *out_image_width = n.image_width;
  }
  if (out_image_height) {
    *out_image_height = n.image_height;
  }

  return heif_error_success;
}


heif_error heif_image_get_sensor_nuc_data(const heif_image* image,
                                           int nuc_index,
                                           uint32_t* out_component_indices,
                                           float* out_nuc_gains,
                                           float* out_nuc_offsets)
{
  if (image == nullptr) {
    return heif_error_null_pointer_argument;
  }

  const auto& nucs = image->image->get_sensor_nuc();
  if (nuc_index < 0 || static_cast<size_t>(nuc_index) >= nucs.size()) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "Sensor NUC index out of range."};
  }

  const auto& n = nucs[nuc_index];

  if (out_component_indices && !n.component_indices.empty()) {
    std::copy(n.component_indices.begin(), n.component_indices.end(), out_component_indices);
  }

  size_t num_pixels = size_t{n.image_width} * n.image_height;

  if (out_nuc_gains && !n.nuc_gains.empty()) {
    std::copy(n.nuc_gains.begin(), n.nuc_gains.begin() + num_pixels, out_nuc_gains);
  }

  if (out_nuc_offsets && !n.nuc_offsets.empty()) {
    std::copy(n.nuc_offsets.begin(), n.nuc_offsets.begin() + num_pixels, out_nuc_offsets);
  }

  return heif_error_success;
}


heif_error heif_image_set_chroma_location(heif_image* image, uint8_t chroma_location)
{
  if (image == nullptr) {
    return heif_error_null_pointer_argument;
  }

  if (chroma_location > 6) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "Chroma location must be in the range 0-6."};
  }

  image->image->set_chroma_location(chroma_location);

  return heif_error_success;
}


int heif_image_has_chroma_location(const heif_image* image)
{
  if (image == nullptr) {
    return 0;
  }

  return image->image->has_chroma_location() ? 1 : 0;
}


uint8_t heif_image_get_chroma_location(const heif_image* image)
{
  if (image == nullptr) {
    return 0;
  }

  return image->image->get_chroma_location();
}


heif_unci_image_parameters* heif_unci_image_parameters_alloc()
{
  auto* params = new heif_unci_image_parameters();

  params->version = 1;

  // --- version 1

  params->image_width = 0;
  params->image_height = 0;

  // TODO: should we define that tile size = 0 means no tiling?
  params->tile_width = 0;
  params->tile_height = 0;

  params->compression = heif_unci_compression_off;

  return params;
}


void heif_unci_image_parameters_copy(heif_unci_image_parameters* dst,
                                     const heif_unci_image_parameters* src)
{
  if (src == nullptr || dst == nullptr) {
    return;
  }

  int min_version = std::min(src->version, dst->version);

  switch (min_version) {
    case 1:
      dst->image_width = src->image_width;
      dst->image_height = src->image_height;
      dst->tile_width = src->tile_width;
      dst->tile_height = src->tile_height;
      dst->compression = src->compression;
      break;
  }
}


void heif_unci_image_parameters_release(heif_unci_image_parameters* params)
{
  delete params;
}


// --- index-based component access

uint32_t heif_image_get_number_of_used_components(const heif_image* image)
{
  if (!image || !image->image) {
    return 0;
  }
  return image->image->get_number_of_used_components();
}


uint32_t heif_image_get_total_number_of_cmpd_components(const heif_image* image)
{
  if (!image || !image->image) {
    return 0;
  }
  return image->image->get_total_number_of_cmpd_components();
}


void heif_image_get_used_component_indices(const heif_image* image, uint32_t* out_component_indices)
{
  if (!image || !image->image || !out_component_indices) {
    return;
  }

  auto indices = image->image->get_used_component_indices();
  for (size_t i = 0; i < indices.size(); i++) {
    out_component_indices[i] = indices[i];
  }
}


heif_channel heif_image_get_component_channel(const heif_image* image, uint32_t component_idx)
{
  if (!image || !image->image) {
    return heif_channel_Y;
  }
  return image->image->get_component_channel(component_idx);
}


uint32_t heif_image_get_component_width(const heif_image* image, uint32_t component_idx)
{
  if (!image || !image->image) {
    return 0;
  }
  return image->image->get_component_width(component_idx);
}


uint32_t heif_image_get_component_height(const heif_image* image, uint32_t component_idx)
{
  if (!image || !image->image) {
    return 0;
  }
  return image->image->get_component_height(component_idx);
}


int heif_image_get_component_bits_per_pixel(const heif_image* image, uint32_t component_idx)
{
  if (!image || !image->image) {
    return 0;
  }
  return image->image->get_component_bits_per_pixel(component_idx);
}


uint16_t heif_image_get_component_type(const heif_image* image, uint32_t component_idx)
{
  if (!image || !image->image) {
    return 0;
  }
  return image->image->get_component_type(component_idx);
}


heif_error heif_image_add_component(heif_image* image,
                                    int width, int height,
                                    uint16_t component_type,
                                    heif_channel_datatype datatype,
                                    int bit_depth,
                                    uint32_t* out_component_idx)
{
  if (!image || !image->image) {
    return heif_error_null_pointer_argument;
  }

  auto result = image->image->add_component(width, height, component_type, datatype, bit_depth, nullptr);
  if (!result) {
    return result.error_struct(image->image.get());
  }

  if (out_component_idx) {
    *out_component_idx = *result;
  }

  return heif_error_success;
}


const uint8_t* heif_image_get_component_readonly(const heif_image* image, uint32_t component_idx, size_t* out_stride)
{
  if (!image || !image->image) {
    if (out_stride) *out_stride = 0;
    return nullptr;
  }
  return image->image->get_component(component_idx, out_stride);
}


uint8_t* heif_image_get_component(heif_image* image, uint32_t component_idx, size_t* out_stride)
{
  if (!image || !image->image) {
    if (out_stride) *out_stride = 0;
    return nullptr;
  }
  return image->image->get_component(component_idx, out_stride);
}


#define heif_image_get_component_X(name, type) \
const type* heif_image_get_component_ ## name ## _readonly(const struct heif_image* image, \
                                                            uint32_t component_idx, \
                                                            size_t* out_stride) \
{                                                            \
  if (!image || !image->image) {                             \
    if (out_stride) *out_stride = 0;                         \
    return nullptr;                                          \
  }                                                          \
  return image->image->get_component_data<type>(component_idx, out_stride); \
}                                                            \
                                                             \
type* heif_image_get_component_ ## name (struct heif_image* image, \
                                         uint32_t component_idx,  \
                                         size_t* out_stride)      \
{                                                            \
  if (!image || !image->image) {                             \
    if (out_stride) *out_stride = 0;                         \
    return nullptr;                                          \
  }                                                          \
  return image->image->get_component_data<type>(component_idx, out_stride); \
}

heif_image_get_component_X(uint16, uint16_t)
heif_image_get_component_X(uint32, uint32_t)
heif_image_get_component_X(uint64, uint64_t)
heif_image_get_component_X(int8, int8_t)
heif_image_get_component_X(int16, int16_t)
heif_image_get_component_X(int32, int32_t)
heif_image_get_component_X(int64, int64_t)
heif_image_get_component_X(float32, float)
heif_image_get_component_X(float64, double)
heif_image_get_component_X(complex32, heif_complex32)
heif_image_get_component_X(complex64, heif_complex64)


// --- GIMI component content IDs

int heif_image_has_component_content_ids(const heif_image* image)
{
  if (!image || !image->image) {
    return 0;
  }
  return static_cast<int>(image->image->get_component_content_ids().size());
}


const char* heif_image_get_component_content_id(const heif_image* image, uint32_t component_idx)
{
  if (!image || !image->image || !image->image->has_component_content_ids()) {
    return nullptr;
  }

  const auto& ids = image->image->get_component_content_ids();
  if (component_idx >= ids.size()) {
    return nullptr;
  }

  char* idstring = new char[ids[component_idx].size() + 1];
  strcpy(idstring, ids[component_idx].c_str());
  return idstring;
}


heif_error heif_image_set_component_content_id(heif_image* image,
                                               uint32_t component_idx,
                                               const char* content_id)
{
  if (!image || !image->image || !content_id) {
    return heif_error_null_pointer_argument;
  }

  auto ids = image->image->get_component_content_ids();
  if (component_idx >= ids.size()) {
    ids.resize(component_idx + 1);
  }
  ids[component_idx] = content_id;
  image->image->set_component_content_ids(ids);

  return heif_error_success;
}


heif_error heif_context_add_empty_unci_image(heif_context* ctx,
                                                    const heif_unci_image_parameters* parameters,
                                                    const heif_encoding_options* encoding_options,
                                                    const heif_image* prototype,
                                                    heif_image_handle** out_unci_image_handle)
{
#if WITH_UNCOMPRESSED_CODEC
  if (prototype == nullptr || out_unci_image_handle == nullptr) {
    return heif_error_null_pointer_argument;
  }

  heif_encoding_options* default_options = nullptr;
  if (encoding_options == nullptr) {
    default_options = heif_encoding_options_alloc();
    encoding_options = default_options;
  }

  Result<std::shared_ptr<ImageItem_uncompressed>> unciImageResult;
  unciImageResult = ImageItem_uncompressed::add_unci_item(ctx->context.get(), parameters, encoding_options, prototype->image);

  if (encoding_options) {
    heif_encoding_options_free(default_options);
  }

  if (!unciImageResult) {
    return unciImageResult.error_struct(ctx->context.get());
  }

  assert(out_unci_image_handle);
  *out_unci_image_handle = new heif_image_handle;
  (*out_unci_image_handle)->image = *unciImageResult;
  (*out_unci_image_handle)->context = ctx->context;

  return heif_error_success;
#else
  return {heif_error_Unsupported_feature,
          heif_suberror_Unspecified,
          "support for uncompressed images (ISO23001-17) has been disabled."};
#endif
}
