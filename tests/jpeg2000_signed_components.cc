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

// A JPEG 2000 codestream declares in its SIZ marker, per component, whether the samples are
// signed. For a signed component OpenJPEG returns negative values. The OpenJPEG decoder
// plugin copied them into the unsigned samples of the image planes with a plain cast, so
// that e.g. -1 became 65535 in a plane with a bit depth of 10. Code that relies on the bit
// depth of a plane (libsharpyuv uses the samples as table indices) then read out of bounds.
// The plugin now refuses codestreams with signed components.
//
// The test encodes a small 10-bit image, sets the sign bit of every component in the SIZ
// marker of the written file, and decodes it again.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int W = 32;
constexpr int H = 32;
constexpr int BIT_DEPTH = 10;

heif_error write_to_vector(heif_context*, const void* data, size_t size, void* userdata)
{
  auto* out = static_cast<std::vector<uint8_t>*>(userdata);
  const auto* bytes = static_cast<const uint8_t*>(data);
  out->insert(out->end(), bytes, bytes + size);
  return heif_error{heif_error_Ok, heif_suberror_Unspecified, "Success"};
}


std::vector<uint8_t> encode_jpeg2000_image()
{
  heif_image* img = nullptr;
  heif_error err = heif_image_create(W, H, heif_colorspace_RGB, heif_chroma_444, &img);
  REQUIRE(err.code == heif_error_Ok);

  for (heif_channel channel : {heif_channel_R, heif_channel_G, heif_channel_B}) {
    err = heif_image_add_plane(img, channel, W, H, BIT_DEPTH);
    REQUIRE(err.code == heif_error_Ok);

    size_t stride = 0;
    uint8_t* p = heif_image_get_plane2(img, channel, &stride);
    REQUIRE(p != nullptr);
    for (int y = 0; y < H; y++) {
      uint16_t* row = reinterpret_cast<uint16_t*>(p + y * stride);
      for (int x = 0; x < W; x++) {
        row[x] = static_cast<uint16_t>((x * 31 + y * 17 + channel * 5) & ((1 << BIT_DEPTH) - 1));
      }
    }
  }

  heif_context* ctx = heif_context_alloc();
  heif_encoder* encoder = nullptr;
  err = heif_context_get_encoder_for_format(ctx, heif_compression_JPEG2000, &encoder);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_encode_image(ctx, img, encoder, nullptr, nullptr);
  INFO("encode: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  std::vector<uint8_t> file;
  heif_writer writer;
  writer.writer_api_version = 1;
  writer.write = write_to_vector;
  err = heif_context_write(ctx, &writer, &file);
  REQUIRE(err.code == heif_error_Ok);

  heif_encoder_release(encoder);
  heif_context_free(ctx);
  heif_image_release(img);

  return file;
}


// Sets the sign bit in the Ssiz field of every component in the SIZ marker segment.
// Returns the number of components that were changed.
int make_components_signed(std::vector<uint8_t>& file)
{
  // SOC marker (FF4F), directly followed by the SIZ marker (FF51).
  const uint8_t start[4] = {0xFF, 0x4F, 0xFF, 0x51};

  for (size_t i = 0; i + 4 + 38 < file.size(); i++) {
    if (memcmp(&file[i], start, 4) != 0) {
      continue;
    }

    // After the SIZ marker: Lsiz(2) Rsiz(2) Xsiz(4) Ysiz(4) XOsiz(4) YOsiz(4) XTsiz(4)
    // YTsiz(4) XTOsiz(4) YTOsiz(4) Csiz(2), then Ssiz(1) XRsiz(1) YRsiz(1) per component.
    size_t siz = i + 4;
    int num_components = (file[siz + 36] << 8) | file[siz + 37];
    if (siz + 38 + 3 * static_cast<size_t>(num_components) > file.size()) {
      return 0;
    }

    for (int c = 0; c < num_components; c++) {
      file[siz + 38 + 3 * c] |= 0x80;
    }
    return num_components;
  }

  return 0;
}


struct DecodeResult
{
  heif_error_code code;
  std::string message;
  bool has_image;
  uint32_t largest_sample;
};

DecodeResult decode(const std::vector<uint8_t>& file)
{
  heif_context* ctx = heif_context_alloc();
  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);

  // Decode without conversion, so that the planes come back as the plugin filled them.
  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);

  // The message may point into the context, so copy it before the context is freed.
  DecodeResult result{err.code, err.message ? err.message : "", img != nullptr, 0};

  if (img) {
    for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr,
                                 heif_channel_R, heif_channel_G, heif_channel_B}) {
      if (!heif_image_has_channel(img, channel)) {
        continue;
      }

      REQUIRE(heif_image_get_bits_per_pixel_range(img, channel) == BIT_DEPTH);

      size_t stride = 0;
      const uint8_t* p = heif_image_get_plane_readonly2(img, channel, &stride);
      REQUIRE(p != nullptr);
      int w = heif_image_get_width(img, channel);
      int h = heif_image_get_height(img, channel);
      for (int y = 0; y < h; y++) {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(p + y * stride);
        for (int x = 0; x < w; x++) {
          if (row[x] > result.largest_sample) {
            result.largest_sample = row[x];
          }
        }
      }
    }

    heif_image_release(img);
  }

  heif_image_handle_release(handle);
  heif_context_free(ctx);
  return result;
}

} // namespace


TEST_CASE("JPEG 2000 codestreams with signed components are refused")
{
  if (!heif_have_encoder_for_format(heif_compression_JPEG2000) ||
      !heif_have_decoder_for_format(heif_compression_JPEG2000)) {
    SKIP("no JPEG 2000 encoder or decoder available");
  }

  std::vector<uint8_t> file = encode_jpeg2000_image();

  SECTION("control: the unmodified file decodes, with all samples within the bit depth") {
    DecodeResult result = decode(file);
    INFO("decode: " << result.message);
    REQUIRE(result.code == heif_error_Ok);
    REQUIRE(result.has_image);
    CHECK(result.largest_sample <= (1u << BIT_DEPTH) - 1);
  }

  SECTION("signed components") {
    REQUIRE(make_components_signed(file) > 0);

    DecodeResult result = decode(file);
    INFO("decode: " << result.message);

    // Whatever the decoder does with such a file, it must not hand out samples above the
    // bit depth of the plane.
    CHECK(result.largest_sample <= (1u << BIT_DEPTH) - 1);

    CHECK(result.code == heif_error_Unsupported_feature);
    CHECK(!result.has_image);
  }
}
