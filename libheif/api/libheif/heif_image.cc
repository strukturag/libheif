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

#include "heif_image.h"
#include "heif.h"
#include "pixelimage.h"
#include "api_structs.h"
#include "error.h"
#include <limits>

#include <algorithm>
#include <memory>
#include <utility>
#include <cstring>
#include <array>



heif_colorspace heif_image_get_colorspace(const struct heif_image* img)
{
  return img->image->get_colorspace();
}

enum heif_chroma heif_image_get_chroma_format(const struct heif_image* img)
{
  return img->image->get_chroma_format();
}


static int uint32_to_int(uint32_t v)
{
  if (v == 0 || v > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
    return -1;
  }
  else {
    return static_cast<int>(v);
  }
}


int heif_image_get_width(const struct heif_image* img, enum heif_channel channel)
{
  return uint32_to_int(img->image->get_width(channel));
}


int heif_image_get_height(const struct heif_image* img, enum heif_channel channel)
{
  return uint32_to_int(img->image->get_height(channel));
}


int heif_image_get_primary_width(const struct heif_image* img)
{
  if (img->image->get_colorspace() == heif_colorspace_RGB) {
    if (img->image->get_chroma_format() == heif_chroma_444) {
      return uint32_to_int(img->image->get_width(heif_channel_G));
    }
    else {
      return uint32_to_int(img->image->get_width(heif_channel_interleaved));
    }
  }
  else {
    return uint32_to_int(img->image->get_width(heif_channel_Y));
  }
}


int heif_image_get_primary_height(const struct heif_image* img)
{
  if (img->image->get_colorspace() == heif_colorspace_RGB) {
    if (img->image->get_chroma_format() == heif_chroma_444) {
      return uint32_to_int(img->image->get_height(heif_channel_G));
    }
    else {
      return uint32_to_int(img->image->get_height(heif_channel_interleaved));
    }
  }
  else {
    return uint32_to_int(img->image->get_height(heif_channel_Y));
  }
}


heif_error heif_image_crop(struct heif_image* img,
                           int left, int right, int top, int bottom)
{
  uint32_t w = img->image->get_width();
  uint32_t h = img->image->get_height();

  if (w==0 || w>0x7FFFFFFF ||
      h==0 || h>0x7FFFFFFF) {
    return heif_error{heif_error_Usage_error,
                      heif_suberror_Invalid_image_size,
                      "Image size exceeds maximum supported size"};
  }

  auto cropResult = img->image->crop(left, static_cast<int>(w) - 1 - right, top, static_cast<int>(h) - 1 - bottom, nullptr);
  if (cropResult.error) {
    return cropResult.error.error_struct(img->image.get());
  }

  img->image = cropResult.value;

  return heif_error_success;
}


struct heif_error heif_image_extract_area(const heif_image* srcimg,
                                          uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                                          const heif_security_limits* limits,
                                          struct heif_image** out_image)
{
  auto extractResult = srcimg->image->extract_image_area(x0,y0,w,h, limits);
  if (extractResult.error) {
    return extractResult.error.error_struct(srcimg->image.get());
  }

  heif_image* area = new heif_image;
  area->image = extractResult.value;

  *out_image = area;

  return heif_error_success;
}


int heif_image_get_bits_per_pixel(const struct heif_image* img, enum heif_channel channel)
{
  return img->image->get_storage_bits_per_pixel(channel);
}


int heif_image_get_bits_per_pixel_range(const struct heif_image* img, enum heif_channel channel)
{
  return img->image->get_bits_per_pixel(channel);
}


int heif_image_has_channel(const struct heif_image* img, enum heif_channel channel)
{
  return img->image->has_channel(channel);
}


const uint8_t* heif_image_get_plane_readonly(const struct heif_image* image,
                                             enum heif_channel channel,
                                             int* out_stride)
{
  if (!out_stride) {
    return nullptr;
  }

  if (!image || !image->image) {
    *out_stride = 0;
    return nullptr;
  }

  size_t stride;
  const auto* p = image->image->get_plane(channel, &stride);

  // TODO: use C++20 std::cmp_greater()
  if (stride > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    return nullptr;
  }

  *out_stride = static_cast<int>(stride);
  return p;
}


uint8_t* heif_image_get_plane(struct heif_image* image,
                              enum heif_channel channel,
                              int* out_stride)
{
  if (!out_stride) {
    return nullptr;
  }

  if (!image || !image->image) {
    *out_stride = 0;
    return nullptr;
  }

  size_t stride;
  uint8_t* p = image->image->get_plane(channel, &stride);

  // TODO: use C++20 std::cmp_greater()
  if (stride > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    return nullptr;
  }

  *out_stride = static_cast<int>(stride);
  return p;
}


const uint8_t* heif_image_get_plane_readonly2(const struct heif_image* image,
                                              enum heif_channel channel,
                                              size_t* out_stride)
{
  if (!out_stride) {
    return nullptr;
  }

  if (!image || !image->image) {
    *out_stride = 0;
    return nullptr;
  }

  return image->image->get_plane(channel, out_stride);
}


uint8_t* heif_image_get_plane2(struct heif_image* image,
                               enum heif_channel channel,
                               size_t* out_stride)
{
  if (!out_stride) {
    return nullptr;
  }

  if (!image || !image->image) {
    *out_stride = 0;
    return nullptr;
  }

  return image->image->get_plane(channel, out_stride);
}


struct heif_error heif_image_scale_image(const struct heif_image* input,
                                         struct heif_image** output,
                                         int width, int height,
                                         const struct heif_scaling_options* options)
{
  std::shared_ptr<HeifPixelImage> out_img;

  Error err = input->image->scale_nearest_neighbor(out_img, width, height, nullptr);
  if (err) {
    return err.error_struct(input->image.get());
  }

  *output = new heif_image;
  (*output)->image = std::move(out_img);

  return Error::Ok.error_struct(input->image.get());
}


struct heif_error heif_image_extend_to_size_fill_with_zero(struct heif_image* image,
                                                           uint32_t width, uint32_t height)
{
  Error err = image->image->extend_to_size_with_zero(width, height, nullptr);
  if (err) {
    return err.error_struct(image->image.get());
  }

  return heif_error_ok;
}


int heif_image_get_decoding_warnings(struct heif_image* image,
                                     int first_warning_idx,
                                     struct heif_error* out_warnings,
                                     int max_output_buffer_entries)
{
  if (max_output_buffer_entries == 0) {
    return (int) image->image->get_warnings().size();
  }
  else {
    const auto& warnings = image->image->get_warnings();
    int n;
    for (n = 0; n + first_warning_idx < (int) warnings.size(); n++) {
      out_warnings[n] = warnings[n + first_warning_idx].error_struct(image->image.get());
    }
    return n;
  }
}

void heif_image_add_decoding_warning(struct heif_image* image,
                                     struct heif_error err)
{
  image->image->add_warning(Error(err.code, err.subcode));
}


void heif_image_release(const struct heif_image* img)
{
  delete img;
}


void heif_image_get_pixel_aspect_ratio(const struct heif_image* image, uint32_t* aspect_h, uint32_t* aspect_v)
{
  image->image->get_pixel_ratio(aspect_h, aspect_v);
}


void heif_image_set_pixel_aspect_ratio(struct heif_image* image, uint32_t aspect_h, uint32_t aspect_v)
{
  image->image->set_pixel_ratio(aspect_h, aspect_v);
}


struct heif_error heif_image_create(int width, int height,
                                    heif_colorspace colorspace,
                                    heif_chroma chroma,
                                    struct heif_image** image)
{
  if (image == nullptr) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "heif_image_create: NULL passed as image pointer."};
  }

  // auto-correct colorspace_YCbCr + chroma_monochrome to colorspace_monochrome
  // TODO: this should return an error in a later version (see below)
  if (chroma == heif_chroma_monochrome && colorspace == heif_colorspace_YCbCr) {
    colorspace = heif_colorspace_monochrome;

    std::cerr << "libheif warning: heif_image_create() used with an illegal colorspace/chroma combination. This will return an error in a future version.\n";
  }

  // return error if invalid colorspace + chroma combination is used
  auto validChroma = get_valid_chroma_values_for_colorspace(colorspace);
  if (std::find(validChroma.begin(), validChroma.end(), chroma) == validChroma.end()) {
    *image = nullptr;
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "Invalid colorspace/chroma combination."};
  }

  struct heif_image* img = new heif_image;
  img->image = std::make_shared<HeifPixelImage>();

  img->image->create(width, height, colorspace, chroma);

  *image = img;

  return heif_error_success;
}

struct heif_error heif_image_add_plane(struct heif_image* image,
                                       heif_channel channel, int width, int height, int bit_depth)
{
  // Note: no security limit, because this is explicitly requested by the user.
  if (auto err = image->image->add_plane(channel, width, height, bit_depth, nullptr)) {
    return err.error_struct(image->image.get());
  }
  else {
    return heif_error_success;
  }
}


struct heif_error heif_image_add_plane_safe(struct heif_image* image,
                                            heif_channel channel, int width, int height, int bit_depth,
                                            const heif_security_limits* limits)
{
  if (auto err = image->image->add_plane(channel, width, height, bit_depth, limits)) {
    return err.error_struct(image->image.get());
  }
  else {
    return heif_error_success;
  }
}


void heif_image_set_premultiplied_alpha(struct heif_image* image,
                                        int is_premultiplied_alpha)
{
  if (image == nullptr) {
    return;
  }

  image->image->set_premultiplied_alpha(is_premultiplied_alpha);
}


int heif_image_is_premultiplied_alpha(struct heif_image* image)
{
  if (image == nullptr) {
    return 0;
  }

  return image->image->is_premultiplied_alpha();
}


struct heif_error heif_image_extend_padding_to_size(struct heif_image* image, int min_physical_width, int min_physical_height)
{
  Error err = image->image->extend_padding_to_size(min_physical_width, min_physical_height, false, nullptr);
  if (err) {
    return err.error_struct(image->image.get());
  }
  else {
    return heif_error_success;
  }
}


