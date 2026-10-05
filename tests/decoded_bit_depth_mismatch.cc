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

// The bit depth that heif_image_handle_get_luma_bits_per_pixel() reports is taken from the
// configuration box of the image item ('hvcC', 'av1C'). The decoded image gets its bit depth
// from the bitstream. A file in which the two disagree was decoded without an error, and the
// decoded image had another bit depth than the handle reported. An application that reads
// the planes of the decoded image with the bit depth from the handle (16-bit samples from a
// plane with 8-bit samples) reads out of bounds (GHSA-vv35-6hxg-95x8). Such a file is
// refused now, like a file whose decoded image does not have the signaled size.
//
// The test encodes an 8-bit image and changes the configuration box so that it claims 10 bit.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

constexpr int W = 64;
constexpr int H = 64;

heif_error write_to_vector(heif_context*, const void* data, size_t size, void* userdata)
{
  auto* out = static_cast<std::vector<uint8_t>*>(userdata);
  const auto* bytes = static_cast<const uint8_t*>(data);
  out->insert(out->end(), bytes, bytes + size);
  return heif_error{heif_error_Ok, heif_suberror_Unspecified, "Success"};
}


std::vector<uint8_t> encode_8bit_image(heif_compression_format format)
{
  heif_image* img = nullptr;
  heif_error err = heif_image_create(W, H, heif_colorspace_YCbCr, heif_chroma_420, &img);
  REQUIRE(err.code == heif_error_Ok);

  for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}) {
    int w = (channel == heif_channel_Y) ? W : W / 2;
    int h = (channel == heif_channel_Y) ? H : H / 2;
    err = heif_image_add_plane(img, channel, w, h, 8);
    REQUIRE(err.code == heif_error_Ok);

    size_t stride = 0;
    uint8_t* p = heif_image_get_plane2(img, channel, &stride);
    REQUIRE(p != nullptr);
    for (int y = 0; y < h; y++) {
      memset(p + y * stride, 0x60, w);
    }
  }

  heif_context* ctx = heif_context_alloc();
  heif_encoder* encoder = nullptr;
  err = heif_context_get_encoder_for_format(ctx, format, &encoder);
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


// Makes the configuration box claim a bit depth of 10 for luma and chroma.
void claim_10_bit(std::vector<uint8_t>& file, heif_compression_format format)
{
  const char* fourcc = (format == heif_compression_HEVC) ? "hvcC" : "av1C";

  for (size_t i = 0; i + 4 + 19 < file.size(); i++) {
    if (memcmp(&file[i], fourcc, 4) != 0) {
      continue;
    }

    uint8_t* config = &file[i + 4];

    if (format == heif_compression_HEVC) {
      // HEVCDecoderConfigurationRecord: bitDepthLumaMinus8 and bitDepthChromaMinus8 are the
      // lower three bits of the bytes 17 and 18.
      config[17] = static_cast<uint8_t>((config[17] & 0xF8) | 2);
      config[18] = static_cast<uint8_t>((config[18] & 0xF8) | 2);
    }
    else {
      // AV1CodecConfigurationRecord: high_bitdepth is the second bit of the third byte.
      config[2] = static_cast<uint8_t>(config[2] | 0x40);
    }
    return;
  }

  FAIL("configuration box not found");
}


struct Decoded
{
  int handle_luma_bits = 0;
  heif_error_code code = heif_error_Ok;
  std::string message;
  bool has_image = false;
  int image_luma_bits = 0;
};

Decoded decode(const std::vector<uint8_t>& file, heif_colorspace colorspace, heif_chroma chroma)
{
  Decoded result;

  heif_context* ctx = heif_context_alloc();
  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  INFO("read: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);

  result.handle_luma_bits = heif_image_handle_get_luma_bits_per_pixel(handle);

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, colorspace, chroma, nullptr);
  result.code = err.code;
  result.message = err.message ? err.message : "";
  result.has_image = (img != nullptr);

  if (img) {
    if (heif_image_has_channel(img, heif_channel_Y)) {
      result.image_luma_bits = heif_image_get_bits_per_pixel_range(img, heif_channel_Y);
    }
    heif_image_release(img);
  }

  heif_image_handle_release(handle);
  heif_context_free(ctx);
  return result;
}

} // namespace


TEST_CASE("an image whose decoded bit depth contradicts the image handle is refused")
{
  int num_formats_tested = 0;

  for (heif_compression_format format : {heif_compression_AV1, heif_compression_HEVC}) {
    if (!heif_have_encoder_for_format(format) || !heif_have_decoder_for_format(format)) {
      continue;
    }
    num_formats_tested++;

    INFO("format: " << (format == heif_compression_HEVC ? "HEVC" : "AV1"));

    std::vector<uint8_t> file = encode_8bit_image(format);

    // --- control: the unmodified file

    Decoded control = decode(file, heif_colorspace_undefined, heif_chroma_undefined);
    INFO("control: " << control.message);
    REQUIRE(control.code == heif_error_Ok);
    CHECK(control.handle_luma_bits == 8);
    CHECK(control.image_luma_bits == 8);

    // --- the configuration box claims 10 bit, the bitstream has 8 bit

    claim_10_bit(file, format);

    Decoded native = decode(file, heif_colorspace_undefined, heif_chroma_undefined);
    INFO("decoding without conversion: " << native.message);
    CHECK(native.handle_luma_bits == 10);

    // The decoded image must never have another bit depth than the handle reports.
    if (native.has_image) {
      CHECK(native.image_luma_bits == native.handle_luma_bits);
    }
    CHECK(native.code != heif_error_Ok);
    CHECK(!native.has_image);

    // A conversion does not hide the contradiction.
    Decoded converted = decode(file, heif_colorspace_RGB, heif_chroma_interleaved_RGB);
    INFO("decoding to RGB: " << converted.message);
    CHECK(converted.code != heif_error_Ok);
    CHECK(!converted.has_image);
  }

  if (num_formats_tested == 0) {
    SKIP("neither an AV1 nor an HEVC encoder and decoder available");
  }
}
