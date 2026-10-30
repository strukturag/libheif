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

// Regression tests for GHSA-g89c-p67h-r497.

TEST_CASE("transfer_channel_from_image_as rejects a duplicate destination channel")
{
  auto* limits = heif_get_global_security_limits();

  auto dst = std::make_shared<HeifPixelImage>();
  dst->create(4, 4, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(dst->add_channel(heif_channel_Y, 4, 4, 8, limits).error_code == heif_error_Ok);

  // Attaching a channel the destination doesn't have yet succeeds.
  auto alpha1 = std::make_shared<HeifPixelImage>();
  alpha1->create(4, 4, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(alpha1->add_channel(heif_channel_Y, 4, 4, 8, limits).error_code == heif_error_Ok);

  Error err1 = dst->transfer_channel_from_image_as(alpha1, heif_channel_Y, heif_channel_Alpha);
  REQUIRE(err1.error_code == heif_error_Ok);
  REQUIRE(dst->has_channel(heif_channel_Alpha));
  REQUIRE(dst->get_bits_per_pixel(heif_channel_Alpha) == 8);

  // A second attach to the same, already-occupied destination channel must be
  // rejected instead of silently appending a duplicate plane.
  auto alpha2 = std::make_shared<HeifPixelImage>();
  alpha2->create(4, 4, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(alpha2->add_channel(heif_channel_Y, 4, 4, 10, limits).error_code == heif_error_Ok);

  Error err2 = dst->transfer_channel_from_image_as(alpha2, heif_channel_Y, heif_channel_Alpha);
  REQUIRE(err2.error_code == heif_error_Invalid_input);

  // The destination must still describe only the original 8-bit Alpha plane.
  REQUIRE(dst->get_bits_per_pixel(heif_channel_Alpha) == 8);

  // The rejected source must be left untouched: the duplicate check must run
  // before any plane is moved out of it, not after.
  REQUIRE(alpha2->has_channel(heif_channel_Y));
}

TEST_CASE("transfer_channel_from_image_as rejects a multicomponent plane for a planar channel")
{
  const auto chroma = GENERATE(heif_chroma_interleaved_RGB, heif_chroma_interleaved_RGBA);
  const auto channel = GENERATE(heif_channel_Y, heif_channel_Alpha);
  auto* limits = heif_get_global_security_limits();
  auto source = std::make_shared<HeifPixelImage>();
  source->create(128, 64, heif_colorspace_RGB, chroma);
  REQUIRE_FALSE(source->add_channel(heif_channel_interleaved, 128, 64, 8, limits));
  size_t stride;
  auto* data = source->get_channel_memory(heif_channel_interleaved, &stride);
  data[0] = 0x5a;
  const auto component_count = source->get_number_of_used_components();

  auto destination = std::make_shared<HeifPixelImage>();
  destination->create(128, 64, heif_colorspace_monochrome, heif_chroma_monochrome);
  auto err = destination->transfer_channel_from_image_as(source, heif_channel_interleaved, channel);
  REQUIRE(err.error_code == heif_error_Invalid_input);
  REQUIRE_FALSE(destination->has_channel(channel));
  REQUIRE(destination->get_number_of_used_components() == 0);
  REQUIRE(source->get_number_of_used_components() == component_count);
  REQUIRE(source->get_channel_memory(heif_channel_interleaved, &stride) == data);
  REQUIRE(data[0] == 0x5a);
}

TEST_CASE("transfer_channel_from_image_as requires matching interleaved component counts")
{
  const auto destination_chroma = GENERATE(heif_chroma_interleaved_RGB, heif_chroma_interleaved_RGBA);
  auto* limits = heif_get_global_security_limits();
  auto source = std::make_shared<HeifPixelImage>();
  source->create(128, 64, heif_colorspace_RGB, heif_chroma_interleaved_RGBA);
  REQUIRE_FALSE(source->add_channel(heif_channel_interleaved, 128, 64, 8, limits));
  auto destination = std::make_shared<HeifPixelImage>();
  destination->create(128, 64, heif_colorspace_RGB, destination_chroma);

  auto err = destination->transfer_channel_from_image_as(source, heif_channel_interleaved, heif_channel_interleaved);
  if (destination_chroma == heif_chroma_interleaved_RGBA) {
    REQUIRE_FALSE(err);
    REQUIRE_FALSE(source->has_channel(heif_channel_interleaved));
    REQUIRE(destination->get_number_of_interleaved_components(heif_channel_interleaved) == 4);
    const auto ids = destination->get_component_ids_interleaved();
    REQUIRE(ids.size() == 4);
    REQUIRE(destination->get_component_type(ids[0]) == heif_cmpd_component_type_red);
    REQUIRE(destination->get_component_type(ids[1]) == heif_cmpd_component_type_green);
    REQUIRE(destination->get_component_type(ids[2]) == heif_cmpd_component_type_blue);
    REQUIRE(destination->get_component_type(ids[3]) == heif_cmpd_component_type_alpha);
  }
  else {
    REQUIRE(err.error_code == heif_error_Invalid_input);
    REQUIRE(source->has_channel(heif_channel_interleaved));
    REQUIRE_FALSE(destination->has_channel(heif_channel_interleaved));
  }
}

TEST_CASE("overlay rejects an Alpha plane whose size does not match the other channels")
{
  auto* limits = heif_get_global_security_limits();

  auto base = std::make_shared<HeifPixelImage>();
  base->create(4, 4, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(base->add_channel(heif_channel_Y, 4, 4, 8, limits).error_code == heif_error_Ok);

  // An overlay image whose Alpha plane is smaller than its own color plane
  // must be rejected up front, since the blend loop indexes the Alpha plane
  // using the color channel's extent.
  auto overlay_img = std::make_shared<HeifPixelImage>();
  overlay_img->create(4, 4, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(overlay_img->add_channel(heif_channel_Y, 4, 4, 8, limits).error_code == heif_error_Ok);
  REQUIRE(overlay_img->add_channel(heif_channel_Alpha, 2, 2, 8, limits).error_code == heif_error_Ok);

  Error err = base->overlay(overlay_img, 0, 0);
  REQUIRE(err.error_code == heif_error_Unsupported_feature);
}

TEST_CASE("overlay blends normally when the Alpha plane size matches")
{
  auto* limits = heif_get_global_security_limits();

  auto base = std::make_shared<HeifPixelImage>();
  base->create(2, 2, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(base->add_channel(heif_channel_Y, 2, 2, 8, limits).error_code == heif_error_Ok);
  base->fill_channel(heif_channel_Y, 0);

  auto overlay_img = std::make_shared<HeifPixelImage>();
  overlay_img->create(2, 2, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(overlay_img->add_channel(heif_channel_Y, 2, 2, 8, limits).error_code == heif_error_Ok);
  REQUIRE(overlay_img->add_channel(heif_channel_Alpha, 2, 2, 8, limits).error_code == heif_error_Ok);
  overlay_img->fill_channel(heif_channel_Y, 200);
  overlay_img->fill_channel(heif_channel_Alpha, 255); // fully opaque

  Error err = base->overlay(overlay_img, 0, 0);
  REQUIRE(err.error_code == heif_error_Ok);

  size_t stride;
  const uint8_t* data = base->get_channel_memory(heif_channel_Y, &stride);
  REQUIRE(data[0] == 200);
}
