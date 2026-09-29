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

// Regression test for OSS-Fuzz 5154611212910592 (sequence_fuzzer).
//
// ISO/IEC 23001-17 lets an 'unci' component declare a bit depth of up to 256
// bits. libheif accepts up to 128 of those on purpose: the byte-aligned
// component decoder stores 32/64/128 bit samples so that they can be read back
// through the component API. The color-conversion operators, however, are all written
// for 8-bit or 16-bit integer samples. They access the planes through
// uint8_t* / uint16_t* and derive shift amounts and midpoint values from the
// bit depth. A 64-bit monochrome component converted to YCbCr 4:2:0 evaluated
// '128 << (bit_depth - 8)', shifting an 'int' by 56: undefined behaviour
// (UndefinedBehaviorSanitizer abort in Op_mono_to_YCbCr420::convert_colorspace).
//
// Each operator now declines a bit depth it cannot access, so an operator that
// cannot handle a wide sample never offers itself to the pipeline and pipeline
// construction fails; convert_colorspace() keeps a backstop check behind that.
// This test builds such a file, confirms the decoder path itself is unaffected
// (the untransformed decode still returns the 64-bit monochrome plane), and
// requires the conversions that used to run the unsupported operators to fail
// cleanly instead.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <vector>

namespace {

constexpr uint32_t WIDTH = 4;
constexpr uint32_t HEIGHT = 4;
constexpr uint32_t BYTES_PER_SAMPLE = 8; // 64 bit

// Build a minimal HEIF file with a single 'unci' item: one monochrome
// component of 64 bits, component interleave, no subsampling, uncompressed.
std::vector<uint8_t> build_heif_unci_mono64() {
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

  // cmpd: a single monochrome component.
  std::vector<uint8_t> cmpd_payload;
  put_u32_be(cmpd_payload, 1);
  put_u16_be(cmpd_payload, 0); // monochrome
  auto cmpd = make_box("cmpd", cmpd_payload);

  // uncC (v0): component interleave, no subsampling, one 64-bit component.
  std::vector<uint8_t> uncC_payload;
  put_u32_be(uncC_payload, 0); // profile
  put_u32_be(uncC_payload, 1); // component_count
  put_u16_be(uncC_payload, 0); // component_index
  uncC_payload.push_back(63);  // component_bit_depth_minus_one -> 64 bit
  uncC_payload.push_back(0);   // component_format (unsigned)
  uncC_payload.push_back(0);   // component_align_size
  uncC_payload.push_back(0);   // sampling_type = none
  uncC_payload.push_back(0);   // interleave_type = component
  uncC_payload.push_back(0);   // block_size
  uncC_payload.push_back(0);   // flags (big-endian components)
  put_u32_be(uncC_payload, 0); // pixel_size
  put_u32_be(uncC_payload, 0); // row_align_size
  put_u32_be(uncC_payload, 0); // tile_align_size
  put_u32_be(uncC_payload, 0); // num_tile_cols_minus_one
  put_u32_be(uncC_payload, 0); // num_tile_rows_minus_one
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

  // Tile data: one plane of 64-bit big-endian samples.
  std::vector<uint8_t> tile_data;
  tile_data.reserve(WIDTH * HEIGHT * BYTES_PER_SAMPLE);
  for (uint32_t i = 0; i < WIDTH * HEIGHT; i++) {
    for (uint32_t b = 0; b < BYTES_PER_SAMPLE; b++) {
      tile_data.push_back(static_cast<uint8_t>(i + b));
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

TEST_CASE("unci with a 64-bit component refuses color conversion instead of shifting out of range") {
  std::vector<uint8_t> file = build_heif_unci_mono64();

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

  // The decoder itself still handles the wide component: decoding without a
  // colorspace conversion returns the monochrome plane at its declared 64 bits.
  // The guard sits behind the nop check, so this path must keep working.
  {
    heif_image* img = nullptr;
    err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
    INFO("native decode error (" << err.code << "/" << err.subcode << "): " << err.message);
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(img != nullptr);
    REQUIRE(heif_image_get_bits_per_pixel(img, heif_channel_Y) == 64);
    heif_image_release(img);
  }

  // Converting to YCbCr 4:2:0 selects Op_mono_to_YCbCr420, which computed the
  // chroma midpoint as '128 << (bit_depth - 8)'. With a 64-bit component that
  // shifted an 'int' by 56. The conversion must now be refused cleanly.
  //
  // Op_mono_to_YCbCr420::state_after_conversion() declines a sample wider than 16
  // bits, so pipeline construction fails before the catch-all in
  // convert_colorspace() is reached: the subcode is Unsupported_color_conversion.
  // Should the operators ever stop declining wide samples, the catch-all answers
  // with Unsupported_bit_depth instead. Either refusal is correct here; what
  // matters is that the conversion does not run and nothing shifts out of range.
  {
    heif_image* img = nullptr;
    err = heif_decode_image(handle, &img, heif_colorspace_YCbCr, heif_chroma_420, nullptr);
    INFO("YCbCr decode error (" << err.code << "/" << err.subcode << "): " << err.message);
    REQUIRE(err.code == heif_error_Unsupported_feature);
    REQUIRE((err.subcode == heif_suberror_Unsupported_color_conversion ||
             err.subcode == heif_suberror_Unsupported_bit_depth));
    REQUIRE(img == nullptr);

    if (img != nullptr) {
      heif_image_release(img);
    }
  }

  // The same applies to an RGB target, which would read the 8-byte samples
  // through a uint8_t* / uint16_t* view of the plane.
  {
    heif_image* img = nullptr;
    err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);
    INFO("RGB decode error (" << err.code << "/" << err.subcode << "): " << err.message);
    REQUIRE(err.code != heif_error_Ok);
    REQUIRE(img == nullptr);

    if (img != nullptr) {
      heif_image_release(img);
    }
  }

  heif_image_handle_release(handle);
  heif_context_free(ctx);
}
