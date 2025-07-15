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

void check_image_size_rgb16(struct heif_context *&context, int expect_alpha) {
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
  REQUIRE(pixel_depth == 16);
  pixel_depth = heif_image_get_bits_per_pixel(img, heif_channel_G);
  REQUIRE(pixel_depth == 16);
  pixel_depth = heif_image_get_bits_per_pixel(img, heif_channel_B);
  REQUIRE(pixel_depth == 16);

  int pixel_range = heif_image_get_bits_per_pixel_range(img, heif_channel_R);
  REQUIRE(pixel_range == 16);
  pixel_range = heif_image_get_bits_per_pixel_range(img, heif_channel_G);
  REQUIRE(pixel_range == 16);
  pixel_range = heif_image_get_bits_per_pixel_range(img, heif_channel_B);
  REQUIRE(pixel_range == 16);

  heif_image_release(img);
  heif_image_handle_release(handle);
}

TEST_CASE("check image size 16 bit RGB") {
  auto file = GENERATE(FILES_16BIT_RGB);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  int expect_alpha = (strchr(file, 'A') == NULL) ? 0 : 1;
  check_image_size_rgb16(context, expect_alpha);
  heif_context_free(context);
}

void check_image_content_rgb16(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  heif_image *img = get_primary_image(handle);

  int stride;
  const uint8_t *img_plane =
      heif_image_get_plane_readonly(img, heif_channel_R, &stride);
  REQUIRE(stride == 128);
  for (int row = 0; row < 4; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 128);
  }
  for (int row = 4; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 255);
  }
  for (int row = 8; row < 12; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 35])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 36])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 43])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 44])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 51])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 52])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 238);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 238);
  }
  for (int row = 12; row < 16; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 238);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 238);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 255);
  }
  for (int row = 16; row < 20; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 238);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 238);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 0);
  }
  img_plane = heif_image_get_plane_readonly(img, heif_channel_G, &stride);
  REQUIRE(stride == 128);
  for (int row = 0; row < 4; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 128);
  }
  for (int row = 4; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 165);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 165);
  }
  for (int row = 8; row < 12; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 35])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 36])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 43])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 44])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 165);
    REQUIRE(((int)(img_plane[stride * row + 51])) == 165);
    REQUIRE(((int)(img_plane[stride * row + 52])) == 165);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 165);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 130);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 130);
  }
  for (int row = 12; row < 16; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 165);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 165);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 130);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 130);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 0);
  }
  for (int row = 16; row < 20; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 165);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 165);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 130);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 130);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 128);
  }
  img_plane = heif_image_get_plane_readonly(img, heif_channel_B, &stride);
  REQUIRE(stride == 128);
  for (int row = 0; row < 4; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 128);
  }
  for (int row = 4; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 0);
  }
  for (int row = 8; row < 12; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 35])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 36])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 43])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 44])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 51])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 52])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 238);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 238);
  }
  for (int row = 12; row < 16; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 238);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 238);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 0);
  }
  for (int row = 16; row < 20; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 238);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 238);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 0);
  }

  heif_image_release(img);
  heif_image_handle_release(handle);
}

TEST_CASE("check image content 16 bit RGB") {
  auto file = GENERATE(FILES_16BIT_RGB);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_content_rgb16(context);
  heif_context_free(context);
}
