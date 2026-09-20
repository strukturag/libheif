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

// HeifPixelImage::check_plane_layout() is the gate through which images enter the color
// conversion pipeline and the encoders. The operators read the planes their colorspace and
// chroma format imply, at the sizes they imply; an image with a missing, duplicate, foreign or
// undersized plane used to fail somewhere inside an operator, or not at all (a YCbCr image
// without a Cr plane passed the pipeline as a no-op and reached the x265 plugin). Planes with
// channel heif_channel_unknown carry multi-component data without a colour meaning (the
// padding components of 'unci') and are tolerated and ignored.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "image/pixelimage.h"
#include "color-conversion/colorconversion.h"

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr uint32_t W = 16;
constexpr uint32_t H = 8;

std::shared_ptr<HeifPixelImage> make_image(heif_colorspace cs, heif_chroma chroma,
                                           std::initializer_list<heif_channel> channels, int bpp = 8)
{
  auto img = std::make_shared<HeifPixelImage>();
  img->create(W, H, cs, chroma);
  for (heif_channel ch : channels) {
    Error err = img->add_channel(ch, channel_width(W, chroma, ch), channel_height(H, chroma, ch), bpp, nullptr);
    INFO("add_channel: " << err.message);
    REQUIRE(!err);
  }
  return img;
}

void add_unknown_components(const std::shared_ptr<HeifPixelImage>& img, int count)
{
  for (int i = 0; i < count; i++) {
    auto result = img->add_component(W, H, heif_cmpd_component_type_padded,
                                     heif_component_datatype_unsigned_integer, 8, nullptr);
    REQUIRE(result);
  }
  REQUIRE(img->has_channel(heif_channel_unknown));
}

bool mentions(const Error& err, const char* text)
{
  return err.message.find(text) != std::string::npos;
}

} // namespace


TEST_CASE("check_plane_layout accepts the canonical layouts")
{
  struct Layout
  {
    const char* name;
    heif_colorspace cs;
    heif_chroma chroma;
    std::vector<heif_channel> planes;
    int bpp;
  };

  const Layout layouts[] = {
      {"RGB 4:4:4", heif_colorspace_RGB, heif_chroma_444, {heif_channel_R, heif_channel_G, heif_channel_B}, 8},
      {"RGB 4:4:4 + alpha", heif_colorspace_RGB, heif_chroma_444, {heif_channel_R, heif_channel_G, heif_channel_B, heif_channel_Alpha}, 8},
      {"interleaved RGB", heif_colorspace_RGB, heif_chroma_interleaved_RGB, {heif_channel_interleaved}, 8},
      {"interleaved RGBA", heif_colorspace_RGB, heif_chroma_interleaved_RGBA, {heif_channel_interleaved}, 8},
      {"interleaved RRGGBB_LE", heif_colorspace_RGB, heif_chroma_interleaved_RRGGBB_LE, {heif_channel_interleaved}, 10},
      {"interleaved RRGGBBAA_BE", heif_colorspace_RGB, heif_chroma_interleaved_RRGGBBAA_BE, {heif_channel_interleaved}, 12},
      {"YCbCr 4:4:4", heif_colorspace_YCbCr, heif_chroma_444, {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}, 8},
      {"YCbCr 4:2:2 + alpha", heif_colorspace_YCbCr, heif_chroma_422, {heif_channel_Y, heif_channel_Cb, heif_channel_Cr, heif_channel_Alpha}, 8},
      {"YCbCr 4:2:0", heif_colorspace_YCbCr, heif_chroma_420, {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}, 8},
      {"YCbCr 4:2:0 10-bit + alpha", heif_colorspace_YCbCr, heif_chroma_420, {heif_channel_Y, heif_channel_Cb, heif_channel_Cr, heif_channel_Alpha}, 10},
      {"YCbCr luma only", heif_colorspace_YCbCr, heif_chroma_monochrome, {heif_channel_Y}, 8},
      {"monochrome", heif_colorspace_monochrome, heif_chroma_monochrome, {heif_channel_Y}, 8},
      {"monochrome + alpha", heif_colorspace_monochrome, heif_chroma_monochrome, {heif_channel_Y, heif_channel_Alpha}, 8},
      {"filter array", heif_colorspace_filter_array, heif_chroma_planar, {heif_channel_filter_array}, 12},
  };

  for (const Layout& l : layouts) {
    INFO(l.name);
    auto img = std::make_shared<HeifPixelImage>();
    img->create(W, H, l.cs, l.chroma);
    for (heif_channel ch : l.planes) {
      REQUIRE(!img->add_channel(ch, channel_width(W, l.chroma, ch), channel_height(H, l.chroma, ch), l.bpp, nullptr));
    }
    Error err = img->check_plane_layout();
    INFO(err.message);
    CHECK(!err);
  }

  SECTION("planes with channel unknown are tolerated") {
    auto img = make_image(heif_colorspace_RGB, heif_chroma_444, {heif_channel_R, heif_channel_G, heif_channel_B});
    add_unknown_components(img, 2);
    CHECK(!img->check_plane_layout());
  }
}


TEST_CASE("check_plane_layout rejects non-canonical layouts")
{
  SECTION("missing colour plane") {
    auto img = make_image(heif_colorspace_RGB, heif_chroma_444, {heif_channel_R, heif_channel_G});
    Error err = img->check_plane_layout();
    REQUIRE(err);
    CHECK(err.error_code == heif_error_Usage_error);
    CHECK(mentions(err, "no B plane"));
  }

  SECTION("missing chroma plane") {
    auto img = make_image(heif_colorspace_YCbCr, heif_chroma_420, {heif_channel_Y, heif_channel_Cb});
    Error err = img->check_plane_layout();
    REQUIRE(err);
    CHECK(mentions(err, "no Cr plane"));
  }

  SECTION("foreign plane") {
    auto img = make_image(heif_colorspace_RGB, heif_chroma_444, {heif_channel_R, heif_channel_G, heif_channel_B, heif_channel_depth});
    Error err = img->check_plane_layout();
    REQUIRE(err);
    CHECK(mentions(err, "depth plane"));
  }

  SECTION("stray chroma plane on a monochrome image") {
    auto img = make_image(heif_colorspace_monochrome, heif_chroma_monochrome, {heif_channel_Y, heif_channel_Cb});
    Error err = img->check_plane_layout();
    REQUIRE(err);
    CHECK(mentions(err, "Cb plane"));
  }

  SECTION("duplicate colour plane") {
    // HeifPixelImage itself allows this (multi-spectral images consist of several monochrome
    // planes); the layout check is what refuses it for colour conversion and encoding.
    auto img = make_image(heif_colorspace_RGB, heif_chroma_444, {heif_channel_R, heif_channel_G, heif_channel_B});
    REQUIRE(!img->add_channel(heif_channel_R, W, H, 8, nullptr));
    Error err = img->check_plane_layout();
    REQUIRE(err);
    CHECK(mentions(err, "more than one R plane"));
  }

  SECTION("chroma plane with the wrong size") {
    auto img = std::make_shared<HeifPixelImage>();
    img->create(W, H, heif_colorspace_YCbCr, heif_chroma_420);
    REQUIRE(!img->add_channel(heif_channel_Y, W, H, 8, nullptr));
    REQUIRE(!img->add_channel(heif_channel_Cb, W, H, 8, nullptr)); // should be W/2 x H/2
    REQUIRE(!img->add_channel(heif_channel_Cr, W / 2, H / 2, 8, nullptr));
    Error err = img->check_plane_layout();
    REQUIRE(err);
    CHECK(mentions(err, "Cb plane has size"));
  }

  SECTION("alpha next to a filter array") {
    auto img = make_image(heif_colorspace_filter_array, heif_chroma_planar, {heif_channel_filter_array, heif_channel_Alpha});
    CHECK(img->check_plane_layout());
  }

  SECTION("colorspace and chroma do not fit") {
    auto rgb420 = make_image(heif_colorspace_RGB, heif_chroma_420, {});
    CHECK(rgb420->check_plane_layout());

    auto ycc_interleaved = make_image(heif_colorspace_YCbCr, heif_chroma_interleaved_RGB, {});
    CHECK(ycc_interleaved->check_plane_layout());
  }

  SECTION("custom colorspace has no layout") {
    auto img = std::make_shared<HeifPixelImage>();
    img->create(W, H, heif_colorspace_custom, heif_chroma_planar);
    for (int i = 0; i < 3; i++) {
      REQUIRE(img->add_component(W, H, heif_cmpd_component_type_monochrome,
                                 heif_component_datatype_unsigned_integer, 8, nullptr));
    }
    Error err = img->check_plane_layout();
    REQUIRE(err);
    CHECK(mentions(err, "no defined plane layout"));
  }
}


TEST_CASE("convert_colorspace refuses images with a non-canonical plane layout")
{
  heif_color_conversion_options options{};

  SECTION("RGB without B") {
    auto img = make_image(heif_colorspace_RGB, heif_chroma_444, {heif_channel_R, heif_channel_G});
    auto result = convert_colorspace(img, heif_colorspace_RGB, heif_chroma_interleaved_RGB,
                                     nclx_profile::defaults(), 0, options, nullptr,
                                     heif_get_disabled_security_limits());
    REQUIRE(!result);
    CHECK(result.error().error_code == heif_error_Unsupported_feature);
    CHECK(result.error().sub_error_code == heif_suberror_Unsupported_image_type);
  }

  SECTION("YCbCr 4:2:0 without Cr, same-layout target (used to be a no-op)") {
    auto img = make_image(heif_colorspace_YCbCr, heif_chroma_420, {heif_channel_Y, heif_channel_Cb});
    auto result = convert_colorspace(img, heif_colorspace_YCbCr, heif_chroma_420,
                                     nclx_profile::defaults(), 0, options, nullptr,
                                     heif_get_disabled_security_limits());
    REQUIRE(!result);
    CHECK(result.error().sub_error_code == heif_suberror_Unsupported_image_type);
  }

  SECTION("custom colorspace") {
    auto img = std::make_shared<HeifPixelImage>();
    img->create(W, H, heif_colorspace_custom, heif_chroma_planar);
    REQUIRE(img->add_component(W, H, heif_cmpd_component_type_monochrome,
                               heif_component_datatype_unsigned_integer, 8, nullptr));
    auto result = convert_colorspace(img, heif_colorspace_RGB, heif_chroma_interleaved_RGB,
                                     nclx_profile::defaults(), 0, options, nullptr,
                                     heif_get_disabled_security_limits());
    REQUIRE(!result);
    CHECK(result.error().sub_error_code == heif_suberror_Unsupported_image_type);
  }

  SECTION("unknown planes are tolerated and not carried into the output") {
    auto img = make_image(heif_colorspace_RGB, heif_chroma_444, {heif_channel_R, heif_channel_G, heif_channel_B});
    add_unknown_components(img, 2);
    auto result = convert_colorspace(img, heif_colorspace_RGB, heif_chroma_interleaved_RGB,
                                     nclx_profile::defaults(), 0, options, nullptr,
                                     heif_get_disabled_security_limits());
    REQUIRE(result);
    CHECK((*result)->has_channel(heif_channel_interleaved));
    CHECK(!(*result)->has_channel(heif_channel_unknown));
  }
}
