/*
  libheif regression tests for grid image decoding edge cases.

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

#include <cstdint>
#include <vector>

namespace {

void put_u32_be(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v >> 24));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

void put_u16_be(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

void append_fourcc(std::vector<uint8_t>& out, const char fourcc[4]) {
  out.insert(out.end(), fourcc, fourcc + 4);
}

void append(std::vector<uint8_t>& out, const std::vector<uint8_t>& v) {
  out.insert(out.end(), v.begin(), v.end());
}

std::vector<uint8_t> make_box(const char fourcc[4],
                              const std::vector<uint8_t>& payload,
                              bool is_full_box = false,
                              uint8_t version = 0,
                              uint32_t flags = 0) {
  std::vector<uint8_t> body;
  if (is_full_box) {
    body.push_back(version);
    body.push_back(static_cast<uint8_t>(flags >> 16));
    body.push_back(static_cast<uint8_t>(flags >> 8));
    body.push_back(static_cast<uint8_t>(flags));
  }
  body.insert(body.end(), payload.begin(), payload.end());

  std::vector<uint8_t> box;
  put_u32_be(box, static_cast<uint32_t>(8 + body.size()));
  append_fourcc(box, fourcc);
  box.insert(box.end(), body.begin(), body.end());
  return box;
}

// Build a minimal HEIF with one 'grid' item (id=1, 1x1, 64x64) whose 'dimg'
// iref references a tile item id (99) that has no corresponding 'infe' entry.
// Used to crash ImageItem_Grid::decode_grid_tile() with a NULL deref because
// HeifContext::get_image() returns nullptr for the unknown id.
std::vector<uint8_t> build_heif_with_missing_grid_tile() {
  // ftyp: heic / 0 / [mif1, heic]
  std::vector<uint8_t> ftyp_payload;
  append_fourcc(ftyp_payload, "heic");
  put_u32_be(ftyp_payload, 0);
  append_fourcc(ftyp_payload, "mif1");
  append_fourcc(ftyp_payload, "heic");
  auto ftyp = make_box("ftyp", ftyp_payload);

  // hdlr: handler type 'pict' (so the file is treated as an image collection
  // and HeifContext discovers items)
  std::vector<uint8_t> hdlr_payload;
  put_u32_be(hdlr_payload, 0);
  append_fourcc(hdlr_payload, "pict");
  put_u32_be(hdlr_payload, 0);
  put_u32_be(hdlr_payload, 0);
  put_u32_be(hdlr_payload, 0);
  hdlr_payload.push_back(0);
  auto hdlr = make_box("hdlr", hdlr_payload, /*full=*/true);

  // pitm v0: primary item = id 1 (the grid)
  std::vector<uint8_t> pitm_payload;
  put_u16_be(pitm_payload, 1);
  auto pitm = make_box("pitm", pitm_payload, /*full=*/true);

  // iinf v0 containing one infe v2 entry: id=1, type='grid'
  std::vector<uint8_t> infe_payload;
  put_u16_be(infe_payload, 1);          // item_ID
  put_u16_be(infe_payload, 0);          // item_protection_index
  append_fourcc(infe_payload, "grid");  // item_type
  infe_payload.push_back(0);            // item_name (empty, NUL-terminated)
  auto infe = make_box("infe", infe_payload, /*full=*/true, /*version=*/2);

  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, 1);          // entry_count
  append(iinf_payload, infe);
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // iprp / ipco { ispe(64x64) } / ipma associating ispe (prop index 1) with item 1
  std::vector<uint8_t> ispe_payload;
  put_u32_be(ispe_payload, 64);
  put_u32_be(ispe_payload, 64);
  auto ispe = make_box("ispe", ispe_payload, /*full=*/true);
  auto ipco = make_box("ipco", ispe);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, 1);          // entry_count
  put_u16_be(ipma_payload, 1);          // item_ID
  ipma_payload.push_back(1);            // association_count
  ipma_payload.push_back(0x80 | 1);     // essential=1, property_index=1
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // iloc v1: item 1, construction_method=1 (idat), extent offset=0 length=8
  std::vector<uint8_t> iloc_payload;
  put_u16_be(iloc_payload, (4 << 12) | (4 << 8) | (0 << 4) | 0);  // sizes
  put_u16_be(iloc_payload, 1);          // item_count
  put_u16_be(iloc_payload, 1);          // item_ID
  put_u16_be(iloc_payload, 0x0001);     // reserved(12) + construction_method=1
  put_u16_be(iloc_payload, 0);          // data_reference_index
  // base_offset omitted (base_offset_size=0)
  put_u16_be(iloc_payload, 1);          // extent_count
  put_u32_be(iloc_payload, 0);          // extent_offset (within idat)
  put_u32_be(iloc_payload, 8);          // extent_length
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);

  // iref v0 with a single 'dimg' subbox: from item 1 to item 99 (which does
  // NOT exist as an infe entry).
  std::vector<uint8_t> dimg_payload;
  put_u16_be(dimg_payload, 1);          // from_item_ID
  put_u16_be(dimg_payload, 1);          // reference_count
  put_u16_be(dimg_payload, 99);         // to_item_ID
  auto dimg = make_box("dimg", dimg_payload);
  auto iref = make_box("iref", dimg, /*full=*/true);

  // idat: 8-byte ImageGrid header (v=0, flags=0, rows-1=0, cols-1=0, w=64, h=64)
  std::vector<uint8_t> idat_payload = {0, 0, 0, 0};
  put_u16_be(idat_payload, 64);
  put_u16_be(idat_payload, 64);
  auto idat = make_box("idat", idat_payload);

  // meta = hdlr + pitm + iinf + iprp + iloc + iref + idat
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

// Build a minimal HEIF with a 2x1 'grid' item (id=1) that has an associated
// 'irot' rotation of 270° CCW. In the displayed image the grid is logically
// 1 column by 2 rows. The grid references a missing tile (id 99), but the
// test never gets that far: it passes (tile_x=1, tile_y=0) which is valid in
// the file's 2x1 grid but out-of-range in the displayed 1x2 grid. The old
// transform code did `num_rows - 1 - tile_x = 1 - 1 - 1` → unsigned underflow
// (UBSan).
std::vector<uint8_t> build_heif_grid_with_irot270() {
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
  put_u16_be(pitm_payload, 1);
  auto pitm = make_box("pitm", pitm_payload, /*full=*/true);

  std::vector<uint8_t> infe_payload;
  put_u16_be(infe_payload, 1);
  put_u16_be(infe_payload, 0);
  append_fourcc(infe_payload, "grid");
  infe_payload.push_back(0);
  auto infe = make_box("infe", infe_payload, /*full=*/true, /*version=*/2);

  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, 1);
  append(iinf_payload, infe);
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // ispe (prop 1): 128 (w) x 64 (h) -- the file/grid dimensions.
  std::vector<uint8_t> ispe_payload;
  put_u32_be(ispe_payload, 128);
  put_u32_be(ispe_payload, 64);
  auto ispe = make_box("ispe", ispe_payload, /*full=*/true);

  // irot (prop 2): rotation = 3 * 90° = 270° CCW.
  std::vector<uint8_t> irot_payload;
  irot_payload.push_back(3);
  auto irot = make_box("irot", irot_payload);

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, ispe);
  append(ipco_payload, irot);
  auto ipco = make_box("ipco", ipco_payload);

  // ipma: item 1 associated with prop 1 (ispe) and prop 2 (irot).
  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, 1);
  put_u16_be(ipma_payload, 1);
  ipma_payload.push_back(2);
  ipma_payload.push_back(0x80 | 1);
  ipma_payload.push_back(0x80 | 2);
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // iloc v1: item 1 in idat, 8-byte ImageGrid header.
  std::vector<uint8_t> iloc_payload;
  put_u16_be(iloc_payload, (4 << 12) | (4 << 8) | (0 << 4) | 0);
  put_u16_be(iloc_payload, 1);
  put_u16_be(iloc_payload, 1);
  put_u16_be(iloc_payload, 0x0001);
  put_u16_be(iloc_payload, 0);
  put_u16_be(iloc_payload, 1);
  put_u32_be(iloc_payload, 0);
  put_u32_be(iloc_payload, 8);
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);

  // iref: grid (item 1) references 2 missing tiles (ids 99 and 100). Two
  // distinct ids are required because iref rejects duplicate references.
  std::vector<uint8_t> dimg_payload;
  put_u16_be(dimg_payload, 1);
  put_u16_be(dimg_payload, 2);
  put_u16_be(dimg_payload, 99);
  put_u16_be(dimg_payload, 100);
  auto dimg = make_box("dimg", dimg_payload);
  auto iref = make_box("iref", dimg, /*full=*/true);

  // idat: ImageGrid header — v=0, flags=0, rows-1=0, cols-1=1, w=128, h=64.
  std::vector<uint8_t> idat_payload = {0, 0, 0, 1};
  put_u16_be(idat_payload, 128);
  put_u16_be(idat_payload, 64);
  auto idat = make_box("idat", idat_payload);

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

} // namespace


// Regression for PR #1807 / NULL deref in ImageItem_Grid::decode_grid_tile
// when a grid's 'dimg' iref references an item id with no matching infe.
TEST_CASE("grid: missing tile reference returns error instead of crashing") {
  auto data = build_heif_with_missing_grid_tile();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory_without_copy(
      ctx, data.data(), data.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  heif_image* tile = nullptr;
  err = heif_image_handle_decode_image_tile(handle, &tile,
                                            heif_colorspace_YCbCr,
                                            heif_chroma_420,
                                            nullptr, 0, 0);

  // Must surface as a clean error, not a crash.
  REQUIRE(err.code == heif_error_Invalid_input);
  REQUIRE(err.subcode == heif_suberror_Missing_grid_images);
  REQUIRE(tile == nullptr);

  heif_image_handle_release(handle);
  heif_context_free(ctx);
}


// Regression for PR #1808 / OOB read in ImageItem_Grid::decode_grid_tile
// when the caller passes (tile_x, tile_y) coordinates outside the grid's
// declared (columns, rows). The previous assert() was a no-op in Release
// builds and the indexing then read past m_grid_tile_ids.
TEST_CASE("grid: out-of-range tile coordinate returns error instead of OOB") {
  // Reuses the same 1x1 grid built for the prior test. Coordinates (1, 0)
  // are outside its (cols=1, rows=1) bounds.
  auto data = build_heif_with_missing_grid_tile();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory_without_copy(
      ctx, data.data(), data.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  heif_image* tile = nullptr;
  err = heif_image_handle_decode_image_tile(handle, &tile,
                                            heif_colorspace_YCbCr,
                                            heif_chroma_420,
                                            nullptr, /*tile_x=*/1, /*tile_y=*/0);

  // The out-of-range coordinate is now rejected up-front by the tile-position
  // transform (see transform_requested_tile_position_to_original_tile_position).
  REQUIRE(err.code == heif_error_Usage_error);
  REQUIRE(tile == nullptr);

  heif_image_handle_release(handle);
  heif_context_free(ctx);
}


// Regression for the UBSan crash in
// ImageItem::transform_requested_tile_position_to_original_tile_position:
// `tiling.num_rows - 1 - tile_x` underflowed when a 90°/270° irot was present
// and the caller passed a tile coordinate that was valid in the file's grid
// but out-of-range in the displayed (rotated) grid.
TEST_CASE("grid: rotated grid rejects tile coord beyond displayed bounds") {
  auto data = build_heif_grid_with_irot270();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory_without_copy(
      ctx, data.data(), data.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  // Displayed tiling should reflect the 270° rotation: 1 column, 2 rows.
  heif_image_tiling tiling = {};
  err = heif_image_handle_get_image_tiling(handle, /*xform=*/1, &tiling);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(tiling.num_columns == 1);
  REQUIRE(tiling.num_rows == 2);

  // (tile_x=1, tile_y=0) is in-range for the file's 2x1 grid but out-of-range
  // for the displayed 1x2 grid. Pre-fix this hit unsigned underflow inside
  // the inverse 270° transform. It must now be rejected as a usage error
  // instead of crashing under UBSan.
  heif_item_id tile_id = 0;
  err = heif_image_handle_get_grid_image_tile_id(
      handle, /*xform=*/1, /*tile_x=*/1, /*tile_y=*/0, &tile_id);
  REQUIRE(err.code == heif_error_Usage_error);

  heif_image* tile = nullptr;
  err = heif_image_handle_decode_image_tile(handle, &tile,
                                            heif_colorspace_YCbCr,
                                            heif_chroma_420,
                                            nullptr, /*tile_x=*/1, /*tile_y=*/0);
  REQUIRE(err.code == heif_error_Usage_error);
  REQUIRE(tile == nullptr);

  // Sanity: an in-range displayed coordinate is *not* rejected by the bounds
  // check (it reaches the missing-tile path).
  err = heif_image_handle_get_grid_image_tile_id(
      handle, /*xform=*/1, /*tile_x=*/0, /*tile_y=*/1, &tile_id);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle_release(handle);
  heif_context_free(ctx);
}
