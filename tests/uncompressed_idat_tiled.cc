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

// Regression test: a multi-tile uncompressed 'unci' image whose data is stored
// in the 'idat' box (construction_method == 1) must decode correctly.
//
// The decoder reads each tile as a sub-range of the item data
// (unc_decoder::get_compressed_image_data_uncompressed() ->
// DataExtent::read_data(offset, size)). The 'idat' branch of
// Box_iloc::read_data() used to ignore that (offset, size) window: it read the
// whole extent and did `size -= extent.length` unconditionally, so any partial
// read (size < extent.length) underflowed `size` and returned
// "Not enough data present in 'iloc' to satisfy request." That made every tile
// of an idat-stored multi-tile image fail to decode.
//
// This builds a 4x2 monochrome image split into two 2x2 tiles, stored raw in
// 'idat', and checks that the full-image decode succeeds AND that every pixel
// (including the ones from the second tile) has the expected value, which also
// verifies the offset handling and not merely the absence of an error.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <vector>

namespace {

constexpr uint32_t WIDTH = 4;
constexpr uint32_t HEIGHT = 2;
constexpr uint32_t TILE_COLS = 2;   // two 2x2 tiles side by side
constexpr uint32_t TILE_ROWS = 1;

// Raw pixel data, laid out per tile (component interleave, one 8-bit component),
// row-major within each tile: tile 0 covers x[0..1], tile 1 covers x[2..3].
//
//   image:  row0: 10 11 | 12 13
//           row1: 20 21 | 22 23
const std::vector<uint8_t> kPixelData = {
    10, 11, 20, 21,   // tile 0 (x0..1, y0..1)
    12, 13, 22, 23};  // tile 1 (x2..3, y0..1)

// Expected decoded plane, indexed [y][x].
constexpr uint8_t kExpected[HEIGHT][WIDTH] = {
    {10, 11, 12, 13},
    {20, 21, 22, 23}};

std::vector<uint8_t> build_heif_unci_idat_tiled() {
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

  // cmpd: a single monochrome component (type 0).
  std::vector<uint8_t> cmpd_payload;
  put_u32_be(cmpd_payload, 1);
  put_u16_be(cmpd_payload, 0);
  auto cmpd = make_box("cmpd", cmpd_payload);

  // uncC (v0): component interleave, one 8-bit component, 2x1 tiles.
  std::vector<uint8_t> uncC_payload;
  put_u32_be(uncC_payload, 0);           // profile
  put_u32_be(uncC_payload, 1);           // component_count
  put_u16_be(uncC_payload, 0);           // component_index
  uncC_payload.push_back(7);             // component_bit_depth_minus_one -> 8 bit
  uncC_payload.push_back(0);             // component_format (unsigned)
  uncC_payload.push_back(0);             // component_align_size
  uncC_payload.push_back(0);             // sampling_type (no subsampling)
  uncC_payload.push_back(0);             // interleave_type (component)
  uncC_payload.push_back(0);             // block_size
  uncC_payload.push_back(0);             // flags
  put_u32_be(uncC_payload, 0);           // pixel_size
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

  // idat: the raw pixel data.
  auto idat = make_box("idat", kPixelData);

  // iloc (version 1): item 1 stored in idat (construction_method=1).
  std::vector<uint8_t> iloc_payload;
  put_u16_be(iloc_payload, (4 << 12) | (4 << 8) | (0 << 4) | 0); // offset_size=4, length_size=4
  put_u16_be(iloc_payload, 1);          // item_count
  put_u16_be(iloc_payload, 1);          // item_ID
  put_u16_be(iloc_payload, 0x0001);     // construction_method=1 (idat)
  put_u16_be(iloc_payload, 0);          // data_reference_index
  put_u16_be(iloc_payload, 1);          // extent_count
  put_u32_be(iloc_payload, 0);          // extent_offset (within idat)
  put_u32_be(iloc_payload, static_cast<uint32_t>(kPixelData.size())); // extent_length
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

TEST_CASE("unci multi-tile image stored in idat decodes correctly") {
  std::vector<uint8_t> file = build_heif_unci_idat_tiled();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  // Full-image decode. Before the fix, reading the second (and, because of the
  // size underflow, even the first) tile from the idat extent failed with
  // "Not enough data present in 'iloc'".
  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img,
                          heif_colorspace_monochrome, heif_chroma_monochrome,
                          nullptr);
  INFO("decode error code: " << err.code);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(img != nullptr);

  REQUIRE(heif_image_get_width(img, heif_channel_Y) == static_cast<int>(WIDTH));
  REQUIRE(heif_image_get_height(img, heif_channel_Y) == static_cast<int>(HEIGHT));

  int stride = 0;
  const uint8_t* plane = heif_image_get_plane_readonly(img, heif_channel_Y, &stride);
  REQUIRE(plane != nullptr);

  for (uint32_t y = 0; y < HEIGHT; y++) {
    for (uint32_t x = 0; x < WIDTH; x++) {
      INFO("pixel (" << x << "," << y << ")");
      REQUIRE(static_cast<int>(plane[y * stride + x]) == static_cast<int>(kExpected[y][x]));
    }
  }

  heif_image_release(img);
  heif_image_handle_release(handle);
  heif_context_free(ctx);
}
