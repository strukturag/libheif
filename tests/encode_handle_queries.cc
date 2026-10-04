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

// A heif_context is used either for reading or for writing. The decoder object of an image
// item is only created when the item is read from a file, so an image that was added to the
// context by encoding has none. Decoding such an image, and also the plain queries for its
// bit depth, colorspace and alpha channel on the handle that heif_context_encode_image()
// returns, dereferenced the missing decoder and crashed. This is not a supported use of the
// API, but it has to fail with an error.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>

namespace {

constexpr int W = 64;
constexpr int H = 64;

heif_image* create_rgb_image()
{
  heif_image* img = nullptr;
  heif_error err = heif_image_create(W, H, heif_colorspace_RGB, heif_chroma_444, &img);
  REQUIRE(err.code == heif_error_Ok);

  for (heif_channel channel : {heif_channel_R, heif_channel_G, heif_channel_B}) {
    err = heif_image_add_plane(img, channel, W, H, 8);
    REQUIRE(err.code == heif_error_Ok);

    size_t stride = 0;
    uint8_t* p = heif_image_get_plane2(img, channel, &stride);
    REQUIRE(p != nullptr);
    for (int y = 0; y < H; y++) {
      memset(p + y * stride, 0x40, W);
    }
  }

  return img;
}

const char* format_name(heif_compression_format format)
{
  switch (format) {
    case heif_compression_HEVC: return "HEVC";
    case heif_compression_AVC: return "AVC";
    case heif_compression_JPEG: return "JPEG";
    case heif_compression_AV1: return "AV1";
    case heif_compression_VVC: return "VVC";
    case heif_compression_JPEG2000: return "JPEG 2000";
    case heif_compression_uncompressed: return "uncompressed";
    case heif_compression_HTJ2K: return "HT-J2K";
    default: return "?";
  }
}

} // namespace


TEST_CASE("queries and decoding on the handle of an encoded image fail without crashing")
{
  int num_formats_tested = 0;

  for (heif_compression_format format : {heif_compression_HEVC, heif_compression_AVC, heif_compression_JPEG,
                                         heif_compression_AV1, heif_compression_VVC, heif_compression_JPEG2000,
                                         heif_compression_uncompressed, heif_compression_HTJ2K}) {
    if (!heif_have_encoder_for_format(format)) {
      continue;
    }

    INFO("format: " << format_name(format));

    heif_image* img = create_rgb_image();
    heif_context* ctx = heif_context_alloc();

    heif_encoder* encoder = nullptr;
    heif_error err = heif_context_get_encoder_for_format(ctx, format, &encoder);
    REQUIRE(err.code == heif_error_Ok);

    heif_image_handle* handle = nullptr;
    err = heif_context_encode_image(ctx, img, encoder, nullptr, &handle);
    if (err.code != heif_error_Ok) {
      // an encoder that does not take this kind of image is not what is tested here
      heif_encoder_release(encoder);
      heif_context_free(ctx);
      heif_image_release(img);
      continue;
    }
    REQUIRE(handle != nullptr);
    num_formats_tested++;

    // --- these work on a handle of an image that is being written

    CHECK(heif_image_handle_get_width(handle) == W);
    CHECK(heif_image_handle_get_height(handle) == H);

    // --- these need the decoder of the image item, which an encoded image does not have

    // The bit depth is unknown (-1).
    CHECK(heif_image_handle_get_luma_bits_per_pixel(handle) == -1);
    CHECK(heif_image_handle_get_chroma_bits_per_pixel(handle) == -1);

    heif_colorspace colorspace;
    heif_chroma chroma;
    err = heif_image_handle_get_preferred_decoding_colorspace(handle, &colorspace, &chroma);
    CHECK(err.code != heif_error_Ok);

    CHECK(heif_image_handle_has_alpha_channel(handle) == 0);

    // The uncompressed codec happens to decode from an encoding context. That is not
    // required. What is required is that no format crashes and that a failure is
    // reported together with a null image.
    heif_image* decoded = nullptr;
    err = heif_decode_image(handle, &decoded, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
    if (format != heif_compression_uncompressed) {
      CHECK(err.code != heif_error_Ok);
    }
    CHECK((err.code == heif_error_Ok) == (decoded != nullptr));
    if (decoded) {
      heif_image_release(decoded);
    }

    decoded = nullptr;
    err = heif_image_handle_decode_image_tile(handle, &decoded, heif_colorspace_undefined, heif_chroma_undefined,
                                              nullptr, 0, 0);
    if (format != heif_compression_uncompressed) {
      CHECK(err.code != heif_error_Ok);
    }
    CHECK((err.code == heif_error_Ok) == (decoded != nullptr));
    if (decoded) {
      heif_image_release(decoded);
    }

    // --- the same through the primary image handle of the context

    heif_image_handle* primary = nullptr;
    err = heif_context_get_primary_image_handle(ctx, &primary);
    REQUIRE(err.code == heif_error_Ok);
    CHECK(heif_image_handle_get_luma_bits_per_pixel(primary) == -1);
    CHECK(heif_image_handle_has_alpha_channel(primary) == 0);
    heif_image_handle_release(primary);

    heif_image_handle_release(handle);
    heif_encoder_release(encoder);
    heif_context_free(ctx);
    heif_image_release(img);
  }

  if (num_formats_tested == 0) {
    SKIP("no encoder available");
  }
}
