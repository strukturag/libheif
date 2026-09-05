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

// FFmpeg's JPEG 2000 (and MJPEG) decoders have no exact-depth pixel format for every
// coded bit depth. A 10- or 12-bit grayscale codestream is returned as GRAY16 with the
// samples shifted left to fill the 16-bit container. The FFmpeg decoder plugin has to
// undo this so that the decoded image has the bit depth declared in the file and the
// original sample values. These tests encode high bit depth JPEG 2000 images with the
// OpenJPEG encoder and decode them again explicitly with the FFmpeg decoder.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstring>
#include <string>
#include <vector>

static const int W = 64;
static const int H = 48;

static bool have_decoder(heif_compression_format format, const char* id)
{
  int n = heif_get_decoder_descriptors(format, nullptr, 0);
  std::vector<const heif_decoder_descriptor*> descs(n);
  n = heif_get_decoder_descriptors(format, descs.data(), n);
  for (int i = 0; i < n; i++) {
    const char* name = heif_decoder_descriptor_get_id_name(descs[i]);
    if (name && strcmp(name, id) == 0) {
      return true;
    }
  }
  return false;
}


// Deterministic test pattern that covers the full value range of the bit depth.
static uint16_t pattern(int x, int y, int bpp)
{
  uint32_t maxval = (1u << bpp) - 1;
  if (x == 0 && y == 0) return static_cast<uint16_t>(maxval);
  if (x == 1 && y == 0) return 0;
  return static_cast<uint16_t>((static_cast<uint32_t>(x) * 37 + static_cast<uint32_t>(y) * 101 + x * y) & maxval);
}


static void fill_plane(heif_image* img, heif_channel channel, int w, int h, int bpp)
{
  heif_error err = heif_image_add_plane(img, channel, w, h, bpp);
  REQUIRE(err.code == heif_error_Ok);

  size_t stride;
  uint8_t* p = heif_image_get_plane2(img, channel, &stride);
  REQUIRE(p != nullptr);

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint16_t v = pattern(x, y, bpp);
      if (bpp > 8) {
        reinterpret_cast<uint16_t*>(p + y * stride)[x] = v;
      }
      else {
        p[y * stride + x] = static_cast<uint8_t>(v);
      }
    }
  }
}


static void check_plane(const heif_image* img, heif_channel channel, int w, int h, int bpp)
{
  REQUIRE(heif_image_has_channel(img, channel));
  CHECK(heif_image_get_bits_per_pixel_range(img, channel) == bpp);
  CHECK(heif_image_get_width(img, channel) == w);
  CHECK(heif_image_get_height(img, channel) == h);

  size_t stride;
  const uint8_t* p = heif_image_get_plane_readonly2(img, channel, &stride);
  REQUIRE(p != nullptr);

  int mismatches = 0;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint16_t expected = pattern(x, y, bpp);
      uint16_t actual;
      if (bpp > 8) {
        actual = reinterpret_cast<const uint16_t*>(p + y * stride)[x];
      }
      else {
        actual = p[y * stride + x];
      }
      if (actual != expected) {
        if (mismatches < 3) {
          INFO("channel " << channel << " at (" << x << "," << y << "): expected " << expected << ", got " << actual);
          CHECK(actual == expected);
        }
        mismatches++;
      }
    }
  }
  CHECK(mismatches == 0);
}


static std::string encode_j2k_lossless(heif_image* img, const char* filename)
{
  heif_encoder* encoder = get_encoder_or_skip_test(heif_compression_JPEG2000);

  heif_context* ctx = heif_context_alloc();

  heif_error err = heif_encoder_set_lossless(encoder, 1);
  REQUIRE(err.code == heif_error_Ok);

  heif_encoding_options* options = heif_encoding_options_alloc();
  options->macOS_compatibility_workaround = false;
  options->macOS_compatibility_workaround_no_nclx_profile = true;

  heif_image_handle* handle;
  err = heif_context_encode_image(ctx, img, encoder, options, &handle);
  REQUIRE(err.code == heif_error_Ok);
  heif_image_handle_release(handle);

  std::string path = get_tests_output_file_path(filename);
  err = heif_context_write_to_file(ctx, path.c_str());
  REQUIRE(err.code == heif_error_Ok);

  heif_encoding_options_free(options);
  heif_encoder_release(encoder);
  heif_context_free(ctx);

  return path;
}


static heif_image* decode_with_ffmpeg(const std::string& path, heif_colorspace colorspace, heif_chroma chroma, int expected_bpp)
{
  heif_context* ctx = get_context_for_local_file(path);
  heif_image_handle* handle = get_primary_image_handle(ctx);

  // The declared bit depth comes from the codestream header (SIZ), independent of the decoder.
  CHECK(heif_image_handle_get_luma_bits_per_pixel(handle) == expected_bpp);

  heif_decoding_options* options = heif_decoding_options_alloc();
  options->decoder_id = "ffmpeg";

  heif_image* img;
  heif_error err = heif_decode_image(handle, &img, colorspace, chroma, options);
  INFO("decode error: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  heif_decoding_options_free(options);
  heif_image_handle_release(handle);
  heif_context_free(ctx);

  return img;
}


TEST_CASE("ffmpeg decodes high bit depth monochrome JPEG 2000 at the coded bit depth")
{
  if (!have_decoder(heif_compression_JPEG2000, "ffmpeg")) {
    SKIP("FFmpeg decoder not available, skipping test");
  }

  // FFmpeg returns 10- and 12-bit grayscale as GRAY16 with left-shifted samples; 8- and 16-bit
  // have exact formats (GRAY8 / GRAY16) and must be passed through unchanged.
  int bpp = GENERATE(8, 10, 12, 16);
  INFO("bit depth " << bpp);

  heif_image* input;
  heif_error err = heif_image_create(W, H, heif_colorspace_monochrome, heif_chroma_monochrome, &input);
  REQUIRE(err.code == heif_error_Ok);
  fill_plane(input, heif_channel_Y, W, H, bpp);

  std::string filename = "ffmpeg_j2k_mono" + std::to_string(bpp) + ".heif";
  std::string path = encode_j2k_lossless(input, filename.c_str());
  heif_image_release(input);

  heif_image* decoded = decode_with_ffmpeg(path, heif_colorspace_monochrome, heif_chroma_monochrome, bpp);
  check_plane(decoded, heif_channel_Y, W, H, bpp);
  heif_image_release(decoded);
}


TEST_CASE("ffmpeg decodes high bit depth YCbCr 4:4:4 JPEG 2000 at the coded bit depth")
{
  if (!have_decoder(heif_compression_JPEG2000, "ffmpeg")) {
    SKIP("FFmpeg decoder not available, skipping test");
  }

  // FFmpeg returns a 3-component 4:4:4 codestream with more than 8 bits as packed RGB48
  // with left-shifted samples (8 bit as packed RGB24). The libheif OpenJPEG encoder always
  // writes 4:4:4, so subsampled codestreams cannot be generated here.
  int bpp = GENERATE(8, 10, 12, 16);
  INFO("bit depth " << bpp);

  heif_image* input;
  heif_error err = heif_image_create(W, H, heif_colorspace_YCbCr, heif_chroma_444, &input);
  REQUIRE(err.code == heif_error_Ok);
  fill_plane(input, heif_channel_Y, W, H, bpp);
  fill_plane(input, heif_channel_Cb, W, H, bpp);
  fill_plane(input, heif_channel_Cr, W, H, bpp);

  std::string filename = "ffmpeg_j2k_yuv444_" + std::to_string(bpp) + ".heif";
  std::string path = encode_j2k_lossless(input, filename.c_str());
  heif_image_release(input);

  heif_image* decoded = decode_with_ffmpeg(path, heif_colorspace_YCbCr, heif_chroma_444, bpp);
  check_plane(decoded, heif_channel_Y, W, H, bpp);
  check_plane(decoded, heif_channel_Cb, W, H, bpp);
  check_plane(decoded, heif_channel_Cr, W, H, bpp);
  heif_image_release(decoded);
}
