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

// Regression test: Op_to_hdr_planes and Op_to_sdr_planes accepted Bayer images at plan
// time (colorspace filter_array uses chroma planar, which has the same value as
// heif_chroma_monochrome) but their runtime only handles the Y/Cb/Cr/R/G/B/alpha planes,
// so they produced an image without any plane. The planner preferred that two-step route
// over demosaicing first: decoding an 8-bit Bayer 'unci' to 16-bit interleaved RGB and a
// 12-bit one to 8-bit interleaved RGB failed with "Invalid bit depth" from the Bayer
// operator, and a native decode of a 12-bit Bayer image with convert_hdr_to_8bit returned
// success with an image that had no planes at all. Both operators now decline the
// filter-array colorspace, so the Bayer operator runs first and the depth change is
// applied to its RGB output.
//
// The files are built through the public API and the uncompressed encoder.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_properties.h"
#include "test_utils.h"

#include <cstdint>
#include <string>

namespace {

constexpr uint32_t W = 64;
constexpr uint32_t H = 64;

std::string write_bayer_unci(int depth, const char* filename)
{
  heif_image* image = nullptr;
  heif_error err = heif_image_create(W, H, heif_colorspace_filter_array, heif_chroma_planar, &image);
  REQUIRE(err.code == heif_error_Ok);

  uint32_t fa_id = 0;
  err = heif_image_add_component(image, W, H, heif_cmpd_component_type_filter_array,
                                 heif_component_datatype_unsigned_integer, depth, &fa_id);
  REQUIRE(err.code == heif_error_Ok);

  size_t stride = 0;
  uint8_t* fa = heif_image_get_component(image, fa_id, &stride);
  REQUIRE(fa != nullptr);
  for (uint32_t y = 0; y < H; y++) {
    for (uint32_t x = 0; x < W; x++) {
      uint32_t v = (x * 31 + y * 17) & ((1u << depth) - 1);
      if (depth <= 8) {
        fa[y * stride + x] = static_cast<uint8_t>(v);
      }
      else {
        reinterpret_cast<uint16_t*>(fa + y * stride)[x] = static_cast<uint16_t>(v);
      }
    }
  }

  uint32_t r_id = 0, g_id = 0, b_id = 0;
  err = heif_image_add_bayer_component(image, heif_cmpd_component_type_red, &r_id);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_image_add_bayer_component(image, heif_cmpd_component_type_green, &g_id);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_image_add_bayer_component(image, heif_cmpd_component_type_blue, &b_id);
  REQUIRE(err.code == heif_error_Ok);

  heif_bayer_pattern_pixel pattern[4] = {{r_id, 1.0f}, {g_id, 1.0f}, {g_id, 1.0f}, {b_id, 1.0f}};
  err = heif_image_set_bayer_pattern(image, fa_id, 2, 2, pattern);
  REQUIRE(err.code == heif_error_Ok);

  heif_context* ctx = heif_context_alloc();
  heif_encoder* encoder = nullptr;
  err = heif_context_get_encoder_for_format(ctx, heif_compression_uncompressed, &encoder);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_encode_image(ctx, image, encoder, nullptr, nullptr);
  REQUIRE(err.code == heif_error_Ok);

  std::string path = get_tests_output_file_path(filename);
  err = heif_context_write_to_file(ctx, path.c_str());
  REQUIRE(err.code == heif_error_Ok);

  heif_encoder_release(encoder);
  heif_image_release(image);
  heif_context_free(ctx);
  return path;
}

// Decodes to an interleaved RGB target and checks that a plane of the expected depth exists.
void decode_to_interleaved(const std::string& path, heif_chroma chroma, int expected_range_bits)
{
  heif_context* ctx = heif_context_alloc();
  heif_error err = heif_context_read_from_file(ctx, path.c_str(), nullptr);
  REQUIRE(err.code == heif_error_Ok);
  heif_image_handle* handle = get_primary_image_handle(ctx);

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_RGB, chroma, nullptr);
  INFO("decode error (" << err.code << "/" << err.subcode << "): " << err.message);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(img != nullptr);

  CHECK(heif_image_get_chroma_format(img) == chroma);
  CHECK(heif_image_get_bits_per_pixel_range(img, heif_channel_interleaved) == expected_range_bits);
  CHECK(heif_image_get_width(img, heif_channel_interleaved) == static_cast<int>(W));
  CHECK(heif_image_get_height(img, heif_channel_interleaved) == static_cast<int>(H));

  size_t stride = 0;
  const uint8_t* p = heif_image_get_plane_readonly2(img, heif_channel_interleaved, &stride);
  CHECK(p != nullptr);

  heif_image_release(img);
  heif_image_handle_release(handle);
  heif_context_free(ctx);
}

} // namespace

TEST_CASE("Bayer unci depth conversion goes through the Bayer operator")
{
  if (!heif_have_decoder_for_format(heif_compression_uncompressed)) {
    SKIP("uncompressed decoder not available");
  }
  if (!heif_have_encoder_for_format(heif_compression_uncompressed)) {
    SKIP("uncompressed encoder not available");
  }

  std::string bayer8 = write_bayer_unci(8, "bayer_8bit.heif");
  std::string bayer12 = write_bayer_unci(12, "bayer_12bit.heif");

  SECTION("direct routes (unchanged)") {
    decode_to_interleaved(bayer8, heif_chroma_interleaved_RGB, 8);
    decode_to_interleaved(bayer12, heif_chroma_interleaved_RRGGBB_LE, 12);
  }

  SECTION("8-bit Bayer to 16-bit interleaved RGB (was to_hdr on the filter array)") {
    // An interleaved RRGGBB target without a requested depth is 10 bits.
    decode_to_interleaved(bayer8, heif_chroma_interleaved_RRGGBB_LE, 10);
  }

  SECTION("12-bit Bayer to 8-bit interleaved RGB (was to_sdr on the filter array)") {
    decode_to_interleaved(bayer12, heif_chroma_interleaved_RGB, 8);
  }

  SECTION("12-bit Bayer, native layout with convert_hdr_to_8bit, never yields an image without planes") {
    heif_context* ctx = heif_context_alloc();
    heif_error err = heif_context_read_from_file(ctx, bayer12.c_str(), nullptr);
    REQUIRE(err.code == heif_error_Ok);
    heif_image_handle* handle = get_primary_image_handle(ctx);

    heif_decoding_options* options = heif_decoding_options_alloc();
    REQUIRE(options != nullptr);
    options->convert_hdr_to_8bit = true;

    heif_image* img = nullptr;
    err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, options);
    INFO("decode error (" << err.code << "/" << err.subcode << "): " << err.message);

    if (err.code == heif_error_Ok) {
      // Lowering a Bayer image to 8 bits in place is not implemented today. If it is one
      // day, the result has to carry the filter-array plane.
      REQUIRE(img != nullptr);
      CHECK(heif_image_has_channel(img, heif_channel_filter_array));
      heif_image_release(img);
    }
    else {
      CHECK(err.code == heif_error_Unsupported_feature);
      CHECK(err.subcode == heif_suberror_Unsupported_color_conversion);
      CHECK(img == nullptr);
    }

    heif_decoding_options_free(options);
    heif_image_handle_release(handle);
    heif_context_free(ctx);
  }
}
