/*
  libheif unit tests

  MIT License

  Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>

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

#include "image/pixelimage.h"
#include "catch_amalgamated.hpp"
#include "libheif/heif.h"

#include <algorithm>

// Regression tests for HeifPixelImage::extend_to_size_with_zero().
//
// This function only grows an image. The per-row right-edge fill computes its
// memset length as (subsampled_width - old_width); on a shrink request that
// unsigned subtraction underflows to a huge value and the memset writes far
// past the end of the pixel plane (GHSA-hqc2-cx5m-g6ff, reachable through the
// public heif_image_extend_to_size_fill_with_zero() API). The function now
// rejects any target smaller than the current size before touching a plane.

TEST_CASE("extend_to_size_with_zero rejects a smaller width") {
  auto* limits = heif_get_global_security_limits();

  auto image = std::make_shared<HeifPixelImage>();
  image->create(64, 8, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(image->add_channel(heif_channel_Y, 64, 8, 8, limits).error_code == heif_error_Ok);

  // Requesting width 1 < 64 previously underflowed the fill length -> heap OOB write.
  Error err = image->extend_to_size_with_zero(1, 8, limits);
  REQUIRE(err.error_code == heif_error_Usage_error);
  REQUIRE(err.sub_error_code == heif_suberror_Invalid_parameter_value);

  // The image must be left unchanged on the rejected call.
  REQUIRE(image->get_width() == 64);
  REQUIRE(image->get_height() == 8);
}

TEST_CASE("extend_to_size_with_zero rejects a smaller height") {
  auto* limits = heif_get_global_security_limits();

  auto image = std::make_shared<HeifPixelImage>();
  image->create(8, 64, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(image->add_channel(heif_channel_Y, 8, 64, 8, limits).error_code == heif_error_Ok);

  Error err = image->extend_to_size_with_zero(8, 1, limits);
  REQUIRE(err.error_code == heif_error_Usage_error);
  REQUIRE(err.sub_error_code == heif_suberror_Invalid_parameter_value);

  REQUIRE(image->get_width() == 8);
  REQUIRE(image->get_height() == 64);
}

TEST_CASE("extend_to_size_with_zero still grows an image") {
  auto* limits = heif_get_global_security_limits();

  auto image = std::make_shared<HeifPixelImage>();
  image->create(4, 4, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(image->add_channel(heif_channel_Y, 4, 4, 8, limits).error_code == heif_error_Ok);

  // Fill the visible area with a marker so we can check the padding is zero.
  size_t stride;
  uint8_t* p = image->get_channel_memory(heif_channel_Y, &stride);
  for (uint32_t y = 0; y < 4; y++)
    for (uint32_t x = 0; x < 4; x++)
      p[y * stride + x] = 0xAB;

  Error err = image->extend_to_size_with_zero(8, 8, limits);
  REQUIRE(err.error_code == heif_error_Ok);
  REQUIRE(image->get_width() == 8);
  REQUIRE(image->get_height() == 8);

  p = image->get_channel_memory(heif_channel_Y, &stride);

  // Original pixels are preserved.
  for (uint32_t y = 0; y < 4; y++)
    for (uint32_t x = 0; x < 4; x++)
      REQUIRE(p[y * stride + x] == 0xAB);

  // Right and bottom borders are zero-filled.
  for (uint32_t y = 0; y < 8; y++)
    for (uint32_t x = 0; x < 8; x++)
      if (x >= 4 || y >= 4)
        REQUIRE(p[y * stride + x] == 0);
}

TEST_CASE("extend_to_size_with_zero accepts an unchanged size") {
  auto* limits = heif_get_global_security_limits();

  auto image = std::make_shared<HeifPixelImage>();
  image->create(8, 8, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(image->add_channel(heif_channel_Y, 8, 8, 8, limits).error_code == heif_error_Ok);

  Error err = image->extend_to_size_with_zero(8, 8, limits);
  REQUIRE(err.error_code == heif_error_Ok);
  REQUIRE(image->get_width() == 8);
  REQUIRE(image->get_height() == 8);
}


static void check_smaller_padding_request(uint32_t width, uint32_t height,
                                          uint32_t requested_width, uint32_t requested_height)
{
  auto* limits = heif_get_global_security_limits();
  auto image = std::make_shared<HeifPixelImage>();
  image->create(width, height, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(image->add_channel(heif_channel_Y, width, height, 8, limits).error_code == heif_error_Ok);

  size_t stride;
  uint8_t* pixels = image->get_channel_memory(heif_channel_Y, &stride);
  pixels[(height - 1) * stride + width - 1] = 0x5a;

  Error err = image->extend_padding_to_size(requested_width, requested_height, false, limits);
  REQUIRE(err.error_code == heif_error_Usage_error);
  REQUIRE(err.sub_error_code == heif_suberror_Invalid_parameter_value);
  REQUIRE(image->get_width() == width);
  REQUIRE(image->get_height() == height);
  REQUIRE(image->get_width(heif_channel_Y) == width);
  REQUIRE(image->get_height(heif_channel_Y) == height);
  REQUIRE(pixels[(height - 1) * stride + width - 1] == 0x5a);
}


TEST_CASE("extend_padding_to_size rejects a smaller height even when the width grows") {
  check_smaller_padding_request(64, 128, 128, 64);
}


TEST_CASE("extend_padding_to_size rejects a smaller width even when the height grows") {
  check_smaller_padding_request(128, 64, 64, 128);
}


TEST_CASE("heif_image_extend_padding_to_size rejects negative dimensions") {
  heif_image* image = nullptr;
  REQUIRE(heif_image_create(64, 64, heif_colorspace_monochrome,
                            heif_chroma_monochrome, &image).code == heif_error_Ok);
  REQUIRE(heif_image_add_plane(image, heif_channel_Y, 64, 64, 8).code == heif_error_Ok);

  heif_error err = heif_image_extend_padding_to_size(image, -1, 64);
  REQUIRE(err.code == heif_error_Usage_error);
  REQUIRE(err.subcode == heif_suberror_Invalid_parameter_value);
  heif_image_release(image);
}


TEST_CASE("extend_padding_to_size grows padding without changing the visible plane") {
  auto* limits = heif_get_global_security_limits();
  auto image = std::make_shared<HeifPixelImage>();
  image->create(64, 64, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(image->add_channel(heif_channel_Y, 64, 64, 8, limits).error_code == heif_error_Ok);

  size_t stride;
  uint8_t* pixels = image->get_channel_memory(heif_channel_Y, &stride);
  for (uint32_t y = 0; y < 64; y++) {
    for (uint32_t x = 0; x < 64; x++) {
      pixels[y * stride + x] = static_cast<uint8_t>(x + y);
    }
  }

  REQUIRE(image->extend_padding_to_size(128, 64, false, limits).error_code == heif_error_Ok);
  REQUIRE(image->get_width(heif_channel_Y) == 64);
  REQUIRE(image->get_height(heif_channel_Y) == 64);
  REQUIRE(image->extend_padding_to_size(128, 128, false, limits).error_code == heif_error_Ok);
  REQUIRE(image->get_width() == 64);
  REQUIRE(image->get_height() == 64);
  REQUIRE(image->get_width(heif_channel_Y) == 64);
  REQUIRE(image->get_height(heif_channel_Y) == 64);

  pixels = image->get_channel_memory(heif_channel_Y, &stride);
  REQUIRE(stride >= 128);
  for (uint32_t y = 0; y < 128; y++) {
    for (uint32_t x = 0; x < 128; x++) {
      REQUIRE(pixels[y * stride + x] == static_cast<uint8_t>(std::min(x, 63U) + std::min(y, 63U)));
    }
  }
}
