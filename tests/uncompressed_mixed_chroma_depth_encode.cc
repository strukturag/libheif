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

// Regression test for the encode-path counterpart of GHSA-w7mc-p8jc-p853.
//
// The per-channel bit-depth check added for that advisory lives in
// convert_colorspace(). On the encode path that function is not always reached:
// Encoder::convert_colorspace_for_encoding() returns early when the image
// already has the colorspace and chroma format the encoder asked for, which is
// exactly the case for a YCbCr 4:2:0 image handed to the AV1 or HEVC encoders.
// The image then arrived at the plugin with Y at 10 bits and Cb/Cr at 8, and
// the aom and x265 plugins took the sample width from the luma channel and
// applied it to the chroma planes. HeifPixelImage allocates one byte per sample
// at 8 bits and two above that, so each chroma row was read at twice its
// allocated width: a heap out-of-bounds read whose bytes ended up in the
// encoded output.
//
// The fix does not live in the color conversion. Whether an image with
// differing per-channel bit depths can be encoded depends on the codec and on
// the encoder implementation (HEVC and H.264 can signal separate luma and
// chroma bit depths, AV1 and VVC cannot, and JPEG 2000 genuinely supports a
// precision per component), so each encoder plugin checks its own input.
//
// This test builds a 'unci' file with Y=10 bit and Cb=Cr=8 bit, confirms that
// it still decodes natively to a mismatched-depth planar image (the decoder is
// allowed to produce one), and then requires the encoders to refuse it cleanly
// instead of over-reading the chroma planes. Under the unfixed code the encode
// reproduces a heap-buffer-overflow READ in the encoder plugin.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <vector>

namespace {

// The over-read is roughly half a chroma row per row, so the image has to be
// wide enough that it exceeds the stride padding and reaches a sanitizer
// redzone. Small images hide the bug.
constexpr uint32_t WIDTH = 512;
constexpr uint32_t HEIGHT = 512;
constexpr uint32_t CHROMA_WIDTH = WIDTH / 2;
constexpr uint32_t CHROMA_HEIGHT = HEIGHT / 2;

// 10 bits, not 16: it has to be a bit depth the encoders actually accept, or
// they would reject the image for an unrelated reason before ever reading the
// chroma planes, and the test would pass without testing anything.
constexpr int LUMA_BITS = 10;

// Bit-packed big-endian writer for the luma samples.
class BitWriter {
public:
  explicit BitWriter(std::vector<uint8_t>& out) : m_out(out) {}

  void write(uint32_t value, int bits) {
    m_acc = (m_acc << bits) | (value & ((1u << bits) - 1));
    m_nbits += bits;
    while (m_nbits >= 8) {
      m_nbits -= 8;
      m_out.push_back(static_cast<uint8_t>((m_acc >> m_nbits) & 0xFF));
    }
  }

  void flush() {
    if (m_nbits > 0) {
      m_out.push_back(static_cast<uint8_t>((m_acc << (8 - m_nbits)) & 0xFF));
      m_nbits = 0;
    }
  }

private:
  std::vector<uint8_t>& m_out;
  uint32_t m_acc = 0;
  int m_nbits = 0;
};

// Build a minimal HEIF file with a single 'unci' item: mixed interleave,
// 4:2:0 sampling, Y=10 bit, Cb=8 bit, Cr=8 bit, uncompressed (no cmpC).
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

  std::vector<uint8_t> ispe_payload;
  put_u32_be(ispe_payload, WIDTH);
  put_u32_be(ispe_payload, HEIGHT);
  auto ispe = make_box("ispe", ispe_payload, /*full=*/true);

  std::vector<uint8_t> cmpd_payload;
  put_u32_be(cmpd_payload, 3);
  put_u16_be(cmpd_payload, 1); // Y
  put_u16_be(cmpd_payload, 2); // Cb
  put_u16_be(cmpd_payload, 3); // Cr
  auto cmpd = make_box("cmpd", cmpd_payload);

  // uncC (v0): mixed interleave, 4:2:0, Y=10 bit, Cb=8 bit, Cr=8 bit.
  const uint8_t depths[3] = {LUMA_BITS, 8, 8};
  std::vector<uint8_t> uncC_payload;
  put_u32_be(uncC_payload, 0); // profile
  put_u32_be(uncC_payload, 3); // component_count
  for (uint16_t idx = 0; idx < 3; idx++) {
    put_u16_be(uncC_payload, idx);                                 // component_index
    uncC_payload.push_back(static_cast<uint8_t>(depths[idx] - 1)); // component_bit_depth_minus_one
    uncC_payload.push_back(0);                                     // component_format (unsigned)
    uncC_payload.push_back(0);                                     // component_align_size
  }
  uncC_payload.push_back(2);   // sampling_type = 4:2:0
  uncC_payload.push_back(2);   // interleave_type = mixed
  uncC_payload.push_back(0);   // block_size
  uncC_payload.push_back(0);   // flags (big-endian components)
  put_u32_be(uncC_payload, 0); // pixel_size
  put_u32_be(uncC_payload, 0); // row_align_size
  put_u32_be(uncC_payload, 0); // tile_align_size
  put_u32_be(uncC_payload, 0); // num_tile_cols_minus_one
  put_u32_be(uncC_payload, 0); // num_tile_rows_minus_one
  auto uncC = make_box("uncC", uncC_payload, /*full=*/true);

  // colr (nclx). The values matter: if they disagree with the encoding target,
  // Encoder::convert_colorspace_for_encoding() runs a color conversion and the
  // guard in convert_colorspace() catches the image before it reaches the
  // plugin. These are the defaults that heif-enc requests, so the early return
  // is taken and the image reaches the encoder unconverted.
  std::vector<uint8_t> colr_payload;
  append_fourcc(colr_payload, "nclx");
  put_u16_be(colr_payload, 1);  // colour_primaries (BT.709)
  put_u16_be(colr_payload, 13); // transfer_characteristics (sRGB)
  put_u16_be(colr_payload, 6);  // matrix_coefficients (BT.601)
  colr_payload.push_back(0x80); // full_range_flag = 1
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

  // Tile data: the bit-packed 10-bit luma plane, followed by the interleaved
  // chroma block (Cb, Cr; one byte each per chroma position).
  std::vector<uint8_t> tile_data;
  tile_data.reserve(WIDTH * HEIGHT * LUMA_BITS / 8 + CHROMA_WIDTH * CHROMA_HEIGHT * 2);

  {
    BitWriter bw(tile_data);
    for (uint32_t y = 0; y < HEIGHT; y++) {
      for (uint32_t x = 0; x < WIDTH; x++) {
        bw.write(0x200 + ((x + y) & 0xFF), LUMA_BITS);
      }
    }
    bw.flush();
  }

  for (uint32_t y = 0; y < CHROMA_HEIGHT; y++) {
    for (uint32_t x = 0; x < CHROMA_WIDTH; x++) {
      tile_data.push_back(static_cast<uint8_t>(0x40 + ((y * CHROMA_WIDTH + x) & 0x3F))); // Cb
      tile_data.push_back(static_cast<uint8_t>(0x80 + ((y * CHROMA_WIDTH + x) & 0x3F))); // Cr
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
  put_u32_be(iloc_payload, static_cast<uint32_t>(tile_data.size()));
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

// Decode the crafted file in its native colorspace, the way heif-enc does, so
// that the mismatched per-channel bit depths survive into the encoder.
heif_image* decode_mismatched_image(heif_context* ctx) {
  heif_image_handle* handle = nullptr;
  heif_error err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  heif_decoding_options* options = heif_decoding_options_alloc();
  REQUIRE(options != nullptr);
  options->output_image_nclx_profile_passthrough = true;

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, options);

  heif_decoding_options_free(options);
  heif_image_handle_release(handle);

  INFO("decode error (" << err.code << "/" << err.subcode << "): " << err.message);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(img != nullptr);

  return img;
}

// Encoding must fail cleanly. It must never over-read the chroma planes, which
// is what a sanitizer build catches, and it must never silently succeed, which
// would mean the over-read bytes were encoded into the output.
void require_encode_refused(heif_image* img, heif_compression_format format) {
  heif_context* out_ctx = heif_context_alloc();
  REQUIRE(out_ctx != nullptr);

  heif_encoder* encoder = nullptr;
  heif_error err = heif_context_get_encoder_for_format(out_ctx, format, &encoder);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(encoder != nullptr);

  heif_image_handle* out_handle = nullptr;
  err = heif_context_encode_image(out_ctx, img, encoder, nullptr, &out_handle);

  INFO("encode error (" << err.code << "/" << err.subcode << "): "
                        << (err.message ? err.message : "(null)"));
  REQUIRE(err.code != heif_error_Ok);

  // An encoder may bail out earlier for a reason of its own (for example a
  // build of x265 without high bit depth support), but when it is our check
  // that fires, it must report the bit depth as the reason.
  if (err.code == heif_error_Encoder_plugin_error) {
    REQUIRE(err.subcode == heif_suberror_Unsupported_bit_depth);
  }

  if (out_handle != nullptr) {
    heif_image_handle_release(out_handle);
  }
  heif_encoder_release(encoder);
  heif_context_free(out_ctx);
}

} // namespace

TEST_CASE("unci YCbCr with mismatched luma/chroma bit depths is refused by the encoders")
{
  if (!heif_have_decoder_for_format(heif_compression_uncompressed)) {
    SKIP("Skipping test because uncompressed codec is not compiled.");
  }

  std::vector<uint8_t> file = build_heif_unci_ycbcr_mismatched_luma_depth();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image* img = decode_mismatched_image(ctx);

  // The decoder is allowed to produce this image: ISO/IEC 23001-17 declares
  // component_bit_depth per component. It is the encoders that cannot take it.
  REQUIRE(heif_image_get_colorspace(img) == heif_colorspace_YCbCr);
  REQUIRE(heif_image_get_chroma_format(img) == heif_chroma_420);
  REQUIRE(heif_image_get_bits_per_pixel_range(img, heif_channel_Y) == LUMA_BITS);
  REQUIRE(heif_image_get_bits_per_pixel_range(img, heif_channel_Cb) == 8);
  REQUIRE(heif_image_get_bits_per_pixel_range(img, heif_channel_Cr) == 8);

  if (heif_have_encoder_for_format(heif_compression_AV1)) {
    require_encode_refused(img, heif_compression_AV1);
  }

  if (heif_have_encoder_for_format(heif_compression_HEVC)) {
    require_encode_refused(img, heif_compression_HEVC);
  }

  heif_image_release(img);
  heif_context_free(ctx);
}
