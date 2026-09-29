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

// Regression test: an 'unci' image whose colour planes have different bit depths, for
// example R=8, G=8, B=12, could not be decoded to 8-bit RGB (interleaved RGB, or planar
// with convert_hdr_to_8bit), while the very same planes in the order 12/8/8 could.
// Op_to_sdr_planes lowers every plane to 8 bits on its own, but it was only offered when
// the first colour plane was wider than 8 bits, because the planner asked for "the" colour
// depth of an image whose planes do not share one. ColorState now reports a colour depth
// only when all colour planes agree, and Op_to_sdr_planes declines only when all of them
// are already 8 bits.
//
// The files are built through the public API and the uncompressed encoder, so the test
// needs both halves of the uncompressed codec.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <string>

namespace {

constexpr uint32_t W = 64;
constexpr uint32_t H = 64;

const heif_channel CHANNELS[3] = {heif_channel_R, heif_channel_G, heif_channel_B};

// Every plane holds one constant. At 8 bits these are the values below; a 12-bit plane
// holds the same value shifted up by four bits plus a few low bits that the reduction to
// 8 bits must drop again.
constexpr uint8_t VALUES[3] = {100, 50, 25};

std::string write_unci_rgb(const int bits[3], const char* filename)
{
  heif_image* image = nullptr;
  heif_error err = heif_image_create(W, H, heif_colorspace_RGB, heif_chroma_444, &image);
  REQUIRE(err.code == heif_error_Ok);

  for (int i = 0; i < 3; i++) {
    err = heif_image_add_plane(image, CHANNELS[i], W, H, bits[i]);
    REQUIRE(err.code == heif_error_Ok);

    size_t stride = 0;
    uint8_t* p = heif_image_get_plane2(image, CHANNELS[i], &stride);
    REQUIRE(p != nullptr);

    for (uint32_t y = 0; y < H; y++) {
      for (uint32_t x = 0; x < W; x++) {
        if (bits[i] == 8) {
          p[y * stride + x] = VALUES[i];
        }
        else {
          reinterpret_cast<uint16_t*>(p + y * stride)[x] = static_cast<uint16_t>((VALUES[i] << (bits[i] - 8)) | 0xB);
        }
      }
    }
  }

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

heif_image* decode(const std::string& path, heif_colorspace colorspace, heif_chroma chroma, bool hdr_to_8bit)
{
  heif_context* ctx = heif_context_alloc();
  heif_error err = heif_context_read_from_file(ctx, path.c_str(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = get_primary_image_handle(ctx);

  heif_decoding_options* options = heif_decoding_options_alloc();
  REQUIRE(options != nullptr);
  options->convert_hdr_to_8bit = hdr_to_8bit;

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, colorspace, chroma, options);
  INFO("decode error (" << err.code << "/" << err.subcode << "): " << err.message);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(img != nullptr);

  heif_decoding_options_free(options);
  heif_image_handle_release(handle);
  heif_context_free(ctx);
  return img;
}

} // namespace

TEST_CASE("unci RGB with mixed plane depths decodes to 8 bits in any plane order")
{
  if (!heif_have_decoder_for_format(heif_compression_uncompressed)) {
    SKIP("uncompressed decoder not available");
  }
  if (!heif_have_encoder_for_format(heif_compression_uncompressed)) {
    SKIP("uncompressed encoder not available");
  }

  const int layouts[3][3] = {{8, 8, 12}, {12, 8, 8}, {8, 12, 8}};
  const char* filenames[3] = {"mixed_rgb_8_8_12.heif", "mixed_rgb_12_8_8.heif", "mixed_rgb_8_12_8.heif"};

  for (int l = 0; l < 3; l++) {
    const int* bits = layouts[l];
    INFO("R/G/B = " << bits[0] << "/" << bits[1] << "/" << bits[2]);

    std::string path = write_unci_rgb(bits, filenames[l]);

    // A native decode keeps the declared depths, so the file really is mixed.
    {
      heif_image* img = decode(path, heif_colorspace_undefined, heif_chroma_undefined, false);
      for (int i = 0; i < 3; i++) {
        CHECK(heif_image_get_bits_per_pixel_range(img, CHANNELS[i]) == bits[i]);
      }
      heif_image_release(img);
    }

    // 8-bit interleaved RGB: the wide plane has to be lowered first.
    {
      heif_image* img = decode(path, heif_colorspace_RGB, heif_chroma_interleaved_RGB, false);
      size_t stride = 0;
      const uint8_t* p = heif_image_get_plane_readonly2(img, heif_channel_interleaved, &stride);
      REQUIRE(p != nullptr);
      const uint8_t* last = p + (H - 1) * stride + (W - 1) * 3;
      for (int i = 0; i < 3; i++) {
        CHECK(p[i] == VALUES[i]);
        CHECK(last[i] == VALUES[i]);
      }
      heif_image_release(img);
    }

    // Planar output with convert_hdr_to_8bit: every plane ends up at 8 bits.
    {
      heif_image* img = decode(path, heif_colorspace_undefined, heif_chroma_undefined, true);
      for (int i = 0; i < 3; i++) {
        CHECK(heif_image_get_bits_per_pixel_range(img, CHANNELS[i]) == 8);
        size_t stride = 0;
        const uint8_t* p = heif_image_get_plane_readonly2(img, CHANNELS[i], &stride);
        REQUIRE(p != nullptr);
        CHECK(p[0] == VALUES[i]);
        CHECK(p[(H - 1) * stride + (W - 1)] == VALUES[i]);
      }
      heif_image_release(img);
    }
  }
}
