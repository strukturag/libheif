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

// Check the per-item HEIF cardinality rule and the cumulative decode-path limit.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

// Build a minimal HEIF file with
//   item 1 ('mski', 8x8, 8bpp): real, decodable content (the mask codec needs
//           no bitstream -- its "compressed" data is just the raw pixel bytes).
//   items 2 ... 1+num_iden ('iden'): each one 'dimg'-references the item before it.
//           The last one is the primary item.
// The single 'imir' property of the file is associated `mirrors_on_coded_item` times
// with item 1 and `mirrors_per_iden` times with each 'iden' item.
// No codec plugin is required for any item.
std::vector<uint8_t> build_heif_with_mirrors(uint16_t num_iden, uint8_t mirrors_per_iden,
                                             uint8_t mirrors_on_coded_item,
                                             const char* transform_type = "imir") {
  const uint16_t num_items = static_cast<uint16_t>(1 + num_iden);

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

  // pitm: primary item = top of the chain.
  std::vector<uint8_t> pitm_payload;
  put_u16_be(pitm_payload, num_items);
  auto pitm = make_box("pitm", pitm_payload, /*full=*/true);

  // iinf: item 1 = 'mski', all others = 'iden'.
  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, num_items);
  for (uint16_t id = 1; id <= num_items; id++) {
    std::vector<uint8_t> infe_payload;
    put_u16_be(infe_payload, id);
    put_u16_be(infe_payload, 0);
    append_fourcc(infe_payload, id == 1 ? "mski" : "iden");
    infe_payload.push_back(0);
    append(iinf_payload, make_box("infe", infe_payload, /*full=*/true, /*version=*/2));
  }
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // iprp / ipco: prop 1 = ispe(8,8); prop 2 = mskC(8bpp); prop 3 = imir.
  std::vector<uint8_t> ispe_payload;
  put_u32_be(ispe_payload, 8);
  put_u32_be(ispe_payload, 8);

  std::vector<uint8_t> mskC_payload;
  mskC_payload.push_back(8); // bits_per_pixel

  std::vector<uint8_t> transform_payload;
  if (std::string(transform_type) == "clap") {
    // Full 8x8 crop with zero offsets, each rational having denominator 1.
    for (uint32_t value : {8u, 1u, 8u, 1u, 0u, 1u, 0u, 1u}) {
      put_u32_be(transform_payload, value);
    }
  }
  else {
    transform_payload.push_back(0); // imir axis or irot angle
  }

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, make_box("ispe", ispe_payload, /*full=*/true));
  append(ipco_payload, make_box("mskC", mskC_payload, /*full=*/true));
  append(ipco_payload, make_box(transform_type, transform_payload));
  auto ipco = make_box("ipco", ipco_payload);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, num_items); // entry_count
  for (uint16_t id = 1; id <= num_items; id++) {
    const uint8_t num_descriptive = (id == 1 ? 2 : 1);
    const uint8_t num_mirrors = (id == 1 ? mirrors_on_coded_item : mirrors_per_iden);

    put_u16_be(ipma_payload, id);
    ipma_payload.push_back(static_cast<uint8_t>(num_descriptive + num_mirrors)); // association_count
    ipma_payload.push_back(0x80 | 1); // essential, ispe
    if (id == 1) {
      ipma_payload.push_back(0x80 | 2); // essential, mskC
    }
    for (uint8_t i = 0; i < num_mirrors; i++) {
      ipma_payload.push_back(0x80 | 3); // essential, imir
    }
  }
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // iloc: only item 1 has data (raw 8x8 8bpp mask, in idat).
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

  // iref: each 'iden' item 'dimg'-references the item before it.
  std::vector<uint8_t> iref_payload;
  for (uint16_t id = 2; id <= num_items; id++) {
    std::vector<uint8_t> dimg_payload;
    put_u16_be(dimg_payload, id);                             // from_item_ID
    put_u16_be(dimg_payload, 1);                              // reference_count
    put_u16_be(dimg_payload, static_cast<uint16_t>(id - 1));  // to_item_ID
    append(iref_payload, make_box("dimg", dimg_payload));
  }
  auto iref = make_box("iref", iref_payload, /*full=*/true);

  // idat: 8x8 = 64 bytes of raw 8bpp mask pixel data.
  std::vector<uint8_t> idat_payload(64, 0x7F);
  auto idat = make_box("idat", idat_payload);

  std::vector<uint8_t> meta_payload;
  append(meta_payload, hdlr);
  append(meta_payload, pitm);
  append(meta_payload, iinf);
  append(meta_payload, iprp);
  append(meta_payload, iloc);
  if (num_iden > 0) {
    append(meta_payload, iref);
  }
  append(meta_payload, idat);
  auto meta = make_box("meta", meta_payload, /*full=*/true);

  std::vector<uint8_t> file;
  append(file, ftyp);
  append(file, meta);
  return file;
}


heif_error decode_primary_image(const std::vector<uint8_t>& data,
                                bool ignore_transformations = false,
                                const heif_security_limits* limits = nullptr) {
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  if (limits) {
    REQUIRE(heif_context_set_security_limits(ctx, limits).code == heif_error_Ok);
  }

  heif_error err = heif_context_read_from_memory_without_copy(ctx, data.data(), data.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  heif_decoding_options* options = heif_decoding_options_alloc();
  options->ignore_transformations = ignore_transformations;

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, options);
  REQUIRE((img != nullptr) == (err.code == heif_error_Ok));

  // The message belongs to the context, which is released below.
  err.message = nullptr;

  heif_image_release(img);
  heif_decoding_options_free(options);
  heif_image_handle_release(handle);
  heif_context_free(ctx);

  return err;
}

} // namespace


TEST_CASE("one transformation per item decodes normally") {
  REQUIRE(decode_primary_image(build_heif_with_mirrors(0, 0, 1)).code == heif_error_Ok);
  REQUIRE(decode_primary_image(build_heif_with_mirrors(1, 1, 1)).code == heif_error_Ok);
  REQUIRE(decode_primary_image(build_heif_with_mirrors(4, 1, 1)).code == heif_error_Ok);
}


TEST_CASE("a long 'iden' chain without transformations decodes normally") {
  REQUIRE(decode_primary_image(build_heif_with_mirrors(100, 0, 1)).code == heif_error_Ok);
}


TEST_CASE("a transformative property associated twice with one item is rejected") {
  SECTION("coded item") {
    heif_error err = decode_primary_image(build_heif_with_mirrors(0, 0, 2));
    REQUIRE(err.code == heif_error_Invalid_input);
    REQUIRE(err.subcode == heif_suberror_Invalid_property);
  }

  SECTION("'iden' item") {
    heif_error err = decode_primary_image(build_heif_with_mirrors(1, 2, 0));
    REQUIRE(err.code == heif_error_Invalid_input);
    REQUIRE(err.subcode == heif_suberror_Invalid_property);
  }

  SECTION("rotation") {
    heif_error err = decode_primary_image(build_heif_with_mirrors(0, 0, 2, "irot"));
    REQUIRE(err.code == heif_error_Invalid_input);
    REQUIRE(err.subcode == heif_suberror_Invalid_property);
  }

  SECTION("crop") {
    heif_error err = decode_primary_image(build_heif_with_mirrors(0, 0, 2, "clap"));
    REQUIRE(err.code == heif_error_Invalid_input);
    REQUIRE(err.subcode == heif_suberror_Invalid_property);
  }

  SECTION("disabled security limits still enforce the format rule") {
    heif_error err = decode_primary_image(build_heif_with_mirrors(0, 0, 2), false,
                                          heif_get_disabled_security_limits());
    REQUIRE(err.code == heif_error_Invalid_input);
    REQUIRE(err.subcode == heif_suberror_Invalid_property);
  }
}


TEST_CASE("the transformations of the items on an 'iden' chain add up") {
  REQUIRE(decode_primary_image(build_heif_with_mirrors(31, 1, 1)).code == heif_error_Ok);
  heif_error err = decode_primary_image(build_heif_with_mirrors(32, 1, 1));
  REQUIRE(err.code == heif_error_Invalid_input);
  REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);
}


TEST_CASE("transformations that are not applied are not counted") {
  REQUIRE(decode_primary_image(build_heif_with_mirrors(100, 1, 1), true).code == heif_error_Ok);
}


TEST_CASE("no transformation limit when the security limits are disabled") {
  REQUIRE(decode_primary_image(build_heif_with_mirrors(100, 1, 1), false,
                               heif_get_disabled_security_limits()).code == heif_error_Ok);
}
