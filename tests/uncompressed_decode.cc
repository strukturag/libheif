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

void check_image_handle_size(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  int ispe_width = heif_image_handle_get_ispe_width(handle);
  REQUIRE(ispe_width == 30);
  int ispe_height = heif_image_handle_get_ispe_height(handle);
  REQUIRE(ispe_height == 20);
  int width = heif_image_handle_get_width(handle);
  REQUIRE(width == 30);
  int height = heif_image_handle_get_height(handle);
  REQUIRE(height == 20);

  heif_image_handle_release(handle);
}

TEST_CASE("check image handle size") {
  auto file = GENERATE(FILES, MONO_FILES, YUV_FILES);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_handle_size(context);
  heif_context_free(context);
}

void check_image_handle_size_subsampled(struct heif_context *&context) {
  heif_image_handle *handle = get_primary_image_handle(context);
  int ispe_width = heif_image_handle_get_ispe_width(handle);
  REQUIRE(ispe_width == 32);
  int ispe_height = heif_image_handle_get_ispe_height(handle);
  REQUIRE(ispe_height == 20);
  int width = heif_image_handle_get_width(handle);
  REQUIRE(width == 32);
  int height = heif_image_handle_get_height(handle);
  REQUIRE(height == 20);

  heif_image_handle_release(handle);
}


TEST_CASE("check image handle size subsampled") {
  auto file = GENERATE(YUV_422_FILES, YUV_420_FILES, YUV_16BIT_422_FILES, YUV_16BIT_420_FILES);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_handle_size_subsampled(context);
  heif_context_free(context);
}

TEST_CASE("check image handle alpha channel") {
  auto file = GENERATE(FILES, MONO_FILES, ALL_YUV_FILES);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  int expect_alpha = (strchr(file, 'A') == NULL) ? 0 : 1;
  heif_image_handle *handle = get_primary_image_handle(context);
  int has_alpha = heif_image_handle_has_alpha_channel(handle);
  REQUIRE(has_alpha == expect_alpha);

  heif_image_handle_release(handle);
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
  auto file = GENERATE(FILES, MONO_FILES, ALL_YUV_FILES);
  auto context = get_context_for_test_file(file);
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
  auto file = GENERATE(FILES, MONO_FILES, ALL_YUV_FILES);
  auto context = get_context_for_test_file(file);
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
  auto file = GENERATE(FILES, MONO_FILES, ALL_YUV_FILES);
  auto context = get_context_for_test_file(file);
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
  auto file = GENERATE(FILES, MONO_FILES, ALL_YUV_FILES);
  auto context = get_context_for_test_file(file);
  INFO("file name: " << file);
  check_image_handle_no_metadata(context);
  heif_context_free(context);
}

TEST_CASE("check uncompressed is advertised") {
  REQUIRE(heif_have_decoder_for_format(heif_compression_uncompressed));
}

