/*
  libheif integration tests for uncompressed decoder

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
#include "libheif/heif.h"
#include "api_structs.h"
#include <cstdint>
#include <stdio.h>
#include "test_utils.h"
#include <string.h>

#include "uncompressed_decode.h"

void check_image_size_rgb565(struct heif_context *&context, int expect_alpha) {
  heif_image_handle *handle = get_primary_image_handle(context);
  heif_image *img = get_primary_image(handle);

  REQUIRE(heif_image_has_channel(img, heif_channel_Y) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_Cb) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_Cr) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_R) == 1);
  REQUIRE(heif_image_has_channel(img, heif_channel_G) == 1);
  REQUIRE(heif_image_has_channel(img, heif_channel_B) == 1);
  REQUIRE(heif_image_has_channel(img, heif_channel_Alpha) == expect_alpha);
  REQUIRE(heif_image_has_channel(img, heif_channel_interleaved) == 0);
  int width = heif_image_get_primary_width(img);
  REQUIRE(width == 30);
  int height = heif_image_get_primary_height(img);
  REQUIRE(height == 20);
  width = heif_image_get_width(img, heif_channel_R);
  REQUIRE(width == 30);
  height = heif_image_get_height(img, heif_channel_R);
  REQUIRE(height == 20);
  width = heif_image_get_width(img, heif_channel_G);
  REQUIRE(width == 30);
  height = heif_image_get_height(img, heif_channel_G);
  REQUIRE(height == 20);
  width = heif_image_get_width(img, heif_channel_B);
  REQUIRE(width == 30);
  height = heif_image_get_height(img, heif_channel_B);
  REQUIRE(height == 20);
  if (expect_alpha == 1) {
    width = heif_image_get_width(img, heif_channel_Alpha);
    REQUIRE(width == 30);
    height = heif_image_get_height(img, heif_channel_Alpha);
    REQUIRE(height == 20);
  }

  int pixel_depth = heif_image_get_bits_per_pixel(img, heif_channel_R);
  REQUIRE(pixel_depth == 8);
  pixel_depth = heif_image_get_bits_per_pixel(img, heif_channel_G);
  REQUIRE(pixel_depth == 8);
  pixel_depth = heif_image_get_bits_per_pixel(img, heif_channel_B);
  REQUIRE(pixel_depth == 8);

  int pixel_range = heif_image_get_bits_per_pixel_range(img, heif_channel_R);
  REQUIRE(pixel_range == 5);
  pixel_range = heif_image_get_bits_per_pixel_range(img, heif_channel_G);
  REQUIRE(pixel_range == 6);
  pixel_range = heif_image_get_bits_per_pixel_range(img, heif_channel_B);
  REQUIRE(pixel_range == 5);

  heif_image_release(img);
  heif_image_handle_release(handle);
}

TEST_CASE("check image size 5-6-5 bit RGB") {
  auto file = GENERATE(FILES_565_RGB);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  int expect_alpha = (strchr(file, 'A') == NULL) ? 0 : 1;
  check_image_size_rgb565(context, expect_alpha);
  heif_context_free(context);
}

void check_image_content_rgb565(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  heif_image *img = get_primary_image(handle);

  int stride;
  const uint8_t *img_plane =
      heif_image_get_plane_readonly(img, heif_channel_R, &stride);
  REQUIRE(stride == 64);
  for (int row = 0; row < 4; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 15);
  }
  for (int row = 4; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 31);
  }
  for (int row = 8; row < 12; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 28);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 28);
  }
  for (int row = 12; row < 16; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 28);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 28);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 31);
  }
  for (int row = 16; row < 20; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 28);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 28);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 0);
  }
  img_plane = heif_image_get_plane_readonly(img, heif_channel_G, &stride);
  REQUIRE(stride == 64);
  for (int row = 0; row < 4; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 31);
  }
  for (int row = 4; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 40);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 40);
  }
  for (int row = 8; row < 12; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 40);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 40);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 32);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 32);
  }
  for (int row = 12; row < 16; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 40);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 40);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 32);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 32);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 0);
  }
  for (int row = 16; row < 20; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 63);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 40);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 40);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 32);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 32);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 31);
  }
  img_plane = heif_image_get_plane_readonly(img, heif_channel_B, &stride);
  REQUIRE(stride == 64);
  for (int row = 0; row < 4; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 15);
  }
  for (int row = 4; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 0);
  }
  for (int row = 8; row < 12; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 28);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 28);
  }
  for (int row = 12; row < 16; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 28);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 28);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 0);
  }
  for (int row = 16; row < 20; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 31);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 15);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 28);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 28);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 0);
  }

  heif_image_release(img);
  heif_image_handle_release(handle);
}

TEST_CASE("check image content 5-6-5 bit RGB") {
  auto file = GENERATE(FILES_565_RGB);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_content_rgb565(context);
  heif_context_free(context);
}
