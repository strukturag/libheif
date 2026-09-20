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

// Regression tests for the Cb/Cr size check in HeifPixelImage::add_channel().
//
// A Cb/Cr plane must have exactly the chroma-subsampled size of the logical image
// (round_up). Building one at a different size makes the plane's allocation disagree
// with the size other code derives from the logical dimensions. An oversized plane
// let extend_to_size_with_zero() underflow its per-row fill length and write ~4 GB
// past the plane (GHSA-j2rv-58fh-w8pw); an undersized plane caused out-of-bounds
// reads during RGB conversion (issue #1796). add_channel() now rejects the mismatch
// at construction. Only Cb/Cr are constrained; alpha/depth/... may differ.

TEST_CASE("add_channel rejects an oversized Cb plane (GHSA-j2rv-58fh-w8pw)") {
  auto* limits = heif_get_global_security_limits();

  auto image = std::make_shared<HeifPixelImage>();
  image->create(12, 8, heif_colorspace_YCbCr, heif_chroma_420);
  REQUIRE(image->add_channel(heif_channel_Y, 12, 8, 8, limits).error_code == heif_error_Ok);

  // The subsampled Cb size of a 12x8 4:2:0 image is 6x4; 32x8 is far too large.
  Error err = image->add_channel(heif_channel_Cb, 32, 8, 8, limits);
  REQUIRE(err.error_code == heif_error_Usage_error);
  REQUIRE(err.sub_error_code == heif_suberror_Invalid_parameter_value);
}

TEST_CASE("add_channel rejects an undersized (round-down) chroma plane") {
  auto* limits = heif_get_global_security_limits();

  // Odd dimensions: the round_up subsampled size is 7x5, so a floor-division 6x4
  // plane (as an out-of-spec producer might compute) must be rejected.
  auto image = std::make_shared<HeifPixelImage>();
  image->create(13, 9, heif_colorspace_YCbCr, heif_chroma_420);
  REQUIRE(image->add_channel(heif_channel_Y, 13, 9, 8, limits).error_code == heif_error_Ok);

  REQUIRE(image->add_channel(heif_channel_Cb, 6, 4, 8, limits).error_code == heif_error_Usage_error);
  REQUIRE(image->add_channel(heif_channel_Cr, 6, 4, 8, limits).error_code == heif_error_Usage_error);
}

TEST_CASE("add_channel accepts correctly-subsampled chroma planes") {
  auto* limits = heif_get_global_security_limits();

  SECTION("4:2:0, even dimensions") {
    auto image = std::make_shared<HeifPixelImage>();
    image->create(12, 8, heif_colorspace_YCbCr, heif_chroma_420);
    REQUIRE(image->add_channel(heif_channel_Y, 12, 8, 8, limits).error_code == heif_error_Ok);
    REQUIRE(image->add_channel(heif_channel_Cb, 6, 4, 8, limits).error_code == heif_error_Ok);
    REQUIRE(image->add_channel(heif_channel_Cr, 6, 4, 8, limits).error_code == heif_error_Ok);
  }

  SECTION("4:2:0, odd dimensions round up") {
    auto image = std::make_shared<HeifPixelImage>();
    image->create(13, 9, heif_colorspace_YCbCr, heif_chroma_420);
    REQUIRE(image->add_channel(heif_channel_Y, 13, 9, 8, limits).error_code == heif_error_Ok);
    REQUIRE(image->add_channel(heif_channel_Cb, 7, 5, 8, limits).error_code == heif_error_Ok);
    REQUIRE(image->add_channel(heif_channel_Cr, 7, 5, 8, limits).error_code == heif_error_Ok);
  }

  SECTION("4:2:2 subsamples width only") {
    auto image = std::make_shared<HeifPixelImage>();
    image->create(12, 8, heif_colorspace_YCbCr, heif_chroma_422);
    REQUIRE(image->add_channel(heif_channel_Y, 12, 8, 8, limits).error_code == heif_error_Ok);
    REQUIRE(image->add_channel(heif_channel_Cb, 6, 8, 8, limits).error_code == heif_error_Ok);
    REQUIRE(image->add_channel(heif_channel_Cr, 6, 8, 8, limits).error_code == heif_error_Ok);
  }

  SECTION("4:4:4 chroma is full size") {
    auto image = std::make_shared<HeifPixelImage>();
    image->create(12, 8, heif_colorspace_YCbCr, heif_chroma_444);
    REQUIRE(image->add_channel(heif_channel_Y, 12, 8, 8, limits).error_code == heif_error_Ok);
    REQUIRE(image->add_channel(heif_channel_Cb, 12, 8, 8, limits).error_code == heif_error_Ok);
    REQUIRE(image->add_channel(heif_channel_Cr, 12, 8, 8, limits).error_code == heif_error_Ok);
  }
}

TEST_CASE("add_channel does not constrain non-chroma auxiliary planes") {
  auto* limits = heif_get_global_security_limits();

  // The size rule is Cb/Cr only. An auxiliary plane such as depth may legitimately
  // have a size unrelated to the colour planes, so add_channel must still accept it.
  auto image = std::make_shared<HeifPixelImage>();
  image->create(12, 8, heif_colorspace_YCbCr, heif_chroma_420);
  REQUIRE(image->add_channel(heif_channel_Y, 12, 8, 8, limits).error_code == heif_error_Ok);
  REQUIRE(image->add_channel(heif_channel_Cb, 6, 4, 8, limits).error_code == heif_error_Ok);
  REQUIRE(image->add_channel(heif_channel_Cr, 6, 4, 8, limits).error_code == heif_error_Ok);

  REQUIRE(image->add_channel(heif_channel_depth, 5, 5, 8, limits).error_code == heif_error_Ok);
}
