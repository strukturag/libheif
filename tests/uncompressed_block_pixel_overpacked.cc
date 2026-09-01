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

// Regression test: block-pixel interleave with components that do not fit the
// block must be rejected on the per-tile decode path.
//
// A crafted 'unci' image uses pixel interleave with block_size == pixel_size == 8
// bytes (64 bits) and five 16-bit components (80 bits total). The block-pixel
// decoder packs all components into one 64-bit block and derives each component's
// bit shift by accumulating the component bit depths. When the depths sum to at
// least the block width, the highest shift reaches or exceeds 64, so
// `block_val >> shift` on the uint64_t block in
// unc_decoder_block_pixel_interleave::decode_tile() is undefined behaviour
// (UBSan abort; otherwise corrupted output).
//
// The full-image decode path already rejected this via check_hard_limits(), but
// the per-tile decode path (UncompressedImageCodec::decode_uncompressed_image_tile,
// reachable through the public heif_image_handle_decode_image_tile()) skipped that
// validation. The fix runs check_hard_limits() on the tile path too, and also
// teaches unc_decoder_factory_block_pixel_interleave::can_decode() to refuse a
// layout whose components overflow the block, so the decoder is safe regardless of
// which path selected it.
//
// The file is structurally valid and must open; requesting a tile must now return
// a structured heif_error_Invalid_input instead of triggering the undefined shift.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <vector>

namespace {

constexpr uint32_t WIDTH = 2;
constexpr uint32_t HEIGHT = 2;
constexpr uint32_t TILE_COLS = 1;    // single tile: the decoder reads the whole
constexpr uint32_t TILE_ROWS = 1;    // item extent in one go (idat ranged reads
                                     // only support a single full-extent read).
constexpr uint32_t COMPONENTS = 5;   // 5 x 16 bit = 80 bits > 64-bit block
constexpr uint32_t PIXEL_SIZE = 8;   // block is 8 bytes = 64 bits

// Exactly one tile worth of raw pixel data (bytes_per_row = WIDTH * PIXEL_SIZE,
// times HEIGHT rows). The single tile spans the whole item, so before the fix
// the decoder reaches the block-unpacking loop instead of failing a short read.
std::vector<uint8_t> make_pixel_data() {
  return std::vector<uint8_t>(WIDTH * PIXEL_SIZE * HEIGHT, 0x00);
}

// Build a minimal HEIF file with a single 'unci' item using uncompressed
// block-pixel interleave whose components overflow the block.
std::vector<uint8_t> build_heif_unci_block_pixel_overpacked() {
  const std::vector<uint8_t> pixel_data = make_pixel_data();

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

  // uncC (v0): pixel interleave, 16-bit components, block_size == pixel_size == 8.
  std::vector<uint8_t> uncC_payload;
  put_u32_be(uncC_payload, 0);           // profile
  put_u32_be(uncC_payload, COMPONENTS);  // component_count
  for (uint32_t i = 0; i < COMPONENTS; i++) {
    put_u16_be(uncC_payload, static_cast<uint16_t>(i)); // component_index
    uncC_payload.push_back(15);          // component_bit_depth_minus_one -> 16 bit
    uncC_payload.push_back(0);           // component_format (unsigned)
    uncC_payload.push_back(0);           // component_align_size
  }
  uncC_payload.push_back(0);             // sampling_type (no subsampling)
  uncC_payload.push_back(1);             // interleave_type (pixel)
  uncC_payload.push_back(PIXEL_SIZE);    // block_size == pixel_size
  uncC_payload.push_back(0);             // flags
  put_u32_be(uncC_payload, PIXEL_SIZE);  // pixel_size
  put_u32_be(uncC_payload, 0);           // row_align_size
  put_u32_be(uncC_payload, 0);           // tile_align_size
  put_u32_be(uncC_payload, TILE_COLS - 1);
  put_u32_be(uncC_payload, TILE_ROWS - 1);
  auto uncC = make_box("uncC", uncC_payload, /*full=*/true);

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, ispe);
  append(ipco_payload, cmpd);
  append(ipco_payload, uncC);
  auto ipco = make_box("ipco", ipco_payload);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, 1); // entry_count
  put_u16_be(ipma_payload, 1); // item_ID 1
  ipma_payload.push_back(3);   // association_count
  ipma_payload.push_back(0x80 | 1); // essential, ispe
  ipma_payload.push_back(0x80 | 2); // essential, cmpd
  ipma_payload.push_back(0x80 | 3); // essential, uncC
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // idat: the raw (uncompressed) pixel data.
  auto idat = make_box("idat", pixel_data);

  // iloc (version 1): item 1 stored in idat (construction_method=1).
  std::vector<uint8_t> iloc_payload;
  put_u16_be(iloc_payload, (4 << 12) | (4 << 8) | (0 << 4) | 0); // offset_size=4, length_size=4
  put_u16_be(iloc_payload, 1);          // item_count
  put_u16_be(iloc_payload, 1);          // item_ID
  put_u16_be(iloc_payload, 0x0001);     // construction_method=1 (idat)
  put_u16_be(iloc_payload, 0);          // data_reference_index
  put_u16_be(iloc_payload, 1);          // extent_count
  put_u32_be(iloc_payload, 0);          // extent_offset (within idat)
  put_u32_be(iloc_payload, static_cast<uint32_t>(pixel_data.size())); // extent_length
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

} // namespace

TEST_CASE("unci block-pixel overpacked layout is rejected on the tile path") {
  std::vector<uint8_t> file = build_heif_unci_block_pixel_overpacked();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  // The file is structurally valid, so opening it must succeed. The bug is only
  // reachable by then requesting a tile decode.
  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  heif_image_tiling tiling;
  heif_image_handle_get_image_tiling(handle, 1, &tiling);
  REQUIRE(tiling.num_columns == TILE_COLS);
  REQUIRE(tiling.num_rows == TILE_ROWS);

  // Request tile (0,0). Before the fix this reached
  // unc_decoder_block_pixel_interleave::decode_tile() and executed an undefined
  // `block_val >> shift` with shift >= 64 (UBSan abort, else corrupted output).
  // It must now be rejected with a structured invalid-input error from
  // check_hard_limits() on the tile path.
  heif_image* img = nullptr;
  err = heif_image_handle_decode_image_tile(handle, &img,
                                            heif_colorspace_undefined, heif_chroma_undefined,
                                            nullptr,
                                            0, 0);
  INFO("decode error code: " << err.code);
  REQUIRE(err.code == heif_error_Invalid_input);
  REQUIRE(img == nullptr);

  if (img) {
    heif_image_release(img);
  }
  heif_image_handle_release(handle);
  heif_context_free(ctx);
}
