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

// Regression test for GHSA-x8r2-mggj-j6wr, modeled on the reporter's PoC
// ('overflow.heif': a Y8/Cb16/Cr8, 4:2:0, mixed-interleave 'unci' image).
//
// unc_decoder_mixed_interleave::processTile() handles Cb and Cr together: it
// reads and writes both chroma samples per pixel using only the byte width of
// whichever of the two components is declared first in the uncC component
// list. Each chroma plane is allocated according to its OWN component's bit
// depth, so when Cb and Cr are declared with different depths (here Cb=16
// bit, Cr=8 bit), the second (Cr) write used the first (Cb) component's
// 2-byte width and column stride against a plane sized for 1 byte per sample:
// a heap out-of-bounds write, repeated once per chroma sample, with
// attacker-controlled bytes.
//
// This test builds a minimal 8x8 (chroma 4x4) file with that exact
// configuration and decodes it natively (heif_colorspace_undefined /
// heif_chroma_undefined, to avoid the unrelated, non-security limitation
// that YCbCr->RGB conversion does not support mismatched chroma bit depths).
// Under the unfixed decoder this reproduces the reporter's ASAN trace
// (heap-buffer-overflow WRITE in memcpy_to_native_endian(), called from
// unc_decoder_mixed_interleave::processTile()). With the fix, decoding must
// succeed and every Y/Cb/Cr sample must come back exactly as encoded.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <vector>

namespace {

constexpr uint32_t WIDTH = 8;
constexpr uint32_t HEIGHT = 8;
constexpr uint32_t CHROMA_WIDTH = WIDTH / 2;
constexpr uint32_t CHROMA_HEIGHT = HEIGHT / 2;

// Build a minimal HEIF file with a single 'unci' item: mixed interleave,
// 4:2:0 sampling, Y=8 bit, Cb=16 bit, Cr=8 bit, uncompressed (no cmpC).
std::vector<uint8_t> build_heif_unci_mixed_chroma_depth_mismatch() {
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

  // cmpd: Y, Cb, Cr.
  std::vector<uint8_t> cmpd_payload;
  put_u32_be(cmpd_payload, 3);
  put_u16_be(cmpd_payload, 1); // Y
  put_u16_be(cmpd_payload, 2); // Cb
  put_u16_be(cmpd_payload, 3); // Cr
  auto cmpd = make_box("cmpd", cmpd_payload);

  // uncC (v0): mixed interleave, 4:2:0, Y=8 bit, Cb=16 bit, Cr=8 bit.
  const uint8_t depths[3] = {8, 16, 8}; // Y, Cb, Cr
  std::vector<uint8_t> uncC_payload;
  put_u32_be(uncC_payload, 0); // profile
  put_u32_be(uncC_payload, 3); // component_count
  for (uint16_t idx = 0; idx < 3; idx++) {
    put_u16_be(uncC_payload, idx);                              // component_index
    uncC_payload.push_back(static_cast<uint8_t>(depths[idx] - 1)); // component_bit_depth_minus_one
    uncC_payload.push_back(0);                                  // component_format (unsigned)
    uncC_payload.push_back(0);                                  // component_align_size
  }
  uncC_payload.push_back(2);            // sampling_type = 4:2:0
  uncC_payload.push_back(2);            // interleave_type = mixed
  uncC_payload.push_back(0);            // block_size
  uncC_payload.push_back(0);            // flags (big-endian components)
  put_u32_be(uncC_payload, 0);          // pixel_size
  put_u32_be(uncC_payload, 0);          // row_align_size
  put_u32_be(uncC_payload, 0);          // tile_align_size
  put_u32_be(uncC_payload, 0);          // num_tile_cols_minus_one
  put_u32_be(uncC_payload, 0);          // num_tile_rows_minus_one
  auto uncC = make_box("uncC", uncC_payload, /*full=*/true);

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, ispe);
  append(ipco_payload, cmpd);
  append(ipco_payload, uncC);
  auto ipco = make_box("ipco", ipco_payload);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, 1);      // entry_count
  put_u16_be(ipma_payload, 1);      // item_ID 1
  ipma_payload.push_back(3);        // association_count
  ipma_payload.push_back(0x80 | 1); // essential, ispe
  ipma_payload.push_back(0x80 | 2); // essential, cmpd
  ipma_payload.push_back(0x80 | 3); // essential, uncC
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // --- Tile data: Y plane (row-major, 1 byte/sample), followed by the
  // interleaved chroma block. get_tile_data_sizes() sums each component's
  // own row size independently, which for these byte-aligned depths equals
  // the actual interleaved byte count consumed by processTile(): 64 (Y) +
  // 4*4*(2+1) (Cb+Cr interleaved) = 112 bytes.
  std::vector<uint8_t> tile_data;
  tile_data.reserve(WIDTH * HEIGHT + CHROMA_WIDTH * CHROMA_HEIGHT * 3);

  for (uint32_t y = 0; y < HEIGHT; y++) {
    for (uint32_t x = 0; x < WIDTH; x++) {
      tile_data.push_back(static_cast<uint8_t>((x + WIDTH * y) & 0xFF));
    }
  }
  for (uint32_t y = 0; y < CHROMA_HEIGHT; y++) {
    for (uint32_t x = 0; x < CHROMA_WIDTH; x++) {
      uint16_t cb = static_cast<uint16_t>(0x1000 + y * CHROMA_WIDTH + x);
      uint8_t cr = static_cast<uint8_t>(0x40 + y * CHROMA_WIDTH + x);
      put_u16_be(tile_data, cb);
      tile_data.push_back(cr);
    }
  }

  auto idat = make_box("idat", tile_data);

  // iloc (version 1): item 1 stored in idat (construction_method=1).
  std::vector<uint8_t> iloc_payload;
  put_u16_be(iloc_payload, (4 << 12) | (4 << 8) | (0 << 4) | 0); // offset_size=4, length_size=4
  put_u16_be(iloc_payload, 1);      // item_count
  put_u16_be(iloc_payload, 1);      // item_ID
  put_u16_be(iloc_payload, 0x0001); // construction_method=1 (idat)
  put_u16_be(iloc_payload, 0);      // data_reference_index
  put_u16_be(iloc_payload, 1);      // extent_count
  put_u32_be(iloc_payload, 0);      // extent_offset (within idat)
  put_u32_be(iloc_payload, static_cast<uint32_t>(tile_data.size())); // extent_length
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

TEST_CASE("unci mixed-interleave with mismatched Cb/Cr bit depths decodes without heap overflow") {
  std::vector<uint8_t> file = build_heif_unci_mixed_chroma_depth_mismatch();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  // Decode natively (no forced colorspace conversion): before the fix, this
  // already reaches the vulnerable paired-chroma write in
  // unc_decoder_mixed_interleave::processTile() and corrupts the heap.
  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(img != nullptr);

  REQUIRE(heif_image_get_bits_per_pixel(img, heif_channel_Y) == 8);
  REQUIRE(heif_image_get_bits_per_pixel(img, heif_channel_Cb) == 16);
  REQUIRE(heif_image_get_bits_per_pixel(img, heif_channel_Cr) == 8);
  REQUIRE(heif_image_get_width(img, heif_channel_Cb) == static_cast<int>(CHROMA_WIDTH));
  REQUIRE(heif_image_get_height(img, heif_channel_Cb) == static_cast<int>(CHROMA_HEIGHT));

  int stride = 0;
  const uint8_t* y_plane = heif_image_get_plane_readonly(img, heif_channel_Y, &stride);
  REQUIRE(y_plane != nullptr);
  for (uint32_t y = 0; y < HEIGHT; y++) {
    for (uint32_t x = 0; x < WIDTH; x++) {
      INFO("Y at (" << x << "," << y << ")");
      REQUIRE(y_plane[y * stride + x] == static_cast<uint8_t>((x + WIDTH * y) & 0xFF));
    }
  }

  // The Cr plane is the one that was undersized relative to the (wrongly
  // reused) 2-byte write width. Every sample must come back exactly as
  // encoded, not corrupted or shifted by the overflowing Cb-sized writes.
  const auto* cb_plane = reinterpret_cast<const uint16_t*>(
      heif_image_get_plane_readonly(img, heif_channel_Cb, &stride));
  REQUIRE(cb_plane != nullptr);
  int cb_stride_elems = stride / 2;
  for (uint32_t y = 0; y < CHROMA_HEIGHT; y++) {
    for (uint32_t x = 0; x < CHROMA_WIDTH; x++) {
      INFO("Cb at (" << x << "," << y << ")");
      REQUIRE(cb_plane[y * cb_stride_elems + x] == static_cast<uint16_t>(0x1000 + y * CHROMA_WIDTH + x));
    }
  }

  const uint8_t* cr_plane = heif_image_get_plane_readonly(img, heif_channel_Cr, &stride);
  REQUIRE(cr_plane != nullptr);
  for (uint32_t y = 0; y < CHROMA_HEIGHT; y++) {
    for (uint32_t x = 0; x < CHROMA_WIDTH; x++) {
      INFO("Cr at (" << x << "," << y << ")");
      REQUIRE(cr_plane[y * stride + x] == static_cast<uint8_t>(0x40 + y * CHROMA_WIDTH + x));
    }
  }

  heif_image_release(img);
  heif_image_handle_release(handle);
  heif_context_free(ctx);
}
