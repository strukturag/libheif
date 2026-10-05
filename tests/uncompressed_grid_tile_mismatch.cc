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

// The canvas of a 'grid' image takes its planes and bit depths from the first decoded tile,
// and the other tiles are copied into it. Two things went wrong when the tiles of a grid do
// not have the same format (GHSA-vv35-6hxg-95x8):
//
// - The function that copies a tile refuses a tile with another bit depth, but its error was
//   discarded. The tile was left out, its area stayed zero, and decoding reported success.
// - With parallel tile decoding, "the first decoded tile" is the tile of whichever thread
//   finishes first. The format of the decoded image, and which of the tiles was left out,
//   changed from run to run.
//
// The grid item now remembers the description of the first tile that is decoded, and every
// tile that is decoded afterwards has to have the same format: colorspace, chroma format, and
// the same components with the same datatypes and bit depths. A tile that differs is an
// error. Which tile is the first one still depends on timing, but that no longer matters:
// whichever it is, a tile of another format does not match it.
//
// This also covers the interface that decodes single tiles, which has no canvas to compare
// a tile with: the tiles that are decoded from one context are compared with each other.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint32_t TILE_W = 16;
constexpr uint32_t TILE_H = 16;

// component types of 'cmpd'
constexpr uint16_t MONO = 0, Y = 1, CB = 2, CR = 3, R = 4, G = 5, B = 6, A = 7;

struct Component
{
  uint16_t type;
  uint8_t bit_depth; // 8 or 16
};

struct Tile
{
  std::vector<Component> components;
  uint8_t fill; // every byte of the tile data
};


// A 'grid' of 'unci' tiles in one row. 'output_width' is the width of the grid image.
std::vector<uint8_t> build_grid_file(const std::vector<Tile>& tiles, uint32_t output_width)
{
  const uint16_t num_tiles = static_cast<uint16_t>(tiles.size());

  std::vector<uint8_t> ftyp_payload;
  append_fourcc(ftyp_payload, "mif1");
  put_u32_be(ftyp_payload, 0);
  append_fourcc(ftyp_payload, "mif1");
  append_fourcc(ftyp_payload, "heic");
  auto ftyp = make_box("ftyp", ftyp_payload);

  std::vector<uint8_t> hdlr_payload;
  put_u32_be(hdlr_payload, 0);
  append_fourcc(hdlr_payload, "pict");
  put_u32_be(hdlr_payload, 0);
  put_u32_be(hdlr_payload, 0);
  put_u32_be(hdlr_payload, 0);
  hdlr_payload.push_back(0);
  auto hdlr = make_box("hdlr", hdlr_payload, /*full=*/true);

  std::vector<uint8_t> pitm_payload;
  put_u16_be(pitm_payload, 1);
  auto pitm = make_box("pitm", pitm_payload, /*full=*/true);

  // items: 1 = grid, 2... = unci tiles
  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, static_cast<uint16_t>(1 + num_tiles));
  for (uint16_t id = 1; id <= 1 + num_tiles; id++) {
    std::vector<uint8_t> infe_payload;
    put_u16_be(infe_payload, id);
    put_u16_be(infe_payload, 0);
    append_fourcc(infe_payload, id == 1 ? "grid" : "unci");
    append_cstr(infe_payload, "");
    append(iinf_payload, make_box("infe", infe_payload, /*full=*/true, /*version=*/2));
  }
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  std::vector<uint8_t> dimg_payload;
  put_u16_be(dimg_payload, 1); // from the grid
  put_u16_be(dimg_payload, num_tiles);
  for (uint16_t i = 0; i < num_tiles; i++) {
    put_u16_be(dimg_payload, static_cast<uint16_t>(2 + i));
  }
  auto iref = make_box("iref", make_box("dimg", dimg_payload), /*full=*/true);

  // properties: 1 = ispe of the grid, 2 = ispe of a tile, then cmpd and uncC of each tile
  std::vector<uint8_t> grid_ispe_payload;
  put_u32_be(grid_ispe_payload, output_width);
  put_u32_be(grid_ispe_payload, TILE_H);

  std::vector<uint8_t> tile_ispe_payload;
  put_u32_be(tile_ispe_payload, TILE_W);
  put_u32_be(tile_ispe_payload, TILE_H);

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, make_box("ispe", grid_ispe_payload, /*full=*/true));
  append(ipco_payload, make_box("ispe", tile_ispe_payload, /*full=*/true));

  for (const Tile& tile : tiles) {
    const uint32_t num_components = static_cast<uint32_t>(tile.components.size());

    std::vector<uint8_t> cmpd_payload;
    put_u32_be(cmpd_payload, num_components);
    for (const Component& c : tile.components) {
      put_u16_be(cmpd_payload, c.type);
    }

    std::vector<uint8_t> uncC_payload;
    put_u32_be(uncC_payload, 0); // profile
    put_u32_be(uncC_payload, num_components);
    for (uint16_t idx = 0; idx < num_components; idx++) {
      put_u16_be(uncC_payload, idx);                                                   // component_index
      uncC_payload.push_back(static_cast<uint8_t>(tile.components[idx].bit_depth - 1)); // component_bit_depth_minus_one
      uncC_payload.push_back(0);                                                       // component_format (unsigned)
      uncC_payload.push_back(0);                                                       // component_align_size
    }
    uncC_payload.push_back(0);   // sampling_type = no subsampling
    uncC_payload.push_back(0);   // interleave_type = component
    uncC_payload.push_back(0);   // block_size
    uncC_payload.push_back(0);   // flags (big-endian components)
    put_u32_be(uncC_payload, 0); // pixel_size
    put_u32_be(uncC_payload, 0); // row_align_size
    put_u32_be(uncC_payload, 0); // tile_align_size
    put_u32_be(uncC_payload, 0); // num_tile_cols_minus_one
    put_u32_be(uncC_payload, 0); // num_tile_rows_minus_one

    append(ipco_payload, make_box("cmpd", cmpd_payload));
    append(ipco_payload, make_box("uncC", uncC_payload, /*full=*/true));
  }
  auto ipco = make_box("ipco", ipco_payload);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, 1u + num_tiles); // entry_count
  put_u16_be(ipma_payload, 1);              // the grid
  ipma_payload.push_back(1);
  ipma_payload.push_back(0x80 | 1);
  for (uint16_t i = 0; i < num_tiles; i++) {
    put_u16_be(ipma_payload, static_cast<uint16_t>(2 + i));
    ipma_payload.push_back(3);
    ipma_payload.push_back(0x80 | 2);
    ipma_payload.push_back(static_cast<uint8_t>(0x80 | (3 + 2 * i)));
    ipma_payload.push_back(static_cast<uint8_t>(0x80 | (4 + 2 * i)));
  }
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // --- item data, all in 'idat'

  std::vector<std::vector<uint8_t>> item_data;

  std::vector<uint8_t> grid_data;
  grid_data.push_back(0); // version
  grid_data.push_back(0); // flags
  grid_data.push_back(0); // rows_minus_one
  grid_data.push_back(static_cast<uint8_t>(num_tiles - 1)); // columns_minus_one
  put_u16_be(grid_data, static_cast<uint16_t>(output_width));
  put_u16_be(grid_data, TILE_H);
  item_data.push_back(grid_data);

  for (const Tile& tile : tiles) {
    size_t size = 0;
    for (const Component& c : tile.components) {
      size += TILE_W * TILE_H * (c.bit_depth / 8);
    }
    item_data.emplace_back(size, tile.fill);
  }

  std::vector<uint8_t> idat_payload;
  for (const auto& data : item_data) {
    append(idat_payload, data);
  }
  auto idat = make_box("idat", idat_payload);

  std::vector<uint8_t> iloc_payload;
  put_u16_be(iloc_payload, (4 << 12) | (4 << 8)); // offset_size=4, length_size=4
  put_u16_be(iloc_payload, static_cast<uint16_t>(item_data.size()));
  uint32_t offset = 0;
  for (uint16_t i = 0; i < item_data.size(); i++) {
    put_u16_be(iloc_payload, static_cast<uint16_t>(1 + i));
    put_u16_be(iloc_payload, 0x0001); // construction_method=1 (idat)
    put_u16_be(iloc_payload, 0);      // data_reference_index
    put_u16_be(iloc_payload, 1);      // extent_count
    put_u32_be(iloc_payload, offset);
    put_u32_be(iloc_payload, static_cast<uint32_t>(item_data[i].size()));
    offset += static_cast<uint32_t>(item_data[i].size());
  }
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);

  std::vector<uint8_t> meta_payload;
  append(meta_payload, hdlr);
  append(meta_payload, pitm);
  append(meta_payload, iinf);
  append(meta_payload, iref);
  append(meta_payload, iprp);
  append(meta_payload, iloc);
  append(meta_payload, idat);
  auto meta = make_box("meta", meta_payload, /*full=*/true);

  std::vector<uint8_t> file;
  append(file, ftyp);
  append(file, meta);
  return file;
}


// What one decode of the file returned, in a form that can be compared between runs.
struct Outcome
{
  heif_error_code code = heif_error_Ok;
  heif_suberror_code subcode = heif_suberror_Unspecified;
  std::string message;

  bool has_alpha = false;
  int bit_depth = 0;                 // of the first colour plane
  std::vector<uint8_t> first_row;    // first byte of each sample of the first colour plane
  std::vector<uint8_t> alpha_row;    // first byte of each alpha sample

  bool operator==(const Outcome&) const = default;
};


Outcome decode(const std::vector<uint8_t>& file)
{
  Outcome outcome;

  heif_context* ctx = heif_context_alloc();
  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  INFO("read: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
  outcome.code = err.code;
  outcome.subcode = err.subcode;
  outcome.message = err.message ? err.message : "";

  if (img) {
    heif_channel channel = heif_image_has_channel(img, heif_channel_Y) ? heif_channel_Y : heif_channel_R;
    REQUIRE(heif_image_has_channel(img, channel));

    auto first_bytes_of_row = [img](heif_channel ch) {
      size_t stride = 0;
      const uint8_t* p = heif_image_get_plane_readonly2(img, ch, &stride);
      REQUIRE(p != nullptr);
      int w = heif_image_get_width(img, ch);
      int bytes_per_sample = (heif_image_get_bits_per_pixel_range(img, ch) + 7) / 8;
      std::vector<uint8_t> row;
      for (int x = 0; x < w; x++) {
        row.push_back(p[static_cast<size_t>(x) * bytes_per_sample]);
      }
      return row;
    };

    outcome.bit_depth = heif_image_get_bits_per_pixel_range(img, channel);
    outcome.first_row = first_bytes_of_row(channel);

    outcome.has_alpha = heif_image_has_channel(img, heif_channel_Alpha);
    if (outcome.has_alpha) {
      outcome.alpha_row = first_bytes_of_row(heif_channel_Alpha);
    }

    heif_image_release(img);
  }

  heif_image_handle_release(handle);
  heif_context_free(ctx);
  return outcome;
}


// Decodes the file several times, because the tiles are decoded in parallel: the result
// must not depend on which tile is finished first and creates the canvas.
Outcome decode_repeatedly(const std::vector<uint8_t>& file)
{
  Outcome first = decode(file);
  for (int i = 0; i < 20; i++) {
    Outcome again = decode(file);
    INFO("run " << i << ": " << again.message);
    REQUIRE(again == first);
  }
  return first;
}


// Decodes the given tiles of the grid, one after the other, through the tile interface of
// one context.
std::vector<Outcome> decode_tiles(const std::vector<uint8_t>& file, std::initializer_list<uint32_t> tiles_x)
{
  std::vector<Outcome> outcomes;

  heif_context* ctx = heif_context_alloc();
  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);

  for (uint32_t tile_x : tiles_x) {
    Outcome outcome;

    heif_image* img = nullptr;
    err = heif_image_handle_decode_image_tile(handle, &img, heif_colorspace_undefined, heif_chroma_undefined,
                                              nullptr, tile_x, 0);
    outcome.code = err.code;
    outcome.subcode = err.subcode;
    outcome.message = err.message ? err.message : "";

    if (img) {
      heif_channel channel = heif_image_has_channel(img, heif_channel_Y) ? heif_channel_Y : heif_channel_R;
      REQUIRE(heif_image_has_channel(img, channel));

      size_t stride = 0;
      const uint8_t* p = heif_image_get_plane_readonly2(img, channel, &stride);
      REQUIRE(p != nullptr);
      outcome.bit_depth = heif_image_get_bits_per_pixel_range(img, channel);
      int bytes_per_sample = (outcome.bit_depth + 7) / 8;
      for (int x = 0; x < heif_image_get_width(img, channel); x++) {
        outcome.first_row.push_back(p[static_cast<size_t>(x) * bytes_per_sample]);
      }
      outcome.has_alpha = heif_image_has_channel(img, heif_channel_Alpha);

      heif_image_release(img);
    }

    outcomes.push_back(outcome);
  }

  heif_image_handle_release(handle);
  heif_context_free(ctx);
  return outcomes;
}


std::vector<uint8_t> row_of(std::initializer_list<uint8_t> tile_values)
{
  std::vector<uint8_t> row;
  for (uint8_t v : tile_values) {
    row.insert(row.end(), TILE_W, v);
  }
  return row;
}

} // namespace


TEST_CASE("grid with tiles of the same format")
{
  auto file = build_grid_file({{{{MONO, 8}}, 0xAA}, {{{MONO, 8}}, 0xBB}}, 2 * TILE_W);

  Outcome outcome = decode_repeatedly(file);
  INFO("decode: " << outcome.message);
  REQUIRE(outcome.code == heif_error_Ok);
  CHECK(outcome.bit_depth == 8);
  CHECK(outcome.first_row == row_of({0xAA, 0xBB}));
}


TEST_CASE("grid with tiles of different bit depths is refused")
{
  SECTION("8 bit, then 16 bit") {
    auto file = build_grid_file({{{{MONO, 8}}, 0xAA}, {{{MONO, 16}}, 0xBB}}, 2 * TILE_W);

    Outcome outcome = decode_repeatedly(file);
    INFO("decode: " << outcome.message);
    CHECK(outcome.code == heif_error_Invalid_input);
    CHECK(outcome.subcode == heif_suberror_Wrong_tile_image_pixel_depth);
  }

  SECTION("16 bit, then 8 bit") {
    auto file = build_grid_file({{{{MONO, 16}}, 0xAA}, {{{MONO, 8}}, 0xBB}}, 2 * TILE_W);

    Outcome outcome = decode_repeatedly(file);
    INFO("decode: " << outcome.message);
    CHECK(outcome.code == heif_error_Invalid_input);
    CHECK(outcome.subcode == heif_suberror_Wrong_tile_image_pixel_depth);
  }

  SECTION("only one plane differs") {
    auto file = build_grid_file({{{{R, 8}, {G, 8}, {B, 8}}, 0xAA}, {{{R, 8}, {G, 8}, {B, 16}}, 0xBB}}, 2 * TILE_W);

    Outcome outcome = decode_repeatedly(file);
    INFO("decode: " << outcome.message);
    CHECK(outcome.code == heif_error_Invalid_input);
    CHECK(outcome.subcode == heif_suberror_Wrong_tile_image_pixel_depth);
  }
}


TEST_CASE("grid with tiles of different colorspaces is refused")
{
  // RGB and YCbCr 4:4:4 have the same chroma format but no plane in common.
  auto file = build_grid_file({{{{R, 8}, {G, 8}, {B, 8}}, 0xAA}, {{{Y, 8}, {CB, 8}, {CR, 8}}, 0xBB}}, 2 * TILE_W);

  Outcome outcome = decode_repeatedly(file);
  INFO("decode: " << outcome.message);
  CHECK(outcome.code == heif_error_Invalid_input);
  CHECK(outcome.subcode == heif_suberror_Wrong_tile_image_chroma_format);
}


TEST_CASE("grid with tiles that differ in their components is refused")
{
  // Only one of the tiles has an alpha plane. Whether the decoded image had an alpha plane
  // used to depend on which tile was decoded first.
  SECTION("first tile with alpha") {
    auto file = build_grid_file({{{{R, 8}, {G, 8}, {B, 8}, {A, 8}}, 0x40}, {{{R, 8}, {G, 8}, {B, 8}}, 0x80}}, 2 * TILE_W);

    Outcome outcome = decode_repeatedly(file);
    INFO("decode: " << outcome.message);
    CHECK(outcome.code == heif_error_Invalid_input);
    CHECK(outcome.subcode == heif_suberror_Invalid_grid_data);
  }

  SECTION("second tile with alpha") {
    auto file = build_grid_file({{{{R, 8}, {G, 8}, {B, 8}}, 0x40}, {{{R, 8}, {G, 8}, {B, 8}, {A, 8}}, 0x80}}, 2 * TILE_W);

    Outcome outcome = decode_repeatedly(file);
    INFO("decode: " << outcome.message);
    CHECK(outcome.code == heif_error_Invalid_input);
    CHECK(outcome.subcode == heif_suberror_Invalid_grid_data);
  }

  SECTION("all tiles with alpha") {
    auto file = build_grid_file({{{{R, 8}, {G, 8}, {B, 8}, {A, 8}}, 0x40}, {{{R, 8}, {G, 8}, {B, 8}, {A, 8}}, 0x80}}, 2 * TILE_W);

    Outcome outcome = decode_repeatedly(file);
    INFO("decode: " << outcome.message);
    REQUIRE(outcome.code == heif_error_Ok);
    CHECK(outcome.first_row == row_of({0x40, 0x80}));
    REQUIRE(outcome.has_alpha);
    CHECK(outcome.alpha_row == row_of({0x40, 0x80}));
  }
}


TEST_CASE("grid with a tile completely outside of the image")
{
  // Three tiles in a row, but the image is only two tiles wide. The third tile has nothing
  // to contribute. This is not an error.
  auto file = build_grid_file({{{{MONO, 8}}, 0xAA}, {{{MONO, 8}}, 0xBB}, {{{MONO, 8}}, 0xCC}}, 2 * TILE_W);

  Outcome outcome = decode_repeatedly(file);
  INFO("decode: " << outcome.message);
  REQUIRE(outcome.code == heif_error_Ok);
  CHECK(outcome.first_row == row_of({0xAA, 0xBB}));
}


TEST_CASE("decoding single grid tiles refuses a tile that does not have the format of the other tiles")
{
  SECTION("tiles of the same format") {
    auto file = build_grid_file({{{{MONO, 8}}, 0xAA}, {{{MONO, 8}}, 0xBB}}, 2 * TILE_W);

    auto tiles = decode_tiles(file, {0, 1});
    INFO("tile 0: " << tiles[0].message << ", tile 1: " << tiles[1].message);
    REQUIRE(tiles[0].code == heif_error_Ok);
    CHECK(tiles[0].first_row == row_of({0xAA}));
    REQUIRE(tiles[1].code == heif_error_Ok);
    CHECK(tiles[1].first_row == row_of({0xBB}));
  }

  SECTION("another bit depth") {
    auto file = build_grid_file({{{{R, 8}, {G, 8}, {B, 8}}, 0xAA}, {{{R, 8}, {G, 8}, {B, 16}}, 0xBB}}, 2 * TILE_W);

    // The tile that is decoded first is the one the others are compared with.
    for (bool reversed : {false, true}) {
      auto tiles = reversed ? decode_tiles(file, {1, 0}) : decode_tiles(file, {0, 1});
      INFO("reversed=" << reversed << ", first: " << tiles[0].message << ", second: " << tiles[1].message);
      CHECK(tiles[0].code == heif_error_Ok);
      CHECK(tiles[1].code == heif_error_Invalid_input);
      CHECK(tiles[1].subcode == heif_suberror_Wrong_tile_image_pixel_depth);
    }
  }

  SECTION("another colorspace") {
    auto file = build_grid_file({{{{R, 8}, {G, 8}, {B, 8}}, 0xAA}, {{{Y, 8}, {CB, 8}, {CR, 8}}, 0xBB}}, 2 * TILE_W);

    for (bool reversed : {false, true}) {
      auto tiles = reversed ? decode_tiles(file, {1, 0}) : decode_tiles(file, {0, 1});
      INFO("reversed=" << reversed << ", first: " << tiles[0].message << ", second: " << tiles[1].message);
      CHECK(tiles[0].code == heif_error_Ok);
      CHECK(tiles[1].code == heif_error_Invalid_input);
      CHECK(tiles[1].subcode == heif_suberror_Wrong_tile_image_chroma_format);
    }
  }

  SECTION("other components") {
    auto file = build_grid_file({{{{R, 8}, {G, 8}, {B, 8}, {A, 8}}, 0x40}, {{{R, 8}, {G, 8}, {B, 8}}, 0x80}}, 2 * TILE_W);

    for (bool reversed : {false, true}) {
      auto tiles = reversed ? decode_tiles(file, {1, 0}) : decode_tiles(file, {0, 1});
      INFO("reversed=" << reversed << ", first: " << tiles[0].message << ", second: " << tiles[1].message);
      CHECK(tiles[0].code == heif_error_Ok);
      CHECK(tiles[1].code == heif_error_Invalid_input);
      CHECK(tiles[1].subcode == heif_suberror_Invalid_grid_data);
    }
  }

  SECTION("a tile whose bit depth contradicts the image handle") {
    // The handle of the grid reports the bit depth of its first tile (8 bit). The second
    // tile has 16 bit. It is refused even when it is the only tile that is decoded, because
    // a decoded image must not have another bit depth than the handle reports.
    auto file = build_grid_file({{{{MONO, 8}}, 0xAA}, {{{MONO, 16}}, 0xBB}}, 2 * TILE_W);

    auto tiles = decode_tiles(file, {1});
    INFO("tile 1: " << tiles[0].message);
    CHECK(tiles[0].code == heif_error_Invalid_input);

    tiles = decode_tiles(file, {0, 1});
    INFO("tile 0: " << tiles[0].message << ", tile 1: " << tiles[1].message);
    REQUIRE(tiles[0].code == heif_error_Ok);
    CHECK(tiles[0].bit_depth == 8);
    CHECK(tiles[1].code == heif_error_Invalid_input);
    CHECK(tiles[1].subcode == heif_suberror_Wrong_tile_image_pixel_depth);
  }

  SECTION("decoding the whole image after a single tile") {
    auto file = build_grid_file({{{{MONO, 8}}, 0xAA}, {{{MONO, 8}}, 0xBB}}, 2 * TILE_W);

    // The stored description of a tile must not get in the way of later decoding.
    heif_context* ctx = heif_context_alloc();
    heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
    REQUIRE(err.code == heif_error_Ok);
    heif_image_handle* handle = nullptr;
    REQUIRE(heif_context_get_primary_image_handle(ctx, &handle).code == heif_error_Ok);

    for (int round = 0; round < 3; round++) {
      heif_image* img = nullptr;
      err = heif_image_handle_decode_image_tile(handle, &img, heif_colorspace_undefined, heif_chroma_undefined,
                                                nullptr, 1, 0);
      REQUIRE(err.code == heif_error_Ok);
      heif_image_release(img);

      img = nullptr;
      err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
      INFO("decode: " << err.message);
      REQUIRE(err.code == heif_error_Ok);
      heif_image_release(img);
    }

    heif_image_handle_release(handle);
    heif_context_free(ctx);
  }
}
