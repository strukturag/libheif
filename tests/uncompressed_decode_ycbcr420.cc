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
#include "catch.hpp"
#include "libheif/heif.h"
#include "libheif/api_structs.h"
#include <cstdint>
#include <stdio.h>
#include "test_utils.h"
#include <string.h>

#include "uncompressed_decode.h"


void check_image_size_ycbcr_420(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  heif_image *img = get_primary_image_ycbcr(handle, heif_chroma_420);

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
  REQUIRE(height == 10);
  width = heif_image_get_width(img, heif_channel_Cr);
  REQUIRE(width == 16);
  height = heif_image_get_height(img, heif_channel_Cr);
  REQUIRE(height == 10);

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

TEST_CASE("check image size YCbCr 4:2:0") {
  auto file = GENERATE(YUV_420_FILES);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_size_ycbcr_420(context);
  heif_context_free(context);
}


void check_image_size_ycbcr_420_16bit(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  heif_image *img = get_primary_image_ycbcr(handle, heif_chroma_420);

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
  REQUIRE(height == 10);
  width = heif_image_get_width(img, heif_channel_Cr);
  REQUIRE(width == 16);
  height = heif_image_get_height(img, heif_channel_Cr);
  REQUIRE(height == 10);

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

TEST_CASE("check image size YCbCr 4:2:0 16 bit") {
  auto file = GENERATE(YUV_16BIT_420_FILES);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_size_ycbcr_420_16bit(context);
  heif_context_free(context);
}

void check_image_content_ycbcr420(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  heif_image *img = get_primary_image_ycbcr(handle, heif_chroma_420);

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
  for (int row = 0; row < 2; row++) {
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
  for (int row = 2; row < 4; row++) {
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
  for (int row = 4; row < 6; row++) {
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
  for (int row = 6; row < 8; row++) {
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
  for (int row = 8; row < 10; row++) {
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
  for (int row = 0; row < 2; row++) {
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
  for (int row = 2; row < 4; row++) {
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
  for (int row = 4; row < 6; row++) {
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
  for (int row = 6; row < 8; row++) {
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
  for (int row = 8; row < 10; row++) {
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

TEST_CASE("check image content YCbCr 4:2:0") {
  auto file = GENERATE(YUV_420_FILES);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_content_ycbcr420(context);
  heif_context_free(context);
}

void check_image_content_ycbcr420_16bit(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  heif_image *img = get_primary_image_ycbcr(handle, heif_chroma_420);

  int stride;
  const uint8_t *img_plane =
      heif_image_get_plane_readonly(img, heif_channel_Y, &stride);
  REQUIRE(stride == 128);
  for (int row = 0; row < 4; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x8A);
    REQUIRE(((int)(img_plane[stride * row + 1])) == 0x4C);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 0x8A);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0x4C);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x8A);
    REQUIRE(((int)(img_plane[stride * row + 5])) == 0x4C);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 0x8A);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0x4C);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x6D);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 0x4B);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 0x6D);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0x4B);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0x6D);
    REQUIRE(((int)(img_plane[stride * row + 13])) == 0x4B);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 0x6D);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0x4B);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0x2E);
    REQUIRE(((int)(img_plane[stride * row + 17])) == 0x1D);
    REQUIRE(((int)(img_plane[stride * row + 18])) == 0x2E);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0x1D);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0x2E);
    REQUIRE(((int)(img_plane[stride * row + 21])) == 0x1D);
    REQUIRE(((int)(img_plane[stride * row + 22])) == 0x2E);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0x1D);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0xFE);
    REQUIRE(((int)(img_plane[stride * row + 25])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 26])) == 0xFE);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0xFE);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 30])) == 0xFE);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 33])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 34])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 35])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 36])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 37])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 38])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 0xD0);
    REQUIRE(((int)(img_plane[stride * row + 41])) == 0xE2);
    REQUIRE(((int)(img_plane[stride * row + 42])) == 0xD0);
    REQUIRE(((int)(img_plane[stride * row + 43])) == 0xE2);
    REQUIRE(((int)(img_plane[stride * row + 44])) == 0xD0);
    REQUIRE(((int)(img_plane[stride * row + 45])) == 0xE2);
    REQUIRE(((int)(img_plane[stride * row + 46])) == 0xD0);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 0xE2);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 0x74);
    REQUIRE(((int)(img_plane[stride * row + 49])) == 0xB3);
    REQUIRE(((int)(img_plane[stride * row + 50])) == 0x74);
    REQUIRE(((int)(img_plane[stride * row + 51])) == 0xB3);
    REQUIRE(((int)(img_plane[stride * row + 52])) == 0x74);
    REQUIRE(((int)(img_plane[stride * row + 53])) == 0xB3);
    REQUIRE(((int)(img_plane[stride * row + 54])) == 0x74);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 0xB3);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 57])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 59])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 60])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 61])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 62])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 63])) == 0x80);
  }
  for (int row = 4; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x6D);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0x4B);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x6D);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0x4B);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x2E);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0x1D);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0xFE);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 0xD0);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 0xE2);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 0x74);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 0xB3);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 0xC6);
    REQUIRE(((int)(img_plane[stride * row + 63])) == 0xAD);
  }
  for (int row = 8; row < 12; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x2E);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0x1D);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0xFE);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0xD0);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0xE2);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 0x74);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 0xB3);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 0xC6);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 0xAD);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 0x49);
    REQUIRE(((int)(img_plane[stride * row + 63])) == 0xAF);
  }
  for (int row = 12; row < 16; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0xFE);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0xD0);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0xE2);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0x74);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0xB3);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 0xC6);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 0xAD);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 0x49);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 0xAF);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 0x8A);
    REQUIRE(((int)(img_plane[stride * row + 63])) == 0x4C);
  }
  for (int row = 16; row < 20; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0xD0);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0xE2);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0x74);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0xB3);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 32])) == 0xC6);
    REQUIRE(((int)(img_plane[stride * row + 39])) == 0xAD);
    REQUIRE(((int)(img_plane[stride * row + 40])) == 0x49);
    REQUIRE(((int)(img_plane[stride * row + 47])) == 0xAF);
    REQUIRE(((int)(img_plane[stride * row + 48])) == 0x8A);
    REQUIRE(((int)(img_plane[stride * row + 55])) == 0x4C);
    REQUIRE(((int)(img_plane[stride * row + 56])) == 0x6D);
    REQUIRE(((int)(img_plane[stride * row + 63])) == 0x4B);
  }
  img_plane = heif_image_get_plane_readonly(img, heif_channel_Cb, &stride);
  REQUIRE(stride == 128);
  for (int row = 0; row < 2; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0xBC);
    REQUIRE(((int)(img_plane[stride * row + 1])) == 0x54);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 0xBC);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0x54);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x76);
    REQUIRE(((int)(img_plane[stride * row + 5])) == 0x55);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 0x76);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0x55);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0xBD);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 0xBD);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0xBD);
    REQUIRE(((int)(img_plane[stride * row + 13])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 0xBD);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 17])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 18])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 21])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 22])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0x01);
    REQUIRE(((int)(img_plane[stride * row + 25])) == 0xAB);
    REQUIRE(((int)(img_plane[stride * row + 26])) == 0x01);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0xAB);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0xDE);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 30])) == 0xDE);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0x7F);
  }
  for (int row = 2; row < 4; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x76);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0x55);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0xBD);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0xBD);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0x01);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0xAB);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0xDE);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0xE8);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0x1D);
  }
  for (int row = 4; row < 6; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0xBD);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0xBD);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0x01);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0xAB);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0xDE);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0xE8);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0x1D);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0xA5);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0xA3);
  }
  for (int row = 6; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0xBD);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0x01);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0xAB);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0xDE);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0xE8);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0x1D);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0xA5);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0xA3);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0xBC);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0x54);
  }
  for (int row = 8; row < 10; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x01);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0xAB);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0xDE);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0xE8);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0x1D);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0xA5);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0xA3);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0xBC);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0x54);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0x76);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0x55);
  }
  img_plane = heif_image_get_plane_readonly(img, heif_channel_Cr, &stride);
  REQUIRE(stride == 128);
  for (int row = 0; row < 2; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0xBD);
    REQUIRE(((int)(img_plane[stride * row + 1])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 2])) == 0xBD);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x48);
    REQUIRE(((int)(img_plane[stride * row + 5])) == 0x4A);
    REQUIRE(((int)(img_plane[stride * row + 6])) == 0x48);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0x4A);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x2F);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 0x6B);
    REQUIRE(((int)(img_plane[stride * row + 10])) == 0x2F);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0x6B);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0xEB);
    REQUIRE(((int)(img_plane[stride * row + 13])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 14])) == 0xEB);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 17])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 18])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0xBB);
    REQUIRE(((int)(img_plane[stride * row + 21])) == 0x94);
    REQUIRE(((int)(img_plane[stride * row + 22])) == 0xBB);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0x94);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0x2D);
    REQUIRE(((int)(img_plane[stride * row + 25])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 26])) == 0x2D);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0xF5);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 30])) == 0xF5);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0x7F);
  }
  for (int row = 2; row < 4; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x48);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0x4A);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0x2F);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0x6B);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0xEB);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0xBB);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0x94);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0x2D);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0xF5);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0xBA);
  }
  for (int row = 4; row < 6; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0x2F);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0x6B);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0xEB);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0xBB);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0x94);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0x2D);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0xF5);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0xBA);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0x3F);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0xAD);
  }
  for (int row = 6; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0xEB);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0xBB);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0x94);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0x2D);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0xF5);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0xBA);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0x3F);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0xAD);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0xBD);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0xFF);
  }
  for (int row = 8; row < 10; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0xBB);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0x94);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0x2D);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0x00);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0xF5);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0x7F);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0x80);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0xBA);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0x3F);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0xAD);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0xBD);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0xFF);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 0x48);
    REQUIRE(((int)(img_plane[stride * row + 31])) == 0x4A);
  }

  heif_image_release(img);
  heif_image_handle_release(handle);
}

TEST_CASE("check image content YCbCr 4:2:0 16 bit") {
  auto file = GENERATE(YUV_16BIT_420_FILES);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_content_ycbcr420_16bit(context);
  heif_context_free(context);
}