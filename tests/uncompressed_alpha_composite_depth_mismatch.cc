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

// Regression test for GHSA-r7gr-2xm2-23wf, modeled on the reporter's PoC
// (a planar RGBA 'unci' image with R=16 bit, G=16 bit, B=8 bit, Alpha=16 bit).
//
// Op_flatten_alpha_plane composites the alpha plane onto the colour planes when
// the output cannot carry alpha. It reads every colour plane, and the alpha
// plane, through a single 'Pixel' type selected from one channel's bit depth,
// which fixes one byte stride for all of them. A file may declare per-component
// bit depths (e.g. 'unci'): with a 16-bit-wide Pixel the operator halves the
// 8-bit blue plane's real stride and reads two bytes per sample, walking off the
// end of the plane on every row -> a heap out-of-bounds read whose bytes are
// folded into the composited RGB output (information disclosure).
//
// The fix is made in the pipeline planner, not in the operator: ColorState
// carries one bit depth per plane, and Op_flatten_alpha_plane::state_after_conversion
// declines any input whose planes mix SDR (one byte per sample) and HDR (two
// bytes per sample). The RGB operators that could otherwise take over with a
// uniform declared output (and fail at runtime) decline a mismatch the same way,
// so no conversion pipeline exists and the decode is refused before any operator
// runs. This test builds such a file, confirms it still decodes natively to the
// mismatched-depth planar image (so the decoder path itself is unaffected),
// requires a decode with alpha compositing to a 16-bit interleaved RGB target to
// be refused cleanly as an unsupported conversion, and checks that the 8-bit
// interleaved target still works (Op_to_sdr_planes equalizes the planes first).
// Under the unfixed code the 16-bit decode reproduces the reporter's ASAN
// heap-buffer-overflow READ in Op_flatten_alpha_plane::convert_colorspace.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <vector>

namespace {

// Use a reasonably large image. The over-read is roughly one blue-plane row per
// row, so the image must be wide/tall enough that it exceeds the plane's
// allocated (stride-padded) size and reaches an AddressSanitizer redzone. Small
// images hide the bug because the stride padding absorbs the doubled read;
// HeifPixelImage also floors every plane allocation at 64 per dimension.
constexpr uint32_t WIDTH = 64;
constexpr uint32_t HEIGHT = 64;

// Build a minimal HEIF file with a single 'unci' item: component (planar)
// interleave, 4:4:4, R=16 bit, G=16 bit, B=8 bit, Alpha=16 bit, uncompressed.
std::vector<uint8_t> build_heif_unci_rgba_mismatched_blue_depth() {
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

  // cmpd: R, G, B, Alpha (component types 4, 5, 6, 7).
  std::vector<uint8_t> cmpd_payload;
  put_u32_be(cmpd_payload, 4);
  put_u16_be(cmpd_payload, 4); // red
  put_u16_be(cmpd_payload, 5); // green
  put_u16_be(cmpd_payload, 6); // blue
  put_u16_be(cmpd_payload, 7); // alpha
  auto cmpd = make_box("cmpd", cmpd_payload);

  // uncC (v0): component (planar) interleave, 4:4:4, R=16, G=16, B=8, Alpha=16.
  const uint8_t depths[4] = {16, 16, 8, 16}; // R, G, B, Alpha
  std::vector<uint8_t> uncC_payload;
  put_u32_be(uncC_payload, 0); // profile
  put_u32_be(uncC_payload, 4); // component_count
  for (uint16_t idx = 0; idx < 4; idx++) {
    put_u16_be(uncC_payload, idx);                                // component_index
    uncC_payload.push_back(static_cast<uint8_t>(depths[idx] - 1)); // component_bit_depth_minus_one
    uncC_payload.push_back(0);                                    // component_format (unsigned)
    uncC_payload.push_back(0);                                    // component_align_size
  }
  uncC_payload.push_back(0);            // sampling_type = 4:4:4
  uncC_payload.push_back(0);            // interleave_type = component (planar)
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

  // --- Tile data (planar): R plane (2 bytes/sample, big-endian), then G plane
  // (2 bytes/sample), then B plane (1 byte/sample), then Alpha plane (2
  // bytes/sample). Alpha is near-maximum so the composite weights the colour
  // sample heavily (this is what surfaces over-read bytes in the output).
  std::vector<uint8_t> tile_data;
  tile_data.reserve(WIDTH * HEIGHT * (2 + 2 + 1 + 2));

  for (uint32_t y = 0; y < HEIGHT; y++)      // R (16 bit)
    for (uint32_t x = 0; x < WIDTH; x++)
      put_u16_be(tile_data, static_cast<uint16_t>(0x1000 + x + WIDTH * y));

  for (uint32_t y = 0; y < HEIGHT; y++)      // G (16 bit)
    for (uint32_t x = 0; x < WIDTH; x++)
      put_u16_be(tile_data, static_cast<uint16_t>(0x2000 + x + WIDTH * y));

  for (uint32_t y = 0; y < HEIGHT; y++)      // B (8 bit)
    for (uint32_t x = 0; x < WIDTH; x++)
      tile_data.push_back(static_cast<uint8_t>(0x40 + x + WIDTH * y));

  for (uint32_t y = 0; y < HEIGHT; y++)      // Alpha (16 bit)
    for (uint32_t x = 0; x < WIDTH; x++)
      put_u16_be(tile_data, 0xFF00);

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

TEST_CASE("unci RGBA with mismatched colour bit depths refuses alpha compositing without heap overread") {
  std::vector<uint8_t> file = build_heif_unci_rgba_mismatched_blue_depth();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  REQUIRE(heif_image_handle_get_width(handle) == static_cast<int>(WIDTH));
  REQUIRE(heif_image_handle_get_height(handle) == static_cast<int>(HEIGHT));

  // The mismatched-depth planes decode natively without any color conversion,
  // so the decoder path itself is unaffected.
  {
    heif_image* img = nullptr;
    err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
    INFO("native decode error (" << err.code << "/" << err.subcode << "): " << err.message);
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(img != nullptr);
    if (img != nullptr) {
      heif_image_release(img);
    }
  }

  heif_decoding_options* options = heif_decoding_options_alloc();
  REQUIRE(options != nullptr);

  heif_color_conversion_options_ext* ext = heif_color_conversion_options_ext_alloc();
  REQUIRE(ext != nullptr);
  ext->alpha_composition_mode = heif_alpha_composition_mode_solid_color;
  ext->background_red = 0xFFFF;
  ext->background_green = 0xFFFF;
  ext->background_blue = 0xFFFF;
  options->color_conversion_options_ext = ext;

  // Request alpha compositing onto a solid background while decoding to a
  // 16-bit interleaved RGB target. This used to select Op_flatten_alpha_plane
  // with a 16-bit Pixel, which is where the over-read of the 8-bit blue plane
  // occurred. With the fix no operator accepts the mixed SDR/HDR planes, so no
  // conversion pipeline can be built and the decode must fail cleanly as an
  // unsupported conversion rather than reading past the blue plane. Under the
  // unfixed code this decode reproduces the reporter's ASAN heap-buffer-overflow
  // READ in Op_flatten_alpha_plane::convert_colorspace.
  {
    heif_image* img = nullptr;
    err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RRGGBB_LE, options);
    INFO("composite decode error (" << err.code << "/" << err.subcode << "): " << err.message);
    REQUIRE(err.code == heif_error_Unsupported_feature);
    REQUIRE(err.subcode == heif_suberror_Unsupported_color_conversion);
    REQUIRE(img == nullptr);

    if (img != nullptr) {
      heif_image_release(img);
    }
  }

  // The same request for an 8-bit interleaved target must still work: the
  // pipeline lowers every plane to 8 bits first (Op_to_sdr_planes handles each
  // plane at its own depth), after which the planes are uniform and compositing
  // is safe. The fix must not reject this.
  {
    heif_image* img = nullptr;
    err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, options);
    INFO("8-bit composite decode error (" << err.code << "/" << err.subcode << "): " << err.message);
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(img != nullptr);
    REQUIRE(heif_image_get_chroma_format(img) == heif_chroma_interleaved_RGB);
    REQUIRE(heif_image_has_channel(img, heif_channel_Alpha) == 0);
    REQUIRE(heif_image_get_bits_per_pixel_range(img, heif_channel_interleaved) == 8);
    heif_image_release(img);
  }

  heif_color_conversion_options_ext_free(ext);
  heif_decoding_options_free(options);

  heif_image_handle_release(handle);
  heif_context_free(ctx);
}
