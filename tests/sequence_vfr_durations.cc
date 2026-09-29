/*
  libheif regression test for the per-frame durations of visual sequences.

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

// Track_Visual::decode_next_image_sample() looked up the duration of a decoded
// frame with the index of the sample that was pushed into the decoder last. That
// index runs ahead of the output by the decoder latency, so every frame reported
// the duration of the next sample and the last frame wrapped around to sample 0:
// a sequence with the durations {100, 250, 400} decoded as {250, 400, 100}.
//
// The test encodes a variable frame rate sequence and checks the durations after
// decoding in the native colorspace and after a conversion to RGB, which creates
// a new image that has to carry the duration over.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "test_utils.h"

#include <algorithm>
#include <cstdint>
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

heif_image* make_gray_image(uint32_t duration, uint8_t value)
{
  heif_image* img = nullptr;
  REQUIRE(heif_image_create(WIDTH, HEIGHT, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &img).code == heif_error_Ok);
  REQUIRE(img != nullptr);

  REQUIRE(heif_image_add_plane(img, heif_channel_interleaved, WIDTH, HEIGHT, 8).code == heif_error_Ok);

  size_t stride = 0;
  uint8_t* p = heif_image_get_plane2(img, heif_channel_interleaved, &stride);
  REQUIRE(p != nullptr);

  for (uint32_t y = 0; y < HEIGHT; ++y) {
    std::fill(p + y * stride, p + y * stride + WIDTH * 3, value);
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
  INFO("add_visual_sequence_track: " << (err.message ? err.message : ""));
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(track != nullptr);

  for (size_t i = 0; i < durations.size(); ++i) {
    heif_image* img = make_gray_image(durations[i], static_cast<uint8_t>((i + 1) * 60));

    err = heif_track_encode_sequence_image(track, img, encoder, nullptr);
    INFO("encode_sequence_image: " << (err.message ? err.message : ""));
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

  heif_track_release(track);
  heif_encoder_release(encoder);
  heif_context_free(ctx);

  return file;
}

std::vector<uint32_t> decode_durations(const std::vector<uint8_t>& file,
                                       heif_colorspace colorspace, heif_chroma chroma)
{
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  REQUIRE(heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr).code == heif_error_Ok);
  REQUIRE(heif_context_has_sequence(ctx) == 1);

  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  std::vector<uint32_t> durations;
  for (;;) {
    heif_image* img = nullptr;
    heif_error err = heif_track_decode_next_image(track, &img, colorspace, chroma, nullptr);
    if (err.code == heif_error_End_of_sequence) {
      break;
    }
    INFO("decode_next_image: " << (err.message ? err.message : ""));
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(img != nullptr);

    durations.push_back(heif_image_get_duration(img));
    heif_image_release(img);
  }

  heif_track_release(track);
  heif_context_free(ctx);

  return durations;
}

} // namespace


TEST_CASE("variable frame rate sequence reports the per-frame durations")
{
  heif_compression_format format = heif_compression_undefined;
  for (heif_compression_format candidate : {heif_compression_AV1, heif_compression_HEVC}) {
    if (heif_have_encoder_for_format(candidate) && heif_have_decoder_for_format(candidate)) {
      format = candidate;
      break;
    }
  }
  if (format == heif_compression_undefined) {
    SKIP("No AV1 or HEVC encoder/decoder pair available, skipping test");
  }

  const std::vector<uint32_t> expected_durations = {100, 250, 400};
  std::vector<uint8_t> file = encode_vfr_sequence(expected_durations, format);
  REQUIRE(!file.empty());

  CHECK(decode_durations(file, heif_colorspace_undefined, heif_chroma_undefined) == expected_durations);
  CHECK(decode_durations(file, heif_colorspace_RGB, heif_chroma_interleaved_RGB) == expected_durations);
}
