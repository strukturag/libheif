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
#include "sequence_file_builder.h"

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

std::vector<uint8_t> encode_vfr_sequence(const std::vector<uint32_t>& durations, heif_compression_format format,
                                         bool intra_only = false, uint32_t repetitions = 1)
{
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  heif_context_set_number_of_sequence_repetitions(ctx, repetitions);

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

  heif_sequence_encoding_options* options = heif_sequence_encoding_options_alloc();
  REQUIRE(options != nullptr);
  if (intra_only) {
    options->gop_structure = heif_sequence_gop_structure_intra_only;
  }
  for (size_t i = 0; i < durations.size(); ++i) {
    heif_image* img = make_gray_image(durations[i], static_cast<uint8_t>((i + 1) * 60));

    err = heif_track_encode_sequence_image(track, img, encoder, options);
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

  heif_sequence_encoding_options_release(options);
  heif_track_release(track);
  heif_encoder_release(encoder);
  heif_context_free(ctx);

  return file;
}

std::vector<uint32_t> decode_durations(const std::vector<uint8_t>& file,
                                       heif_colorspace colorspace, heif_chroma chroma,
                                       bool check_pixels = false, bool ignore_editlist = false)
{
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  REQUIRE(heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr).code == heif_error_Ok);
  REQUIRE(heif_context_has_sequence(ctx) == 1);

  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  heif_decoding_options* options = heif_decoding_options_alloc();
  REQUIRE(options != nullptr);
  options->ignore_sequence_editlist = ignore_editlist;
  std::vector<uint32_t> durations;
  for (;;) {
    heif_image* img = nullptr;
    heif_error err = heif_track_decode_next_image(track, &img, colorspace, chroma, options);
    if (err.code == heif_error_End_of_sequence) {
      break;
    }
    INFO("decode_next_image: " << (err.message ? err.message : ""));
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(img != nullptr);

    if (check_pixels) {
      size_t stride = 0;
      const auto* pixels = heif_image_get_plane_readonly2(img, heif_channel_interleaved, &stride);
      REQUIRE(pixels != nullptr);
      const int expected = static_cast<int>((durations.size() % 3 + 1) * 60);
      for (size_t channel = 0; channel < 3; ++channel) {
        CHECK(pixels[channel] >= expected - 3);
        CHECK(pixels[channel] <= expected + 3);
      }
    }
    durations.push_back(heif_image_get_duration(img));
    heif_image_release(img);
  }

  heif_decoding_options_free(options);
  heif_track_release(track);
  heif_context_free(ctx);

  return durations;
}

} // namespace

static uint32_t sequence_box_u32(const std::vector<uint8_t>& data, size_t offset)
{
  REQUIRE(offset + 4 <= data.size());
  return (static_cast<uint32_t>(data[offset]) << 24) |
         (static_cast<uint32_t>(data[offset + 1]) << 16) |
         (static_cast<uint32_t>(data[offset + 2]) << 8) | data[offset + 3];
}

static std::vector<uint8_t> split_sequence_chunks(const std::vector<uint8_t>& data,
                                                  bool different_descriptions,
                                                  uint32_t& first_sample_size)
{
  std::vector<uint8_t> output;
  for (size_t offset = 0; offset < data.size();) {
    const uint32_t size = sequence_box_u32(data, offset);
    REQUIRE(size >= 8);
    REQUIRE(size <= data.size() - offset);
    const std::string type(data.begin() + offset + 4, data.begin() + offset + 8);
    std::vector<uint8_t> payload(data.begin() + offset + 8, data.begin() + offset + size);
    if (type == "moov" || type == "trak" || type == "mdia" || type == "minf" || type == "stbl") {
      // Read stsz before rewriting stco, regardless of their order in the file.
      if (type == "stbl") {
        for (size_t child = 0; child < payload.size();) {
          const uint32_t child_size = sequence_box_u32(payload, child);
          REQUIRE(child_size >= 8);
          REQUIRE(child_size <= payload.size() - child);
          if (std::string(payload.begin() + child + 4, payload.begin() + child + 8) == "stsz") {
            REQUIRE(sequence_box_u32(payload, child + 16) == 3);
            first_sample_size = sequence_box_u32(payload, child + 12);
            if (first_sample_size == 0) {
              first_sample_size = sequence_box_u32(payload, child + 20);
            }
          }
          child += child_size;
        }
      }
      payload = split_sequence_chunks(payload, different_descriptions, first_sample_size);
    }
    else if (type == "stsd" && different_descriptions) {
      REQUIRE(sequence_box_u32(payload, 4) == 1);
      const std::vector<uint8_t> entry(payload.begin() + 8, payload.end());
      payload[7] = 2;
      seqfile::append(payload, entry);
    }
    else if (type == "stsc") {
      payload.clear();
      for (uint32_t value : {0u, 2u, 1u, 1u, 1u, 2u, 2u, different_descriptions ? 2u : 1u}) {
        seqfile::put32(payload, value);
      }
    }
    else if (type == "stco") {
      REQUIRE(sequence_box_u32(payload, 4) == 1);
      REQUIRE(first_sample_size > 0);
      const uint32_t first_offset = sequence_box_u32(payload, 8);
      payload.clear();
      for (uint32_t value : {0u, 2u, first_offset, first_offset + first_sample_size}) {
        seqfile::put32(payload, value);
      }
    }
    seqfile::append(output, seqfile::box(type.c_str(), payload));
    offset += size;
  }
  return output;
}

static std::vector<uint8_t> make_two_chunk_sequence(std::vector<uint8_t> file,
                                                    bool different_descriptions)
{
  for (size_t offset = 0; offset < file.size();) {
    const uint32_t size = sequence_box_u32(file, offset);
    REQUIRE(size >= 8);
    REQUIRE(size <= file.size() - offset);
    if (std::string(file.begin() + offset + 4, file.begin() + offset + 8) == "moov") {
      const std::vector<uint8_t> movie(file.begin() + offset, file.begin() + offset + size);
      uint32_t first_sample_size = 0;
      const auto replacement = split_sequence_chunks(movie, different_descriptions, first_sample_size);
      // Keep media offsets unchanged by replacing the old movie with a free box.
      std::copy_n("free", 4, file.begin() + offset + 4);
      std::fill(file.begin() + offset + 8, file.begin() + offset + size, 0);
      seqfile::append(file, replacement);
      return file;
    }
    offset += size;
  }
  FAIL("Encoded sequence has no movie box");
  return {};
}


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


TEST_CASE("sequence decoding drains all frames across chunk decoder changes")
{
  if (!heif_have_encoder_for_format(heif_compression_AV1) ||
      !heif_have_decoder_for_format(heif_compression_AV1)) {
    SKIP("No AV1 encoder/decoder pair available");
  }
  const bool different_descriptions = GENERATE(false, true);
  const uint32_t repetitions = GENERATE(1u, 2u);
  const std::vector<uint32_t> durations = {100, 250, 400};
  const auto file = make_two_chunk_sequence(
    encode_vfr_sequence(durations, heif_compression_AV1, true, repetitions), different_descriptions);
  std::vector<uint32_t> expected;
  for (uint32_t i = 0; i < repetitions; ++i) {
    expected.insert(expected.end(), durations.begin(), durations.end());
  }
  CHECK(decode_durations(file, heif_colorspace_undefined, heif_chroma_undefined) == expected);
  CHECK(decode_durations(file, heif_colorspace_RGB, heif_chroma_interleaved_RGB, true) == expected);
  CHECK(decode_durations(file, heif_colorspace_RGB, heif_chroma_interleaved_RGB, true, true) == durations);
}
