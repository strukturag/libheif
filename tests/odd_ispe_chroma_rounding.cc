/*
  libheif test: autocorrect a coded frame that rounds an odd 'ispe' up to the chroma grid

  MIT License

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

// Some encoders write an odd image size into 'ispe' and an identical 'clap'
// with a zero offset, e.g. 'ispe' 900x719 with 'clap' 900x719. A 4:2:0 HEVC
// conformance window can only crop in steps of two luma samples, so the coded
// frame decodes to 900x720. Such files are rejected because the decoded image
// does not have the signaled size. Apple ImageIO decodes them by dropping the
// extra last row/column. With autocorrect_broken_input, libheif does the same.
//
// The test encodes an odd-size image (libheif writes the conformant form:
// 'ispe' with the even coded size plus a 'clap' with the odd size) and then
// rewrites 'ispe' and 'clap' into the broken form.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

heif_error write_to_vector(heif_context*, const void* data, size_t size, void* userdata)
{
  auto* out = static_cast<std::vector<uint8_t>*>(userdata);
  const auto* bytes = static_cast<const uint8_t*>(data);
  out->insert(out->end(), bytes, bytes + size);
  return heif_error{heif_error_Ok, heif_suberror_Unspecified, "Success"};
}


// Encodes a WxH 4:2:0 HEVC image with a luma gradient, so that a crop at the
// wrong position changes the pixels.
std::vector<uint8_t> encode_image(int W, int H)
{
  heif_image* img = nullptr;
  heif_error err = heif_image_create(W, H, heif_colorspace_YCbCr, heif_chroma_420, &img);
  REQUIRE(err.code == heif_error_Ok);

  for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}) {
    int w = (channel == heif_channel_Y) ? W : (W + 1) / 2;
    int h = (channel == heif_channel_Y) ? H : (H + 1) / 2;
    err = heif_image_add_plane(img, channel, w, h, 8);
    REQUIRE(err.code == heif_error_Ok);

    size_t stride = 0;
    uint8_t* p = heif_image_get_plane2(img, channel, &stride);
    REQUIRE(p != nullptr);
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        p[y * stride + x] = (channel == heif_channel_Y) ? static_cast<uint8_t>(16 + (x + 3 * y) % 220) : 128;
      }
    }
  }

  heif_context* ctx = heif_context_alloc();
  heif_encoder* encoder = nullptr;
  err = heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);
  REQUIRE(err.code == heif_error_Ok);
  heif_encoder_set_lossless(encoder, 1);

  err = heif_context_encode_image(ctx, img, encoder, nullptr, nullptr);
  INFO("encode: " << (err.message ? err.message : ""));
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


void put_u32(uint8_t* p, uint32_t v)
{
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}


uint8_t* find_box_payload(std::vector<uint8_t>& file, const char* fourcc)
{
  for (size_t i = 4; i + 4 < file.size(); i++) {
    if (memcmp(&file[i], fourcc, 4) == 0) {
      return &file[i + 4];
    }
  }
  return nullptr;
}


// Rewrites 'ispe' to WxH and, if present, 'clap' to WxH with a zero offset.
void write_broken_size(std::vector<uint8_t>& file, uint32_t W, uint32_t H)
{
  uint8_t* ispe = find_box_payload(file, "ispe");
  REQUIRE(ispe != nullptr);
  put_u32(ispe + 4, W);   // after version/flags
  put_u32(ispe + 8, H);

  if (uint8_t* clap = find_box_payload(file, "clap")) {
    const uint32_t values[8] = {W, 1, H, 1, 0, 2, 0, 2};
    for (int k = 0; k < 8; k++) {
      put_u32(clap + 4 * k, values[k]);
    }
  }
}


struct Decoded
{
  heif_error_code code = heif_error_Ok;
  heif_suberror_code subcode = heif_suberror_Unspecified;
  std::string message;
  int width = 0;
  int height = 0;
  bool size_warning = false;
  std::vector<uint8_t> luma;  // tightly packed
};

Decoded decode(const std::vector<uint8_t>& file, bool autocorrect, bool ignore_transformations = false)
{
  Decoded result;

  heif_context* ctx = heif_context_alloc();
  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  INFO("read: " << (err.message ? err.message : ""));
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);

  heif_decoding_options* options = heif_decoding_options_alloc();
  options->autocorrect_broken_input = autocorrect;
  options->ignore_transformations = ignore_transformations;

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_YCbCr, heif_chroma_420, options);
  heif_decoding_options_free(options);

  result.code = err.code;
  result.subcode = err.subcode;
  result.message = err.message ? err.message : "";

  if (img) {
    result.width = heif_image_get_primary_width(img);
    result.height = heif_image_get_primary_height(img);

    int n = heif_image_get_decoding_warnings(img, 0, nullptr, 0);
    std::vector<heif_error> warnings(n);
    heif_image_get_decoding_warnings(img, 0, warnings.data(), n);
    for (const heif_error& w : warnings) {
      if (w.subcode == heif_suberror_Invalid_image_size) {
        result.size_warning = true;
      }
    }

    size_t stride = 0;
    const uint8_t* p = heif_image_get_plane_readonly2(img, heif_channel_Y, &stride);
    REQUIRE(p != nullptr);
    for (int y = 0; y < result.height; y++) {
      result.luma.insert(result.luma.end(), p + y * stride, p + y * stride + result.width);
    }

    heif_image_release(img);
  }

  heif_image_handle_release(handle);
  heif_context_free(ctx);
  return result;
}

} // namespace


TEST_CASE("odd 'ispe' one row/column smaller than the 4:2:0 coded frame")
{
  if (!heif_have_encoder_for_format(heif_compression_HEVC) ||
      !heif_have_decoder_for_format(heif_compression_HEVC)) {
    SKIP("no HEVC encoder and decoder");
  }

  struct Size {
    int w, h;
  };

  for (Size size : {Size{451, 461}, Size{451, 462}, Size{452, 461}}) {
    INFO("size: " << size.w << "x" << size.h);

    std::vector<uint8_t> file = encode_image(size.w, size.h);

    // The coded frame (as written by libheif: 'ispe' = coded frame size).
    Decoded coded = decode(file, false, true);
    REQUIRE(coded.code == heif_error_Ok);
    const int coded_w = coded.width;
    const int coded_h = coded.height;
    REQUIRE(coded_w == size.w + (size.w % 2));
    REQUIRE(coded_h == size.h + (size.h % 2));

    write_broken_size(file, size.w, size.h);

    // Without autocorrection, the size mismatch is an error.
    Decoded strict = decode(file, false);
    INFO("default: " << strict.message);
    CHECK(strict.code == heif_error_Invalid_input);
    CHECK(strict.subcode == heif_suberror_Invalid_image_size);

    // With autocorrection, the top-left 'ispe' area of the coded frame is returned.
    for (bool ignore_transformations : {false, true}) {
      INFO("ignore_transformations: " << ignore_transformations);
      Decoded corrected = decode(file, true, ignore_transformations);
      INFO("autocorrect: " << corrected.message);
      REQUIRE(corrected.code == heif_error_Ok);
      CHECK(corrected.width == size.w);
      CHECK(corrected.height == size.h);
      CHECK(corrected.size_warning);

      bool same = true;
      for (int y = 0; y < size.h && same; y++) {
        same = memcmp(&corrected.luma[y * size.w], &coded.luma[y * coded_w], size.w) == 0;
      }
      CHECK(same);
    }
  }
}


TEST_CASE("autocorrection does not accept other size mismatches")
{
  if (!heif_have_encoder_for_format(heif_compression_HEVC) ||
      !heif_have_decoder_for_format(heif_compression_HEVC)) {
    SKIP("no HEVC encoder and decoder");
  }

  std::vector<uint8_t> file = encode_image(451, 461);  // coded frame 452x462

  // even 'ispe' (frame is larger, but not by the chroma rounding of an odd size),
  // two rows/columns smaller, and larger than the frame
  struct Size {
    uint32_t w, h;
  };
  for (Size size : {Size{450, 462}, Size{452, 460}, Size{449, 461}, Size{453, 463}}) {
    INFO("ispe: " << size.w << "x" << size.h);
    std::vector<uint8_t> broken = file;
    write_broken_size(broken, size.w, size.h);

    Decoded corrected = decode(broken, true);
    INFO("autocorrect: " << corrected.message);
    CHECK(corrected.code == heif_error_Invalid_input);
  }
}
