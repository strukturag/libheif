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

// The 'hvcC' box stores the bit depths in 3 bit fields and can signal at most 15 bits per
// sample. An HEVC image with 16 bits would be written with a bit depth of 8 in its 'hvcC'
// box. libheif refuses to encode such an image, and it does so itself, before the image
// reaches the encoder plugin: the error has to come with heif_error_Unsupported_feature,
// not with the heif_error_Encoder_plugin_error of a plugin that happens to reject the
// bit depth as well.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

constexpr uint32_t W = 64;
constexpr uint32_t H = 64;

void add_plane(heif_image* img, heif_channel channel, uint32_t w, uint32_t h, int bit_depth)
{
  heif_error err = heif_image_add_plane(img, channel, w, h, bit_depth);
  REQUIRE(err.code == heif_error_Ok);

  size_t stride = 0;
  uint8_t* p = heif_image_get_plane2(img, channel, &stride);
  REQUIRE(p != nullptr);

  const uint16_t value = static_cast<uint16_t>(1u << (bit_depth - 1));
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      if (bit_depth > 8) {
        memcpy(p + y * stride + 2 * x, &value, 2);
      }
      else {
        p[y * stride + x] = static_cast<uint8_t>(value);
      }
    }
  }
}

heif_image* create_image(heif_colorspace colorspace, heif_chroma chroma, int bit_depth)
{
  heif_image* img = nullptr;
  heif_error err = heif_image_create(W, H, colorspace, chroma, &img);
  REQUIRE(err.code == heif_error_Ok);

  if (colorspace == heif_colorspace_RGB) {
    for (heif_channel channel : {heif_channel_R, heif_channel_G, heif_channel_B}) {
      add_plane(img, channel, W, H, bit_depth);
    }
  }
  else {
    add_plane(img, heif_channel_Y, W, H, bit_depth);
    if (chroma == heif_chroma_420) {
      add_plane(img, heif_channel_Cb, W / 2, H / 2, bit_depth);
      add_plane(img, heif_channel_Cr, W / 2, H / 2, bit_depth);
    }
  }
  return img;
}

// heif_error::message may point into the context that reported the error, so it has to be
// copied before the context is freed.
struct EncodeResult
{
  heif_error_code code;
  heif_suberror_code subcode;
  std::string message;
};

EncodeResult encode_image(heif_image* img)
{
  heif_context* ctx = heif_context_alloc();
  heif_encoder* encoder = nullptr;
  heif_error err = heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_encode_image(ctx, img, encoder, nullptr, nullptr);
  EncodeResult result{err.code, err.subcode, err.message};

  heif_encoder_release(encoder);
  heif_context_free(ctx);
  return result;
}

EncodeResult encode_sequence_frame(heif_image* img)
{
  heif_context* ctx = heif_context_alloc();
  heif_encoder* encoder = nullptr;
  heif_error err = heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);
  REQUIRE(err.code == heif_error_Ok);

  heif_track* track = nullptr;
  err = heif_context_add_visual_sequence_track(ctx, static_cast<uint16_t>(W), static_cast<uint16_t>(H),
                                               heif_track_type_video, nullptr, nullptr, &track);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_set_duration(img, 1);
  err = heif_track_encode_sequence_image(track, img, encoder, nullptr);
  EncodeResult result{err.code, err.subcode, err.message};

  heif_track_release(track);
  heif_encoder_release(encoder);
  heif_context_free(ctx);
  return result;
}

void check_refused_by_libheif(const EncodeResult& result)
{
  INFO("encode error (" << result.code << "/" << result.subcode << "): " << result.message);
  CHECK(result.code == heif_error_Unsupported_feature);
  CHECK(result.subcode == heif_suberror_Unsupported_bit_depth);
  CHECK(result.message.find("hvcC") != std::string::npos);
}

} // namespace


TEST_CASE("HEVC images with 16 bits per sample are refused")
{
  if (!heif_have_encoder_for_format(heif_compression_HEVC)) {
    SKIP("no HEVC encoder available");
  }

  SECTION("control: an 8 bit image encodes") {
    heif_image* img = create_image(heif_colorspace_YCbCr, heif_chroma_420, 8);
    EncodeResult result = encode_image(img);
    INFO("encode error (" << result.code << "/" << result.subcode << "): " << result.message);
    CHECK(result.code == heif_error_Ok);
    heif_image_release(img);
  }

  SECTION("YCbCr 4:2:0") {
    heif_image* img = create_image(heif_colorspace_YCbCr, heif_chroma_420, 16);
    check_refused_by_libheif(encode_image(img));
    heif_image_release(img);
  }

  SECTION("monochrome") {
    heif_image* img = create_image(heif_colorspace_monochrome, heif_chroma_monochrome, 16);
    check_refused_by_libheif(encode_image(img));
    heif_image_release(img);
  }

  SECTION("RGB, which is converted to YCbCr for the encoder") {
    heif_image* img = create_image(heif_colorspace_RGB, heif_chroma_444, 16);
    check_refused_by_libheif(encode_image(img));
    heif_image_release(img);
  }

  SECTION("frame of an image sequence") {
    heif_image* img = create_image(heif_colorspace_YCbCr, heif_chroma_420, 16);
    check_refused_by_libheif(encode_sequence_frame(img));
    heif_image_release(img);
  }
}
