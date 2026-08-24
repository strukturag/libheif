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

// Regression test for GHSA-24wx-9w62-c96w: a compressed ('mime', content_encoding
// "br") metadata item is a decompression bomb. Opening the file decompresses the
// item in HeifContext::interpret_heif_file_images(), which used to grow an output
// vector with no size accounting at all, bypassing the configured security limits
// and allowing OOM from a few hundred bytes of input.
//
// The test builds a tiny HEIF whose 'mime' item is a real brotli payload that
// expands to tens of MB, then opens it once with a tight total-memory limit
// (expecting a security-limit error) and once with the default limits (expecting
// success, proving the file itself is valid and it is the limit that is enforced).

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "compression.h"

#include <cstdint>
#include <string>
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

// Append a null-terminated string (as read by Box_infe via read_string()).
void append_cstr(std::vector<uint8_t>& out, const char* s) {
  while (*s) { out.push_back(static_cast<uint8_t>(*s)); ++s; }
  out.push_back(0);
}

void append(std::vector<uint8_t>& out, const std::vector<uint8_t>& v) {
  out.insert(out.end(), v.begin(), v.end());
}

std::vector<uint8_t> make_box(const char fourcc[4],
                              const std::vector<uint8_t>& payload,
                              bool is_full_box = false,
                              uint8_t version = 0,
                              uint32_t flags = 0) {
  const size_t header_size = is_full_box ? 12 : 8;

  std::vector<uint8_t> box;
  box.reserve(header_size + payload.size());

  put_u32_be(box, static_cast<uint32_t>(header_size + payload.size()));
  append_fourcc(box, fourcc);
  if (is_full_box) {
    box.push_back(version);
    box.push_back(static_cast<uint8_t>(flags >> 16));
    box.push_back(static_cast<uint8_t>(flags >> 8));
    box.push_back(static_cast<uint8_t>(flags));
  }
  append(box, payload);
  return box;
}

// Build a minimal HEIF file with two items:
//   item 1 ('mski', 8x8, 8bpp): the primary image. The mask codec needs no
//           bitstream (its "compressed" data is just the raw pixel bytes), so no
//           codec plugin is required and the file opens without decoding.
//   item 2 ('mime', content_encoding "br"): a brotli-compressed metadata item,
//           'cdsc'-referencing item 1, carrying the supplied bomb payload.
std::vector<uint8_t> build_heif_with_brotli_mime(const std::vector<uint8_t>& brotli_payload) {
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

  // pitm: primary item = item 1 (the 'mski' image).
  std::vector<uint8_t> pitm_payload;
  put_u16_be(pitm_payload, 1);
  auto pitm = make_box("pitm", pitm_payload, /*full=*/true);

  // iinf: item 1 = 'mski', item 2 = 'mime'.
  std::vector<uint8_t> infe1_payload;
  put_u16_be(infe1_payload, 1);
  put_u16_be(infe1_payload, 0);
  append_fourcc(infe1_payload, "mski");
  infe1_payload.push_back(0); // item_name (empty)
  auto infe1 = make_box("infe", infe1_payload, /*full=*/true, /*version=*/2);

  std::vector<uint8_t> infe2_payload;
  put_u16_be(infe2_payload, 2);
  put_u16_be(infe2_payload, 0);
  append_fourcc(infe2_payload, "mime");
  append_cstr(infe2_payload, "");                          // item_name
  append_cstr(infe2_payload, "application/octet-stream");  // content_type
  append_cstr(infe2_payload, "br");                        // content_encoding
  auto infe2 = make_box("infe", infe2_payload, /*full=*/true, /*version=*/2);

  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, 2);
  append(iinf_payload, infe1);
  append(iinf_payload, infe2);
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // iprp / ipco: prop 1 = ispe(8,8), prop 2 = mskC(8bpp), both for item 1.
  std::vector<uint8_t> ispe_payload;
  put_u32_be(ispe_payload, 8);
  put_u32_be(ispe_payload, 8);
  auto ispe = make_box("ispe", ispe_payload, /*full=*/true);

  std::vector<uint8_t> mskC_payload;
  mskC_payload.push_back(8); // bits_per_pixel
  auto mskC = make_box("mskC", mskC_payload, /*full=*/true);

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, ispe);
  append(ipco_payload, mskC);
  auto ipco = make_box("ipco", ipco_payload);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, 1); // entry_count
  put_u16_be(ipma_payload, 1); // item_ID 1
  ipma_payload.push_back(2);   // association_count
  ipma_payload.push_back(0x80 | 1); // essential, ispe
  ipma_payload.push_back(0x80 | 2); // essential, mskC
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // idat: 64 bytes of raw 8x8 8bpp mask data, followed by the brotli bomb payload.
  std::vector<uint8_t> idat_payload;
  idat_payload.reserve(64 + brotli_payload.size());
  idat_payload.resize(64, 0x7F);
  const uint32_t bomb_offset = static_cast<uint32_t>(idat_payload.size());
  const uint32_t bomb_length = static_cast<uint32_t>(brotli_payload.size());
  append(idat_payload, brotli_payload);
  auto idat = make_box("idat", idat_payload);

  // iloc (version 1): both items store their data in idat (construction_method=1).
  std::vector<uint8_t> iloc_payload;
  put_u16_be(iloc_payload, (4 << 12) | (4 << 8) | (0 << 4) | 0); // offset_size=4, length_size=4
  put_u16_be(iloc_payload, 2);          // item_count
  // item 1: mask pixels
  put_u16_be(iloc_payload, 1);          // item_ID
  put_u16_be(iloc_payload, 0x0001);     // reserved(12) + construction_method=1 (idat)
  put_u16_be(iloc_payload, 0);          // data_reference_index
  put_u16_be(iloc_payload, 1);          // extent_count
  put_u32_be(iloc_payload, 0);          // extent_offset (within idat)
  put_u32_be(iloc_payload, 64);         // extent_length
  // item 2: brotli bomb
  put_u16_be(iloc_payload, 2);          // item_ID
  put_u16_be(iloc_payload, 0x0001);     // reserved(12) + construction_method=1 (idat)
  put_u16_be(iloc_payload, 0);          // data_reference_index
  put_u16_be(iloc_payload, 1);          // extent_count
  put_u32_be(iloc_payload, bomb_offset);// extent_offset (within idat)
  put_u32_be(iloc_payload, bomb_length);// extent_length
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);

  // iref: item 2 ('mime') 'cdsc'-references item 1 (metadata describes the image).
  std::vector<uint8_t> cdsc_payload;
  put_u16_be(cdsc_payload, 2);          // from_item_ID
  put_u16_be(cdsc_payload, 1);          // reference_count
  put_u16_be(cdsc_payload, 1);          // to_item_ID
  auto cdsc = make_box("cdsc", cdsc_payload);
  auto iref = make_box("iref", cdsc, /*full=*/true);

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


TEST_CASE("brotli mime metadata decompression bomb is bounded by security limits") {
#if HAVE_BROTLI
  // A brotli payload that expands to ~40 MB but is only a few hundred bytes on disk.
  const size_t decompressed_size = 40u * 1024 * 1024;
  std::vector<uint8_t> zeros(decompressed_size, 0);
  std::vector<uint8_t> bomb = compress_brotli(zeros.data(), zeros.size());
  REQUIRE(!bomb.empty());
  REQUIRE(bomb.size() < decompressed_size); // it really is highly compressible

  std::vector<uint8_t> file = build_heif_with_brotli_mime(bomb);

  // --- with a tight total-memory limit, opening must fail rather than allocate the
  //     whole decompressed payload (previously it ignored the limit entirely).
  {
    heif_context* ctx = heif_context_alloc();
    REQUIRE(ctx != nullptr);

    heif_security_limits* limits = heif_context_get_security_limits(ctx);
    REQUIRE(limits != nullptr);
    limits->max_total_memory = 8u * 1024 * 1024; // 8 MB << 40 MB bomb

    heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
    REQUIRE(err.code != heif_error_Ok);
    REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);

    heif_context_free(ctx);
  }

  // --- with the default (generous) limits, the very same file opens successfully,
  //     proving it is structurally valid and that the limit is what is enforced.
  {
    heif_context* ctx = heif_context_alloc();
    REQUIRE(ctx != nullptr);

    heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
    REQUIRE(err.code == heif_error_Ok);

    heif_context_free(ctx);
  }
#else
  SUCCEED("brotli support not compiled in - skipping decompression bomb test");
#endif
}
