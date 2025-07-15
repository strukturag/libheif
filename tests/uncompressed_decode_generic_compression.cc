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

void check_image_size(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  heif_image *img = get_primary_image(handle);

  REQUIRE(heif_image_has_channel(img, heif_channel_Y) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_Cb) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_Cr) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_R) == 1);
  REQUIRE(heif_image_has_channel(img, heif_channel_G) == 1);
  REQUIRE(heif_image_has_channel(img, heif_channel_B) == 1);
  REQUIRE(heif_image_has_channel(img, heif_channel_Alpha) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_interleaved) == 0);
  int width = heif_image_get_primary_width(img);
  REQUIRE(width == 128);
  int height = heif_image_get_primary_height(img);
  REQUIRE(height == 72);
  width = heif_image_get_width(img, heif_channel_R);
  REQUIRE(width == 128);
  height = heif_image_get_height(img, heif_channel_R);
  REQUIRE(height == 72);
  width = heif_image_get_width(img, heif_channel_G);
  REQUIRE(width == 128);
  height = heif_image_get_height(img, heif_channel_G);
  REQUIRE(height == 72);
  width = heif_image_get_width(img, heif_channel_B);
  REQUIRE(width == 128);
  height = heif_image_get_height(img, heif_channel_B);
  REQUIRE(height == 72);

  int pixel_depth = heif_image_get_bits_per_pixel(img, heif_channel_R);
  REQUIRE(pixel_depth == 8);
  pixel_depth = heif_image_get_bits_per_pixel(img, heif_channel_G);
  REQUIRE(pixel_depth == 8);
  pixel_depth = heif_image_get_bits_per_pixel(img, heif_channel_B);
  REQUIRE(pixel_depth == 8);

  int pixel_range = heif_image_get_bits_per_pixel_range(img, heif_channel_R);
  REQUIRE(pixel_range == 8);
  pixel_range = heif_image_get_bits_per_pixel_range(img, heif_channel_G);
  REQUIRE(pixel_range == 8);
  pixel_range = heif_image_get_bits_per_pixel_range(img, heif_channel_B);
  REQUIRE(pixel_range == 8);

  heif_image_release(img);
  heif_image_handle_release(handle);
}

TEST_CASE("check image size") {
  auto file = GENERATE(FILES_GENERIC_COMPRESSED);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_size(context);
  heif_context_free(context);
}


void check_image_content(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  heif_image *img = get_primary_image(handle);

  int stride;
  const uint8_t *img_plane =
      heif_image_get_plane_readonly(img, heif_channel_R, &stride);
  REQUIRE(stride == 128);
  for (int row = 0; row < 24; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 63])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 64])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 95])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 96])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 127])) == 0);
  }
  for (int row = 24; row < 48; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 64);
    REQUIRE(((int)(img_plane[stride * row + 63])) == 64);
    REQUIRE(((int)(img_plane[stride * row + 64])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 95])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 96])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 127])) == 255);
  }
  for (int row = 48; row < 72; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 192);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 192);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 63])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 64])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 95])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 96])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 127])) == 255);
  }

  img_plane = heif_image_get_plane_readonly(img, heif_channel_G, &stride);
  REQUIRE(stride == 128);
  for (int row = 0; row < 24; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 63])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 64])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 95])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 96])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 127])) == 0);
  }
  for (int row = 24; row < 48; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 64);
    REQUIRE(((int)(img_plane[stride * row + 63])) == 64);
    REQUIRE(((int)(img_plane[stride * row + 64])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 95])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 96])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 127])) == 0);
  }
  for (int row = 48; row < 72; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 192);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 192);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 63])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 64])) == 175);
    REQUIRE(((int)(img_plane[stride * row + 95])) == 175);
    REQUIRE(((int)(img_plane[stride * row + 96])) == 200);
    REQUIRE(((int)(img_plane[stride * row + 127])) == 200);
  }

  img_plane = heif_image_get_plane_readonly(img, heif_channel_B, &stride);
  REQUIRE(stride == 128);
  for (int row = 0; row < 24; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 63])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 64])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 95])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 96])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 127])) == 0);
  }
  for (int row = 24; row < 48; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 64);
    REQUIRE(((int)(img_plane[stride * row + 63])) == 64);
    REQUIRE(((int)(img_plane[stride * row + 64])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 95])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 96])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 127])) == 255);
  }
  for (int row = 48; row < 72; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 192);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 192);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 63])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 64])) == 175);
    REQUIRE(((int)(img_plane[stride * row + 95])) == 175);
    REQUIRE(((int)(img_plane[stride * row + 96])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 127])) == 0);
  }

  heif_image_release(img);
  heif_image_handle_release(handle);
}

TEST_CASE("check image content") {
  auto file = GENERATE(FILES_GENERIC_COMPRESSED);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_content(context);
  heif_context_free(context);
}
