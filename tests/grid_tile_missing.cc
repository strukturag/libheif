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
