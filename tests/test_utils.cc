/*
  libheif test support utilities

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

#include "test_utils.h"
#include "libheif/heif.h"
#include "test-config.h"
#include <cstring>
#include "catch_amalgamated.hpp"


struct heif_context * get_context_for_test_file(std::string filename)
{
  return get_context_for_local_file(tests_data_directory + "/" + filename);
}

struct heif_context * get_context_for_local_file(std::string filename)
{
  struct heif_context* context;
  struct heif_error err;
  context = heif_context_alloc();
  err = heif_context_read_from_file(context, filename.c_str(), NULL);
  INFO(filename);
  REQUIRE(err.code == heif_error_Ok);
  return context;
}

struct heif_image_handle * get_primary_image_handle(heif_context *context)
{
  struct heif_error err;
  struct heif_image_handle * image_handle;
  int num_images = heif_context_get_number_of_top_level_images(context);
  REQUIRE(num_images == 1);
  err =  heif_context_get_primary_image_handle(context, &image_handle);
  REQUIRE(err.code == heif_error_Ok);
  return image_handle;
}

struct heif_image * get_primary_image(heif_image_handle * handle)
{
  struct heif_error err;
  struct heif_image* img;
  err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_444, NULL);
  REQUIRE(err.code == heif_error_Ok);
  return img;
}

struct heif_image * get_primary_image_mono(heif_image_handle * handle)
{
  struct heif_error err;
  struct heif_image* img;
  err = heif_decode_image(handle, &img, heif_colorspace_monochrome, heif_chroma_monochrome, NULL);
  REQUIRE(err.code == heif_error_Ok);
  return img;
}

struct heif_image * get_primary_image_ycbcr(heif_image_handle * handle, heif_chroma chroma)
{
  struct heif_error err;
  struct heif_image* img;
  err = heif_decode_image(handle, &img, heif_colorspace_YCbCr, chroma, NULL);
  REQUIRE(err.code == heif_error_Ok);
  return img;
}

void fill_new_plane(heif_image* img, heif_channel channel, int w, int h)
{
  struct heif_error err;

  err = heif_image_add_plane(img, channel, w, h, 8);
  REQUIRE(err.code == heif_error_Ok);

  int stride;
  uint8_t* p = heif_image_get_plane(img, channel, &stride);

  for (int y = 0; y < h; y++) {
    memset(p + y * stride, 128, w);
  }
}

struct heif_image * createImage_RGB_planar()
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


  int stride;
  uint8_t *r = heif_image_get_plane(image, heif_channel_R, &stride);
  uint8_t *g = heif_image_get_plane(image, heif_channel_G, &stride);
  uint8_t *b = heif_image_get_plane(image, heif_channel_B, &stride);

  int y = 0;
  for (; y < h / 2; y++) {
    int x = 0;
    for (; x < w / 3; x++) {
      r[y * stride + x] = 1;
      g[y * stride + x] = 255;
      b[y * stride + x] = 2;
    }
    for (; x < 2 * w / 3; x++) {
      r[y * stride + x] = 4;
      g[y * stride + x] = 5;
      b[y * stride + x] = 255;
    }
    for (; x < w; x++) {
      r[y * stride + x] = 255;
      g[y * stride + x] = 6;
      b[y * stride + x] = 7;
    }
  }
  for (; y < h; y++) {
    int x = 0;
    for (; x < w / 3; x++) {
      r[y * stride + x]= 8;
      g[y * stride + x] = 9;
      b[y * stride + x] = 255;
    }
    for (; x < 2 * w / 3; x++) {
      r[y * stride + x] = 253;
      g[y * stride + x] = 10;
      b[y * stride + x] = 11;
    }
    for (; x < w; x++) {
      r[y * stride + x] = 13;
      g[y * stride + x] = 252;
      b[y * stride + x] = 12;
    }
  }
  if (err.code) {
    heif_image_release(image);
    return nullptr;
  }

  return image;
}


std::string get_path_for_heifio_test_file(std::string filename)
{
  return libheifio_tests_data_directory + "/" + filename;
}


heif_encoder* get_encoder_or_skip_test(heif_compression_format format)
{
  heif_encoder* encoder = nullptr;
  heif_error err = heif_context_get_encoder_for_format(nullptr, format, &encoder);
  if (err.code != heif_error_Ok) {
    if (format == heif_compression_HEVC) {
      SKIP("Encoder for HEVC not found, skipping test");
    }
    else if (format == heif_compression_AV1) {
      SKIP("Encoder for AV1 not found, skipping test");
    }
    else {
      SKIP("Encoder not found, skipping test");
    }
  }

  return encoder;
}


fs::path get_tests_output_dir()
{
  if (const char* env_p = std::getenv("LIBHEIF_TEST_OUTPUT_DIR")) {
    return fs::path(env_p);
  }

  static const fs::path output_dir = fs::current_path() / "libheif_test_output";

  if (!fs::exists(output_dir)) {
    fs::create_directories(output_dir);
  }

  return output_dir;
}


std::string get_tests_output_file_path(const char* filename)
{
  fs::path dir = get_tests_output_dir();
  dir /= filename;
  return dir.string();
}
