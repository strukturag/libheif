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
// A crop, rotation or mirror that splits a 4:2:0 chroma sample converts the
// image to 4:4:4 first. That conversion has to keep the image's own colour
// profile: with a default (full-range) target profile, limited-range samples
// were expanded to full range here and then again when converted to RGB, so
// such an image came out brighter than the same area of the untransformed
// image.

static std::shared_ptr<HeifPixelImage> make_flat_limited_range_420(uint32_t width, uint32_t height,
                                                                   const heif_security_limits* limits)
{
  auto image = std::make_shared<HeifPixelImage>();
  image->create(width, height, heif_colorspace_YCbCr, heif_chroma_420);
  REQUIRE(image->add_channel(heif_channel_Y, width, height, 8, limits).error_code == heif_error_Ok);
  REQUIRE(image->add_channel(heif_channel_Cb, (width + 1) / 2, (height + 1) / 2, 8, limits).error_code == heif_error_Ok);
  REQUIRE(image->add_channel(heif_channel_Cr, (width + 1) / 2, (height + 1) / 2, 8, limits).error_code == heif_error_Ok);

  REQUIRE(!image->fill_channel(heif_channel_Y, 180));
  REQUIRE(!image->fill_channel(heif_channel_Cb, 100));
  REQUIRE(!image->fill_channel(heif_channel_Cr, 150));

  nclx_profile nclx;
  nclx.set_colour_primaries(heif_color_primaries_ITU_R_BT_709_5);
  nclx.set_transfer_characteristics(heif_transfer_characteristic_ITU_R_BT_709_5);
  nclx.set_matrix_coefficients(heif_matrix_coefficients_ITU_R_BT_709_5);
  nclx.set_full_range_flag(false);
  image->set_color_profile_nclx(nclx);
  return image;
}

static void require_flat_limited_range_444(const std::shared_ptr<HeifPixelImage>& image)
{
  REQUIRE(image->get_chroma_format() == heif_chroma_444);

  for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}) {
    uint8_t expected = channel == heif_channel_Y ? 180 : (channel == heif_channel_Cb ? 100 : 150);
    size_t stride;
    const uint8_t* plane = image->get_channel_memory(channel, &stride);
    for (uint32_t y = 0; y < image->get_height(channel); y++) {
      for (uint32_t x = 0; x < image->get_width(channel); x++) {
        INFO("channel " << channel << " at " << x << "," << y);
        REQUIRE(plane[y * stride + x] == expected);
      }
    }
  }
}

TEST_CASE("crop of 4:2:0 at an odd offset keeps the sample values and range") {
  auto* limits = heif_get_global_security_limits();
  auto image = make_flat_limited_range_420(8, 8, limits);

  auto result = image->crop(1, 6, 1, 6, limits);
  REQUIRE(!result.is_error());
  require_flat_limited_range_444(*result);
}

TEST_CASE("rotation of 4:2:0 with an odd width keeps the sample values and range") {
  auto* limits = heif_get_global_security_limits();
  auto image = make_flat_limited_range_420(7, 8, limits);

  auto result = image->rotate_ccw(90, limits);
  REQUIRE(!result.is_error());
  require_flat_limited_range_444(*result);
}

TEST_CASE("mirroring of 4:2:0 with an odd width keeps the sample values and range") {
  auto* limits = heif_get_global_security_limits();
  auto image = make_flat_limited_range_420(7, 8, limits);

  auto result = image->mirror_inplace(heif_transform_mirror_direction_horizontal, limits);
  REQUIRE(!result.is_error());
  require_flat_limited_range_444(*result);
}
