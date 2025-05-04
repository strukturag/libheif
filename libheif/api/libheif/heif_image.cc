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

#include "security_limits.h"
#include "common_utils.h"
#include <cstdint>
#include "heif.h"
#include "pixelimage.h"
#include "api_structs.h"
#include "error.h"
#include "init.h"
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

#ifdef _WIN32
// for _write
#include <io.h>
#else

#include <unistd.h>

#endif

#include <cassert>


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


void heif_image_get_pixel_aspect_ratio(const struct heif_image* image, uint32_t* aspect_h, uint32_t* aspect_v)
{
  image->image->get_pixel_ratio(aspect_h, aspect_v);
}

void heif_image_set_pixel_aspect_ratio(struct heif_image* image, uint32_t aspect_h, uint32_t aspect_v)
{
  image->image->set_pixel_ratio(aspect_h, aspect_v);
}


void heif_image_release(const struct heif_image* img)
{
  delete img;
}

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


struct heif_error heif_image_add_channel(struct heif_image* image,
                                         enum heif_channel channel,
                                         int width, int height,
                                         heif_channel_datatype datatype, int bit_depth)
{
  if (auto err = image->image->add_channel(channel, width, height, datatype, bit_depth, nullptr)) {
    return err.error_struct(image->image.get());
  }
  else {
    return heif_error_success;
  }
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


enum heif_channel_datatype heif_image_get_datatype(const struct heif_image* image, enum heif_channel channel)
{
  if (image == nullptr) {
    return heif_channel_datatype_undefined;
  }

  return image->image->get_datatype(channel);
}


int heif_image_list_channels(struct heif_image* image,
                             enum heif_channel** out_channels)
{
  if (!image || !out_channels) {
    return 0;
  }

  auto channels = image->image->get_channel_set();

  *out_channels = new heif_channel[channels.size()];
  heif_channel* p = *out_channels;
  for (heif_channel c : channels) {
    *p++ = c;
  }

  assert(channels.size() < static_cast<size_t>(std::numeric_limits<int>::max()));

  return static_cast<int>(channels.size());
}


void heif_channel_release_list(enum heif_channel** channels)
{
  delete[] channels;
}



#define heif_image_get_channel_X(name, type, datatype, bits) \
const type* heif_image_get_channel_ ## name ## _readonly(const struct heif_image* image, \
                                                         enum heif_channel channel, \
                                                         size_t* out_stride) \
{                                                            \
  if (!image || !image->image) {                             \
    *out_stride = 0;                                         \
    return nullptr;                                          \
  }                                                          \
                                                             \
  if (image->image->get_datatype(channel) != datatype) {     \
    return nullptr;                                          \
  }                                                          \
  if (image->image->get_storage_bits_per_pixel(channel) != bits) {     \
    return nullptr;                                          \
  }                                                          \
  return  image->image->get_channel<type>(channel, out_stride);                      \
}                                                            \
                                                             \
type* heif_image_get_channel_ ## name (struct heif_image* image, \
                                       enum heif_channel channel, \
                                       size_t* out_stride)      \
{                                                            \
  if (!image || !image->image) {                             \
    *out_stride = 0;                                         \
    return nullptr;                                          \
  }                                                          \
                                                             \
  if (image->image->get_datatype(channel) != datatype) {     \
    return nullptr;                                          \
  }                                                          \
  if (image->image->get_storage_bits_per_pixel(channel) != bits) {     \
    return nullptr;                                          \
  }                                                          \
  return image->image->get_channel<type>(channel, out_stride); \
}

heif_image_get_channel_X(uint16, uint16_t, heif_channel_datatype_unsigned_integer, 16)
heif_image_get_channel_X(uint32, uint32_t, heif_channel_datatype_unsigned_integer, 32)
heif_image_get_channel_X(uint64, uint64_t, heif_channel_datatype_unsigned_integer, 64)
heif_image_get_channel_X(int16, int16_t, heif_channel_datatype_signed_integer, 16)
heif_image_get_channel_X(int32, int32_t, heif_channel_datatype_signed_integer, 32)
heif_image_get_channel_X(int64, int64_t, heif_channel_datatype_signed_integer, 64)
heif_image_get_channel_X(float32, float, heif_channel_datatype_floating_point, 32)
heif_image_get_channel_X(float64, double, heif_channel_datatype_floating_point, 64)
heif_image_get_channel_X(complex32, heif_complex32, heif_channel_datatype_complex_number, 64)
heif_image_get_channel_X(complex64, heif_complex64, heif_channel_datatype_complex_number, 64)


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


struct heif_error heif_image_set_raw_color_profile(struct heif_image* image,
                                                   const char* color_profile_type_fourcc,
                                                   const void* profile_data,
                                                   const size_t profile_size)
{
  if (strlen(color_profile_type_fourcc) != 4) {
    heif_error err = {heif_error_Usage_error,
                      heif_suberror_Unspecified,
                      "Invalid color_profile_type (must be 4 characters)"};
    return err;
  }

  uint32_t color_profile_type = fourcc(color_profile_type_fourcc);

  std::vector<uint8_t> data;
  data.insert(data.end(),
              (const uint8_t*) profile_data,
              (const uint8_t*) profile_data + profile_size);

  auto color_profile = std::make_shared<color_profile_raw>(color_profile_type, data);

  image->image->set_color_profile_icc(color_profile);

  return heif_error_success;
}


struct heif_error heif_image_set_nclx_color_profile(struct heif_image* image,
                                                    const struct heif_color_profile_nclx* color_profile)
{
  auto nclx = std::make_shared<color_profile_nclx>();

  nclx->set_colour_primaries(color_profile->color_primaries);
  nclx->set_transfer_characteristics(color_profile->transfer_characteristics);
  nclx->set_matrix_coefficients(color_profile->matrix_coefficients);
  nclx->set_full_range_flag(color_profile->full_range_flag);

  image->image->set_color_profile_nclx(nclx);

  return heif_error_success;
}


/*
void heif_image_remove_color_profile(struct heif_image* image)
{
  image->image->set_color_profile(nullptr);
}
*/


enum heif_color_profile_type heif_image_get_color_profile_type(const struct heif_image* image)
{
  std::shared_ptr<const color_profile> profile;

  profile = image->image->get_color_profile_icc();
  if (!profile) {
    profile = image->image->get_color_profile_nclx();
  }

  if (!profile) {
    return heif_color_profile_type_not_present;
  }
  else {
    return (heif_color_profile_type) profile->get_type();
  }
}


size_t heif_image_get_raw_color_profile_size(const struct heif_image* image)
{
  auto raw_profile = image->image->get_color_profile_icc();
  if (raw_profile) {
    return raw_profile->get_data().size();
  }
  else {
    return 0;
  }
}


struct heif_error heif_image_get_raw_color_profile(const struct heif_image* image,
                                                   void* out_data)
{
  if (out_data == nullptr) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(image->image.get());
  }

  auto raw_profile = image->image->get_color_profile_icc();
  if (raw_profile) {
    memcpy(out_data,
           raw_profile->get_data().data(),
           raw_profile->get_data().size());
  }
  else {
    Error err(heif_error_Color_profile_does_not_exist,
              heif_suberror_Unspecified);
    return err.error_struct(image->image.get());
  }

  return Error::Ok.error_struct(image->image.get());
}


struct heif_error heif_image_get_nclx_color_profile(const struct heif_image* image,
                                                    struct heif_color_profile_nclx** out_data)
{
  if (!out_data) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(image->image.get());
  }

  auto nclx_profile = image->image->get_color_profile_nclx();

  if (!nclx_profile) {
    Error err(heif_error_Color_profile_does_not_exist,
              heif_suberror_Unspecified);
    return err.error_struct(image->image.get());
  }

  Error err = nclx_profile->get_nclx_color_profile(out_data);

  return err.error_struct(image->image.get());
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
