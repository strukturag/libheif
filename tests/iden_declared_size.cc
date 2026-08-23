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

// Build a minimal HEIF file with two items:
//   item 1 ('mski', 8x8, 8bpp): real, decodable content (the mask codec needs
//           no bitstream -- its "compressed" data is just the raw pixel bytes).
//   item 2 ('iden', primary): 'dimg'-references item 1, and carries its own
//           'ispe' declaring (decl_w x decl_h), which may or may not match
//           item 1's actual 8x8.
// No codec plugin is required for either item.
std::vector<uint8_t> build_heif_with_iden(uint32_t decl_w, uint32_t decl_h) {
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

  // pitm: primary item = item 2 (the 'iden').
  std::vector<uint8_t> pitm_payload;
  put_u16_be(pitm_payload, 2);
  auto pitm = make_box("pitm", pitm_payload, /*full=*/true);

  // iinf: item 1 = 'mski', item 2 = 'iden'.
  std::vector<uint8_t> infe1_payload;
  put_u16_be(infe1_payload, 1);
  put_u16_be(infe1_payload, 0);
  append_fourcc(infe1_payload, "mski");
  infe1_payload.push_back(0);
  auto infe1 = make_box("infe", infe1_payload, /*full=*/true, /*version=*/2);

  std::vector<uint8_t> infe2_payload;
  put_u16_be(infe2_payload, 2);
  put_u16_be(infe2_payload, 0);
  append_fourcc(infe2_payload, "iden");
  infe2_payload.push_back(0);
  auto infe2 = make_box("infe", infe2_payload, /*full=*/true, /*version=*/2);

  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, 2);
  append(iinf_payload, infe1);
  append(iinf_payload, infe2);
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // iprp / ipco: prop 1 = ispe(8,8) for item 1; prop 2 = mskC(8bpp) for item 1;
  // prop 3 = ispe(decl_w, decl_h) for item 2 (the iden's own declared size).
  std::vector<uint8_t> ispe1_payload;
  put_u32_be(ispe1_payload, 8);
  put_u32_be(ispe1_payload, 8);
  auto ispe1 = make_box("ispe", ispe1_payload, /*full=*/true);

  std::vector<uint8_t> mskC_payload;
  mskC_payload.push_back(8); // bits_per_pixel
  auto mskC = make_box("mskC", mskC_payload, /*full=*/true);

  std::vector<uint8_t> ispe2_payload;
  put_u32_be(ispe2_payload, decl_w);
  put_u32_be(ispe2_payload, decl_h);
  auto ispe2 = make_box("ispe", ispe2_payload, /*full=*/true);

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, ispe1);
  append(ipco_payload, mskC);
  append(ipco_payload, ispe2);
  auto ipco = make_box("ipco", ipco_payload);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, 2); // entry_count
  put_u16_be(ipma_payload, 1); // item_ID 1
  ipma_payload.push_back(2);   // association_count
  ipma_payload.push_back(0x80 | 1); // essential, ispe1
  ipma_payload.push_back(0x80 | 2); // essential, mskC
  put_u16_be(ipma_payload, 2); // item_ID 2
  ipma_payload.push_back(1);   // association_count
  ipma_payload.push_back(0x80 | 3); // essential, ispe2
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // iloc: only item 1 has data (raw 8x8 8bpp mask, in idat). The 'iden' item
  // (item 2) has no bitstream of its own and needs no entry.
  std::vector<uint8_t> iloc_payload;
  put_u16_be(iloc_payload, (4 << 12) | (4 << 8) | (0 << 4) | 0);
  put_u16_be(iloc_payload, 1);          // item_count
  put_u16_be(iloc_payload, 1);          // item_ID
  put_u16_be(iloc_payload, 0x0001);     // reserved(12) + construction_method=1 (idat)
  put_u16_be(iloc_payload, 0);          // data_reference_index
  put_u16_be(iloc_payload, 1);          // extent_count
  put_u32_be(iloc_payload, 0);          // extent_offset (within idat)
  put_u32_be(iloc_payload, 64);         // extent_length (8x8x1 byte)
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);

  // iref: item 2 ('iden') 'dimg'-references item 1.
  std::vector<uint8_t> dimg_payload;
  put_u16_be(dimg_payload, 2);          // from_item_ID
  put_u16_be(dimg_payload, 1);          // reference_count
  put_u16_be(dimg_payload, 1);          // to_item_ID
  auto dimg = make_box("dimg", dimg_payload);
  auto iref = make_box("iref", dimg, /*full=*/true);

  // idat: 8x8 = 64 bytes of raw 8bpp mask pixel data.
  std::vector<uint8_t> idat_payload(64, 0x7F);
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


// Regression test for the 'iden' hardening in ImageItem_iden::check_decoded_image_size():
// an 'iden' item's own declared 'ispe' is now validated against what it actually
// forwards to, instead of being skipped unconditionally.
TEST_CASE("iden: declared ispe not matching the referenced item's decoded size is rejected") {
  // iden (item 2) declares 16x16, but 'dimg'-references item 1, which is really 8x8.
  auto data = build_heif_with_iden(16, 16);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory_without_copy(
      ctx, data.data(), data.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);

  REQUIRE(err.code == heif_error_Invalid_input);
  REQUIRE(err.subcode == heif_suberror_Invalid_image_size);
  REQUIRE(img == nullptr);

  heif_image_handle_release(handle);
  heif_context_free(ctx);
}

// Control: same file, but the iden's declared ispe honestly matches item 1's
// actual size. Proves the new check doesn't reject legitimate content.
TEST_CASE("iden: declared ispe matching the referenced item's decoded size decodes normally") {
  auto data = build_heif_with_iden(8, 8);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory_without_copy(
      ctx, data.data(), data.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);

  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(img != nullptr);
  REQUIRE(heif_image_get_width(img, heif_channel_Y) == 8);
  REQUIRE(heif_image_get_height(img, heif_channel_Y) == 8);

  heif_image_release(img);
  heif_image_handle_release(handle);
  heif_context_free(ctx);
}
