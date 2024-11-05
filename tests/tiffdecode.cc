/*
  libheifio TIFF decode unit tests

  MIT License

  Copyright (c) 2024 Brad Hards <bradh@frogmouth.net>

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
#include <cstdint>
#include <iostream>
#include "heifio/decoder.h"
#include "heifio/decoder_tiff.h"
#include "test_utils.h"
#include "libheif/heif.h"
#include "libheif/api_structs.h"

#if HAVE_LIBTIFF
void checkMono(InputImage input_image) {
  REQUIRE(input_image.orientation == heif_orientation_normal);
  REQUIRE(input_image.image != nullptr);
  const struct heif_image* image = input_image.image.get();
  REQUIRE(heif_image_get_colorspace(image) == heif_colorspace_monochrome);
  REQUIRE(heif_image_get_chroma_format(image) == heif_chroma_monochrome);
  REQUIRE(heif_image_get_width(image, heif_channel_Y) == 128);
  REQUIRE(heif_image_get_height(image, heif_channel_Y) == 64);
  REQUIRE(heif_image_get_bits_per_pixel(image, heif_channel_Y) == 8);
  REQUIRE(heif_image_get_bits_per_pixel_range(image, heif_channel_Y) == 8);
  REQUIRE(heif_image_has_channel(image, heif_channel_Y) == 1);
  REQUIRE(heif_image_has_channel(image, heif_channel_R) == 0);
  REQUIRE(heif_image_has_channel(image, heif_channel_G) == 0);
  REQUIRE(heif_image_has_channel(image, heif_channel_B) == 0);
  REQUIRE(heif_image_has_channel(image, heif_channel_interleaved) == 0);
  REQUIRE(heif_image_has_channel(image, heif_channel_Cr) == 0);
  REQUIRE(heif_image_has_channel(image, heif_channel_Cb) == 0);
}

TEST_CASE("mono8") {
  InputImage input_image;
  std::string path = get_path_for_heifio_test_file("mono.tif");
  heif_error err = loadTIFF(path.c_str(), &input_image);
  REQUIRE(err.code == heif_error_Ok);
  checkMono(input_image);
}

TEST_CASE("mono8planar") {
  InputImage input_image;
  std::string path = get_path_for_heifio_test_file("mono_planar.tif");
  heif_error err = loadTIFF(path.c_str(), &input_image);
  REQUIRE(err.code == heif_error_Ok);
  checkMono(input_image);
}

void checkRGB(InputImage input_image) {
  REQUIRE(input_image.orientation == heif_orientation_normal);
  REQUIRE(input_image.image != nullptr);
  const struct heif_image* image = input_image.image.get();
  REQUIRE(heif_image_get_colorspace(image) == heif_colorspace_RGB);
  REQUIRE(heif_image_get_chroma_format(image) == heif_chroma_interleaved_RGB);
  REQUIRE(heif_image_get_width(image, heif_channel_interleaved) == 32);
  REQUIRE(heif_image_get_height(image, heif_channel_interleaved) == 10);
  REQUIRE(heif_image_get_bits_per_pixel(image, heif_channel_interleaved) == 24);
  REQUIRE(heif_image_get_bits_per_pixel_range(image, heif_channel_interleaved) == 8);
  REQUIRE(heif_image_has_channel(image, heif_channel_Y) == 0);
  REQUIRE(heif_image_has_channel(image, heif_channel_R) == 0);
  REQUIRE(heif_image_has_channel(image, heif_channel_G) == 0);
  REQUIRE(heif_image_has_channel(image, heif_channel_B) == 0);
  REQUIRE(heif_image_has_channel(image, heif_channel_interleaved) == 1);
  REQUIRE(heif_image_has_channel(image, heif_channel_Cr) == 0);
  REQUIRE(heif_image_has_channel(image, heif_channel_Cb) == 0);
}

TEST_CASE("rgb") {
  InputImage input_image;
  std::string path = get_path_for_heifio_test_file("rgb.tif");
  heif_error err = loadTIFF(path.c_str(), &input_image);
  REQUIRE(err.code == heif_error_Ok);
  checkRGB(input_image);
}

TEST_CASE("rgb_planar") {
  InputImage input_image;
  std::string path = get_path_for_heifio_test_file("rgb_planar.tif");
  heif_error err = loadTIFF(path.c_str(), &input_image);
  REQUIRE(err.code == heif_error_Ok);
  checkRGB(input_image);
}


void checkRGBA(InputImage input_image) {
  REQUIRE(input_image.orientation == heif_orientation_normal);
  REQUIRE(input_image.image != nullptr);
  const struct heif_image* image = input_image.image.get();
  REQUIRE(heif_image_get_colorspace(image) == heif_colorspace_RGB);
  REQUIRE(heif_image_get_chroma_format(image) == heif_chroma_interleaved_RGBA);
  REQUIRE(heif_image_get_width(image, heif_channel_interleaved) == 32);
  REQUIRE(heif_image_get_height(image, heif_channel_interleaved) == 10);
  REQUIRE(heif_image_get_bits_per_pixel(image, heif_channel_interleaved) == 32);
  REQUIRE(heif_image_get_bits_per_pixel_range(image, heif_channel_interleaved) == 8);
  REQUIRE(heif_image_has_channel(image, heif_channel_Y) == 0);
  REQUIRE(heif_image_has_channel(image, heif_channel_R) == 0);
  REQUIRE(heif_image_has_channel(image, heif_channel_G) == 0);
  REQUIRE(heif_image_has_channel(image, heif_channel_B) == 0);
  REQUIRE(heif_image_has_channel(image, heif_channel_interleaved) == 1);
  REQUIRE(heif_image_has_channel(image, heif_channel_Cr) == 0);
  REQUIRE(heif_image_has_channel(image, heif_channel_Cb) == 0);
}

TEST_CASE("rgba") {
  InputImage input_image;
  std::string path = get_path_for_heifio_test_file("rgba.tif");
  heif_error err = loadTIFF(path.c_str(), &input_image);
  REQUIRE(err.code == heif_error_Ok);
  checkRGBA(input_image);
}

TEST_CASE("rgba_planar") {
  InputImage input_image;
  std::string path = get_path_for_heifio_test_file("rgba_planar.tif");
  heif_error err = loadTIFF(path.c_str(), &input_image);
  REQUIRE(err.code == heif_error_Ok);
  checkRGBA(input_image);
}
#else
TEST_CASE("no_tiff dummy") {
  // Dummy test if we don't have the TIFF library, so that testing does not fail with "No test ran".
}
#endif // HAVE_LIBTIFF