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

void check_image_size_ycbcr_422(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  heif_image *img = get_primary_image_ycbcr(handle, heif_chroma_422);

  REQUIRE(heif_image_has_channel(img, heif_channel_Y) == 1);
  REQUIRE(heif_image_has_channel(img, heif_channel_Cb) == 1);
  REQUIRE(heif_image_has_channel(img, heif_channel_Cr) == 1);
  REQUIRE(heif_image_has_channel(img, heif_channel_R) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_G) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_B) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_Alpha) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_interleaved) == 0);
  int width = heif_image_get_primary_width(img);
  REQUIRE(width == 32);
  int height = heif_image_get_primary_height(img);
  REQUIRE(height == 20);
  width = heif_image_get_width(img, heif_channel_Y);
  REQUIRE(width == 32);
  height = heif_image_get_height(img, heif_channel_Y);
  REQUIRE(height == 20);
  width = heif_image_get_width(img, heif_channel_Cb);
  REQUIRE(width == 16);
  height = heif_image_get_height(img, heif_channel_Cr);
  REQUIRE(height == 20);
  width = heif_image_get_width(img, heif_channel_Cr);
  REQUIRE(width == 16);
  height = heif_image_get_height(img, heif_channel_Cr);
  REQUIRE(height == 20);

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

TEST_CASE("check image size YCbCr 4:2:2") {
  auto file = GENERATE(YUV_422_FILES);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_size_ycbcr_422(context);
  heif_context_free(context);
}

void check_image_size_ycbcr_422_16bit(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  heif_image *img = get_primary_image_ycbcr(handle, heif_chroma_422);

  REQUIRE(heif_image_has_channel(img, heif_channel_Y) == 1);
  REQUIRE(heif_image_has_channel(img, heif_channel_Cb) == 1);
  REQUIRE(heif_image_has_channel(img, heif_channel_Cr) == 1);
  REQUIRE(heif_image_has_channel(img, heif_channel_R) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_G) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_B) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_Alpha) == 0);
  REQUIRE(heif_image_has_channel(img, heif_channel_interleaved) == 0);
  int width = heif_image_get_primary_width(img);
  REQUIRE(width == 32);
  int height = heif_image_get_primary_height(img);
  REQUIRE(height == 20);
  width = heif_image_get_width(img, heif_channel_Y);
  REQUIRE(width == 32);
  height = heif_image_get_height(img, heif_channel_Y);
  REQUIRE(height == 20);
  width = heif_image_get_width(img, heif_channel_Cb);
  REQUIRE(width == 16);
  height = heif_image_get_height(img, heif_channel_Cr);
  REQUIRE(height == 20);
  width = heif_image_get_width(img, heif_channel_Cr);
  REQUIRE(width == 16);
  height = heif_image_get_height(img, heif_channel_Cr);
  REQUIRE(height == 20);

  int pixel_depth = heif_image_get_bits_per_pixel(img, heif_channel_Y);
  REQUIRE(pixel_depth == 16);
  pixel_depth = heif_image_get_bits_per_pixel(img, heif_channel_Cb);
  REQUIRE(pixel_depth == 16);
  pixel_depth = heif_image_get_bits_per_pixel(img, heif_channel_Cr);
  REQUIRE(pixel_depth == 16);
  int pixel_range = heif_image_get_bits_per_pixel_range(img, heif_channel_Y);
  REQUIRE(pixel_range == 16);
  pixel_range = heif_image_get_bits_per_pixel_range(img, heif_channel_Cb);
  REQUIRE(pixel_range == 16);
  pixel_range = heif_image_get_bits_per_pixel_range(img, heif_channel_Cr);
  REQUIRE(pixel_range == 16);

  heif_image_release(img);
  heif_image_handle_release(handle);
}

TEST_CASE("check image size YCbCr 4:2:2 16 bit") {
  auto file = GENERATE(YUV_16BIT_422_FILES);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_size_ycbcr_422_16bit(context);
  heif_context_free(context);
}

void check_image_content_ycbcr422(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  heif_image *img = get_primary_image_ycbcr(handle, heif_chroma_422);

  int stride;
  const uint8_t *img_plane =
      heif_image_get_plane_readonly(img, heif_channel_Y, &stride);
  REQUIRE(stride == 64);
  for (int row = 0; row < 4; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 76);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 76);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 75);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 75);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 225);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 225);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 178);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 178);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 128);
  }
  for (int row = 4; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 75);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 75);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 225);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 225);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 178);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 178);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 173);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 173);
  }
  for (int row = 8; row < 12; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 225);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 225);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 178);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 178);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 173);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 173);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 174);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 174);
  }
  for (int row = 12; row < 16; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 225);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 225);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 178);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 178);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 173);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 173);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 174);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 174);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 76);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 76);
  }
  for (int row = 16; row < 20; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 225);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 225);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 178);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 178);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 173);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 173);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 174);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 174);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 76);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 76);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 75);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 75);
  }
  img_plane = heif_image_get_plane_readonly(img, heif_channel_Cb, &stride);
  REQUIRE(stride == 64);
  for (int row = 0; row < 4; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 84);
    REQUIRE(((int)(img_plane[stride * row + 1])) == 84);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 85);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 85);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 5])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 13])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 127);
  }
  for (int row = 4; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 85);
    REQUIRE(((int)(img_plane[stride * row + 1])) == 85);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 5])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 13])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 29);
  }
  for (int row = 8; row < 12; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 1])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 5])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 13])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 163);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 163);
  }
  for (int row = 12; row < 16; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 1])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 5])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 163);
    REQUIRE(((int)(img_plane[stride * row + 13])) == 163);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 84);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 84);
  }
  for (int row = 16; row < 20; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 1])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 5])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 163);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 163);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 84);
    REQUIRE(((int)(img_plane[stride * row + 13])) == 84);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 85);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 85);
  }
  img_plane = heif_image_get_plane_readonly(img, heif_channel_Cr, &stride);
  REQUIRE(stride == 64);
  for (int row = 0; row < 4; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 1])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 73);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 73);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 106);
    REQUIRE(((int)(img_plane[stride * row + 5])) == 106);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 13])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 127);
  }
  for (int row = 4; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 73);
    REQUIRE(((int)(img_plane[stride * row + 1])) == 73);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 106);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 106);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 5])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 13])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 185);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 185);
  }
  for (int row = 8; row < 12; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 106);
    REQUIRE(((int)(img_plane[stride * row + 1])) == 106);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 5])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 185);
    REQUIRE(((int)(img_plane[stride * row + 13])) == 185);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 172);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 172);
  }
  for (int row = 12; row < 16; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 1])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 5])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 185);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 185);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 172);
    REQUIRE(((int)(img_plane[stride * row + 13])) == 172);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 254);
  }
  for (int row = 16; row < 20; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 1])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 5])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 185);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 185);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 172);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 172);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 13])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 73);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 73);
  }

  heif_image_release(img);
  heif_image_handle_release(handle);
}

TEST_CASE("check image content YCbCr 4:2:2") {
  auto file = GENERATE(YUV_422_FILES);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_content_ycbcr422(context);
  heif_context_free(context);
}


void check_image_content_ycbcr422_16bit(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  heif_image *img = get_primary_image_ycbcr(handle, heif_chroma_422);

  int stride;
  const uint16_t *img_plane =
      (const uint16_t*)heif_image_get_plane_readonly(img, heif_channel_Y, &stride);
  REQUIRE(stride == 128);
  stride /= 2;
  for (int row = 0; row < 4; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x4C8A);
    REQUIRE(((int)(img_plane[stride * row + 1])) == 0x4C8A);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 0x4C8A);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0x4C8A);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x4B6D);
    REQUIRE(((int)(img_plane[stride * row + 5])) == 0x4B6D);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 0x4B6D);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0x4B6D);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x1D2E);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 0x1D2E);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 0x1D2E);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0x1D2E);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0xFFFE);
    REQUIRE(((int)(img_plane[stride * row + 13])) == 0xFFFE);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 0xFFFE);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0xFFFE);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0x0000);
    REQUIRE(((int)(img_plane[stride * row + 17])) == 0x0000);
    REQUIRE(((int)(img_plane[stride * row + 18])) == 0x0000);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0x0000);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0xE2D0);
    REQUIRE(((int)(img_plane[stride * row + 21])) == 0xE2D0);
    REQUIRE(((int)(img_plane[stride * row + 22])) == 0xE2D0);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0xE2D0);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0xB374);
    REQUIRE(((int)(img_plane[stride * row + 25])) == 0xB374);
    REQUIRE(((int)(img_plane[stride * row + 26])) == 0xB374);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0xB374);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0x8080);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 0x8080);
    REQUIRE(((int)(img_plane[stride * row + 30])) == 0x8080);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0x8080);
  }
  for (int row = 4; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x4B6D);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 0x4B6D);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x1D2E);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0xFFFE);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0x0000);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0xE2D0);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0xB374);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0x8080);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0xADC6);
  }
  for (int row = 8; row < 12; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x1D2E);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0xFFFE);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x0000);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0xE2D0);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0xB374);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0x8080);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0xADC6);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0xAF49);
  }
  for (int row = 12; row < 16; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0xFFFE);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x0000);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0xE2D0);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0xB374);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0x8080);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0xADC6);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0xAF49);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0x4C8A);
  }
  for (int row = 16; row < 20; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x0000);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0xE2D0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0xB374);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0x8080);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0xADC6);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0xAF49);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0x4C8A);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0x4B6D);
  }
  img_plane = (const uint16_t*)heif_image_get_plane_readonly(img, heif_channel_Cb, &stride);
  REQUIRE(stride == 128);
  stride /= 2;
  for (int row = 0; row < 4; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x54BC);
    REQUIRE(((int)(img_plane[stride * row + 1])) == 0x54BC);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 0x5576);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0x5576);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0xFFBD);
    REQUIRE(((int)(img_plane[stride * row + 5])) == 0xFFBD);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 0x7FBD);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0x7FBD);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x7FFF);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 0x7FFF);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 0x0000);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0x0000);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0xAB01);
    REQUIRE(((int)(img_plane[stride * row + 13])) == 0xAB01);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 0x7FDE);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0x7FDE);
  }
  for (int row = 5; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x5576);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 0xFFBD);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x7FBD);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 0x7FFF);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x0000);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 0xAB01);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0x7FDE);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 0x1DE8);
  }
  for (int row = 9; row < 12; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0xFFBD);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 0x7FBD);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x7FFF);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 0x0000);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0xAB01);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 0x7FDE);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0x1DE8);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 0xA3A5);
  }
  for (int row = 12; row < 16; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x7FBD);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 0x7FFF);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x0000);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 0xAB01);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x7FDE);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 0x1DE8);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0xA3A5);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 0x54BC);
  }
  for (int row = 16; row < 20; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x7FFF);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 0x0000);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0xAB01);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 0x7FDE);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x1DE8);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 0xA3A5);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0x54BC);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 0x5576);
  }
  img_plane = (const uint16_t*)heif_image_get_plane_readonly(img, heif_channel_Cr, &stride);
  REQUIRE(stride == 128);
  stride /= 2;
  for (int row = 0; row < 4; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0xFFBD);
    REQUIRE(((int)(img_plane[stride * row + 1])) == 0xFFBD);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 0x4A48);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0x4A48);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x6B2F);
    REQUIRE(((int)(img_plane[stride * row + 5])) == 0x6B2F);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 0x7FEB);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0x7FEB);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x7FFF);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 0x7FFF);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 0x94BB);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0x94BB);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0x002D);
    REQUIRE(((int)(img_plane[stride * row + 13])) == 0x002D);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 0x7FF5);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0x7FF5);
  }
  for (int row = 4; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x4A48);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 0x6B2F);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x7FEB);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 0x7FFF);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x94BB);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 0x002D);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0x7FF5);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 0xBA80);
  }
  for (int row = 8; row < 12; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x6B2F);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 0x7FEB);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x7FFF);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 0x94BB);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x002D);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 0x7FF5);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0xBA80);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 0xAD3F);
  }
  for (int row = 12; row < 16; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x7FEB);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 0x7FFF);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x94BB);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 0x002D);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x7FF5);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 0xBA80);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0xAD3F);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 0xFFBD);
  }
  for (int row = 16; row < 20; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x7FFF);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 0x94BB);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x002D);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 0x7FF5);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0xBA80);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 0xAD3F);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0xFFBD);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 0x4A48);
  }

  heif_image_release(img);
  heif_image_handle_release(handle);
}

TEST_CASE("check image content YCbCr 4:2:2 16 bit") {
  auto file = GENERATE(YUV_16BIT_422_FILES);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_content_ycbcr422_16bit(context);
  heif_context_free(context);
}
