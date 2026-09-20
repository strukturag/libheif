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

// An image handed to heif_context_encode_image() has to have exactly the planes of its
// colorspace and chroma format. Before this check, an RGB image without its B plane failed
// deep inside a conversion operator, a YCbCr 4:2:0 image without its Cr plane passed the
// pipeline as a no-op and aborted inside the x265 plugin (a release build would have read a
// null plane), and foreign or duplicate planes were silently dropped or ignored. The check is
// made on the libheif side in Encoder::check_input_image_layout(), before the image is
// converted for, or handed unchanged to, the plugin. Planes with channel heif_channel_unknown
// (custom multi-component data) are tolerated.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t W = 32;
constexpr uint32_t H = 32;

heif_compression_format pick_encoder_format()
{
  for (heif_compression_format format : {heif_compression_HEVC, heif_compression_AV1, heif_compression_uncompressed}) {
    if (heif_have_encoder_for_format(format)) {
      return format;
    }
  }
  return heif_compression_undefined;
}

heif_image* create_image(heif_colorspace cs, heif_chroma chroma)
{
  heif_image* img = nullptr;
  heif_error err = heif_image_create(W, H, cs, chroma, &img);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(img != nullptr);
  return img;
}

void add_plane(heif_image* img, heif_channel channel, uint32_t w, uint32_t h)
{
  heif_error err = heif_image_add_plane(img, channel, w, h, 8);
  INFO("add_plane error: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  size_t stride = 0;
  uint8_t* p = heif_image_get_plane2(img, channel, &stride);
  REQUIRE(p != nullptr);
  for (uint32_t y = 0; y < h; y++) {
    memset(p + y * stride, 0x80, w);
  }
}

heif_error encode(heif_image* img, heif_compression_format format)
{
  heif_context* ctx = heif_context_alloc();
  heif_encoder* encoder = nullptr;
  heif_error err = heif_context_get_encoder_for_format(ctx, format, &encoder);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_encode_image(ctx, img, encoder, nullptr, nullptr);

  heif_encoder_release(encoder);
  heif_context_free(ctx);
  return err;
}

void expect_refused(heif_image* img, heif_compression_format format)
{
  heif_error err = encode(img, format);
  INFO("encode error (" << err.code << "/" << err.subcode << "): " << err.message);
  REQUIRE(err.code != heif_error_Ok);
  CHECK(err.code == heif_error_Usage_error);
  heif_image_release(img);
}

} // namespace


TEST_CASE("encoding refuses images with a non-canonical plane layout")
{
  heif_compression_format format = pick_encoder_format();
  if (format == heif_compression_undefined) {
    SKIP("no HEVC, AV1 or uncompressed encoder available");
  }

  SECTION("control: a complete RGB image encodes") {
    heif_image* img = create_image(heif_colorspace_RGB, heif_chroma_444);
    for (heif_channel ch : {heif_channel_R, heif_channel_G, heif_channel_B}) {
      add_plane(img, ch, W, H);
    }
    heif_error err = encode(img, format);
    INFO("encode error (" << err.code << "/" << err.subcode << "): " << err.message);
    CHECK(err.code == heif_error_Ok);
    heif_image_release(img);
  }

  SECTION("RGB without its B plane") {
    heif_image* img = create_image(heif_colorspace_RGB, heif_chroma_444);
    add_plane(img, heif_channel_R, W, H);
    add_plane(img, heif_channel_G, W, H);
    expect_refused(img, format);
  }

  SECTION("YCbCr 4:2:0 without its Cr plane (reached the x265 plugin before)") {
    heif_image* img = create_image(heif_colorspace_YCbCr, heif_chroma_420);
    add_plane(img, heif_channel_Y, W, H);
    add_plane(img, heif_channel_Cb, W / 2, H / 2);
    expect_refused(img, format);
  }

  SECTION("RGB with a depth plane") {
    heif_image* img = create_image(heif_colorspace_RGB, heif_chroma_444);
    for (heif_channel ch : {heif_channel_R, heif_channel_G, heif_channel_B, heif_channel_depth}) {
      add_plane(img, ch, W, H);
    }
    expect_refused(img, format);
  }

  SECTION("monochrome with a stray Cb plane") {
    heif_image* img = create_image(heif_colorspace_monochrome, heif_chroma_monochrome);
    add_plane(img, heif_channel_Y, W, H);
    add_plane(img, heif_channel_Cb, W, H);
    expect_refused(img, format);
  }

  SECTION("duplicate colour plane (R twice)") {
    // heif_image_add_plane() allows this (multi-spectral images consist of several monochrome
    // planes); the encoder entry is where it is refused.
    heif_image* img = create_image(heif_colorspace_RGB, heif_chroma_444);
    for (heif_channel ch : {heif_channel_R, heif_channel_R, heif_channel_G, heif_channel_B}) {
      add_plane(img, ch, W, H);
    }
    expect_refused(img, format);
  }

  SECTION("a component without colour meaning next to RGB is tolerated") {
    heif_image* img = create_image(heif_colorspace_RGB, heif_chroma_444);
    for (heif_channel ch : {heif_channel_R, heif_channel_G, heif_channel_B}) {
      add_plane(img, ch, W, H);
    }
    uint32_t id = 0;
    heif_error err = heif_image_add_component(img, W, H, heif_cmpd_component_type_padded,
                                              heif_component_datatype_unsigned_integer, 8, &id);
    REQUIRE(err.code == heif_error_Ok);

    err = encode(img, format);
    INFO("encode error (" << err.code << "/" << err.subcode << "): " << err.message);
    CHECK(err.code == heif_error_Ok);
    heif_image_release(img);
  }
}

