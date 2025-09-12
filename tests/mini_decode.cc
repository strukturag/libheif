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

#define MINI_FILES \
  "simple_osm_tile_alpha.avif", "simple_osm_tile_meta.avif"

void check_image_handle_size(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  int ispe_width = heif_image_handle_get_ispe_width(handle);
  REQUIRE(ispe_width == 256);
  int ispe_height = heif_image_handle_get_ispe_height(handle);
  REQUIRE(ispe_height == 256);
  int width = heif_image_handle_get_width(handle);
  REQUIRE(width == 256);
  int height = heif_image_handle_get_height(handle);
  REQUIRE(height == 256);

  heif_image_handle_release(handle);
}

TEST_CASE("check image handle size") {
  auto file = GENERATE(MINI_FILES);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_handle_size(context);
  heif_context_free(context);
}

void check_image_size_heif_mini(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  heif_image *img = get_primary_image_ycbcr(handle, heif_chroma_444);

  REQUIRE(heif_image_has_channel(img, heif_channel_Y) == 1);
  REQUIRE(heif_image_has_channel(img, heif_channel_Cb) == 1);
  REQUIRE(heif_image_has_channel(img, heif_channel_Cr) == 1);
  REQUIRE(heif_image_has_channel(img, heif_channel_R) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_G) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_B) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_Alpha) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_interleaved) == 0);
  int width = heif_image_get_primary_width(img);
  REQUIRE(width == 128);
  int height = heif_image_get_primary_height(img);
  REQUIRE(height == 128);
  width = heif_image_get_width(img, heif_channel_Y);
  REQUIRE(width == 128);
  height = heif_image_get_height(img, heif_channel_Y);
  REQUIRE(height == 128);
  width = heif_image_get_width(img, heif_channel_Cb);
  REQUIRE(width == 128);
  height = heif_image_get_height(img, heif_channel_Cr);
  REQUIRE(height == 128);
  width = heif_image_get_width(img, heif_channel_Cr);
  REQUIRE(width == 128);
  height = heif_image_get_height(img, heif_channel_Cr);
  REQUIRE(height == 128);

  int pixel_depth = heif_image_get_bits_per_pixel(img, heif_channel_Y);
  REQUIRE(pixel_depth == 8);
  pixel_depth = heif_image_get_bits_per_pixel(img, heif_channel_Cb);
  REQUIRE(pixel_depth == 8);
  pixel_depth = heif_image_get_bits_per_pixel(img, heif_channel_Cr);
  REQUIRE(pixel_depth == 8);
  int pixel_range = heif_image_get_bits_per_pixel_range(img, heif_channel_Y);
  REQUIRE(pixel_range == 8);
  pixel_range = heif_image_get_bits_per_pixel_range(img, heif_channel_Cb);
  REQUIRE(pixel_range == 8);
  pixel_range = heif_image_get_bits_per_pixel_range(img, heif_channel_Cr);
  REQUIRE(pixel_range == 8);

  heif_image_release(img);
  heif_image_handle_release(handle);
}

TEST_CASE("check image size HEIF mini") {
  auto context = get_context_for_test_file("lightning_mini.heif");
  check_image_size_heif_mini(context);
  heif_context_free(context);
}
