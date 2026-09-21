/*
  libheif regression test for preserving and mapping sample durations in visual sequences.

  MIT License

  Copyright (c) 2026 libheif contributors

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

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t WIDTH = 64;
constexpr uint32_t HEIGHT = 64;

heif_error mem_writer(heif_context*, const void* data, size_t size, void* userdata)
{
  auto* out = static_cast<std::vector<uint8_t>*>(userdata);
  const auto* p = static_cast<const uint8_t*>(data);
  out->insert(out->end(), p, p + size);
  return heif_error{heif_error_Ok, heif_suberror_Unspecified, nullptr};
}

heif_image* make_rgb_image(uint32_t duration, uint8_t r, uint8_t g, uint8_t b)
{
  heif_image* img = nullptr;
  REQUIRE(heif_image_create(WIDTH, HEIGHT, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &img).code == heif_error_Ok);
  REQUIRE(img != nullptr);

  REQUIRE(heif_image_add_plane(img, heif_channel_interleaved, WIDTH, HEIGHT, 8).code == heif_error_Ok);

  size_t stride = 0;
  uint8_t* p = heif_image_get_plane2(img, heif_channel_interleaved, &stride);
  REQUIRE(p != nullptr);

  for (uint32_t y = 0; y < HEIGHT; ++y) {
    uint8_t* row = p + y * stride;
    for (uint32_t x = 0; x < WIDTH; ++x) {
      row[x * 3 + 0] = r;
      row[x * 3 + 1] = g;
      row[x * 3 + 2] = b;
    }
  }

  heif_image_set_duration(img, duration);
  return img;
}

std::vector<uint8_t> encode_vfr_sequence(const std::vector<uint32_t>& durations, heif_compression_format format)
{
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_encoder* encoder = nullptr;
  REQUIRE(heif_context_get_encoder_for_format(ctx, format, &encoder).code == heif_error_Ok);
  REQUIRE(encoder != nullptr);

  heif_track* track = nullptr;
  heif_error err = heif_context_add_visual_sequence_track(ctx, (uint16_t) WIDTH, (uint16_t) HEIGHT,
                                                          heif_track_type_image_sequence,
                                                          nullptr, nullptr, &track);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(track != nullptr);

  for (size_t i = 0; i < durations.size(); ++i) {
    uint8_t val = static_cast<uint8_t>((i + 1) * 60);
    heif_image* img = make_rgb_image(durations[i], val, val, val);

    err = heif_track_encode_sequence_image(track, img, encoder, nullptr);
    REQUIRE(err.code == heif_error_Ok);
    heif_image_release(img);
  }

  err = heif_track_encode_end_of_sequence(track, encoder);
  REQUIRE(err.code == heif_error_Ok);

  std::vector<uint8_t> file;
  heif_writer writer{};
  writer.writer_api_version = 1;
  writer.write = mem_writer;
  REQUIRE(heif_context_write(ctx, &writer, &file).code == heif_error_Ok);

  heif_encoder_release(encoder);
  heif_context_free(ctx);

  return file;
}

} // namespace

TEST_CASE("variable frame rate sequences preserve and report accurate per-frame durations")
{
  heif_compression_format format = heif_compression_undefined;
  if (heif_have_encoder_for_format(heif_compression_AV1)) {
    format = heif_compression_AV1;
  } else if (heif_have_encoder_for_format(heif_compression_HEVC)) {
    format = heif_compression_HEVC;
  } else {
    SKIP("No AV1 or HEVC encoder available for sequence test.");
  }

  const std::vector<uint32_t> expected_durations = {100, 250, 400};
  std::vector<uint8_t> file = encode_vfr_sequence(expected_durations, format);
  REQUIRE(!file.empty());

  SECTION("decode with native colorspace")
  {
    heif_context* ctx = heif_context_alloc();
    REQUIRE(ctx != nullptr);
    REQUIRE(heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr).code == heif_error_Ok);
    REQUIRE(heif_context_has_sequence(ctx) == 1);

    heif_track* track = heif_context_get_track(ctx, 0);
    REQUIRE(track != nullptr);

    std::vector<uint32_t> decoded_durations;
    for (;;) {
      heif_image* img = nullptr;
      heif_error err = heif_track_decode_next_image(track, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
      if (err.code == heif_error_End_of_sequence) {
        break;
      }
      REQUIRE(err.code == heif_error_Ok);
      REQUIRE(img != nullptr);

      decoded_durations.push_back(heif_image_get_duration(img));
      heif_image_release(img);
    }

    REQUIRE(decoded_durations == expected_durations);
    heif_track_release(track);
    heif_context_free(ctx);
  }

  SECTION("decode with output RGB colorspace conversion")
  {
    heif_context* ctx = heif_context_alloc();
    REQUIRE(ctx != nullptr);
    REQUIRE(heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr).code == heif_error_Ok);

    heif_track* track = heif_context_get_track(ctx, 0);
    REQUIRE(track != nullptr);

    std::vector<uint32_t> decoded_durations;
    for (;;) {
      heif_image* img = nullptr;
      heif_error err = heif_track_decode_next_image(track, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);
      if (err.code == heif_error_End_of_sequence) {
        break;
      }
      REQUIRE(err.code == heif_error_Ok);
      REQUIRE(img != nullptr);

      decoded_durations.push_back(heif_image_get_duration(img));
      heif_image_release(img);
    }

    REQUIRE(decoded_durations == expected_durations);
    heif_track_release(track);
    heif_context_free(ctx);
  }
}
