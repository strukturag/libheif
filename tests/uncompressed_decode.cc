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
#include "test-config.h"

#define FILES "uncompressed_rgb3.heif", "uncompressed_planar_tiled.heif", "uncompressed_row.heif", \
  "uncompressed_row_tiled.heif", "uncompressed_pix_tile_align.heif", \
  "uncompressed_comp_tile_align.heif", "uncompressed_row_tile_align.heif"

struct heif_context * get_context_for_file(std::string filename) {
  struct heif_context* context;
  struct heif_error err;
  context = heif_context_alloc();
  err = heif_context_read_from_file(context, (tests_data_directory + "/" + filename).c_str(), NULL);
  INFO(filename);
  REQUIRE(err.code == heif_error_Ok);
  return context;
}

struct heif_image_handle * get_primary_image_handle(heif_context *context) {
  struct heif_error err;
  struct heif_image_handle * image_handle;
  int num_images = heif_context_get_number_of_top_level_images(context);
  REQUIRE(num_images == 1);
  err =  heif_context_get_primary_image_handle(context, &image_handle);
  REQUIRE(err.code == heif_error_Ok);
  return image_handle;
}

struct heif_image * get_primary_image(heif_image_handle * handle) {
  struct heif_error err;
  struct heif_image* img;
  err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_444, NULL);
  REQUIRE(err.code == heif_error_Ok);
  return img;
}

void check_image_handle_size(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  int ispe_width = heif_image_handle_get_ispe_width(handle);
  REQUIRE(ispe_width == 20);
  int ispe_height = heif_image_handle_get_ispe_height(handle);
  REQUIRE(ispe_height == 10);
  int width = heif_image_handle_get_width(handle);
  REQUIRE(width == 20);
  int height = heif_image_handle_get_height(handle);
  REQUIRE(height == 10);

  heif_image_handle_release(handle);
}

TEST_CASE("check image handle size") {
  auto file = GENERATE(FILES);
  auto context = get_context_for_file(file);
  INFO("file name: " << file);
  check_image_handle_size(context);
  heif_context_free(context);
}

void check_image_handle_no_alpha(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);

  int has_alpha = heif_image_handle_has_alpha_channel(handle);
  REQUIRE(has_alpha == 0);

  heif_image_handle_release(handle);
}

TEST_CASE("check image handle no alpha channel") {
  auto file = GENERATE(FILES);
  auto context = get_context_for_file(file);
  INFO("file name: " << file);
  check_image_handle_no_alpha(context);
  heif_context_free(context);
}

void check_image_handle_no_depth_images(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);

  int has_depth = heif_image_handle_has_depth_image(handle);
  REQUIRE(has_depth == 0);

  int numdepth = heif_image_handle_get_number_of_depth_images(handle);
  REQUIRE(numdepth == 0);

  heif_image_handle_release(handle);
}

TEST_CASE("check image handle no depth images") {
  auto file = GENERATE(FILES);
  auto context = get_context_for_file(file);
  INFO("file name: " << file);
  check_image_handle_no_depth_images(context);
  heif_context_free(context);
}

void check_image_handle_no_thumbnails(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);

  int numthumbs = heif_image_handle_get_number_of_thumbnails(handle);
  REQUIRE(numthumbs == 0);

  heif_image_handle_release(handle);
}

TEST_CASE("check image handle no thumbnails") {
  auto file = GENERATE(FILES);
  auto context = get_context_for_file(file);
  INFO("file name: " << file);
  check_image_handle_no_thumbnails(context);
  heif_context_free(context);
}

void check_image_handle_no_aux_images(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);

  int num_aux = heif_image_handle_get_number_of_auxiliary_images(handle, 0);
  REQUIRE(num_aux == 0);

  heif_image_handle_release(handle);
}

TEST_CASE("check image handle no auxiliary images") {
  auto file = GENERATE(FILES);
  auto context = get_context_for_file(file);
  INFO("file name: " << file);
  check_image_handle_no_aux_images(context);
  heif_context_free(context);
}

void check_image_handle_no_metadata(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);

  int num_metadata_blocks =
      heif_image_handle_get_number_of_metadata_blocks(handle, NULL);
  REQUIRE(num_metadata_blocks == 0);

  heif_image_handle_release(handle);
}

TEST_CASE("check image handle no metadata blocks") {
  auto file = GENERATE(FILES);
  auto context = get_context_for_file(file);
  INFO("file name: " << file);
  check_image_handle_no_metadata(context);
  heif_context_free(context);
}

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
  REQUIRE(width == 20);
  int height = heif_image_get_primary_height(img);
  REQUIRE(height == 10);
  width = heif_image_get_width(img, heif_channel_R);
  REQUIRE(width == 20);
  height = heif_image_get_height(img, heif_channel_R);
  REQUIRE(height == 10);
  width = heif_image_get_width(img, heif_channel_G);
  REQUIRE(width == 20);
  height = heif_image_get_height(img, heif_channel_G);
  REQUIRE(height == 10);
  width = heif_image_get_width(img, heif_channel_B);
  REQUIRE(width == 20);
  height = heif_image_get_height(img, heif_channel_B);
  REQUIRE(height == 10);

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
  auto file = GENERATE(FILES);
  auto context = get_context_for_file(file);
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
  REQUIRE(stride == 64);
  for (int row = 0; row < 10; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0);
  }

  img_plane = heif_image_get_plane_readonly(img, heif_channel_G, &stride);
  REQUIRE(stride == 64);
  for (int row = 0; row < 10; row++) {
    INFO("row: " << row);
    REQUIRE(((int)(img_plane[stride * row + 0])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 3])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 4])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 7])) == 128);
    REQUIRE(((int)(img_plane[stride * row + 8])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 11])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 12])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 15])) == 255);
    REQUIRE(((int)(img_plane[stride * row + 16])) == 0);
    REQUIRE(((int)(img_plane[stride * row + 19])) == 0);
  }

  img_plane = heif_image_get_plane_readonly(img, heif_channel_B, &stride);
  REQUIRE(stride == 64);
  for (int row = 0; row < 10; row++) {
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
  }

  heif_image_release(img);
  heif_image_handle_release(handle);
}

TEST_CASE("check image content") {
  auto file = GENERATE(FILES);
  auto context = get_context_for_file(file);
  INFO("file name: " << file);
  check_image_content(context);
  heif_context_free(context);
}
