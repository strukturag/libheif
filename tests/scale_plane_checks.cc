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

// Regression tests for HeifPixelImage::scale_nearest_neighbor()'s plane checks.
//
// The per-channel loop that does the actual scaling indexes each source plane
// using coordinates derived from the image's logical size, trusting that the
// plane actually covers that geometry. Nothing about add_channel()/
// copy_new_channel_from()/transfer_channel_from_image_as() enforces that as an
// invariant, so two independent guards are needed:
//  - has_standard_plane_sizes() rejects any single plane (Y, Alpha, R, G, B,
//    interleaved: must match the full image size; Cb, Cr: must match the
//    chroma-subsampled size) whose actual size doesn't match what its channel
//    is expected to have -- this covers a single mismatched plane of any
//    channel, not just Alpha;
//  - a plane-count check rejects the source having more planes of a given
//    channel than were allocated for the destination (a same-channel
//    duplicate, e.g. from add_channel()/copy_new_channel_from() being called
//    twice for the same channel -- neither rejects duplicates the way
//    transfer_channel_from_image_as() does): each stored duplicate can have
//    the *correct* size and still slip past has_standard_plane_sizes(), so
//    the loop would write every one of them into the single allocated
//    destination plane, including whatever bit-depth mismatch it carries.

TEST_CASE("scale_nearest_neighbor rejects a duplicate color channel") {
  auto* limits = heif_get_global_security_limits();

  auto image = std::make_shared<HeifPixelImage>();
  image->create(4, 4, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(image->add_channel(heif_channel_Y, 4, 4, 8, limits).error_code == heif_error_Ok);

  // A second, differently-sized-per-sample Y plane. Neither add_channel() nor
  // copy_new_channel_from() reject an already-occupied channel, so this is
  // reachable without going through transfer_channel_from_image_as() (which
  // does reject it).
  REQUIRE(image->add_channel(heif_channel_Y, 4, 4, 16, limits).error_code == heif_error_Ok);

  std::shared_ptr<HeifPixelImage> scaled;
  Error err = image->scale_nearest_neighbor(scaled, 8, 8, limits);
  REQUIRE(err.error_code == heif_error_Unsupported_feature);
}

TEST_CASE("scale_nearest_neighbor rejects an Alpha plane whose size differs from the color planes") {
  auto* limits = heif_get_global_security_limits();

  auto image = std::make_shared<HeifPixelImage>();
  image->create(8, 8, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(image->add_channel(heif_channel_Y, 8, 8, 8, limits).error_code == heif_error_Ok);
  REQUIRE(image->add_channel(heif_channel_Alpha, 4, 4, 8, limits).error_code == heif_error_Ok);

  std::shared_ptr<HeifPixelImage> scaled;
  Error err = image->scale_nearest_neighbor(scaled, 16, 16, limits);
  REQUIRE(err.error_code == heif_error_Unsupported_feature);
}

TEST_CASE("scale_nearest_neighbor rejects a single color channel whose size doesn't match the image") {
  auto* limits = heif_get_global_security_limits();

  // Not a duplicate: exactly one Y plane, but sized smaller than the image's
  // own declared 256x256 -- reachable directly through the public plane API
  // (heif_image_create() + heif_image_add_plane()), with no decode involved.
  auto image = std::make_shared<HeifPixelImage>();
  image->create(256, 256, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(image->add_channel(heif_channel_Y, 8, 8, 8, limits).error_code == heif_error_Ok);

  std::shared_ptr<HeifPixelImage> scaled;
  Error err = image->scale_nearest_neighbor(scaled, 512, 512, limits);
  REQUIRE(err.error_code == heif_error_Unsupported_feature);
}

TEST_CASE("scale_nearest_neighbor scales a well-formed YCbCr 4:2:0 image correctly") {
  auto* limits = heif_get_global_security_limits();

  // Cb/Cr are legitimately half-size in 4:2:0 -- has_standard_plane_sizes()
  // must not reject that.
  auto image = std::make_shared<HeifPixelImage>();
  image->create(4, 4, heif_colorspace_YCbCr, heif_chroma_420);
  REQUIRE(image->add_channel(heif_channel_Y, 4, 4, 8, limits).error_code == heif_error_Ok);
  REQUIRE(image->add_channel(heif_channel_Cb, 2, 2, 8, limits).error_code == heif_error_Ok);
  REQUIRE(image->add_channel(heif_channel_Cr, 2, 2, 8, limits).error_code == heif_error_Ok);

  std::shared_ptr<HeifPixelImage> scaled;
  Error err = image->scale_nearest_neighbor(scaled, 8, 8, limits);
  REQUIRE(err.error_code == heif_error_Ok);
  REQUIRE(scaled->get_width(heif_channel_Y) == 8);
  REQUIRE(scaled->get_height(heif_channel_Y) == 8);
  REQUIRE(scaled->get_width(heif_channel_Cb) == 4);
  REQUIRE(scaled->get_height(heif_channel_Cb) == 4);
}

TEST_CASE("scale_nearest_neighbor scales a well-formed image with matching Alpha correctly") {
  auto* limits = heif_get_global_security_limits();

  auto image = std::make_shared<HeifPixelImage>();
  image->create(2, 2, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(image->add_channel(heif_channel_Y, 2, 2, 8, limits).error_code == heif_error_Ok);
  REQUIRE(image->add_channel(heif_channel_Alpha, 2, 2, 8, limits).error_code == heif_error_Ok);

  {
    size_t stride;
    uint8_t* y = image->get_channel_memory(heif_channel_Y, &stride);
    y[0 * stride + 0] = 10;
    y[0 * stride + 1] = 20;
    y[1 * stride + 0] = 30;
    y[1 * stride + 1] = 40;

    uint8_t* a = image->get_channel_memory(heif_channel_Alpha, &stride);
    a[0 * stride + 0] = 100;
    a[0 * stride + 1] = 110;
    a[1 * stride + 0] = 120;
    a[1 * stride + 1] = 130;
  }

  std::shared_ptr<HeifPixelImage> scaled;
  Error err = image->scale_nearest_neighbor(scaled, 4, 4, limits);
  REQUIRE(err.error_code == heif_error_Ok);
  REQUIRE(scaled->get_width(heif_channel_Y) == 4);
  REQUIRE(scaled->get_height(heif_channel_Y) == 4);
  REQUIRE(scaled->has_channel(heif_channel_Alpha));
  REQUIRE(scaled->get_width(heif_channel_Alpha) == 4);
  REQUIRE(scaled->get_height(heif_channel_Alpha) == 4);

  // Nearest-neighbor 2x2 -> 4x4: each source pixel covers a 2x2 block.
  size_t stride;
  const uint8_t* y = scaled->get_channel_memory(heif_channel_Y, &stride);
  REQUIRE(y[0 * stride + 0] == 10);
  REQUIRE(y[0 * stride + 3] == 20);
  REQUIRE(y[3 * stride + 0] == 30);
  REQUIRE(y[3 * stride + 3] == 40);

  const uint8_t* a = scaled->get_channel_memory(heif_channel_Alpha, &stride);
  REQUIRE(a[0 * stride + 0] == 100);
  REQUIRE(a[3 * stride + 3] == 130);
}
