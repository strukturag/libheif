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

// Regression tests for GHSA-x8xm-cm2c-cfc8: derived-image (grid/iovl/iden)
// reference chains could decode a shared base image an unbounded number of
// times. Because the cycle-detection set is copied per recursion path, a shared
// subtree is re-decoded once per path that reaches it, and nested references
// make the number of decodes grow as branch^depth. A tiny file could thus pin a
// CPU for minutes (linear amplification) or effectively forever (exponential,
// via nested overlays). These tests build the amplification structures out of
// 'iovl' overlays over an 'mski' base (which needs no codec plugin) and check
// that decoding is rejected quickly instead of blowing up.
//
// The number of decodes alone does not bound the work either: what a reference
// uses of the decoded image can be much smaller than the image (a 'clap' crop).
// The last tests check that the total size of the decoded images is bounded, too.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

// ImageOverlay payload (ISO/IEC 23008-12): version, flags, 4x background color,
// canvas width/height, and one (x,y) offset per composited image. flags=0 uses
// 16-bit fields. All composited images are placed at (0,0) here.
std::vector<uint8_t> make_overlay_spec(uint16_t canvas_w, uint16_t canvas_h, size_t num_images) {
  std::vector<uint8_t> s;
  s.push_back(0);           // version
  s.push_back(0);           // flags (16-bit fields)
  for (int i = 0; i < 4; i++) { put_u16_be(s, 0); }  // background color RGBA
  put_u16_be(s, canvas_w);
  put_u16_be(s, canvas_h);
  for (size_t i = 0; i < num_images; i++) {
    put_u16_be(s, 0);       // x offset
    put_u16_be(s, 0);       // y offset
  }
  return s;
}

// ImageGrid payload (ISO/IEC 23008-12) with 16-bit fields.
std::vector<uint8_t> make_grid_spec(uint8_t rows, uint8_t columns, uint16_t width, uint16_t height) {
  std::vector<uint8_t> s;
  s.push_back(0);           // version
  s.push_back(0);           // flags (16-bit fields)
  s.push_back(static_cast<uint8_t>(rows - 1));
  s.push_back(static_cast<uint8_t>(columns - 1));
  put_u16_be(s, width);
  put_u16_be(s, height);
  return s;
}

struct Item {
  uint16_t id = 0;
  std::string type;                 // "mski", "iovl", "iden", "grid"
  std::vector<uint16_t> dimg;       // 'dimg' references (iovl/iden/grid)
  std::vector<uint8_t> data;        // idat payload (mski pixels / iovl or grid spec); empty for iden
  uint32_t width = 8;               // 'ispe' size
  uint32_t height = 8;
  bool crop_to_1x1 = false;         // add a 'clap' that keeps a single pixel
};

// Assemble a minimal, self-contained HEIF file from an explicit item list. Every
// item carries an 'ispe'; the single 'mski' base additionally carries a
// 'mskC'. Items with data store it in 'idat' (construction_method 1).
std::vector<uint8_t> build_file(const std::vector<Item>& items, uint16_t primary_id) {
  std::vector<uint8_t> ftyp_payload;
  append_fourcc(ftyp_payload, "heic");
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
  put_u16_be(pitm_payload, primary_id);
  auto pitm = make_box("pitm", pitm_payload, /*full=*/true);

  // iinf
  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, static_cast<uint16_t>(items.size()));
  for (const auto& it : items) {
    std::vector<uint8_t> infe_payload;
    put_u16_be(infe_payload, it.id);
    put_u16_be(infe_payload, 0);
    append_fourcc(infe_payload, it.type.c_str());
    infe_payload.push_back(0);
    append(iinf_payload, make_box("infe", infe_payload, /*full=*/true, /*version=*/2));
  }
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // iprp / ipco: one mskC for the base, one clap, plus one ispe per image size
  // (shared by all items of that size).
  // Property indices are 1-based in the order boxes appear in ipco.
  std::vector<uint8_t> mskC_payload;
  mskC_payload.push_back(8);  // bits_per_pixel
  auto mskC = make_box("mskC", mskC_payload, /*full=*/true);

  std::vector<uint8_t> clap_payload;
  for (uint32_t v : {1, 1, 1, 1, 0, 1, 0, 1}) {  // width 1/1, height 1/1, centered
    put_u32_be(clap_payload, v);
  }
  auto clap = make_box("clap", clap_payload);

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, mskC);   // property 1: mskC, for the base
  append(ipco_payload, clap);   // property 2: clap

  std::map<std::pair<uint32_t, uint32_t>, uint8_t> ispe_index;
  for (const auto& it : items) {
    if (ispe_index.emplace(std::make_pair(it.width, it.height),
                           static_cast<uint8_t>(3 + ispe_index.size())).second) {
      std::vector<uint8_t> ispe_payload;
      put_u32_be(ispe_payload, it.width);
      put_u32_be(ispe_payload, it.height);
      append(ipco_payload, make_box("ispe", ispe_payload, /*full=*/true));
    }
  }
  auto ipco = make_box("ipco", ipco_payload);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, static_cast<uint32_t>(items.size()));  // entry_count
  for (const auto& it : items) {
    put_u16_be(ipma_payload, it.id);
    ipma_payload.push_back(static_cast<uint8_t>(1 + (it.type == "mski") + it.crop_to_1x1));  // association_count
    ipma_payload.push_back(0x80 | ispe_index[{it.width, it.height}]);  // essential, ispe
    if (it.type == "mski") {
      ipma_payload.push_back(0x80 | 1);   // essential, mskC
    }
    if (it.crop_to_1x1) {
      ipma_payload.push_back(0x80 | 2);   // essential, clap
    }
  }
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // idat + iloc: concatenate the data of all items that have any.
  std::vector<uint8_t> idat_payload;
  struct Extent { uint16_t id; uint32_t off; uint32_t len; };
  std::vector<Extent> extents;
  for (const auto& it : items) {
    if (!it.data.empty()) {
      extents.push_back({it.id, static_cast<uint32_t>(idat_payload.size()),
                         static_cast<uint32_t>(it.data.size())});
      append(idat_payload, it.data);
    }
  }
  auto idat = make_box("idat", idat_payload);

  std::vector<uint8_t> iloc_payload;
  iloc_payload.push_back((4 << 4) | 4);   // offset_size=4, length_size=4
  iloc_payload.push_back((0 << 4) | 0);   // base_offset_size=0, index_size=0
  put_u16_be(iloc_payload, static_cast<uint16_t>(extents.size()));  // item_count
  for (const auto& e : extents) {
    put_u16_be(iloc_payload, e.id);
    put_u16_be(iloc_payload, 0x0001);     // reserved(12) + construction_method=1 (idat)
    put_u16_be(iloc_payload, 0);          // data_reference_index
    put_u16_be(iloc_payload, 1);          // extent_count
    put_u32_be(iloc_payload, e.off);      // extent_offset (within idat)
    put_u32_be(iloc_payload, e.len);      // extent_length
  }
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);

  // iref: one 'dimg' entry per item that references others.
  std::vector<uint8_t> iref_children;
  for (const auto& it : items) {
    if (!it.dimg.empty()) {
      std::vector<uint8_t> dimg_payload;
      put_u16_be(dimg_payload, it.id);
      put_u16_be(dimg_payload, static_cast<uint16_t>(it.dimg.size()));
      for (uint16_t to : it.dimg) { put_u16_be(dimg_payload, to); }
      append(iref_children, make_box("dimg", dimg_payload));
    }
  }
  auto iref = make_box("iref", iref_children, /*full=*/true);

  std::vector<uint8_t> meta_payload;
  append(meta_payload, hdlr);
  append(meta_payload, pitm);
  append(meta_payload, iinf);
  append(meta_payload, iprp);
  append(meta_payload, iloc);
  append(meta_payload, iref);
  append(meta_payload, idat);
  auto meta = make_box("meta", meta_payload, /*full=*/true);

  std::vector<uint8_t> file;
  append(file, ftyp);
  append(file, meta);
  return file;
}

// Build the exponential amplification gadget: a chain of `depth` overlays where
// each overlay composites the next one twice, through two distinct 'iden' items
// (listing the same item twice in one 'dimg' entry is legal too, but the two
// items keep the gadget independent of that). Without the fix, decoding the
// primary overlay decodes the base 2^depth times.
//
// Item layout: id 1 = base ('mski'); then per level k = 0..depth-1:
//   iovl_k  = 2 + 3*k
//   iden_ak = 3 + 3*k, iden_bk = 4 + 3*k   (both reference the next level)
std::vector<uint8_t> build_exponential_overlays(int depth) {
  std::vector<Item> items;
  items.push_back({1, "mski", {}, std::vector<uint8_t>(64, 0x7F)});

  for (int k = 0; k < depth; k++) {
    uint16_t iovl = static_cast<uint16_t>(2 + 3 * k);
    uint16_t iden_a = static_cast<uint16_t>(3 + 3 * k);
    uint16_t iden_b = static_cast<uint16_t>(4 + 3 * k);
    uint16_t next = (k + 1 < depth) ? static_cast<uint16_t>(2 + 3 * (k + 1)) : 1;

    items.push_back({iovl, "iovl", {iden_a, iden_b}, make_overlay_spec(8, 8, 2)});
    items.push_back({iden_a, "iden", {next}, {}});
    items.push_back({iden_b, "iden", {next}, {}});
  }

  return build_file(items, /*primary=*/2);
}

// Build a grid of 1x1 pixel tiles. Every tile is the same 'iden' item, whose 'clap'
// crops one pixel out of an overlay with a canvas of source_size x source_size.
// Without the fix, that canvas is built again for each of the tiles.
std::vector<uint8_t> build_grid_of_cropped_tiles(uint8_t rows, uint8_t columns, uint16_t source_size) {
  std::vector<Item> items;
  items.push_back({1, "mski", {}, std::vector<uint8_t>(64, 0x7F)});
  items.push_back({2, "grid", std::vector<uint16_t>(rows * columns, 3),
                   make_grid_spec(rows, columns, columns, rows), columns, rows});
  items.push_back({3, "iden", {4}, {}, source_size, source_size, /*crop_to_1x1=*/true});
  items.push_back({4, "iovl", {1}, make_overlay_spec(source_size, source_size, 1), source_size, source_size});

  return build_file(items, /*primary=*/2);
}

// Decodes the whole primary image, or only its tile (tile_x, tile_y) with tile_x >= 0.
heif_error decode_primary(const std::vector<uint8_t>& data, bool& read_ok, bool strict = false,
                          int tile_x = -1, int tile_y = -1) {
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory_without_copy(
      ctx, data.data(), data.size(), nullptr);
  read_ok = (err.code == heif_error_Ok);
  if (!read_ok) {
    heif_context_free(ctx);
    return err;
  }

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  if (err.code != heif_error_Ok) {
    heif_context_free(ctx);
    return err;
  }

  // Without strict decoding, a grid tile that cannot be decoded is only a warning
  // (unless it exceeds a security limit).
  heif_decoding_options* options = heif_decoding_options_alloc();
  options->strict_decoding = strict;

  heif_image* img = nullptr;
  if (tile_x >= 0) {
    err = heif_image_handle_decode_image_tile(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, options,
                                              static_cast<uint32_t>(tile_x), static_cast<uint32_t>(tile_y));
  }
  else {
    err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, options);
  }

  heif_decoding_options_free(options);
  if (img) { heif_image_release(img); }
  heif_image_handle_release(handle);
  heif_context_free(ctx);
  return err;
}

} // namespace


// The core DoS: without the fix this decodes the base 2^30 times and never
// returns. With the fix the overlay-nesting guard rejects it after a few levels,
// so decoding completes essentially instantly. The test therefore also proves
// (by completing at all) that the amplification is bounded.
TEST_CASE("overlay amplification: deeply nested overlays are rejected, not decoded") {
  auto data = build_exponential_overlays(/*depth=*/30);

  bool read_ok = false;
  heif_error err = decode_primary(data, read_ok);

  REQUIRE(read_ok);            // the file parses; the blow-up is only at decode
  REQUIRE(err.code != heif_error_Ok);
  REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);
}

// The per-overlay fan-out limit (MAX_OVERLAY_IMAGES): an overlay compositing
// more than the allowed number of input images is rejected while reading the
// overlay spec, so the item becomes undecodable.
TEST_CASE("overlay amplification: overlay with too many input images is rejected") {
  std::vector<Item> items;
  items.push_back({1, "mski", {}, std::vector<uint8_t>(64, 0x7F)});

  // 6 iden items (> MAX_OVERLAY_IMAGES == 5), all pointing at the base.
  std::vector<uint16_t> refs;
  for (uint16_t k = 0; k < 6; k++) {
    uint16_t iden = static_cast<uint16_t>(3 + k);
    items.push_back({iden, "iden", {1}, {}});
    refs.push_back(iden);
  }
  items.push_back({2, "iovl", refs, make_overlay_spec(8, 8, refs.size())});

  auto data = build_file(items, /*primary=*/2);

  bool read_ok = false;
  heif_error err = decode_primary(data, read_ok);

  REQUIRE(err.code != heif_error_Ok);
  REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);
}

// Control: a legitimate two-level overlay (well within all limits) still decodes
// normally, proving the guards do not reject ordinary derived images.
TEST_CASE("overlay amplification: a shallow legitimate overlay still decodes") {
  std::vector<Item> items;
  items.push_back({1, "mski", {}, std::vector<uint8_t>(64, 0x7F)});
  // iovl_2 (primary) -> iovl_3 -> base. Overlay nesting depth 2 (<= 3), fan-out 1.
  items.push_back({2, "iovl", {3}, make_overlay_spec(8, 8, 1)});
  items.push_back({3, "iovl", {1}, make_overlay_spec(8, 8, 1)});

  auto data = build_file(items, /*primary=*/2);

  bool read_ok = false;
  heif_error err = decode_primary(data, read_ok);

  REQUIRE(read_ok);
  REQUIRE(err.code == heif_error_Ok);
}

// The number of decodes (3 per tile) is within the limit, but each tile decodes a
// 2048x2048 image to use one pixel of it.
TEST_CASE("overlay amplification: large image decoded again for each small grid tile is rejected") {
  auto data = build_grid_of_cropped_tiles(/*rows=*/16, /*columns=*/16, /*source_size=*/2048);

  // Also without strict decoding, where a broken grid tile is only a warning: an exceeded
  // budget must not result in an image with blank tiles.
  bool strict = GENERATE(true, false);
  CAPTURE(strict);

  bool read_ok = false;
  heif_error err = decode_primary(data, read_ok, strict);

  REQUIRE(read_ok);
  REQUIRE(err.code != heif_error_Ok);
  REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);
}

// Control: the same reference graph with a small source image is cheap and still decodes.
TEST_CASE("overlay amplification: small image decoded again for each grid tile still decodes") {
  auto data = build_grid_of_cropped_tiles(/*rows=*/16, /*columns=*/16, /*source_size=*/8);

  bool read_ok = false;
  heif_error err = decode_primary(data, read_ok, /*strict=*/true);

  REQUIRE(read_ok);
  REQUIRE(err.code == heif_error_Ok);
}

// Control: cropping a large image once is legitimate.
TEST_CASE("overlay amplification: large image cropped to a single grid tile still decodes") {
  auto data = build_grid_of_cropped_tiles(/*rows=*/1, /*columns=*/1, /*source_size=*/2048);

  bool read_ok = false;
  heif_error err = decode_primary(data, read_ok, /*strict=*/true);

  REQUIRE(read_ok);
  REQUIRE(err.code == heif_error_Ok);
}

// Control: a large grid that uses one tile item for all positions decodes that item many
// times, but not more pixels than the grid has.
TEST_CASE("overlay amplification: grid that repeats one tile still decodes") {
  std::vector<Item> items;
  items.push_back({1, "mski", {}, std::vector<uint8_t>(512 * 512, 0x7F), 512, 512});
  items.push_back({2, "grid", std::vector<uint16_t>(16 * 16, 1), make_grid_spec(16, 16, 8192, 8192), 8192, 8192});

  auto data = build_file(items, /*primary=*/2);

  bool read_ok = false;
  heif_error err = decode_primary(data, read_ok, /*strict=*/true);

  REQUIRE(read_ok);
  REQUIRE(err.code == heif_error_Ok);
}


// Each call of heif_image_handle_decode_image_tile() is a top-level decode of its own.
// The tile item is a 2048x2048 image cropped to its 1x1 tile, which a budget that is
// computed from the items alone would allow for each of the 256 tiles. A tile only gets
// its share of the budget of the whole image, so the tile item is refused.
TEST_CASE("overlay amplification: single tile cropped from a large image is rejected") {
  auto data = build_grid_of_cropped_tiles(/*rows=*/16, /*columns=*/16, /*source_size=*/2048);

  bool read_ok = false;
  heif_error err = decode_primary(data, read_ok, /*strict=*/false, /*tile_x=*/3, /*tile_y=*/5);

  REQUIRE(read_ok);
  REQUIRE(err.code != heif_error_Ok);
  REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);
}

// Control: the same tile with a small source image still decodes.
TEST_CASE("overlay amplification: single tile cropped from a small image still decodes") {
  auto data = build_grid_of_cropped_tiles(/*rows=*/16, /*columns=*/16, /*source_size=*/8);

  bool read_ok = false;
  heif_error err = decode_primary(data, read_ok, /*strict=*/true, /*tile_x=*/3, /*tile_y=*/5);

  REQUIRE(read_ok);
  REQUIRE(err.code == heif_error_Ok);
}

// Control: a single tile of a large grid of tiles of the cell size still decodes.
TEST_CASE("overlay amplification: single tile of a large grid still decodes") {
  std::vector<Item> items;
  items.push_back({1, "mski", {}, std::vector<uint8_t>(512 * 512, 0x7F), 512, 512});
  items.push_back({2, "grid", std::vector<uint16_t>(16 * 16, 1), make_grid_spec(16, 16, 8192, 8192), 8192, 8192});

  auto data = build_file(items, /*primary=*/2);

  bool read_ok = false;
  heif_error err = decode_primary(data, read_ok, /*strict=*/true, /*tile_x=*/15, /*tile_y=*/15);

  REQUIRE(read_ok);
  REQUIRE(err.code == heif_error_Ok);
}
