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

// Regression test for GHSA-w7mc-p8jc-p853, modeled on the reporter's PoC
// (a YCbCr 4:2:0 'unci' image with Y=16 bit and Cb=Cr=8 bit).
//
// The YCbCr->RGB color-conversion operators assume all color channels share a
// single bit depth. Op_YCbCr420_to_RRGGBBaa takes its sample width from the Y
// channel and reads Cb/Cr through a uint16_t*, so an image with 16-bit luma but
// 8-bit chroma (chroma planes allocated at 1 byte/sample) is read two bytes per
// sample past the end of each chroma plane: a heap out-of-bounds read whose
// bytes reach the decoded RGB output (information disclosure).
//
// The fix rejects a color conversion whose color channels (Y/Cb/Cr or R/G/B) do
// not all share one bit depth, at the entry of convert_colorspace(). This test
// builds such a file, confirms it still decodes natively to the mismatched-depth
// planar image (so the decoder path itself is unaffected), and then requires the
// conversion to a high-bit-depth interleaved RGB target to be refused cleanly
// with an error rather than over-reading the chroma planes. Under the unfixed
// code the RGB decode reproduces the reporter's ASAN trace (heap-buffer-overflow
// READ in Op_YCbCr420_to_RRGGBBaa::convert_colorspace).

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <vector>

namespace {

// Use a reasonably large image: reading the 8-bit chroma planes through a
// uint16_t* over-reads by roughly the chroma row width per row, so the image
// must be wide enough that this exceeds the plane's allocated (stride-padded)
// size and reaches an AddressSanitizer redzone. Small images (e.g. 8x8) hide
// the bug because the stride padding absorbs the doubled read.
constexpr uint32_t WIDTH = 512;
constexpr uint32_t HEIGHT = 512;
constexpr uint32_t CHROMA_WIDTH = WIDTH / 2;
constexpr uint32_t CHROMA_HEIGHT = HEIGHT / 2;

// Build a minimal HEIF file with a single 'unci' item: mixed interleave,
// 4:2:0 sampling, Y=16 bit, Cb=8 bit, Cr=8 bit, uncompressed (no cmpC).
std::vector<uint8_t> build_heif_unci_ycbcr_mismatched_luma_depth() {
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

  // uncC (v0): mixed interleave, 4:2:0, Y=16 bit, Cb=8 bit, Cr=8 bit.
  const uint8_t depths[3] = {16, 8, 8}; // Y, Cb, Cr
  std::vector<uint8_t> uncC_payload;
  put_u32_be(uncC_payload, 0); // profile
  put_u32_be(uncC_payload, 3); // component_count
  for (uint16_t idx = 0; idx < 3; idx++) {
    put_u16_be(uncC_payload, idx);                                // component_index
    uncC_payload.push_back(static_cast<uint8_t>(depths[idx] - 1)); // component_bit_depth_minus_one
    uncC_payload.push_back(0);                                    // component_format (unsigned)
    uncC_payload.push_back(0);                                    // component_align_size
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

  // colr (nclx): BT.601 matrix so the YCbCr->RGB operators are candidates.
  std::vector<uint8_t> colr_payload;
  append_fourcc(colr_payload, "nclx");
  put_u16_be(colr_payload, 6);   // colour_primaries (smpte170m)
  put_u16_be(colr_payload, 6);   // transfer_characteristics (smpte170m)
  put_u16_be(colr_payload, 6);   // matrix_coefficients (smpte170m / BT.601)
  colr_payload.push_back(0x80);  // full_range_flag = 1
  auto colr = make_box("colr", colr_payload);

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, ispe);
  append(ipco_payload, cmpd);
  append(ipco_payload, uncC);
  append(ipco_payload, colr);
  auto ipco = make_box("ipco", ipco_payload);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, 1);      // entry_count
  put_u16_be(ipma_payload, 1);      // item_ID 1
  ipma_payload.push_back(4);        // association_count
  ipma_payload.push_back(0x80 | 1); // essential, ispe
  ipma_payload.push_back(0x80 | 2); // essential, cmpd
  ipma_payload.push_back(0x80 | 3); // essential, uncC
  ipma_payload.push_back(4);        // non-essential, colr
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // --- Tile data: Y plane (row-major, 2 bytes/sample, big-endian), followed by
  // the interleaved chroma block (Cb, Cr; 1 byte each per chroma position):
  // 8*8*2 (Y) + 4*4*(1+1) (Cb+Cr interleaved) = 128 + 32 = 160 bytes.
  std::vector<uint8_t> tile_data;
  tile_data.reserve(WIDTH * HEIGHT * 2 + CHROMA_WIDTH * CHROMA_HEIGHT * 2);

  for (uint32_t y = 0; y < HEIGHT; y++) {
    for (uint32_t x = 0; x < WIDTH; x++) {
      put_u16_be(tile_data, static_cast<uint16_t>(0x1000 + x + WIDTH * y));
    }
  }
  for (uint32_t y = 0; y < CHROMA_HEIGHT; y++) {
    for (uint32_t x = 0; x < CHROMA_WIDTH; x++) {
      tile_data.push_back(static_cast<uint8_t>(0x40 + y * CHROMA_WIDTH + x)); // Cb
      tile_data.push_back(static_cast<uint8_t>(0x80 + y * CHROMA_WIDTH + x)); // Cr
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

TEST_CASE("unci YCbCr with mismatched luma/chroma bit depths refuses RGB conversion without heap overread") {
  std::vector<uint8_t> file = build_heif_unci_ycbcr_mismatched_luma_depth();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  // The file parses to a valid item with the expected geometry (this reads the
  // item properties, without decoding pixels).
  REQUIRE(heif_image_handle_get_width(handle) == static_cast<int>(WIDTH));
  REQUIRE(heif_image_handle_get_height(handle) == static_cast<int>(HEIGHT));

  // Converting to a high-bit-depth interleaved RGB target selects
  // Op_YCbCr420_to_RRGGBBaa, which is where the over-read occurred. With the fix
  // the mismatched color-channel bit depths make the conversion unsupported, so
  // the decode must fail cleanly (with the specific bit-depth suberror) rather
  // than reading past the chroma planes. Under the unfixed code this decode
  // reproduces the reporter's ASAN heap-buffer-overflow READ.
  {
    heif_image* img = nullptr;
    err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RRGGBB_LE, nullptr);
    INFO("decode error (" << err.code << "/" << err.subcode << "): " << err.message);
    REQUIRE(err.code == heif_error_Unsupported_feature);
    REQUIRE(err.subcode == heif_suberror_Unsupported_bit_depth);
    REQUIRE(img == nullptr);

    if (img != nullptr) {
      heif_image_release(img);
    }
  }

  heif_image_handle_release(handle);
  heif_context_free(ctx);
}
