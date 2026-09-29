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

// heif_context_write_to_file() did not notice when the file could not be written and
// reported success. An application like heif-enc then ended without any message and without
// an output file.
//
// What is written does not matter here. The tests encode a small image with any encoder that
// is available, since a context without images cannot be written at all.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

struct WriteResult
{
  heif_error_code code;
  heif_suberror_code subcode;
  std::string message;
};

heif_compression_format pick_encoder_format()
{
  for (heif_compression_format format : {heif_compression_AV1,
                                         heif_compression_HEVC,
                                         heif_compression_JPEG,
                                         heif_compression_uncompressed}) {
    if (heif_have_encoder_for_format(format)) {
      return format;
    }
  }

  return heif_compression_undefined;
}

void encode_image(heif_context* ctx, heif_compression_format format)
{
  const uint32_t size = 64;

  heif_image* img = nullptr;
  heif_error err = heif_image_create(size, size, heif_colorspace_YCbCr, heif_chroma_420, &img);
  REQUIRE(err.code == heif_error_Ok);

  for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}) {
    const uint32_t plane_size = (channel == heif_channel_Y ? size : size / 2);
    err = heif_image_add_plane(img, channel, plane_size, plane_size, 8);
    REQUIRE(err.code == heif_error_Ok);

    size_t stride = 0;
    uint8_t* p = heif_image_get_plane2(img, channel, &stride);
    for (uint32_t y = 0; y < plane_size; y++) {
      std::fill(p + y * stride, p + y * stride + plane_size, uint8_t{128});
    }
  }

  heif_encoder* encoder = nullptr;
  err = heif_context_get_encoder_for_format(ctx, format, &encoder);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_encode_image(ctx, img, encoder, nullptr, nullptr);
  INFO("encoding error: " << (err.message ? err.message : ""));
  REQUIRE(err.code == heif_error_Ok);

  heif_encoder_release(encoder);
  heif_image_release(img);
}

// heif_error::message may point into the context, so it is copied before the context is freed.
WriteResult write_to_file(const std::string& filename, heif_compression_format format)
{
  heif_context* ctx = heif_context_alloc();
  encode_image(ctx, format);

  heif_error err = heif_context_write_to_file(ctx, filename.c_str());
  WriteResult result{err.code, err.subcode, err.message ? err.message : ""};

  heif_context_free(ctx);
  return result;
}

} // namespace


TEST_CASE("heif_context_write_to_file writes the file")
{
  const heif_compression_format format = pick_encoder_format();
  if (format == heif_compression_undefined) {
    SKIP("no encoder available");
  }

  const std::string filename = get_tests_output_file_path("write_to_file.heif");

  std::error_code ignored;
  std::filesystem::remove(filename, ignored);

  WriteResult result = write_to_file(filename, format);
  INFO("error (" << result.code << "/" << result.subcode << "): " << result.message);
  REQUIRE(result.code == heif_error_Ok);

  REQUIRE(std::filesystem::exists(filename));
  CHECK(std::filesystem::file_size(filename) > 0);
}


TEST_CASE("heif_context_write_to_file reports a file that cannot be opened")
{
  const heif_compression_format format = pick_encoder_format();
  if (format == heif_compression_undefined) {
    SKIP("no encoder available");
  }

  // a file in a directory that does not exist
  const std::string filename = get_tests_output_file_path("no-such-directory/write_to_file.heif");
  REQUIRE_FALSE(std::filesystem::exists(std::filesystem::path(filename).parent_path()));

  WriteResult result = write_to_file(filename, format);
  INFO("error (" << result.code << "/" << result.subcode << "): " << result.message);

  CHECK(result.code == heif_error_Encoding_error);
  CHECK(result.subcode == heif_suberror_Cannot_write_output_data);

  // The message names the file.
  CHECK(result.message.find(filename) != std::string::npos);

  CHECK_FALSE(std::filesystem::exists(filename));
}


TEST_CASE("heif_context_write_to_file reports a failed write")
{
  const heif_compression_format format = pick_encoder_format();
  if (format == heif_compression_undefined) {
    SKIP("no encoder available");
  }

  // The device /dev/full can be opened, and every write to it fails like a write to a full disk.
  const std::string filename = "/dev/full";

  std::error_code error;
  if (!std::filesystem::is_character_file(filename, error)) {
    SKIP("this system has no /dev/full");
  }

  WriteResult result = write_to_file(filename, format);
  INFO("error (" << result.code << "/" << result.subcode << "): " << result.message);

  CHECK(result.code == heif_error_Encoding_error);
  CHECK(result.subcode == heif_suberror_Cannot_write_output_data);
}
