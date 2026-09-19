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

// Regression test for GHSA-qfj5-c4pq-q998, internal-symbol part.
//
// The public-API part of this regression test lives in
// uncompressed_interleaved_alpha_plane.cc. This file covers the defense-in-depth
// check in the uncompressed encoder factory, which needs library-internal classes
// (HeifPixelImage, unc_encoder_factory) and therefore only builds with full symbol
// visibility (WITH_REDUCED_VISIBILITY=OFF).
//
// transfer_channel_from_image_as() moves a plane between images without going
// through add_channel(). This is how a decoded alpha auxiliary image is attached to
// the main image, so an interleaved image with a separate alpha plane can still be
// assembled inside the library even though heif_image_add_plane() rejects it. The
// uncompressed encoder must refuse such an image instead of reading past the end
// of the interleaved component list.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "api_structs.h"
#include "image/pixelimage.h"
#include "codecs/uncompressed/unc_encoder.h"

#include <memory>

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


TEST_CASE("Uncompressed encoder refuses an interleaved image carrying a separate alpha plane")
{
  const heif_security_limits* limits = heif_get_global_security_limits();

  for (const auto& fmt : interleaved_formats) {
    INFO(fmt.name);

    auto image = std::make_shared<HeifPixelImage>();
    image->create(WIDTH, HEIGHT, heif_colorspace_RGB, fmt.chroma);
    REQUIRE(image->fill_new_channel(heif_channel_interleaved, 0x80, WIDTH, HEIGHT, fmt.bit_depth, limits).error_code == heif_error_Ok);

    auto alpha = std::make_shared<HeifPixelImage>();
    alpha->create(WIDTH, HEIGHT, heif_colorspace_monochrome, heif_chroma_monochrome);
    REQUIRE(alpha->fill_new_channel(heif_channel_Y, 0xFF, WIDTH, HEIGHT, fmt.bit_depth, limits).error_code == heif_error_Ok);

    REQUIRE(image->transfer_channel_from_image_as(alpha, heif_channel_Y, heif_channel_Alpha).error_code == heif_error_Ok);
    REQUIRE(image->has_channel(heif_channel_Alpha));

    // The encoder factory is where the interleaved encoders are instantiated.
    heif_encoding_options* options = heif_encoding_options_alloc();
    auto encoder = unc_encoder_factory::get_unc_encoder(image, *options);
    heif_encoding_options_free(options);
    CHECK(!encoder);

    // Same through the public encode entry point.
    heif_image wrapper;
    wrapper.image = image;
    CHECK(encode_uncompressed(&wrapper).code != heif_error_Ok);
  }
}
