/*
  libheif clean aperture (clap) zero-size unit tests

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

#include "catch_amalgamated.hpp"
#include "box.h"
#include "libheif/heif.h"
#include "test_utils.h"
#include <cstdlib>

// Regression tests for GHSA-jc8f-p23p-5hjg and GHSA-gh5q-69gg-c964. A zero image
// dimension used to underflow `image_width - 1U` to UINT32_MAX, and a dimension above
// INT32_MAX + 1 exceeded the Fraction range directly. Both tripped an assert in the
// former Fraction(uint32_t, uint32_t) constructor (abort in debug builds, corrupt crop
// in release builds). Box_clap::get_crop() must report both as an error instead.

static std::shared_ptr<Box_clap> make_clap()
{
  auto clap = std::make_shared<Box_clap>();
  // clap 100x200 at the top-left corner of a 150x250 image
  REQUIRE(clap->set(100, 200, 150, 250).error_code == heif_error_Ok);
  return clap;
}

TEST_CASE("clap crop for a valid image size") {
  auto clap = make_clap();

  auto crop = clap->get_crop(150, 250);
  REQUIRE(crop);
  REQUIRE(crop->left == 0);
  REQUIRE(crop->right == 99);
  REQUIRE(crop->top == 0);
  REQUIRE(crop->bottom == 199);
}

TEST_CASE("clap crop with zero image size") {
  auto clap = make_clap();

  const uint32_t sizes[][2] = {{0, 250}, {150, 0}, {0, 0}};
  for (auto& size : sizes) {
    auto crop = clap->get_crop(size[0], size[1]);
    REQUIRE(!crop);
    REQUIRE(crop.error().error_code == heif_error_Invalid_input);
    REQUIRE(crop.error().sub_error_code == heif_suberror_Invalid_clean_aperture);
  }
}

TEST_CASE("clap crop with oversized image size") {
  auto clap = make_clap();

  const uint32_t sizes[][2] = {{0xFFFFFFFF, 250}, {150, 0xFFFFFFFF}, {0x80000001, 250}, {150, 0x80000001}};
  for (auto& size : sizes) {
    auto crop = clap->get_crop(size[0], size[1]);
    REQUIRE(!crop);
    REQUIRE(crop.error().error_code == heif_error_Invalid_input);
    REQUIRE(crop.error().sub_error_code == heif_suberror_Invalid_clean_aperture);
  }

  // The largest values that still fit must be computed normally: the crop is centered
  // (up to the Fraction's reduced precision at this magnitude) and keeps its size.
  auto crop = clap->get_crop(0x80000000, 0x80000000);
  REQUIRE(crop);
  REQUIRE(std::abs(crop->left - (0x40000000 - 75)) <= 1);
  REQUIRE(crop->right - crop->left + 1 == 100);
  REQUIRE(std::abs(crop->top - (0x40000000 - 125)) <= 1);
  REQUIRE(crop->bottom - crop->top + 1 == 200);
}

TEST_CASE("clap set() rejects a clean aperture larger than the image") {
  auto clap = std::make_shared<Box_clap>();
  REQUIRE(clap->set(200, 100, 150, 250).error_code == heif_error_Usage_error);
  REQUIRE(clap->set(100, 300, 150, 250).error_code == heif_error_Usage_error);
}


// The same bug through the public API. The files carry an 'ispe' with one dimension of
// 0xFFFFFFFF and a 'clap' property; two of them add an 'irot' so that the oversized
// dimension arrives at the other side of the clap computation after the rotation swaps
// width and height.
TEST_CASE("image tiling with clap and oversized ispe") {
  const char* files[] = {
      "clap_oversized_ispe_height.avif",
      "clap_oversized_ispe_width.avif",
      "clap_oversized_ispe_irot180.avif",
      "clap_oversized_ispe_irot90.avif",
  };

  for (const char* file : files) {
    INFO(file);

    heif_context* ctx = get_context_for_test_file(file);
    heif_image_handle* handle = get_primary_image_handle(ctx);
    heif_image_tiling tiling{};

    // With the default security limits, the tiling API must reject the image like the
    // decoding path does, instead of handing out a size nobody can allocate.
    heif_error err = heif_image_handle_get_image_tiling(handle, 1, &tiling);
    REQUIRE(err.code == heif_error_Memory_allocation_error);
    REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);

    // Without limits, the clap computation itself must report the error instead of
    // aborting on an assert or computing a bogus crop.
    heif_context_set_security_limits(ctx, heif_get_disabled_security_limits());
    err = heif_image_handle_get_image_tiling(handle, 1, &tiling);
    REQUIRE(err.code == heif_error_Invalid_input);
    REQUIRE(err.subcode == heif_suberror_Invalid_clean_aperture);

    // The crop-border query has no error return and reports "no cropping" instead.
    int left = -1, top = -1, right = -1, bottom = -1;
    heif_item_get_property_transform_crop_borders(ctx, heif_image_handle_get_item_id(handle), 0,
                                                  64, -1, &left, &top, &right, &bottom);
    REQUIRE(left == 0);
    REQUIRE(top == 0);
    REQUIRE(right == 0);
    REQUIRE(bottom == 0);

    heif_image_handle_release(handle);
    heif_context_free(ctx);
  }
}
