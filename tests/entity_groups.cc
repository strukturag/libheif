/*
  libheif regression tests for entity groups (grpl) parsing.

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
#include "libheif/heif_entity_groups.h"

#include <cstdint>
#include <cstring>
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

// Build a minimal HEIF file whose 'grpl' box contains a single child box
// whose four-cc is NOT one of the registered Box_EntityToGroup subclasses
// (pymd / altr / ster). Such children parse to Box_other and used to
// trigger a NULL-pointer dereference in heif_context_get_entity_groups().
std::vector<uint8_t> build_minimal_heif_with_bogus_grpl_child() {
  // ftyp: heic / 0 / mif1 heic
  std::vector<uint8_t> ftyp_payload;
  append_fourcc(ftyp_payload, "heic");
  put_u32_be(ftyp_payload, 0);
  append_fourcc(ftyp_payload, "mif1");
  append_fourcc(ftyp_payload, "heic");
  auto ftyp = make_box("ftyp", ftyp_payload);

  // hdlr: handler type 'null' so HeifFile::has_images() returns false and
  // pitm/iprp/ipco/ipma are not required for the file to parse.
  std::vector<uint8_t> hdlr_payload;
  put_u32_be(hdlr_payload, 0);       // pre_defined
  append_fourcc(hdlr_payload, "null"); // handler_type (NOT 'pict')
  put_u32_be(hdlr_payload, 0);
  put_u32_be(hdlr_payload, 0);
  put_u32_be(hdlr_payload, 0);
  hdlr_payload.push_back(0);         // name (empty, NUL-terminated)
  auto hdlr = make_box("hdlr", hdlr_payload, /*full=*/true);

  // iinf: zero entries
  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, 0);
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // iloc: offset_size=0, length_size=0, base_offset_size=0, reserved=0, item_count=0
  std::vector<uint8_t> iloc_payload;
  iloc_payload.push_back(0);
  iloc_payload.push_back(0);
  put_u16_be(iloc_payload, 0);
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true);

  // grpl with a single 'pict' child — 'pict' is not registered as an
  // entity-to-group type, so it parses to Box_other.
  auto bogus_child = make_box("pict", {});
  auto grpl = make_box("grpl", bogus_child);

  // meta = hdlr + iinf + iloc + grpl
  std::vector<uint8_t> meta_payload;
  meta_payload.insert(meta_payload.end(), hdlr.begin(), hdlr.end());
  meta_payload.insert(meta_payload.end(), iinf.begin(), iinf.end());
  meta_payload.insert(meta_payload.end(), iloc.begin(), iloc.end());
  meta_payload.insert(meta_payload.end(), grpl.begin(), grpl.end());
  auto meta = make_box("meta", meta_payload, /*full=*/true);

  std::vector<uint8_t> file;
  file.insert(file.end(), ftyp.begin(), ftyp.end());
  file.insert(file.end(), meta.begin(), meta.end());
  return file;
}

} // namespace


// Regression for PR #1806 / NULL-deref in heif_context_get_entity_groups
// when 'grpl' contains a child whose four-cc is not a registered
// Box_EntityToGroup subclass.
TEST_CASE("entity_groups: unknown grpl child does not crash") {
  auto data = build_minimal_heif_with_bogus_grpl_child();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory_without_copy(
      ctx, data.data(), data.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  int num_groups = -1;
  heif_entity_group* groups =
      heif_context_get_entity_groups(ctx, 0, 0, &num_groups);
  // Unknown four-cc must be silently skipped, not dereferenced.
  REQUIRE(num_groups == 0);

  heif_entity_groups_release(groups, num_groups);
  heif_context_free(ctx);
}
