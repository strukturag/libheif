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

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// A grid's tiles decode on the calling thread and on helpers started once for the image. The
// result must not depend on how many there are, and the application's cancel callback must
// only be called on the calling thread.

namespace {

constexpr uint32_t kTile = 32;
// Square, as in encode_grid.cc: heif_context_encode_grid() takes its rows and columns in the
// other order than its header documents.
constexpr uint16_t kCols = 6;
constexpr uint16_t kRows = 6;

std::string encode_grid_file()
{
  heif_encoder* encoder = get_encoder_or_skip_test(heif_compression_uncompressed);
  REQUIRE(encoder != nullptr);

  std::vector<heif_image*> tiles;
  for (uint32_t t = 0; t < uint32_t{kRows} * kCols; ++t) {
    heif_image* tile = nullptr;
    REQUIRE(heif_image_create(kTile, kTile, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &tile).code == heif_error_Ok);
    REQUIRE(heif_image_add_plane(tile, heif_channel_interleaved, kTile, kTile, 8).code == heif_error_Ok);
    size_t stride = 0;
    uint8_t* p = heif_image_get_plane2(tile, heif_channel_interleaved, &stride);
    for (uint32_t y = 0; y < kTile; ++y) {
      for (uint32_t x = 0; x < kTile; ++x) {
        // Each tile differs from the others, so that a tile pasted in the wrong place shows.
        p[y * stride + 3 * x + 0] = static_cast<uint8_t>(t * 8);
        p[y * stride + 3 * x + 1] = static_cast<uint8_t>(x * 7 + y);
        p[y * stride + 3 * x + 2] = static_cast<uint8_t>(y * 7 + x + t);
      }
    }
    tiles.push_back(tile);
  }

  heif_context* ctx = heif_context_alloc();
  heif_image_handle* grid = nullptr;
  REQUIRE(heif_context_encode_grid(ctx, tiles.data(), kRows, kCols, encoder, nullptr, &grid).code == heif_error_Ok);
  std::string path = get_tests_output_file_path("grid_tile_threads.heif");
  REQUIRE(heif_context_write_to_file(ctx, path.c_str()).code == heif_error_Ok);

  heif_image_handle_release(grid);
  for (heif_image* tile : tiles) {
    heif_image_release(tile);
  }
  heif_encoder_release(encoder);
  heif_context_free(ctx);
  return path;
}

struct cancel_record
{
  std::vector<std::thread::id> threads;
  int cancel_at_call = 0; // 0: never cancel
};

int record_cancel_call(void* user_data)
{
  auto* record = static_cast<cancel_record*>(user_data);
  record->threads.push_back(std::this_thread::get_id());
  return record->cancel_at_call != 0 && static_cast<int>(record->threads.size()) >= record->cancel_at_call;
}

// Decodes the grid with `threads` decoding threads into `pixels`; returns the error code.
heif_error_code decode(const std::string& path, int threads, std::vector<uint8_t>& pixels,
                       cancel_record* record = nullptr)
{
  heif_context* ctx = heif_context_alloc();
  REQUIRE(heif_context_read_from_file(ctx, path.c_str(), nullptr).code == heif_error_Ok);
  heif_context_set_max_decoding_threads(ctx, threads);

  heif_image_handle* handle = nullptr;
  REQUIRE(heif_context_get_primary_image_handle(ctx, &handle).code == heif_error_Ok);
  REQUIRE(heif_image_handle_get_width(handle) == kTile * kCols);
  REQUIRE(heif_image_handle_get_height(handle) == kTile * kRows);

  heif_decoding_options* options = heif_decoding_options_alloc();
  if (record) {
    options->cancel_decoding = record_cancel_call;
    options->progress_user_data = record;
  }

  heif_image* img = nullptr;
  heif_error err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, options);
  if (err.code == heif_error_Ok) {
    size_t stride = 0;
    const uint8_t* p = heif_image_get_plane_readonly2(img, heif_channel_interleaved, &stride);
    pixels.clear();
    for (uint32_t y = 0; y < kTile * kRows; ++y) {
      pixels.insert(pixels.end(), p + y * stride, p + y * stride + 3 * kTile * kCols);
    }
    heif_image_release(img);
  }

  heif_decoding_options_free(options);
  heif_image_handle_release(handle);
  heif_context_free(ctx);
  return err.code;
}

} // namespace


TEST_CASE("grid tiles decode to the same image on any number of threads")
{
  const std::string path = encode_grid_file();

  std::vector<uint8_t> reference;
  REQUIRE(decode(path, 0, reference) == heif_error_Ok);
  REQUIRE(reference.size() == size_t{3} * kTile * kCols * kTile * kRows);

  for (int threads : {1, 2, 3, 4, 8, 64}) {
    INFO(threads << " decoding threads");
    std::vector<uint8_t> pixels;
    REQUIRE(decode(path, threads, pixels) == heif_error_Ok);
    REQUIRE(pixels == reference);
  }
}


TEST_CASE("grid tile decoding calls the cancel callback on the calling thread only")
{
  const std::string path = encode_grid_file();

  cancel_record record;
  std::vector<uint8_t> pixels;
  REQUIRE(decode(path, 4, pixels, &record) == heif_error_Ok);
  REQUIRE(!record.threads.empty());
  for (const std::thread::id& id : record.threads) {
    REQUIRE(id == std::this_thread::get_id());
  }

  cancel_record cancelled;
  cancelled.cancel_at_call = 3;
  REQUIRE(decode(path, 4, pixels, &cancelled) == heif_error_Canceled);
  REQUIRE(cancelled.threads.size() == 3);
}
