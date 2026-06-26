/*
  libheif regression test for requesting a track from a context without sequence tracks.

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

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "test_utils.h"

#include <cstdint>
#include <vector>

namespace {

// Sequence API queries that must work on a context that holds no sequence
// tracks (a still image, or no image at all). heif_context_get_track() is
// documented to return nullptr on failure; it must not abort/crash. This
// formerly tripped assert(has_sequence()) in HeifContext::get_track().
// See https://github.com/strukturag/libheif/issues/1844.
void check_no_sequence_apis(heif_context* ctx)
{
  REQUIRE(heif_context_has_sequence(ctx) == 0);
  REQUIRE(heif_context_number_of_sequence_tracks(ctx) == 0);

  // Listing track IDs must work (and write nothing) when there are no tracks.
  heif_context_get_track_ids(ctx, nullptr);

  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track == nullptr);
}

heif_error mem_writer(heif_context*, const void* data, size_t size, void* userdata)
{
  auto* out = static_cast<std::vector<uint8_t>*>(userdata);
  const auto* p = static_cast<const uint8_t*>(data);
  out->insert(out->end(), p, p + size);
  return heif_error{heif_error_Ok, heif_suberror_Unspecified, nullptr};
}

}

TEST_CASE("get_track on context without sequence returns nullptr")
{
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  // Fresh context, nothing loaded: no sequence tracks present.
  check_no_sequence_apis(ctx);

  heif_context_free(ctx);
}

TEST_CASE("get_track on a still-image file returns nullptr")
{
  // Encode a tiny still image to an in-memory HEIF file, then read it back.
  // A still image is a perfectly valid file that contains no sequence tracks.
  heif_image* img = nullptr;
  REQUIRE(heif_image_create(16, 16, heif_colorspace_YCbCr, heif_chroma_420, &img).code == heif_error_Ok);
  fill_new_plane(img, heif_channel_Y, 16, 16);
  fill_new_plane(img, heif_channel_Cb, 8, 8);
  fill_new_plane(img, heif_channel_Cr, 8, 8);

  heif_encoder* enc = get_encoder_or_skip_test(heif_compression_HEVC);

  heif_context* enc_ctx = heif_context_alloc();
  REQUIRE(heif_context_encode_image(enc_ctx, img, enc, nullptr, nullptr).code == heif_error_Ok);

  std::vector<uint8_t> file;
  heif_writer writer{};
  writer.writer_api_version = 1;
  writer.write = mem_writer;
  REQUIRE(heif_context_write(enc_ctx, &writer, &file).code == heif_error_Ok);

  heif_encoder_release(enc);
  heif_context_free(enc_ctx);
  heif_image_release(img);

  // Read the still image back and query the sequence API on it.
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  REQUIRE(heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr).code == heif_error_Ok);

  check_no_sequence_apis(ctx);

  heif_context_free(ctx);
}
