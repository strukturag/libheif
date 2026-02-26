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
