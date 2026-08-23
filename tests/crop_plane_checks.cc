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

// Regression tests for HeifPixelImage::crop().
//
// crop() validates the requested rectangle against the image's declared
// m_width/m_height, maps it into per-plane coordinates via
// get_subsampled_size_h/v(), and hands it to ComponentStorage::crop(), which
// does a row-wise bulk memcpy. None of that checks that a plane's actual
// stored size matches what m_width/m_height (or, for Cb/Cr, their
// chroma-subsampled size) implies -- a plane can be added at any size through
// add_channel()/copy_new_channel_from()/transfer_channel_from_image_as(). A
// plane smaller than the image claims is read past its end. crop() now calls
// has_standard_plane_sizes() up front to reject that instead.

TEST_CASE("crop rejects a plane smaller than the image's declared size") {
  auto* limits = heif_get_global_security_limits();

  auto image = std::make_shared<HeifPixelImage>();
  image->create(8, 8, heif_colorspace_monochrome, heif_chroma_monochrome);

  // Y plane smaller than the image's declared 8x8 -- add_channel() doesn't
  // check this against m_width/m_height.
  REQUIRE(image->add_channel(heif_channel_Y, 4, 4, 8, limits).error_code == heif_error_Ok);

  auto result = image->crop(0, 3, 0, 3, limits);
  REQUIRE(result.is_error());
  REQUIRE(result.error().error_code == heif_error_Unsupported_feature);
}

TEST_CASE("crop crops a well-formed monochrome image correctly") {
  auto* limits = heif_get_global_security_limits();

  auto image = std::make_shared<HeifPixelImage>();
  image->create(4, 4, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(image->add_channel(heif_channel_Y, 4, 4, 8, limits).error_code == heif_error_Ok);

  size_t stride;
  uint8_t* y = image->get_channel_memory(heif_channel_Y, &stride);
  for (uint32_t row = 0; row < 4; row++) {
    for (uint32_t col = 0; col < 4; col++) {
      y[row * stride + col] = static_cast<uint8_t>(row * 4 + col);
    }
  }

  // Keep the inner 2x2 region: columns/rows 1..2.
  auto result = image->crop(1, 2, 1, 2, limits);
  REQUIRE(!result.is_error());

  auto cropped = *result;
  REQUIRE(cropped->get_width(heif_channel_Y) == 2);
  REQUIRE(cropped->get_height(heif_channel_Y) == 2);

  const uint8_t* out = cropped->get_channel_memory(heif_channel_Y, &stride);
  REQUIRE(out[0 * stride + 0] == 5);  // (1,1)
  REQUIRE(out[0 * stride + 1] == 6);  // (2,1)
  REQUIRE(out[1 * stride + 0] == 9);  // (1,2)
  REQUIRE(out[1 * stride + 1] == 10); // (2,2)
}

TEST_CASE("crop crops a well-formed YCbCr 4:2:0 image correctly") {
  auto* limits = heif_get_global_security_limits();

  // Cb/Cr are legitimately half-size in 4:2:0 -- has_standard_plane_sizes()
  // must not reject that.
  auto image = std::make_shared<HeifPixelImage>();
  image->create(4, 4, heif_colorspace_YCbCr, heif_chroma_420);
  REQUIRE(image->add_channel(heif_channel_Y, 4, 4, 8, limits).error_code == heif_error_Ok);
  REQUIRE(image->add_channel(heif_channel_Cb, 2, 2, 8, limits).error_code == heif_error_Ok);
  REQUIRE(image->add_channel(heif_channel_Cr, 2, 2, 8, limits).error_code == heif_error_Ok);

  // Crop to an even-aligned 2x2 region so no 4:4:4 upconversion is triggered.
  auto result = image->crop(0, 1, 0, 1, limits);
  REQUIRE(!result.is_error());

  auto cropped = *result;
  REQUIRE(cropped->get_width(heif_channel_Y) == 2);
  REQUIRE(cropped->get_height(heif_channel_Y) == 2);
  REQUIRE(cropped->get_width(heif_channel_Cb) == 1);
  REQUIRE(cropped->get_height(heif_channel_Cb) == 1);
}
