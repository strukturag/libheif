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

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_tiling.h"
#include "test_utils.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

// Regression coverage for heif_context_encode_grid().
//
// Two distinct bugs are exercised by this test:
//
//   1) Encoder reuse across tiles. The grid encoder feeds every tile to the
//      same plugin encoder instance. Six plugins (aom, kvazaar, rav1e, svt,
//      uvg266, vvenc) did not reset their internal codec context between
//      tiles, so the second tile asserted/aborted (issue #1827; fixed by
//      PR #1732's *_start_sequence_encoding_intern destroy-on-reuse pattern).
//
//   2) HeifPixelImage geometry drift across resize. heif_image_extract_area()
//      builds an edge tile by cloning at the truncated (minW,minH) size and
//      then calling extend_to_size_with_zero(w,h) to pad back to a full tile.
//      Both create_clone_image_at_new_size and extend_to_size_with_zero must
//      keep the ComponentDescription geometry in sync with the plane sizes;
//      otherwise the uncompressed encoder mixes two dimension sources
//      (compute uses get_width(), the copy loop uses get_component_width())
//      and emits a truncated bitstream for edge tiles, decoding to padding
//      (visible as green stripes on the bottom row / right column).
//
// We use a 451x461 source so that cutting into 128x128 tiles produces a
// 4x4 grid in which the rightmost column (width 67) and bottom row
// (height 77) hit the extend_to_size_with_zero() padding path. The image
// is created as interleaved RGB because heif_context_encode_grid's
// implementation reads tile_width via tiles[0]->get_width(heif_channel_interleaved)
// — that grid-encoder assumption is unrelated to the bugs under test here.

namespace {

constexpr uint32_t kSourceWidth  = 451;
constexpr uint32_t kSourceHeight = 461;
constexpr uint32_t kTileSize     = 128;
constexpr uint16_t kCols = (kSourceWidth  + kTileSize - 1) / kTileSize;  // 4
constexpr uint16_t kRows = (kSourceHeight + kTileSize - 1) / kTileSize;  // 4
constexpr int kChannels = 3;  // RGB interleaved

// Smooth gradients — easy for lossy codecs to roundtrip with small error,
// yet still clearly distinct from the zero/grey padding fill that the
// extend_to_size_with_zero() path would produce on a regression.
uint8_t pattern_R(uint32_t x, uint32_t /*y*/) { return static_cast<uint8_t>(30u + x * 200u / kSourceWidth); }
uint8_t pattern_G(uint32_t /*x*/, uint32_t y) { return static_cast<uint8_t>(30u + y * 200u / kSourceHeight); }
uint8_t pattern_B(uint32_t /*x*/, uint32_t /*y*/) { return 128u; }


heif_image* create_source_image()
{
  heif_image* img = nullptr;
  heif_error err = heif_image_create(kSourceWidth, kSourceHeight,
                                     heif_colorspace_RGB,
                                     heif_chroma_interleaved_RGB, &img);
  REQUIRE(err.code == heif_error_Ok);

  REQUIRE(heif_image_add_plane(img, heif_channel_interleaved,
                               kSourceWidth, kSourceHeight, 8).code == heif_error_Ok);

  size_t stride = 0;
  uint8_t* p = heif_image_get_plane2(img, heif_channel_interleaved, &stride);
  for (uint32_t y = 0; y < kSourceHeight; ++y) {
    uint8_t* row = p + y * stride;
    for (uint32_t x = 0; x < kSourceWidth; ++x) {
      row[kChannels * x + 0] = pattern_R(x, y);
      row[kChannels * x + 1] = pattern_G(x, y);
      row[kChannels * x + 2] = pattern_B(x, y);
    }
  }

  return img;
}


// Extract the source into a kRows x kCols grid of kTileSize tiles via the
// public heif_image_extract_area API. Tiles on the right column / bottom row
// are partial and exercise the extend_to_size_with_zero() padding path.
std::vector<heif_image*> extract_tiles(const heif_image* src)
{
  std::vector<heif_image*> tiles;
  tiles.reserve(kRows * kCols);
  for (uint32_t ty = 0; ty < kRows; ++ty) {
    for (uint32_t tx = 0; tx < kCols; ++tx) {
      heif_image* tile = nullptr;
      heif_error err = heif_image_extract_area(src,
                                               tx * kTileSize, ty * kTileSize,
                                               kTileSize, kTileSize,
                                               nullptr, &tile);
      REQUIRE(err.code == heif_error_Ok);
      REQUIRE(tile != nullptr);
      tiles.push_back(tile);
    }
  }
  return tiles;
}


void check_pixel(const uint8_t* plane, size_t stride, int n_channels,
                 uint32_t x, uint32_t y, int channel,
                 uint8_t expected, int tolerance)
{
  int got = plane[y * stride + n_channels * x + channel];
  int diff = std::abs(got - static_cast<int>(expected));
  INFO("at (" << x << "," << y << ") ch=" << channel
              << ": expected=" << static_cast<int>(expected) << " got=" << got);
  REQUIRE(diff <= tolerance);
}


void run_encode_grid_roundtrip(heif_compression_format format,
                               int tolerance,
                               const char* output_filename)
{
  heif_encoder* encoder = get_encoder_or_skip_test(format);
  REQUIRE(encoder != nullptr);

  // Higher quality keeps lossy spread within `tolerance`. For uncompressed
  // this returns an unsupported-parameter error which we ignore — uncompressed
  // is bit-exact regardless of the quality setting.
  heif_encoder_set_lossy_quality(encoder, 90);

  heif_image* src = create_source_image();
  REQUIRE(src != nullptr);

  std::vector<heif_image*> tiles = extract_tiles(src);
  REQUIRE(tiles.size() == static_cast<size_t>(kRows) * kCols);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  // The grid input here is symmetric (kRows == kCols == 4) so the rows/cols
  // argument-order ambiguity between the public header and the .cc impl
  // does not affect this test.
  heif_image_handle* grid_handle = nullptr;
  heif_error err = heif_context_encode_grid(ctx, tiles.data(),
                                            kRows, kCols,
                                            encoder, nullptr, &grid_handle);
  // Regression for #1827: pre-fix, the AOM/Kvazaar/Rav1e/SVT/uvg266/vvenc
  // plugins aborted (SIGABRT) on the second tile, so this line never returned.
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(grid_handle != nullptr);

  std::string out_path = get_tests_output_file_path(output_filename);
  REQUIRE(heif_context_write_to_file(ctx, out_path.c_str()).code == heif_error_Ok);

  heif_image_handle_release(grid_handle);
  for (heif_image* t : tiles) {
    heif_image_release(t);
  }
  heif_encoder_release(encoder);
  heif_context_free(ctx);
  heif_image_release(src);

  // --- read back & decode

  heif_context* rctx = heif_context_alloc();
  REQUIRE(heif_context_read_from_file(rctx, out_path.c_str(), nullptr).code == heif_error_Ok);

  heif_image_handle* rhandle = nullptr;
  REQUIRE(heif_context_get_primary_image_handle(rctx, &rhandle).code == heif_error_Ok);

  heif_image* dec = nullptr;
  REQUIRE(heif_decode_image(rhandle, &dec,
                            heif_colorspace_RGB, heif_chroma_interleaved_RGB,
                            nullptr).code == heif_error_Ok);
  REQUIRE(dec != nullptr);

  size_t dec_stride = 0;
  const uint8_t* dec_plane = heif_image_get_plane_readonly2(dec, heif_channel_interleaved, &dec_stride);
  REQUIRE(dec_plane != nullptr);

  // Sample points: interior, right-column edge, bottom-row edge, corner.
  // Pre-fix the edge points decoded to padding (zero) for the uncompressed
  // codec because the per-tile bitstream was truncated.
  const std::pair<uint32_t, uint32_t> sample_points[] = {
      { 10u,  10u},   // first tile, interior
      {200u, 200u},   // middle tile, interior
      {448u, 100u},   // right column tile
      {448u, 200u},   // right column tile
      {100u, 458u},   // bottom row tile
      {200u, 458u},   // bottom row tile
      {448u, 458u},   // bottom-right corner tile
  };

  for (const auto& pt : sample_points) {
    uint32_t x = pt.first;
    uint32_t y = pt.second;
    REQUIRE(x < kSourceWidth);
    REQUIRE(y < kSourceHeight);

    check_pixel(dec_plane, dec_stride, kChannels, x, y, /*ch=*/0, pattern_R(x, y), tolerance);
    check_pixel(dec_plane, dec_stride, kChannels, x, y, /*ch=*/1, pattern_G(x, y), tolerance);
    check_pixel(dec_plane, dec_stride, kChannels, x, y, /*ch=*/2, pattern_B(x, y), tolerance);
  }

  heif_image_release(dec);
  heif_image_handle_release(rhandle);
  heif_context_free(rctx);
}

}  // namespace


TEST_CASE("heif_context_encode_grid roundtrip - uncompressed",
          "[heif_context_encode_grid]")
{
  // Uncompressed is bit-exact: edge-tile pixels must match the source.
  // Pre-fix (extend_to_size_with_zero not syncing ComponentDescriptions),
  // the bottom-row / right-column tiles decoded to padding values instead
  // of the source gradient — these REQUIREs would fail.
  run_encode_grid_roundtrip(heif_compression_uncompressed, /*tolerance=*/0,
                            "encode_grid_uncompressed.heif");
}


TEST_CASE("heif_context_encode_grid roundtrip - HEVC",
          "[heif_context_encode_grid]")
{
  // Lossy codec: primarily catches "second tile aborts" reuse regressions
  // for x265/kvazaar. Pixel tolerance is generous since RGB→YCbCr→RGB at
  // lossy quality naturally spreads.
  run_encode_grid_roundtrip(heif_compression_HEVC, /*tolerance=*/15,
                            "encode_grid_hevc.heif");
}


TEST_CASE("heif_context_encode_grid roundtrip - AV1",
          "[heif_context_encode_grid]")
{
  // Lossy codec: regression check for the aom/svt/rav1e reuse abort
  // (issue #1827). Pre-fix, the encode call never returned for the second
  // tile, so reaching the post-encode REQUIRE was impossible.
  run_encode_grid_roundtrip(heif_compression_AV1, /*tolerance=*/15,
                            "encode_grid_av1.heif");
}


// -----------------------------------------------------------------------------
// Tile-by-tile grid encoding via heif_context_add_grid_image() +
// heif_context_add_image_tile() — the API path heif-enc uses for --cut-tiles.
// Uses YCbCr 4:2:0 planar tiles produced by heif_image_extract_area(); this
// exercises the unc encoder's component_interleave variant, which sizes its
// output buffer from get_width() but copies via get_component_width() — the
// exact pattern that regressed when extend_to_size_with_zero() failed to
// keep ComponentDescriptions in sync with extended planes.
// -----------------------------------------------------------------------------

namespace {

heif_image* create_source_image_YCbCr_420()
{
  heif_image* img = nullptr;
  REQUIRE(heif_image_create(kSourceWidth, kSourceHeight,
                            heif_colorspace_YCbCr, heif_chroma_420, &img).code == heif_error_Ok);

  REQUIRE(heif_image_add_plane(img, heif_channel_Y,
                               kSourceWidth, kSourceHeight, 8).code == heif_error_Ok);
  REQUIRE(heif_image_add_plane(img, heif_channel_Cb,
                               (kSourceWidth + 1) / 2, (kSourceHeight + 1) / 2, 8).code == heif_error_Ok);
  REQUIRE(heif_image_add_plane(img, heif_channel_Cr,
                               (kSourceWidth + 1) / 2, (kSourceHeight + 1) / 2, 8).code == heif_error_Ok);

  // Smooth gradients in Y and constant chroma — bit-exact through the
  // uncompressed encoder, and clearly distinct from the zero-fill (Y=0,
  // Cb=Cr=128) that extend_to_size_with_zero() would expose on regression.
  size_t stride = 0;
  uint8_t* p = heif_image_get_plane2(img, heif_channel_Y, &stride);
  for (uint32_t y = 0; y < kSourceHeight; ++y) {
    for (uint32_t x = 0; x < kSourceWidth; ++x) {
      p[y * stride + x] = static_cast<uint8_t>(30u + x * 200u / kSourceWidth);
    }
  }
  p = heif_image_get_plane2(img, heif_channel_Cb, &stride);
  for (uint32_t y = 0; y < (kSourceHeight + 1) / 2; ++y) {
    for (uint32_t x = 0; x < (kSourceWidth + 1) / 2; ++x) {
      p[y * stride + x] = 80u;  // distinctly not the neutral 128 fill
    }
  }
  p = heif_image_get_plane2(img, heif_channel_Cr, &stride);
  for (uint32_t y = 0; y < (kSourceHeight + 1) / 2; ++y) {
    for (uint32_t x = 0; x < (kSourceWidth + 1) / 2; ++x) {
      p[y * stride + x] = 200u;  // distinctly not the neutral 128 fill
    }
  }

  return img;
}

}  // namespace

TEST_CASE("grid encoding tile-by-tile - uncompressed YCbCr 4:2:0 (extract_area edge padding)",
          "[heif_context_encode_grid][heif_context_add_image_tile]")
{
  // This test specifically targets bug #2 (extend_to_size_with_zero failing
  // to update ComponentDescriptions). When the bug is present, the unc
  // encoder's component_interleave variant produces a truncated bitstream
  // for edge tiles and the bottom-row / right-column tiles decode to padding
  // values (Y=0, Cb=Cr=128 grey) instead of the source content.

  heif_encoder* encoder = get_encoder_or_skip_test(heif_compression_uncompressed);
  REQUIRE(encoder != nullptr);

  heif_image* src = create_source_image_YCbCr_420();
  REQUIRE(src != nullptr);

  std::vector<heif_image*> tiles = extract_tiles(src);
  REQUIRE(tiles.size() == static_cast<size_t>(kRows) * kCols);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  // Build the grid item with an explicit overall image size — this path
  // sidesteps the heif_context_encode_grid limitation of reading tile size
  // via get_width(heif_channel_interleaved) (which would be 0 for planar
  // YCbCr tiles).
  heif_encoding_options* enc_options = heif_encoding_options_alloc();
  REQUIRE(enc_options != nullptr);

  heif_image_handle* grid_handle = nullptr;
  REQUIRE(heif_context_add_grid_image(ctx,
                                      kCols * kTileSize, kRows * kTileSize,
                                      kCols, kRows,
                                      enc_options, &grid_handle).code == heif_error_Ok);
  REQUIRE(grid_handle != nullptr);

  for (uint32_t ty = 0; ty < kRows; ++ty) {
    for (uint32_t tx = 0; tx < kCols; ++tx) {
      heif_image* tile = tiles[ty * kCols + tx];
      heif_error err = heif_context_add_image_tile(ctx, grid_handle, tx, ty, tile, encoder);
      REQUIRE(err.code == heif_error_Ok);
    }
  }

  std::string out_path = get_tests_output_file_path("encode_grid_tile_by_tile.heif");
  REQUIRE(heif_context_write_to_file(ctx, out_path.c_str()).code == heif_error_Ok);

  heif_image_handle_release(grid_handle);
  heif_encoding_options_free(enc_options);
  for (heif_image* t : tiles) {
    heif_image_release(t);
  }
  heif_encoder_release(encoder);
  heif_context_free(ctx);
  heif_image_release(src);

  // --- read back & decode

  heif_context* rctx = heif_context_alloc();
  REQUIRE(heif_context_read_from_file(rctx, out_path.c_str(), nullptr).code == heif_error_Ok);

  heif_image_handle* rhandle = nullptr;
  REQUIRE(heif_context_get_primary_image_handle(rctx, &rhandle).code == heif_error_Ok);

  heif_image* dec = nullptr;
  REQUIRE(heif_decode_image(rhandle, &dec,
                            heif_colorspace_YCbCr, heif_chroma_420,
                            nullptr).code == heif_error_Ok);
  REQUIRE(dec != nullptr);

  size_t stride_y = 0, stride_cb = 0, stride_cr = 0;
  const uint8_t* dec_y  = heif_image_get_plane_readonly2(dec, heif_channel_Y,  &stride_y);
  const uint8_t* dec_cb = heif_image_get_plane_readonly2(dec, heif_channel_Cb, &stride_cb);
  const uint8_t* dec_cr = heif_image_get_plane_readonly2(dec, heif_channel_Cr, &stride_cr);
  REQUIRE(dec_y  != nullptr);
  REQUIRE(dec_cb != nullptr);
  REQUIRE(dec_cr != nullptr);

  // Verify edge tiles bit-exact. Pre-fix, sampling within an edge tile
  // beyond the original-source rectangle would expose the truncated-
  // bitstream behaviour; even inside the source rectangle, the decoder
  // would supply padding bytes once the encoded tile data ran out.
  const std::pair<uint32_t, uint32_t> sample_points[] = {
      { 10u,  10u},   // first tile (interior)
      {200u, 200u},   // middle tile (interior)
      {448u, 100u},   // right-column tile (x near right edge)
      {448u, 200u},   // right-column tile
      {100u, 458u},   // bottom-row tile (y near bottom edge)
      {200u, 458u},   // bottom-row tile
      {448u, 458u},   // bottom-right corner tile
  };

  for (const auto& pt : sample_points) {
    uint32_t x = pt.first;
    uint32_t y = pt.second;
    REQUIRE(x < kSourceWidth);
    REQUIRE(y < kSourceHeight);

    uint8_t expected_Y = static_cast<uint8_t>(30u + x * 200u / kSourceWidth);
    INFO("Y at (" << x << "," << y << ")");
    REQUIRE(dec_y[y * stride_y + x] == expected_Y);

    uint32_t cx = x / 2;
    uint32_t cy = y / 2;
    INFO("Cb at (" << cx << "," << cy << ")");
    REQUIRE(dec_cb[cy * stride_cb + cx] == 80u);
    INFO("Cr at (" << cx << "," << cy << ")");
    REQUIRE(dec_cr[cy * stride_cr + cx] == 200u);
  }

  heif_image_release(dec);
  heif_image_handle_release(rhandle);
  heif_context_free(rctx);
}
