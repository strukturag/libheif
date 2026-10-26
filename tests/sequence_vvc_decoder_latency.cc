/*
  libheif integration tests for decoding VVC sequences with many decoder threads.

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

// Sequence decoding limits the number of samples that may be pushed into a decoder
// without getting frames back (GHSA-73j7-gvm9-x8q6). vvdec parses pictures ahead before
// it outputs the first one, and by default this parse delay grows with the number of
// threads. With 32 threads, vvdec took 55 samples before its first frame and every
// VVC sequence failed with "exceeded the limit for samples without enough output frames".
// The vvdec plugin now bounds the parse delay. The test passes 32 threads explicitly, so
// that it does not depend on the number of cores of the test machine.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t WIDTH = 64;
constexpr uint32_t HEIGHT = 64;
constexpr int NUM_FRAMES = 48;

heif_error mem_writer(heif_context*, const void* data, size_t size, void* userdata)
{
  auto* out = static_cast<std::vector<uint8_t>*>(userdata);
  const auto* p = static_cast<const uint8_t*>(data);
  out->insert(out->end(), p, p + size);
  return heif_error{heif_error_Ok, heif_suberror_Unspecified, nullptr};
}

bool have_vvdec()
{
  const heif_decoder_descriptor* descriptors[10];
  int n = heif_get_decoder_descriptors(heif_compression_VVC, descriptors, 10);
  for (int i = 0; i < n; i++) {
    if (strcmp(heif_decoder_descriptor_get_id_name(descriptors[i]), "vvdec") == 0) {
      return true;
    }
  }
  return false;
}

heif_image* make_gray_image(uint8_t value)
{
  heif_image* img = nullptr;
  REQUIRE(heif_image_create(WIDTH, HEIGHT, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &img).code == heif_error_Ok);
  REQUIRE(heif_image_add_plane(img, heif_channel_interleaved, WIDTH, HEIGHT, 8).code == heif_error_Ok);

  size_t stride = 0;
  uint8_t* p = heif_image_get_plane2(img, heif_channel_interleaved, &stride);
  REQUIRE(p != nullptr);
  for (uint32_t y = 0; y < HEIGHT; ++y) {
    std::fill(p + y * stride, p + y * stride + WIDTH * 3, value);
  }

  heif_image_set_duration(img, 1);
  return img;
}

std::vector<uint8_t> encode_vvc_sequence()
{
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_encoder* encoder = nullptr;
  REQUIRE(heif_context_get_encoder_for_format(ctx, heif_compression_VVC, &encoder).code == heif_error_Ok);

  heif_track* track = nullptr;
  REQUIRE(heif_context_add_visual_sequence_track(ctx, (uint16_t) WIDTH, (uint16_t) HEIGHT,
                                                 heif_track_type_image_sequence,
                                                 nullptr, nullptr, &track).code == heif_error_Ok);

  heif_sequence_encoding_options* options = heif_sequence_encoding_options_alloc();
  for (int i = 0; i < NUM_FRAMES; ++i) {
    heif_image* img = make_gray_image(static_cast<uint8_t>(i * 5));
    heif_error err = heif_track_encode_sequence_image(track, img, encoder, options);
    INFO("encode_sequence_image: " << (err.message ? err.message : ""));
    REQUIRE(err.code == heif_error_Ok);
    heif_image_release(img);
  }
  REQUIRE(heif_track_encode_end_of_sequence(track, encoder).code == heif_error_Ok);

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

} // namespace


TEST_CASE("VVC sequence decodes with 32 vvdec threads")
{
  if (!heif_have_encoder_for_format(heif_compression_VVC) || !have_vvdec()) {
    SKIP("VVC encoder or vvdec decoder not available");
  }

  std::vector<uint8_t> file = encode_vvc_sequence();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr).code == heif_error_Ok);
  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  heif_decoding_options* options = heif_decoding_options_alloc();
  options->decoder_id = "vvdec";
  options->num_codec_threads = 32;

  int frames = 0;
  for (;;) {
    heif_image* img = nullptr;
    heif_error err = heif_track_decode_next_image(track, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, options);
    if (err.code == heif_error_End_of_sequence) {
      break;
    }
    INFO("frame " << frames << ": " << (err.message ? err.message : ""));
    REQUIRE(err.code == heif_error_Ok);
    heif_image_release(img);
    frames++;
  }

  REQUIRE(frames == NUM_FRAMES);

  heif_decoding_options_free(options);
  heif_track_release(track);
  heif_context_free(ctx);
}
