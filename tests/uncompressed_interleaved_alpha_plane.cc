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

// Regression test for GHSA-qfj5-c4pq-q998.
//
// heif_image_add_plane() accepted a separate heif_channel_Alpha plane on an image
// whose chroma format is interleaved. The interleaved RGB encoders of the
// uncompressed codec took the component list from the chroma format (three
// entries for interleaved RGB) but the decision whether to write alpha from
// has_alpha(), which also reports a separate alpha plane. With the two
// disagreeing, the encoder indexed the three-entry component list at [3]: a
// heap out-of-bounds read in release builds and an assertion failure in debug
// builds.
//
// An interleaved image carries its alpha inside the interleaved plane (the RGBA
// and RRGGBBAA formats), so a separate alpha plane on such an image is never a
// valid configuration. It is now rejected when the plane is added, and the
// uncompressed encoder additionally refuses such an image should it be
// assembled by another route.
//
// This file only uses the public API and builds in every configuration. The
// encoder-side check needs library-internal classes and is tested in
// uncompressed_interleaved_alpha_plane_internal.cc, which only builds with full
// symbol visibility.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstring>

namespace {

constexpr int WIDTH = 16;
constexpr int HEIGHT = 16;

struct InterleavedFormat
{
  heif_chroma chroma;
  int bit_depth;
  const char* name;
};

// The bit depths are chosen so that every interleaved encoder of the uncompressed
// codec is covered: 8-bit RGB/RGBA use the pixel-interleave encoder, RRGGBB below
// 14 bits the block-pixel encoder, and the remaining formats the byte-aligned one.
const InterleavedFormat interleaved_formats[] = {
    {heif_chroma_interleaved_RGB, 8, "RGB"},
    {heif_chroma_interleaved_RGBA, 8, "RGBA"},
    {heif_chroma_interleaved_RRGGBB_LE, 10, "RRGGBB_LE"},
    {heif_chroma_interleaved_RRGGBB_BE, 16, "RRGGBB_BE"},
    {heif_chroma_interleaved_RRGGBBAA_LE, 10, "RRGGBBAA_LE"},
    {heif_chroma_interleaved_RRGGBBAA_BE, 16, "RRGGBBAA_BE"},
};


void fill_plane(heif_image* image, heif_channel channel, uint8_t value)
{
  size_t stride = 0;
  uint8_t* plane = heif_image_get_plane2(image, channel, &stride);
  REQUIRE(plane != nullptr);

  int height = heif_image_get_height(image, channel);
  for (int y = 0; y < height; y++) {
    memset(plane + y * stride, value, stride);
  }
}


heif_error encode_uncompressed(heif_image* image)
{
  heif_context* ctx = heif_context_alloc();

  heif_encoder* encoder = nullptr;
  heif_error err = heif_context_get_encoder_for_format(ctx, heif_compression_uncompressed, &encoder);
  REQUIRE(err.code == heif_error_Ok);

  heif_encoding_options* options = heif_encoding_options_alloc();
  options->macOS_compatibility_workaround_no_nclx_profile = true;

  heif_image_handle* handle = nullptr;
  err = heif_context_encode_image(ctx, image, encoder, options, &handle);

  if (handle) {
    heif_image_handle_release(handle);
  }
  heif_encoding_options_free(options);
  heif_encoder_release(encoder);
  heif_context_free(ctx);

  return err;
}

} // namespace


TEST_CASE("heif_image_add_plane rejects a separate alpha plane on an interleaved image")
{
  for (const auto& fmt : interleaved_formats) {
    INFO(fmt.name);

    heif_image* image = nullptr;
    heif_error err = heif_image_create(WIDTH, HEIGHT, heif_colorspace_RGB, fmt.chroma, &image);
    REQUIRE(err.code == heif_error_Ok);

    err = heif_image_add_plane(image, heif_channel_interleaved, WIDTH, HEIGHT, fmt.bit_depth);
    REQUIRE(err.code == heif_error_Ok);
    fill_plane(image, heif_channel_interleaved, 0x80);

    err = heif_image_add_plane(image, heif_channel_Alpha, WIDTH, HEIGHT, fmt.bit_depth);
    CHECK(err.code == heif_error_Usage_error);
    CHECK(heif_image_has_channel(image, heif_channel_Alpha) == 0);

    // Without the separate alpha plane the image is valid and still encodes. Under the
    // unfixed library this call read past the end of the interleaved component list for
    // the formats without alpha.
    CHECK(encode_uncompressed(image).code == heif_error_Ok);

    heif_image_release(image);
  }
}


TEST_CASE("heif_image_add_plane rejects a separate alpha plane before the interleaved plane")
{
  // The chroma format is fixed at heif_image_create(), so the order in which the planes
  // are added must not matter.

  heif_image* image = nullptr;
  heif_error err = heif_image_create(WIDTH, HEIGHT, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &image);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_image_add_plane(image, heif_channel_Alpha, WIDTH, HEIGHT, 8);
  CHECK(err.code == heif_error_Usage_error);
  CHECK(heif_image_has_channel(image, heif_channel_Alpha) == 0);

  heif_image_release(image);
}


TEST_CASE("Planar RGB images still accept a separate alpha plane")
{
  heif_image* image = nullptr;
  heif_error err = heif_image_create(WIDTH, HEIGHT, heif_colorspace_RGB, heif_chroma_444, &image);
  REQUIRE(err.code == heif_error_Ok);

  for (heif_channel channel : {heif_channel_R, heif_channel_G, heif_channel_B, heif_channel_Alpha}) {
    err = heif_image_add_plane(image, channel, WIDTH, HEIGHT, 8);
    REQUIRE(err.code == heif_error_Ok);
    fill_plane(image, channel, 0x80);
  }

  CHECK(heif_image_has_channel(image, heif_channel_Alpha) == 1);
  CHECK(encode_uncompressed(image).code == heif_error_Ok);

  heif_image_release(image);
}

