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

void check_image_size_ycbcr(struct heif_context *&context) {
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
  REQUIRE(width == 30);
  int height = heif_image_get_primary_height(img);
  REQUIRE(height == 20);
  width = heif_image_get_width(img, heif_channel_Y);
  REQUIRE(width == 30);
  height = heif_image_get_height(img, heif_channel_Y);
  REQUIRE(height == 20);
  width = heif_image_get_width(img, heif_channel_Cb);
  REQUIRE(width == 30);
  height = heif_image_get_height(img, heif_channel_Cr);
  REQUIRE(height == 20);
  width = heif_image_get_width(img, heif_channel_Cr);
  REQUIRE(width == 30);
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

TEST_CASE("check image size YCbCr") {
  auto file = GENERATE(YUV_FILES);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_size_ycbcr(context);
  heif_context_free(context);
}


void check_image_content_ycbcr(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  heif_image *img = get_primary_image_ycbcr(handle, heif_chroma_444);

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
    REQUIRE(((int)(img_plane[stride * row + 3])) == 84);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 85);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 85);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 127);
  }
  for (int row = 4; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 85);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 85);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 29);
  }
  for (int row = 8; row < 12; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 163);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 163);
  }
  for (int row = 12; row < 16; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 163);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 163);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 84);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 84);
  }
  for (int row = 16; row < 20; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 170);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 29);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 163);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 163);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 84);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 84);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 85);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 85);
  }
  img_plane = heif_image_get_plane_readonly(img, heif_channel_Cr, &stride);
  REQUIRE(stride == 64);
  for (int row = 0; row < 4; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 73);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 73);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 106);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 106);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 127);
  }
  for (int row = 4; row < 8; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 73);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 73);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 106);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 106);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 185);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 185);
  }
  for (int row = 8; row < 12; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 106);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 106);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 185);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 185);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 172);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 172);
  }
  for (int row = 12; row < 16; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 185);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 185);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 172);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 172);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 254);
  }
  for (int row = 16; row < 20; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 148);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 9])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 127);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 185);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 185);
    REQUIRE(((int)(img_plane[stride * row + 20])) == 172);
    REQUIRE(((int)(img_plane[stride * row + 23])) == 172);
    REQUIRE(((int)(img_plane[stride * row + 24])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 27])) == 254);
    REQUIRE(((int)(img_plane[stride * row + 28])) == 73);
    REQUIRE(((int)(img_plane[stride * row + 29])) == 73);
  }

  heif_image_release(img);
  heif_image_handle_release(handle);
}

TEST_CASE("check image content YCbCr") {
  auto file = GENERATE(YUV_FILES);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_content_ycbcr(context);
  heif_context_free(context);
}


