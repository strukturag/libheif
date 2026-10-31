/*
  libheif unit tests

  MIT License

  Copyright (c) 2026 Michael Wallner <mike@php.net>

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

// The chroma format and the bit depth that the image handle reports are taken from the
// configuration box of the image item ('hvcC', 'av1C'), while the decoder allocates its
// pictures in the chroma format and the bit depth of the bitstream (the SPS, the AV1 sequence
// header). A bitstream that codes more than its configuration box states made the decoder
// allocate more than the file announces, four times as much for a 4:4:4 stream of more than
// 8 bits behind a box for 8-bit 4:2:0: libheif compared only the bit depth, and only with the
// decoded image (GHSA-vv35-6hxg-95x8), and returned a 4:4:4 image for a handle that reports
// 4:2:0. Such a file is refused now before it reaches the decoder.
//
// The tests encode an image and change its configuration box so that it claims 4:2:0 for a
// 4:4:4 bitstream, or 8 bits for a 10-bit one.

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


const char* format_name(heif_compression_format format)
{
  return (format == heif_compression_HEVC) ? "HEVC" : "AV1";
}


// Encodes a gray image in `chroma` (4:2:0 or 4:4:4) at `bit_depth`. Returns an empty vector
// when the encoder does not take that format.
std::vector<uint8_t> encode_image(heif_compression_format format, heif_chroma chroma, int bit_depth)
{
  heif_image* img = nullptr;
  heif_error err = heif_image_create(W, H, heif_colorspace_YCbCr, chroma, &img);
  REQUIRE(err.code == heif_error_Ok);

  const int chroma_w = (chroma == heif_chroma_444) ? W : W / 2;
  const int chroma_h = (chroma == heif_chroma_444) ? H : H / 2;
  const uint16_t gray = static_cast<uint16_t>(1 << (bit_depth - 1));

  for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}) {
    int w = (channel == heif_channel_Y) ? W : chroma_w;
    int h = (channel == heif_channel_Y) ? H : chroma_h;
    err = heif_image_add_plane(img, channel, w, h, bit_depth);
    REQUIRE(err.code == heif_error_Ok);

    size_t stride = 0;
    uint8_t* p = heif_image_get_plane2(img, channel, &stride);
    REQUIRE(p != nullptr);
    for (int y = 0; y < h; y++) {
      uint8_t* row = p + y * stride;
      for (int x = 0; x < w; x++) {
        if (bit_depth > 8) {
          memcpy(row + 2 * x, &gray, 2);
        }
        else {
          row[x] = static_cast<uint8_t>(gray);
        }
      }
    }
  }

  heif_context* ctx = heif_context_alloc();
  heif_encoder* encoder = nullptr;
  err = heif_context_get_encoder_for_format(ctx, format, &encoder);
  REQUIRE(err.code == heif_error_Ok);

  // The encoders code 4:2:0 unless they are asked for another chroma format.
  if (chroma == heif_chroma_444) {
    err = heif_encoder_set_parameter_string(encoder, "chroma", "444");
  }
  if (err.code == heif_error_Ok) {
    err = heif_context_encode_image(ctx, img, encoder, nullptr, nullptr);
  }

  std::vector<uint8_t> file;
  if (err.code == heif_error_Ok) {
    heif_writer writer;
    writer.writer_api_version = 1;
    writer.write = write_to_vector;
    err = heif_context_write(ctx, &writer, &file);
    REQUIRE(err.code == heif_error_Ok);
  }

  heif_encoder_release(encoder);
  heif_context_free(ctx);
  heif_image_release(img);

  return file;
}


uint8_t* configuration_box(std::vector<uint8_t>& file, heif_compression_format format)
{
  const char* fourcc = (format == heif_compression_HEVC) ? "hvcC" : "av1C";

  for (size_t i = 0; i + 4 + 19 < file.size(); i++) {
    if (memcmp(&file[i], fourcc, 4) == 0) {
      return &file[i + 4];
    }
  }

  FAIL("configuration box not found");
  return nullptr;
}


// Makes the configuration box claim 4:2:0.
void claim_420(std::vector<uint8_t>& file, heif_compression_format format)
{
  uint8_t* config = configuration_box(file, format);

  if (format == heif_compression_HEVC) {
    // HEVCDecoderConfigurationRecord: chromaFormat is the lower two bits of byte 16.
    config[16] = static_cast<uint8_t>((config[16] & 0xFC) | 1);
  }
  else {
    // AV1CodecConfigurationRecord: chroma_subsampling_x and chroma_subsampling_y are the
    // fifth and the sixth bit of the third byte.
    config[2] = static_cast<uint8_t>(config[2] | 0x0C);
  }
}


// Makes the configuration box claim 8 bits for luma and chroma.
void claim_8_bit(std::vector<uint8_t>& file, heif_compression_format format)
{
  uint8_t* config = configuration_box(file, format);

  if (format == heif_compression_HEVC) {
    // bitDepthLumaMinus8 and bitDepthChromaMinus8 are the lower three bits of bytes 17 and 18.
    config[17] = static_cast<uint8_t>(config[17] & 0xF8);
    config[18] = static_cast<uint8_t>(config[18] & 0xF8);
  }
  else {
    // high_bitdepth and twelve_bit are the second and the third bit of the third byte.
    config[2] = static_cast<uint8_t>(config[2] & ~0x60);
  }
}


struct Decoded
{
  heif_chroma handle_chroma = heif_chroma_undefined;
  int handle_luma_bits = 0;
  heif_error_code code = heif_error_Ok;
  std::string message;
  bool has_image = false;
};

Decoded decode(const std::vector<uint8_t>& file)
{
  Decoded result;

  heif_context* ctx = heif_context_alloc();
  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  INFO("read: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);

  heif_colorspace colorspace = heif_colorspace_undefined;
  err = heif_image_handle_get_preferred_decoding_colorspace(handle, &colorspace, &result.handle_chroma);
  REQUIRE(err.code == heif_error_Ok);
  result.handle_luma_bits = heif_image_handle_get_luma_bits_per_pixel(handle);

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
  result.code = err.code;
  result.message = err.message ? err.message : "";
  result.has_image = (img != nullptr);

  if (img) {
    heif_image_release(img);
  }

  heif_image_handle_release(handle);
  heif_context_free(ctx);
  return result;
}

} // namespace


TEST_CASE("a bitstream in a larger chroma format than its configuration box is refused")
{
  int num_formats_tested = 0;

  for (heif_compression_format format : {heif_compression_AV1, heif_compression_HEVC}) {
    if (!heif_have_encoder_for_format(format) || !heif_have_decoder_for_format(format)) {
      continue;
    }

    INFO("format: " << format_name(format));

    std::vector<uint8_t> file = encode_image(format, heif_chroma_444, 8);
    if (file.empty()) {
      continue;
    }

    // --- control: the unmodified file

    Decoded control = decode(file);
    INFO("control: " << control.message);
    if (control.handle_chroma != heif_chroma_444) {
      continue; // The encoder coded another chroma format.
    }
    REQUIRE(control.code == heif_error_Ok);
    REQUIRE(control.has_image);
    num_formats_tested++;

    // --- the configuration box claims 4:2:0, the bitstream has 4:4:4

    claim_420(file, format);

    Decoded claimed = decode(file);
    INFO("decoding: " << claimed.message);
    CHECK(claimed.handle_chroma == heif_chroma_420);
    CHECK(claimed.code == heif_error_Invalid_input);
    CHECK(!claimed.has_image);
  }

  if (num_formats_tested == 0) {
    SKIP("neither an AV1 nor an HEVC encoder for 4:4:4 with a decoder available");
  }
}


TEST_CASE("a bitstream with a higher bit depth than its configuration box is refused before decoding")
{
  int num_formats_tested = 0;

  for (heif_compression_format format : {heif_compression_AV1, heif_compression_HEVC}) {
    if (!heif_have_encoder_for_format(format) || !heif_have_decoder_for_format(format)) {
      continue;
    }

    INFO("format: " << format_name(format));

    std::vector<uint8_t> file = encode_image(format, heif_chroma_420, 10);
    if (file.empty()) {
      continue;
    }

    // --- control: the unmodified file

    Decoded control = decode(file);
    INFO("control: " << control.message);
    if (control.handle_luma_bits != 10) {
      continue; // The encoder coded another bit depth.
    }
    REQUIRE(control.code == heif_error_Ok);
    REQUIRE(control.has_image);
    num_formats_tested++;

    // --- the configuration box claims 8 bits, the bitstream has 10

    claim_8_bit(file, format);

    Decoded claimed = decode(file);
    INFO("decoding: " << claimed.message);
    CHECK(claimed.handle_luma_bits == 8);
    CHECK(claimed.code == heif_error_Invalid_input);
    CHECK(!claimed.has_image);

    // The check of the decoded image refused this file as well, after the decoder had run.
    // Only the check before decoding names the configuration.
    CHECK(claimed.message.find("configuration") != std::string::npos);
  }

  if (num_formats_tested == 0) {
    SKIP("neither an AV1 nor an HEVC encoder for 10 bits with a decoder available");
  }
}
