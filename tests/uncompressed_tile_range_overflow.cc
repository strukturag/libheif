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

// Regression test for GHSA-hh47-fhqr-cj2r (incomplete fix of
// GHSA-73p7-m7gg-w2jv / CVE-2026-62292).
//
// A crafted 'unci' image advertises a 4096x4096 grid of 1x1 tiles. Large alignment
// values make the computed size of a tile 2^40 bytes, so for the final tile index
// (2^24 - 1) the range of the tile data wraps around:
//
//     range_start_offset = 2^40 * (2^24 - 1) = 2^64 - 2^40
//     range_size         = 2^40
//     range_start_offset + range_size = 2^64 -> 0   (uint64_t wrap)
//
// With generic (zlib, full_item) compression, unc_decoder cut this range out of the
// decompressed item. Its addition-form check `range_start_offset + range_size >
// data->size()` was bypassed (0 > 1 is false) and the code reached memcpy() with an
// out-of-range source pointer and a 1 TiB length: an out-of-bounds read / SIGSEGV
// reachable through the public heif_image_handle_decode_image_tile().
//
// The tests build the fixture in memory, open it (structurally valid, must succeed)
// and request the last tile: decoding must return a structured error instead of
// crashing.
//
// - Without generic compression, the tile range is read from the item data.
//
// - With full_item compression, the tiles cannot be decoded independently and the
//   image is exposed as a single tile (GHSA-6fqc-p7r8-2g36), so the last uncC tile
//   is not accessible anymore. The single tile is the whole image and has to be
//   rejected as well.
//
// One file uses real zlib generic compression, so the tests only run when the
// library was built with zlib (guarded in tests/CMakeLists.txt).

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <vector>

namespace {

constexpr uint32_t WIDTH = 4096;
constexpr uint32_t HEIGHT = 4096;
constexpr uint32_t TILE_COLS = 4096;
constexpr uint32_t TILE_ROWS = 4096;
constexpr uint32_t COMPONENTS = 256;

// zlib.compress(b"A") - a valid zlib stream that decompresses to a single byte.
const std::vector<uint8_t> kZlibOneByte = {
    0x78, 0x9c, 0x73, 0x04, 0x00, 0x00, 0x42, 0x00, 0x42};

// Build a minimal HEIF file with a single 'unci' item and a 4096x4096 tile grid,
// optionally using generic zlib (full_item) compression.
std::vector<uint8_t> build_heif_unci_overflow_tiling(bool generic_compression) {
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

  // iinf: item 1 = 'unci'.
  std::vector<uint8_t> infe_payload;
  put_u16_be(infe_payload, 1);
  put_u16_be(infe_payload, 0);
  append_fourcc(infe_payload, "unci");
  append_cstr(infe_payload, "");
  auto infe = make_box("infe", infe_payload, /*full=*/true, /*version=*/2);

  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, 1);
  append(iinf_payload, infe);
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // ispe
  std::vector<uint8_t> ispe_payload;
  put_u32_be(ispe_payload, WIDTH);
  put_u32_be(ispe_payload, HEIGHT);
  auto ispe = make_box("ispe", ispe_payload, /*full=*/true);

  // cmpd: COMPONENTS monochrome components (type 0).
  std::vector<uint8_t> cmpd_payload;
  put_u32_be(cmpd_payload, COMPONENTS);
  for (uint32_t i = 0; i < COMPONENTS; i++) {
    put_u16_be(cmpd_payload, 0);
  }
  auto cmpd = make_box("cmpd", cmpd_payload);

  // uncC (v0): component interleave, 8-bit, huge alignment values.
  std::vector<uint8_t> uncC_payload;
  put_u32_be(uncC_payload, 0);           // profile
  put_u32_be(uncC_payload, COMPONENTS);  // component_count
  for (uint32_t i = 0; i < COMPONENTS; i++) {
    put_u16_be(uncC_payload, static_cast<uint16_t>(i)); // component_index
    uncC_payload.push_back(7);           // component_bit_depth_minus_one -> 8 bit
    uncC_payload.push_back(0);           // component_format (unsigned)
    uncC_payload.push_back(0);           // component_align_size
  }
  uncC_payload.push_back(0);             // sampling_type (no subsampling)
  uncC_payload.push_back(0);             // interleave_type (component)
  uncC_payload.push_back(0);             // block_size
  uncC_payload.push_back(0);             // flags
  put_u32_be(uncC_payload, 0);           // pixel_size
  put_u32_be(uncC_payload, 0xFFFFFFFF);  // row_align_size
  put_u32_be(uncC_payload, 0x80000000);  // tile_align_size
  put_u32_be(uncC_payload, TILE_COLS - 1);
  put_u32_be(uncC_payload, TILE_ROWS - 1);
  auto uncC = make_box("uncC", uncC_payload, /*full=*/true);

  // cmpC: zlib, compressed_unit_type = full_item (0). No icef box.
  std::vector<uint8_t> cmpC_payload;
  append_fourcc(cmpC_payload, "zlib");
  cmpC_payload.push_back(0);             // heif_cmpC_compressed_unit_type_full_item
  auto cmpC = make_box("cmpC", cmpC_payload, /*full=*/true);

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, ispe);
  append(ipco_payload, cmpd);
  append(ipco_payload, uncC);
  if (generic_compression) {
    append(ipco_payload, cmpC);
  }
  auto ipco = make_box("ipco", ipco_payload);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, 1); // entry_count
  put_u16_be(ipma_payload, 1); // item_ID 1
  ipma_payload.push_back(generic_compression ? 4 : 3); // association_count
  ipma_payload.push_back(0x80 | 1); // essential, ispe
  ipma_payload.push_back(0x80 | 2); // essential, cmpd
  ipma_payload.push_back(0x80 | 3); // essential, uncC
  if (generic_compression) {
    ipma_payload.push_back(0x80 | 4); // essential, cmpC
  }
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // idat: the zlib payload (decompresses to 1 byte). Without generic compression,
  // these are 9 bytes of image data.
  auto idat = make_box("idat", kZlibOneByte);

  // iloc (version 1): item 1 stored in idat (construction_method=1).
  std::vector<uint8_t> iloc_payload;
  put_u16_be(iloc_payload, (4 << 12) | (4 << 8) | (0 << 4) | 0); // offset_size=4, length_size=4
  put_u16_be(iloc_payload, 1);          // item_count
  put_u16_be(iloc_payload, 1);          // item_ID
  put_u16_be(iloc_payload, 0x0001);     // construction_method=1 (idat)
  put_u16_be(iloc_payload, 0);          // data_reference_index
  put_u16_be(iloc_payload, 1);          // extent_count
  put_u32_be(iloc_payload, 0);          // extent_offset (within idat)
  put_u32_be(iloc_payload, static_cast<uint32_t>(kZlibOneByte.size())); // extent_length
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);

  std::vector<uint8_t> meta_payload;
  append(meta_payload, hdlr);
  append(meta_payload, pitm);
  append(meta_payload, iinf);
  append(meta_payload, iprp);
  append(meta_payload, iloc);
  append(meta_payload, idat);
  auto meta = make_box("meta", meta_payload, /*full=*/true);

  std::vector<uint8_t> file;
  append(file, ftyp);
  append(file, meta);
  return file;
}

// Open the file and get the handle of its primary image.
void open_primary_image(const std::vector<uint8_t>& file, heif_context** out_ctx, heif_image_handle** out_handle) {
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  // The file is structurally valid, so opening it must succeed. The bug is only
  // reachable by then decoding a tile.
  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  *out_ctx = ctx;
  *out_handle = handle;
}

} // namespace

TEST_CASE("unci tile range overflow returns error instead of crashing") {
  std::vector<uint8_t> file = build_heif_unci_overflow_tiling(/*generic_compression=*/false);

  heif_context* ctx = nullptr;
  heif_image_handle* handle = nullptr;
  open_primary_image(file, &ctx, &handle);

  heif_image_tiling tiling;
  heif_image_handle_get_image_tiling(handle, 1, &tiling);
  REQUIRE(tiling.num_columns == TILE_COLS);
  REQUIRE(tiling.num_rows == TILE_ROWS);

  // Request the last advertised tile. Its data range wraps around the 64 bit range.
  // This must return a structured invalid-input error.
  heif_image* img = nullptr;
  heif_error err = heif_image_handle_decode_image_tile(handle, &img,
                                                       heif_colorspace_undefined, heif_chroma_undefined,
                                                       nullptr,
                                                       tiling.num_columns - 1, tiling.num_rows - 1);
  REQUIRE(err.code == heif_error_Invalid_input);
  REQUIRE(img == nullptr);

  heif_image_handle_release(handle);
  heif_context_free(ctx);
}


TEST_CASE("unci tile range overflow with full_item compression returns error instead of crashing") {
  std::vector<uint8_t> file = build_heif_unci_overflow_tiling(/*generic_compression=*/true);

  heif_context* ctx = nullptr;
  heif_image_handle* handle = nullptr;
  open_primary_image(file, &ctx, &handle);

  // The uncC tiles cannot be decoded independently, so the image is a single tile.
  heif_image_tiling tiling;
  heif_image_handle_get_image_tiling(handle, 1, &tiling);
  REQUIRE(tiling.num_columns == 1);
  REQUIRE(tiling.num_rows == 1);
  REQUIRE(tiling.tile_width == WIDTH);
  REQUIRE(tiling.tile_height == HEIGHT);

  heif_decoding_options* options = heif_decoding_options_alloc();

  for (bool ignore_transformations : {false, true}) {
    INFO("ignore_transformations: " << ignore_transformations);
    options->ignore_transformations = ignore_transformations;

    // Before the image was exposed as a single tile, requesting the last uncC tile
    // reached memcpy() with a wrapped source pointer and a 1 TiB length
    // (out-of-bounds read / SIGSEGV). The tile does not exist anymore.
    heif_image* img = nullptr;
    heif_error err = heif_image_handle_decode_image_tile(handle, &img,
                                                         heif_colorspace_undefined, heif_chroma_undefined,
                                                         options,
                                                         TILE_COLS - 1, TILE_ROWS - 1);
    REQUIRE(err.code == heif_error_Usage_error);
    REQUIRE(img == nullptr);

    // The single tile is the whole image, which has to be rejected because of its
    // alignment values.
    err = heif_image_handle_decode_image_tile(handle, &img,
                                              heif_colorspace_undefined, heif_chroma_undefined,
                                              options,
                                              0, 0);
    REQUIRE(err.code == heif_error_Invalid_input);
    REQUIRE(img == nullptr);
  }

  heif_decoding_options_free(options);

  heif_image* img = nullptr;
  heif_error err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
  REQUIRE(err.code == heif_error_Invalid_input);
  REQUIRE(img == nullptr);

  heif_image_handle_release(handle);
  heif_context_free(ctx);
}
