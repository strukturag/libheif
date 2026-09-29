/*
  libheif regression tests for zero-length reads and copies into empty buffers
  (GHSA-2764-mqj2-c458).

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

// A box with an empty payload is read into an empty std::vector, whose data() pointer is
// NULL. The same happens for an item whose iloc extent has length zero. Passing that NULL
// pointer to memcpy() is undefined behaviour (until C2y, WG14 N3322) and is reported by
// UBSan's nonnull-attribute check. These tests only detect a regression when they are run
// in a build with -fsanitize=undefined; in a plain build they merely exercise the paths.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_items.h"
#include "test_utils.h"

#include <cstdint>
#include <vector>

namespace {

// ftyp + meta, where meta holds an unknown box with an empty payload (parsed as Box_other)
// and one item whose single iloc extent has length zero.
std::vector<uint8_t> build_heif_with_empty_box_and_empty_item()
{
  std::vector<uint8_t> ftyp_payload;
  append_fourcc(ftyp_payload, "mif1");
  put_u32_be(ftyp_payload, 0);
  append_fourcc(ftyp_payload, "mif1");
  auto ftyp = make_box("ftyp", ftyp_payload);

  // hdlr: handler type 'null' so that no image items are required.
  std::vector<uint8_t> hdlr_payload;
  put_u32_be(hdlr_payload, 0);        // pre_defined
  append_fourcc(hdlr_payload, "null"); // handler_type
  put_u32_be(hdlr_payload, 0);
  put_u32_be(hdlr_payload, 0);
  put_u32_be(hdlr_payload, 0);
  hdlr_payload.push_back(0);          // name
  auto hdlr = make_box("hdlr", hdlr_payload, /*full=*/true);

  // infe (version 2): item 1, unprotected, item type 'test', empty name
  std::vector<uint8_t> infe_payload;
  put_u16_be(infe_payload, 1);
  put_u16_be(infe_payload, 0);
  append_fourcc(infe_payload, "test");
  infe_payload.push_back(0);
  auto infe = make_box("infe", infe_payload, /*full=*/true, /*version=*/2);

  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, 1);
  append(iinf_payload, infe);
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // iloc (version 0): offset_size=4, length_size=4, base_offset_size=0,
  // one item with a single extent of length 0
  std::vector<uint8_t> iloc_payload;
  iloc_payload.push_back(0x44);
  iloc_payload.push_back(0x00);
  put_u16_be(iloc_payload, 1); // item_count
  put_u16_be(iloc_payload, 1); // item_ID
  put_u16_be(iloc_payload, 0); // data_reference_index
  put_u16_be(iloc_payload, 1); // extent_count
  put_u32_be(iloc_payload, 0); // extent_offset
  put_u32_be(iloc_payload, 0); // extent_length
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true);

  // Unknown box type with a header only. Box_other::parse() reads its zero-length payload.
  auto empty_box = make_box("zzzz", {});

  std::vector<uint8_t> meta_payload;
  append(meta_payload, hdlr);
  append(meta_payload, iinf);
  append(meta_payload, iloc);
  append(meta_payload, empty_box);
  auto meta = make_box("meta", meta_payload, /*full=*/true);

  std::vector<uint8_t> file;
  append(file, ftyp);
  append(file, meta);
  return file;
}

} // namespace


TEST_CASE("zero-length box payload and item data do not pass NULL to memcpy") {
  auto data = build_heif_with_empty_box_and_empty_item();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  // Read with a copy so that the memory-backed StreamReader is used for both the
  // input copy and every box payload read.
  heif_error err = heif_context_read_from_memory(ctx, data.data(), data.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  REQUIRE(heif_context_get_number_of_items(ctx) == 1);

  uint8_t* item_data = nullptr;
  size_t item_data_size = 123;
  err = heif_item_get_item_data(ctx, 1, nullptr, &item_data, &item_data_size);
  REQUIRE(err.code == heif_error_Ok);
  CHECK(item_data_size == 0);
  CHECK(item_data != nullptr);
  heif_release_item_data(ctx, &item_data);

  heif_context_free(ctx);
}


TEST_CASE("reading an empty memory buffer does not pass NULL to memcpy") {
  std::vector<uint8_t> empty;

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory(ctx, empty.data(), 0, nullptr);
  CHECK(err.code != heif_error_Ok);

  heif_context_free(ctx);
}
