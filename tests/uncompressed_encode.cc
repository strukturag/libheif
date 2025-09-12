/*
  libheif integration tests for uncompressed encoder

  MIT License

  Copyright (c) 2023 Brad Hards <bradh@frogmouth.net>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include "catch_amalgamated.hpp"
#include "api_structs.h"
#include "libheif/heif.h"
#include <cstdint>
#include <string.h>
#include "test_utils.h"

TEST_CASE("check have uncompressed")
{
  struct heif_error err;
  heif_context *ctx = heif_context_alloc();
  heif_encoder *enc;
  err = heif_context_get_encoder_for_format(ctx, heif_compression_uncompressed,
                                            &enc);
  REQUIRE(err.code == heif_error_Ok);

  const char *name = heif_encoder_get_name(enc);
  REQUIRE(strcmp(name, "builtin") == 0);

  heif_encoder_release(enc);

  heif_context_free(ctx);
}


struct heif_image *createImage_Mono()
{
  struct heif_image *image;
  struct heif_error err;
  int w = 1024;
  int h = 768;
  err = heif_image_create(w, h, heif_colorspace_monochrome,
                          heif_chroma_monochrome, &image);
  if (err.code) {
    return nullptr;
  }

  err = heif_image_add_plane(image, heif_channel_Y, w, h, 8);
  REQUIRE(err.code == heif_error_Ok);

  int stride;
  uint8_t *p = heif_image_get_plane(image, heif_channel_Y, &stride);

  int y = 0;
  for (; y < h / 2; y++) {
    int x = 0;
    for (; x < w / 3; x++) {
      p[y * stride + x] = 255;
    }
    for (; x < 2 * w / 3; x++) {
      p[y * stride + x] = 127;
    }
    for (; x < w; x++) {
      p[y * stride + x] = 1;
    }
  }
  for (; y < h; y++) {
    int x = 0;
    for (; x < w / 3; x++) {
      p[y * stride + x] =  (uint8_t) (x % 256);
    }
    for (; x < 2 * w / 3; x++) {
      p[y * stride + x] = (uint8_t) ((255 - x) % 256);
    }
    for (; x < w; x++) {
      p[y * stride + x] =  (uint8_t) ((x + y) % 256);
    }
  }
  if (err.code) {
    heif_image_release(image);
    return nullptr;
  }

  return image;
}


struct heif_image *createImage_YCbCr()
{
  struct heif_image *image;
  struct heif_error err;
  int w = 1024;
  int h = 768;
  err = heif_image_create(w, h, heif_colorspace_YCbCr,
                          heif_chroma_444, &image);
  if (err.code) {
    return nullptr;
  }

  err = heif_image_add_plane(image, heif_channel_Y, w, h, 8);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_image_add_plane(image, heif_channel_Cb, w, h, 8);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_image_add_plane(image, heif_channel_Cr, w, h, 8);
  REQUIRE(err.code == heif_error_Ok);

  int stride;
  uint8_t *p = heif_image_get_plane(image, heif_channel_Y, &stride);
  uint8_t *cb = heif_image_get_plane(image, heif_channel_Cb, &stride);
  uint8_t *cr = heif_image_get_plane(image, heif_channel_Cr, &stride);

  int y = 0;
  for (; y < h / 2; y++)
  {
    int x = 0;
    for (; x < w / 3; x++) {
      p[y * stride + x] = 255;
      cb[y * stride + x] = 0;
      cr[y * stride + x] = 0;
    }
    for (; x < 2 * w / 3; x++) {
      p[y * stride + x] = 127;
      cb[y * stride + x] = 0;
      cr[y * stride + x] = 0;
    }
    for (; x < w; x++) {
      p[y * stride + x] = 1;
      cb[y * stride + x] = 0;
      cr[y * stride + x] = 0;
    }
  }
  for (; y < h; y++) {
    int x = 0;
    for (; x < w / 3; x++) {
      p[y * stride + x] =  255;
      cb[y * stride + x] = 255;
      cr[y * stride + x] = 0;
    }
    for (; x < 2 * w / 3; x++) {
      p[y * stride + x] = 255;
      cb[y * stride + x] = 255;
      cr[y * stride + x] = 255;
    }
    for (; x < w; x++) {
      p[y * stride + x] =  255;
      cb[y * stride + x] = 0;
      cr[y * stride + x] = 255;
    }
  }
  if (err.code) {
    heif_image_release(image);
    return nullptr;
  }

  return image;
}

struct heif_image* createImage_Mono_plus_alpha()
{
  struct heif_image *image;
  struct heif_error err;
  int w = 1024;
  int h = 768;
  err = heif_image_create(w, h, heif_colorspace_monochrome,
                          heif_chroma_monochrome, &image);
  if (err.code) {
    return nullptr;
  }

  err = heif_image_add_plane(image, heif_channel_Y, w, h, 8);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_image_add_plane(image, heif_channel_Alpha, w, h, 8);
  REQUIRE(err.code == heif_error_Ok);

  int stride;
  uint8_t *p = heif_image_get_plane(image, heif_channel_Y, &stride);
  uint8_t *a = heif_image_get_plane(image, heif_channel_Alpha, &stride);
  int y = 0;
  for (; y < h / 2; y++) {
    int x = 0;
    for (; x < w / 3; x++) {
      p[y * stride + x] = 255;
      a[y * stride + x] = 250;
    }
    for (; x < 2 * w / 3; x++) {
      p[y * stride + x] = 127;
      a[y * stride + x] = 25;
    }
    for (; x < w; x++) {
      p[y * stride + x] = 1;
      a[y * stride + x] = 252;
    }
  }
  for (; y < h; y++) {
    int x = 0;
    for (; x < w / 3; x++) {
      p[y * stride + x] =  (uint8_t) (x % 256);
      a[y * stride + x] = 253;
    }
    for (; x < 2 * w / 3; x++) {
      p[y * stride + x] = (uint8_t) ((255 - x) % 256);
      a[y * stride + x] = 254;
    }
    for (; x < w; x++) {
      p[y * stride + x] =  (uint8_t) ((x + y) % 256);
      a[y * stride + x] = 255;
    }
  }
  if (err.code) {
    heif_image_release(image);
    return nullptr;
  }

  return image;
}

struct heif_image *createImage_RGB_interleaved()
{
  struct heif_image *image;
  struct heif_error err;
  int w = 1024;
  int h = 768;
  err = heif_image_create(w, h, heif_colorspace_RGB,
                          heif_chroma_interleaved_RGB, &image);
  if (err.code) {
    return nullptr;
  }

  err = heif_image_add_plane(image, heif_channel_interleaved, w, h, 8);
  REQUIRE(err.code == heif_error_Ok);

  int stride;
  uint8_t *p = heif_image_get_plane(image, heif_channel_interleaved, &stride);

  int y = 0;
  for (; y < h / 2; y++) {
    int x = 0;
    for (; x < w / 3; x++) {
      p[y * stride + 3 * x] = 1;
      p[y * stride + 3 * x + 1] = 255;
      p[y * stride + 3 * x + 2] = 2;
    }
    for (; x < 2 * w / 3; x++) {
      p[y * stride + 3 * x] = 4;
      p[y * stride + 3 * x + 1] = 5;
      p[y * stride + 3 * x + 2] = 255;
    }
    for (; x < w; x++) {
      p[y * stride + 3 * x] = 255;
      p[y * stride + 3 * x + 1] = 6;
      p[y * stride + 3 * x + 2] = 7;
    }
  }
  for (; y < h; y++) {
    int x = 0;
    for (; x < w / 3; x++) {
      p[y * stride + 3 * x] = 8;
      p[y * stride + 3 * x + 1] = 9;
      p[y * stride + 3 * x + 2] = 255;
    }
    for (; x < 2 * w / 3; x++) {
      p[y * stride + 3 * x] = 253;
      p[y * stride + 3 * x + 1] = 10;
      p[y * stride + 3 * x + 2] = 11;
    }
    for (; x < w; x++) {
      p[y * stride + 3 * x] = 13;
      p[y * stride + 3 * x + 1] = 252;
      p[y * stride + 3 * x + 2] = 12;
    }
  }
  if (err.code) {
    heif_image_release(image);
    return nullptr;
  }

  return image;
}


void set_pixel_on_48bpp(uint8_t* p, int y, int stride, int x, int red, int green, int blue, bool little_endian, int alpha)
{
  uint8_t red_low_byte = (uint8_t)(red & 0xFF);
  uint8_t red_high_byte = (uint8_t)(red >> 8);
  uint8_t green_low_byte = (uint8_t)(green & 0xFF);
  uint8_t green_high_byte = (uint8_t)(green >> 8);
  uint8_t blue_low_byte = (uint8_t)(blue & 0xFF);
  uint8_t blue_high_byte = (uint8_t)(blue >> 8);
  uint8_t alpha_low_byte = (uint8_t)(alpha & 0xFF);
  uint8_t alpha_high_byte = (uint8_t)(alpha >> 8);
  int bytes_per_pixel = 6;
  if (alpha != 0)
  {
    bytes_per_pixel = 8;
  }
  if (little_endian)
  {
    p[y * stride + x * bytes_per_pixel + 0] = red_low_byte;
    p[y * stride + x * bytes_per_pixel + 1] = red_high_byte;
    p[y * stride + x * bytes_per_pixel + 2] = green_low_byte;
    p[y * stride + x * bytes_per_pixel + 3] = green_high_byte;
    p[y * stride + x * bytes_per_pixel + 4] = blue_low_byte;
    p[y * stride + x * bytes_per_pixel + 5] = blue_high_byte;
    if (alpha != 0)
    {
      p[y * stride + x * bytes_per_pixel + 6] = alpha_low_byte;
      p[y * stride + x * bytes_per_pixel + 7] = alpha_high_byte;
    }
  }
  else
  {
    p[y * stride + x * bytes_per_pixel + 0] = red_high_byte;
    p[y * stride + x * bytes_per_pixel + 1] = red_low_byte;
    p[y * stride + x * bytes_per_pixel + 2] = green_high_byte;
    p[y * stride + x * bytes_per_pixel + 3] = green_low_byte;
    p[y * stride + x * bytes_per_pixel + 4] = blue_high_byte;
    p[y * stride + x * bytes_per_pixel + 5] = blue_low_byte;
    if (alpha != 0)
    {
      p[y * stride + x * bytes_per_pixel + 6] = alpha_high_byte;
      p[y * stride + x * bytes_per_pixel + 7] = alpha_low_byte;
    }
  }
}

struct heif_image *createImage_RRGGBB_interleaved(heif_chroma chroma, int bit_depth, bool little_endian, bool with_alpha)
{
  struct heif_image *image;
  struct heif_error err;
  int w = 1024;
  int h = 768;
  err = heif_image_create(w, h, heif_colorspace_RGB, chroma, &image);
  if (err.code) {
    return nullptr;
  }

  int max_value = 0;
  for (int i = 0; i < bit_depth; i++)
  {
    max_value |= (1 << i);
  }
  int mid_value = max_value / 2;
  int alpha = 0;
  int alpha_mid = 0;
  if (with_alpha)
  {
    alpha = max_value;
    alpha_mid = mid_value;
  }

  err = heif_image_add_plane(image, heif_channel_interleaved, w, h, bit_depth);
  REQUIRE(err.code == heif_error_Ok);

  int stride;
  uint8_t *p = heif_image_get_plane(image, heif_channel_interleaved, &stride);

  int y = 0;
  for (; y < h / 2; y++) {
    int x = 0;
    for (; x < w / 4; x++)
    {
      set_pixel_on_48bpp(p, y, stride, x, 0x0000, 0x0000, max_value, little_endian, alpha);
    }
    for (; x < 2 * w / 4; x++)
    {
      set_pixel_on_48bpp(p, y, stride, x, 0x0000, max_value, 0x0000, little_endian, alpha);
    }
    for (; x < 3 * w / 4; x++)
    {
      set_pixel_on_48bpp(p, y, stride, x, max_value, 0x0000, 0x0000, little_endian, alpha);
    }
    for (; x < w; x++)
    {
      set_pixel_on_48bpp(p, y, stride, x, max_value - 2, max_value - 1, max_value, little_endian, alpha);
    }
  }
  for (; y < h; y++) {
    int x = 0;
    for (; x < w / 4; x++)
    {
      set_pixel_on_48bpp(p, y, stride, x, 0x0000, max_value, 0x0000, little_endian, alpha_mid);
    }
    for (; x < 2 * w / 4; x++)
    {
      set_pixel_on_48bpp(p, y, stride, x, max_value, 0x0000, 0x0000, little_endian, alpha_mid);
    }
    for (; x < 3 * w / 4; x++)
    {
      set_pixel_on_48bpp(p, y, stride, x, 0x0000, 0x0000, max_value, little_endian, alpha_mid);
    }
    for (; x < w; x++)
    {
      set_pixel_on_48bpp(p, y, stride, x, mid_value - 2, mid_value -1, mid_value, little_endian, alpha_mid);
    }
  }
  if (err.code) {
    heif_image_release(image);
    return nullptr;
  }

  return image;
}

struct heif_image *createImage_RGBA_interleaved()
{
  struct heif_image *image;
  struct heif_error err;
  int w = 1024;
  int h = 768;
  err = heif_image_create(w, h, heif_colorspace_RGB,
                          heif_chroma_interleaved_RGBA, &image);
  if (err.code) {
    return nullptr;
  }

  err = heif_image_add_plane(image, heif_channel_interleaved, w, h, 8);
  REQUIRE(err.code == heif_error_Ok);

  int stride;
  uint8_t *p = heif_image_get_plane(image, heif_channel_interleaved, &stride);

  int y = 0;
  for (; y < h / 2; y++) {
    int x = 0;
    for (; x < w / 3; x++) {
      p[y * stride + 4 * x] = 1;
      p[y * stride + 4 * x + 1] = 255;
      p[y * stride + 4 * x + 2] = 2;
      p[y * stride + 4 * x + 3] = 255;
    }
    for (; x < 2 * w / 3; x++) {
      p[y * stride + 4 * x] = 4;
      p[y * stride + 4 * x + 1] = 5;
      p[y * stride + 4 * x + 2] = 255;
      p[y * stride + 4 * x + 3] = 128;
    }
    for (; x < w; x++) {
      p[y * stride + 4 * x] = 255;
      p[y * stride + 4 * x + 1] = 6;
      p[y * stride + 4 * x + 2] = 7;
      p[y * stride + 4 * x + 3] = 200;
    }
  }
  for (; y < h; y++) {
    int x = 0;
    for (; x < w / 3; x++) {
      p[y * stride + 4 * x] = 8;
      p[y * stride + 4 * x + 1] = 9;
      p[y * stride + 4 * x + 2] = 255;
      p[y * stride + 4 * x + 3] = 254;
    }
    for (; x < 2 * w / 3; x++) {
      p[y * stride + 4 * x] = 253;
      p[y * stride + 4 * x + 1] = 10;
      p[y * stride + 4 * x + 2] = 11;
      p[y * stride + 4 * x + 3] = 253;
    }
    for (; x < w; x++) {
      p[y * stride + 4 * x] = 13;
      p[y * stride + 4 * x + 1] = 252;
      p[y * stride + 4 * x + 2] = 12;
      p[y * stride + 4 * x + 3] = 250;
    }
  }
  if (err.code) {
    heif_image_release(image);
    return nullptr;
  }

  return image;
}

struct heif_image *createImage_RGBA_planar()
{
  struct heif_image *image;
  struct heif_error err;
  int w = 1024;
  int h = 768;
  err = heif_image_create(w, h, heif_colorspace_RGB,
                          heif_chroma_444, &image);
  if (err.code) {
    return nullptr;
  }

  err = heif_image_add_plane(image, heif_channel_R, w, h, 8);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_image_add_plane(image, heif_channel_G, w, h, 8);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_image_add_plane(image, heif_channel_B, w, h, 8);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_image_add_plane(image, heif_channel_Alpha, w, h, 8);
  REQUIRE(err.code == heif_error_Ok);


  int stride;
  uint8_t *r = heif_image_get_plane(image, heif_channel_R, &stride);
  uint8_t *g = heif_image_get_plane(image, heif_channel_G, &stride);
  uint8_t *b = heif_image_get_plane(image, heif_channel_B, &stride);
  uint8_t *a = heif_image_get_plane(image, heif_channel_Alpha, &stride);

  int y = 0;
  for (; y < h / 2; y++) {
    int x = 0;
    for (; x < w / 3; x++) {
      r[y * stride + x] = 1;
      g[y * stride + x] = 255;
      b[y * stride + x] = 2;
      a[y * stride + x] = 240;
    }
    for (; x < 2 * w / 3; x++) {
      r[y * stride + x] = 4;
      g[y * stride + x] = 5;
      b[y * stride + x] = 255;
      a[y * stride + x] = 128;
    }
    for (; x < w; x++) {
      r[y * stride + x] = 255;
      g[y * stride + x] = 6;
      b[y * stride + x] = 7;
      a[y * stride + x] = 241;
    }
  }
  for (; y < h; y++) {
    int x = 0;
    for (; x < w / 3; x++) {
      r[y * stride + x]= 8;
      g[y * stride + x] = 9;
      b[y * stride + x] = 255;
      a[y * stride + x] = 242;
    }
    for (; x < 2 * w / 3; x++) {
      r[y * stride + x] = 253;
      g[y * stride + x] = 10;
      b[y * stride + x] = 11;
      a[y * stride + x] = 243;
    }
    for (; x < w; x++) {
      r[y * stride + x] = 13;
      g[y * stride + x] = 252;
      b[y * stride + x] = 12;
      a[y * stride + x] = 244;
    }
  }
  if (err.code) {
    heif_image_release(image);
    return nullptr;
  }

  return image;
}

static void do_encode(heif_image* input_image, const char* filename, bool check_decode, uint8_t prefer_uncC_short_form = 0)
{
  REQUIRE(input_image != nullptr);

  heif_context *ctx = heif_context_alloc();
  heif_encoder *encoder;
  struct heif_error err;
  err = heif_context_get_encoder_for_format(ctx, heif_compression_uncompressed, &encoder);
  REQUIRE(err.code == heif_error_Ok);

  struct heif_encoding_options *options;
  options = heif_encoding_options_alloc();
  options->macOS_compatibility_workaround = false;
  options->macOS_compatibility_workaround_no_nclx_profile = true;
  options->image_orientation = heif_orientation_normal;
  options->prefer_uncC_short_form = prefer_uncC_short_form;
  heif_image_handle *output_image_handle;

  err = heif_context_encode_image(ctx, input_image, encoder, options, &output_image_handle);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_context_write_to_file(ctx, filename);
  REQUIRE(err.code == heif_error_Ok);

  if (check_decode)
  {
    // read file back in
    struct heif_context* decode_context;
    decode_context = heif_context_alloc();
    err = heif_context_read_from_file(decode_context, filename, NULL);
    REQUIRE(err.code == heif_error_Ok);
    heif_image_handle *decode_image_handle = get_primary_image_handle(decode_context);
    int ispe_width = heif_image_handle_get_ispe_width(decode_image_handle);
    // TODO: check against input_image ispe width and height if we can
    REQUIRE(ispe_width == 1024);
    int ispe_height = heif_image_handle_get_ispe_height(decode_image_handle);
    REQUIRE(ispe_height == 768);
    int width = heif_image_handle_get_width(decode_image_handle);
    REQUIRE(width == heif_image_get_primary_width(input_image));
    int height = heif_image_handle_get_height(decode_image_handle);
    REQUIRE(height == heif_image_get_primary_height(input_image));
    heif_image* decode_image;
    err = heif_decode_image(decode_image_handle, &decode_image, heif_colorspace_undefined, heif_chroma_undefined, NULL);
    REQUIRE(err.code == heif_error_Ok);
    // REQUIRE(heif_image_has_channel(input_image, heif_channel_Y) == heif_image_has_channel(decode_image, heif_channel_Y));
    REQUIRE(heif_image_has_channel(input_image, heif_channel_Cb) == heif_image_has_channel(decode_image, heif_channel_Cb));
    REQUIRE(heif_image_has_channel(input_image, heif_channel_Cr) == heif_image_has_channel(decode_image, heif_channel_Cr));
    // REQUIRE(heif_image_has_channel(input_image, heif_channel_R) == heif_image_has_channel(decode_image, heif_channel_R));
    // REQUIRE(heif_image_has_channel(input_image, heif_channel_G) == heif_image_has_channel(decode_image, heif_channel_G));
    // REQUIRE(heif_image_has_channel(input_image, heif_channel_B) == heif_image_has_channel(decode_image, heif_channel_B));
    // REQUIRE(heif_image_has_channel(input_image, heif_channel_Alpha) == heif_image_has_channel(decode_image, heif_channel_Alpha));
    // REQUIRE(heif_image_has_channel(input_image, heif_channel_interleaved) == heif_image_has_channel(decode_image, heif_channel_interleaved));
    // TODO: make proper test for interleave to component translation

    // TODO: compare values
    heif_image_release(decode_image);
    heif_image_handle_release(decode_image_handle);
    heif_context_free(decode_context);
  }

  heif_image_handle_release(output_image_handle);
  heif_encoding_options_free(options);
  heif_encoder_release(encoder);
  heif_image_release(input_image);

  heif_context_free(ctx);
}


TEST_CASE("Encode RGB")
{
  heif_image *input_image = createImage_RGB_interleaved();
  do_encode(input_image, "encode_rgb.heif", true);
}


TEST_CASE("Encode Mono")
{
  heif_image* input_image = createImage_Mono();
  do_encode(input_image, "encode_mono.heif", true);
}

TEST_CASE("Encode RGB Version1")
{
  heif_image *input_image = createImage_RGB_interleaved();
  do_encode(input_image, "encode_rgb_version1.heif", true, true);
}

TEST_CASE("Encode Mono with alpha")
{
  heif_image* input_image = createImage_Mono_plus_alpha();
  do_encode(input_image, "encode_mono_plus_alpha.heif", true);
}


TEST_CASE("Encode YCBCr")
{
  // TODO: 422 and 420
  heif_image *input_image = createImage_YCbCr();
  do_encode(input_image, "encode_YCbCr.heif", true);
}


TEST_CASE("Encode RRRGGBB_LE 10 bit")
{
  heif_image *input_image = createImage_RRGGBB_interleaved(heif_chroma_interleaved_RRGGBB_LE, 10, true, false);
  do_encode(input_image, "encode_rrggbb_10_le.heif", false);
}


TEST_CASE("Encode RRRGGBB_BE 10 bit ")
{
  heif_image *input_image = createImage_RRGGBB_interleaved(heif_chroma_interleaved_RRGGBB_BE, 10, false, false);
  do_encode(input_image, "encode_rrggbb_10_be.heif", false);
}


TEST_CASE("Encode RRRGGBB_LE 12 bit")
{
  heif_image *input_image = createImage_RRGGBB_interleaved(heif_chroma_interleaved_RRGGBB_LE, 12, true, false);
  do_encode(input_image, "encode_rrggbb_12_le.heif", false);
}


TEST_CASE("Encode RRRGGBB_BE 12 bit ")
{
  heif_image *input_image = createImage_RRGGBB_interleaved(heif_chroma_interleaved_RRGGBB_BE, 12, false, false);
  do_encode(input_image, "encode_rrggbb_12_be.heif", false);
}


TEST_CASE("Encode RRRGGBB_LE 16 bit")
{
  heif_image *input_image = createImage_RRGGBB_interleaved(heif_chroma_interleaved_RRGGBB_LE, 16, true, false);
  do_encode(input_image, "encode_rrggbb_16_le.heif", false);
}


TEST_CASE("Encode RRRGGBB_BE 16 bit ")
{
  heif_image *input_image = createImage_RRGGBB_interleaved(heif_chroma_interleaved_RRGGBB_BE, 16, false, false);
  do_encode(input_image, "encode_rrggbb_16_be.heif", false);
}


TEST_CASE("Encode RGBA")
{
  heif_image *input_image = createImage_RGBA_interleaved();
  do_encode(input_image, "encode_rgba.heif", true);
}

TEST_CASE("Encode RGBA Version 1")
{
  heif_image *input_image = createImage_RGBA_interleaved();
  do_encode(input_image, "encode_rgba_version1.heif", true, true);
}


TEST_CASE("Encode RRRGGBBAA_LE 10 bit")
{
  heif_image *input_image = createImage_RRGGBB_interleaved(heif_chroma_interleaved_RRGGBBAA_LE, 10, true, true);
  do_encode(input_image, "encode_rrggbbaa_10_le.heif", false);
}


TEST_CASE("Encode RRRGGBBAA_BE 10 bit ")
{
  heif_image *input_image = createImage_RRGGBB_interleaved(heif_chroma_interleaved_RRGGBBAA_BE, 10, false, true);
  do_encode(input_image, "encode_rrggbbaa_10_be.heif", false);
}


TEST_CASE("Encode RRRGGBBAA_LE 12 bit")
{
  heif_image *input_image = createImage_RRGGBB_interleaved(heif_chroma_interleaved_RRGGBBAA_LE, 12, true, true);
  do_encode(input_image, "encode_rrggbbaa_12_le.heif", false);
}


TEST_CASE("Encode RRRGGBBAA_BE 12 bit ")
{
  heif_image *input_image = createImage_RRGGBB_interleaved(heif_chroma_interleaved_RRGGBBAA_BE, 12, false, true);
  do_encode(input_image, "encode_rrggbbaa_12_be.heif", false);
}


TEST_CASE("Encode RRRGGBBAA_LE 16 bit")
{
  heif_image *input_image = createImage_RRGGBB_interleaved(heif_chroma_interleaved_RRGGBBAA_LE, 16, true, true);
  do_encode(input_image, "encode_rrggbbaa_16_le.heif", false);
}


TEST_CASE("Encode RRRGGBBAA_BE 16 bit ")
{
  heif_image *input_image = createImage_RRGGBB_interleaved(heif_chroma_interleaved_RRGGBBAA_BE, 16, false, true);
  do_encode(input_image, "encode_rrggbbaa_16_be.heif", false);
}


TEST_CASE("Encode RGB planar")
{
  heif_image *input_image = createImage_RGB_planar();
  do_encode(input_image, "encode_rgb_planar.heif", true);
}


TEST_CASE("Encode RGBA planar")
{
  heif_image *input_image = createImage_RGBA_planar();
  do_encode(input_image, "encode_rgba_planar.heif", true);
}
