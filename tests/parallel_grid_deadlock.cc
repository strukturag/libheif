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

// Regression test for GHSA-prgh-72vc-3xmc: a lock-order inversion in the
// parallel grid tile-decode path deadlocks the decoder permanently (DoS) in a
// default build (ENABLE_PARALLEL_TILE_DECODING is ON, 4 threads by default).
//
// ImageItem::decode_image() held the per-item, non-recursive m_decode_mutex
// across nested decodes. Two grid tiles decoded on two worker threads that each
// recurse into the other item take the two item mutexes in opposite order and
// wedge forever. The per-path processed_ids cycle guard cannot see it because
// each worker owns its own copy of the set. There are two such nested edges:
//   - the alpha ('auxl') edge, and
//   - the grid-tile ('dimg') edge.
// The parse-time cycle check (HeifFile::check_for_ref_cycle) follows only
// 'dimg' and starts only from the primary item, so a mutual-'dimg' pair under a
// NON-primary top-level parent reaches decode without being rejected.
//
// The fix validates, before any decoding starts, that the decode reference
// graph reached from the requested item (both 'dimg' and 'auxl' edges) is
// acyclic (ImageItem::verify_decodable(), called from HeifContext::decode_image).
// A lock-order inversion requires a reference cycle, so rejecting cycles up
// front makes the held-across-recursion mutex deadlock-free.
//
// All items are 'mski' masks or 'grid' derived images, so no codec plugin is
// required. Because a regression re-introduces a permanent hang (not a
// slow-but-bounded decode), each decode runs on a worker thread guarded by a
// timeout: a regression fails the test quickly instead of hanging the suite.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <vector>

namespace {

// One item in the synthetic file. A 'grid' item's `data` is its ImageGrid
// payload and it lists its tiles in `dimg`. An 'mski' item's `data` is its mask
// pixels; if `is_alpha` it carries an 'auxC' alpha property and `auxl` names the
// master image(s) it is the alpha channel of.
struct Item {
  uint16_t id = 0;
  const char* type = "mski";   // "mski" or "grid"
  uint32_t w = 0, h = 0;       // ispe size for this item
  bool is_alpha = false;
  std::vector<uint16_t> dimg;  // derived-image references (grid tiles)
  std::vector<uint16_t> auxl;  // this item is the alpha of these masters
  std::vector<uint8_t> data;
};

// A 'grid' ImageGrid payload (version 0, 16-bit fields).
std::vector<uint8_t> image_grid(uint8_t rows, uint8_t cols, uint16_t w, uint16_t h) {
  std::vector<uint8_t> g;
  g.push_back(0);              // version
  g.push_back(0);              // flags (16-bit output fields)
  g.push_back(rows - 1);
  g.push_back(cols - 1);
  put_u16_be(g, w);
  put_u16_be(g, h);
  return g;
}

// Assemble a minimal, self-contained HEIF file. Each item gets its own 'ispe'
// (property index = item position, 1-based); the shared 'mskC' and 'auxC'
// follow. Item payloads are stored in 'idat' and located with construction
// method 1.
std::vector<uint8_t> build_file(const std::vector<Item>& items, uint16_t primary_id,
                                bool with_miaf_brand = true) {
  std::vector<uint8_t> ftyp_payload;
  append_fourcc(ftyp_payload, "heic");
  put_u32_be(ftyp_payload, 0);
  append_fourcc(ftyp_payload, "mif1");
  append_fourcc(ftyp_payload, "heic");
  if (with_miaf_brand) {
    append_fourcc(ftyp_payload, "miaf");
  }
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
  put_u16_be(pitm_payload, primary_id);
  auto pitm = make_box("pitm", pitm_payload, /*full=*/true);

  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, static_cast<uint16_t>(items.size()));
  for (const auto& it : items) {
    std::vector<uint8_t> infe_payload;
    put_u16_be(infe_payload, it.id);
    put_u16_be(infe_payload, 0);
    append_fourcc(infe_payload, it.type);
    infe_payload.push_back(0);
    append(iinf_payload, make_box("infe", infe_payload, /*full=*/true, /*version=*/2));
  }
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // ipco: one ispe per item, then shared mskC and auxC.
  const uint8_t mskC_index = static_cast<uint8_t>(items.size() + 1);
  const uint8_t auxC_index = static_cast<uint8_t>(items.size() + 2);

  std::vector<uint8_t> ipco_payload;
  for (const auto& it : items) {
    std::vector<uint8_t> ispe_payload;
    put_u32_be(ispe_payload, it.w);
    put_u32_be(ispe_payload, it.h);
    append(ipco_payload, make_box("ispe", ispe_payload, /*full=*/true));
  }
  {
    std::vector<uint8_t> mskC_payload;
    mskC_payload.push_back(8);  // bits_per_pixel
    append(ipco_payload, make_box("mskC", mskC_payload, /*full=*/true));

    std::vector<uint8_t> auxC_payload;
    append_cstr(auxC_payload, "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha");
    append(ipco_payload, make_box("auxC", auxC_payload, /*full=*/true));
  }
  auto ipco = make_box("ipco", ipco_payload);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, static_cast<uint32_t>(items.size()));
  for (size_t i = 0; i < items.size(); i++) {
    const auto& it = items[i];
    const uint8_t ispe_index = static_cast<uint8_t>(i + 1);
    std::vector<uint8_t> assoc;
    assoc.push_back(0x80 | ispe_index);              // ispe (essential)
    bool is_grid = std::string(it.type) == "grid";
    if (!is_grid) { assoc.push_back(0x80 | mskC_index); }  // mskC for masks
    if (it.is_alpha) { assoc.push_back(0x80 | auxC_index); }
    put_u16_be(ipma_payload, it.id);
    ipma_payload.push_back(static_cast<uint8_t>(assoc.size()));
    append(ipma_payload, assoc);
  }
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // idat + iloc
  std::vector<uint8_t> idat_payload;
  struct Extent { uint16_t id; uint32_t off; uint32_t len; };
  std::vector<Extent> extents;
  for (const auto& it : items) {
    extents.push_back({it.id, static_cast<uint32_t>(idat_payload.size()),
                       static_cast<uint32_t>(it.data.size())});
    append(idat_payload, it.data);
  }
  auto idat = make_box("idat", idat_payload);

  std::vector<uint8_t> iloc_payload;
  iloc_payload.push_back((4 << 4) | 4);   // offset_size=4, length_size=4
  iloc_payload.push_back((0 << 4) | 0);   // base_offset_size=0, index_size=0
  put_u16_be(iloc_payload, static_cast<uint16_t>(extents.size()));
  for (const auto& e : extents) {
    put_u16_be(iloc_payload, e.id);
    put_u16_be(iloc_payload, 0x0001);     // construction_method=1 (idat)
    put_u16_be(iloc_payload, 0);
    put_u16_be(iloc_payload, 1);          // extent_count
    put_u32_be(iloc_payload, e.off);
    put_u32_be(iloc_payload, e.len);
  }
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);

  std::vector<uint8_t> iref_children;
  for (const auto& it : items) {
    if (!it.dimg.empty()) {
      std::vector<uint8_t> p;
      put_u16_be(p, it.id);
      put_u16_be(p, static_cast<uint16_t>(it.dimg.size()));
      for (uint16_t to : it.dimg) { put_u16_be(p, to); }
      append(iref_children, make_box("dimg", p));
    }
    if (!it.auxl.empty()) {
      std::vector<uint8_t> p;
      put_u16_be(p, it.id);
      put_u16_be(p, static_cast<uint16_t>(it.auxl.size()));
      for (uint16_t to : it.auxl) { put_u16_be(p, to); }
      append(iref_children, make_box("auxl", p));
    }
  }
  auto iref = make_box("iref", iref_children, /*full=*/true);

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

// Decode one item (by id) to completion and return the error. Self-contained so
// it can run on a detached worker thread without touching caller-owned state.
heif_error decode_item_blocking(const std::vector<uint8_t>& data, uint16_t item_id) {
  heif_context* ctx = heif_context_alloc();

  heif_error err = heif_context_read_from_memory_without_copy(
      ctx, data.data(), data.size(), nullptr);
  if (err.code != heif_error_Ok) {
    heif_context_free(ctx);
    return err;
  }

  heif_image_handle* handle = nullptr;
  err = heif_context_get_image_handle(ctx, item_id, &handle);
  if (err.code != heif_error_Ok) {
    heif_context_free(ctx);
    return err;
  }

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);

  if (img) { heif_image_release(img); }
  heif_image_handle_release(handle);
  heif_context_free(ctx);
  return err;
}

// Run decode_item_blocking() with a timeout. Returns false on timeout (a
// regression: the decode deadlocked). The worker holds its state through a
// shared_ptr so detaching it on timeout leaves no dangling references.
bool decode_item_with_timeout(const std::vector<uint8_t>& data, uint16_t item_id,
                              std::chrono::seconds timeout, heif_error& out_err) {
  struct Job {
    std::vector<uint8_t> data;
    uint16_t item_id;
    std::promise<heif_error> prom;
  };
  auto job = std::make_shared<Job>();
  job->data = data;
  job->item_id = item_id;
  auto fut = job->prom.get_future();

  std::thread worker([job]() {
    job->prom.set_value(decode_item_blocking(job->data, job->item_id));
  });

  if (fut.wait_for(timeout) == std::future_status::timeout) {
    worker.detach();  // leak the hung thread; the process reclaims it at exit
    return false;
  }

  worker.join();
  out_err = fut.get();
  return true;
}

} // namespace


// The reported DoS. A grid (primary) whose two tiles are each other's alpha:
// the two tiles decode on separate workers and the alpha ('auxl') edge takes the
// two tile mutexes in opposite order. Without the fix this deadlocks; with it
// the alpha edge is followed before the lock and the cycle guard reports it.
TEST_CASE("parallel grid: mutual alpha between grid tiles does not deadlock") {
  const std::vector<uint8_t> pixels(32 * 32, 0x7F);
  std::vector<Item> items;
  //             id  type    w   h   alpha dimg     auxl     data
  items.push_back({1, "grid", 64, 32, false, {2, 3}, {},    image_grid(1, 2, 64, 32)});
  items.push_back({2, "mski", 32, 32, true,  {},     {3},   pixels});  // alpha of tile 3
  items.push_back({3, "mski", 32, 32, true,  {},     {2},   pixels});  // alpha of tile 2

  auto data = build_file(items, /*primary=*/1);

  heif_error err{};
  bool completed = decode_item_with_timeout(data, /*item=*/1, std::chrono::seconds(20), err);

  REQUIRE(completed);                          // fails fast if the deadlock regresses
  REQUIRE(err.code == heif_error_Invalid_input);
}

// The residual dimg-edge variant. A NON-primary parent grid P over two sub-grids
// that reference each other as their single tile. The parse-time cycle check
// runs only from the primary (a separate leaf), so this reaches the parallel
// decode. Decoding P fans the two sub-grids out across workers, which take the
// two mutexes in opposite order. The alpha-only fix does not cover this; the
// full fix (no lock held across the grid-tile recursion) does.
TEST_CASE("parallel grid: mutual dimg between sub-grids does not deadlock") {
  const std::vector<uint8_t> pixels(32 * 64, 0x7F);
  std::vector<Item> items;
  //             id  type    w   h   alpha dimg    auxl data
  items.push_back({1, "grid", 64, 64, false, {2, 3}, {}, image_grid(1, 2, 64, 64)});  // parent P
  items.push_back({2, "grid", 32, 64, false, {3},    {}, image_grid(1, 1, 32, 64)});  // G1 -> G2
  items.push_back({3, "grid", 32, 64, false, {2},    {}, image_grid(1, 1, 32, 64)});  // G2 -> G1
  items.push_back({4, "mski", 32, 64, false, {},     {}, pixels});                     // primary leaf

  auto data = build_file(items, /*primary=*/4);

  heif_error err{};
  bool completed = decode_item_with_timeout(data, /*item=*/1, std::chrono::seconds(20), err);

  REQUIRE(completed);                          // fails fast if the deadlock regresses
  REQUIRE(err.code == heif_error_Invalid_input);
}

// An image that is (transitively) its own alpha, through several alpha ('auxl')
// hops with no 'dimg' involved: alpha(1)=2, alpha(2)=3, alpha(3)=1. The alpha
// edge is what the parse-time 'dimg'-only cycle check misses, so the validator
// must follow it. Not a direct self-reference (which the aux-image attachment
// rejects at parse), so this exercises the validator's alpha-edge walk.
TEST_CASE("parallel grid: an alpha reference cycle through indirection is rejected") {
  const std::vector<uint8_t> pixels(32 * 32, 0x7F);
  std::vector<Item> items;
  // auxl points aux -> master, so alpha(X)=Y is encoded as "Y is the alpha of X".
  //             id  type    w   h   alpha dimg auxl (masters)  data
  items.push_back({1, "mski", 32, 32, true, {}, {3}, pixels});  // item1 is the alpha of item3
  items.push_back({2, "mski", 32, 32, true, {}, {1}, pixels});  // item2 is the alpha of item1
  items.push_back({3, "mski", 32, 32, true, {}, {2}, pixels});  // item3 is the alpha of item2

  auto data = build_file(items, /*primary=*/1);

  heif_error err{};
  bool completed = decode_item_with_timeout(data, /*item=*/1, std::chrono::seconds(20), err);

  REQUIRE(completed);
  REQUIRE(err.code == heif_error_Invalid_input);
}

// A cycle that mixes the two edge kinds: item1's alpha is grid 2, and grid 2's
// single tile is item1 (item1 -alpha-> 2 -dimg-> item1). Neither a pure alpha
// cycle nor a pure dimg cycle; the validator must follow both edge kinds in one
// walk to catch it.
TEST_CASE("parallel grid: an alpha that references a grid referencing back is rejected") {
  const std::vector<uint8_t> pixels(32 * 64, 0x7F);
  std::vector<Item> items;
  //             id  type    w   h   alpha dimg auxl data
  items.push_back({1, "mski", 32, 64, false, {},  {},  pixels});                     // master, decoded
  items.push_back({2, "grid", 32, 64, true,  {1}, {1}, image_grid(1, 1, 32, 64)});   // alpha of 1; its tile is 1

  auto data = build_file(items, /*primary=*/1);

  heif_error err{};
  bool completed = decode_item_with_timeout(data, /*item=*/1, std::chrono::seconds(20), err);

  REQUIRE(completed);
  REQUIRE(err.code == heif_error_Invalid_input);
}

// Control: a benign grid with two independent (acyclic) mask tiles still decodes
// on the parallel path, proving the lock change does not break normal grids.
TEST_CASE("parallel grid: a normal two-tile grid still decodes") {
  const std::vector<uint8_t> pixels(32 * 32, 0x7F);
  std::vector<Item> items;
  items.push_back({1, "grid", 64, 32, false, {2, 3}, {}, image_grid(1, 2, 64, 32)});
  items.push_back({2, "mski", 32, 32, false, {},     {}, pixels});
  items.push_back({3, "mski", 32, 32, false, {},     {}, pixels});

  auto data = build_file(items, /*primary=*/1);

  heif_error err{};
  bool completed = decode_item_with_timeout(data, /*item=*/1, std::chrono::seconds(20), err);

  REQUIRE(completed);
  REQUIRE(err.code == heif_error_Ok);
}

// A reference cycle is not rejected at file load: the file still opens and its
// independently valid items still decode. Only the cyclic item itself fails,
// at decode time. Here the primary (item 1) is a grid whose two sub-grids form
// a 'dimg' cycle, while item 4 is an unrelated valid mask.
TEST_CASE("parallel grid: a cyclic primary does not make valid sibling items undecodable") {
  const std::vector<uint8_t> big(32 * 64, 0x7F);
  const std::vector<uint8_t> small(32 * 32, 0x7F);
  std::vector<Item> items;
  //             id  type    w   h   alpha dimg    auxl data
  items.push_back({1, "grid", 64, 64, false, {2, 3}, {}, image_grid(1, 2, 64, 64)});  // cyclic primary
  items.push_back({2, "grid", 32, 64, false, {3},    {}, image_grid(1, 1, 32, 64)});  // G1 -> G2
  items.push_back({3, "grid", 32, 64, false, {2},    {}, image_grid(1, 1, 32, 64)});  // G2 -> G1
  items.push_back({4, "mski", 32, 32, false, {},     {}, small});                      // valid, unrelated

  auto data = build_file(items, /*primary=*/1);

  // The valid sibling decodes. Because decode reads the file first, this also
  // proves the file loaded despite the cyclic primary.
  heif_error err_valid{};
  bool completed_valid = decode_item_with_timeout(data, /*item=*/4, std::chrono::seconds(20), err_valid);
  REQUIRE(completed_valid);
  REQUIRE(err_valid.code == heif_error_Ok);

  // The cyclic item is still rejected, at decode.
  heif_error err_cyclic{};
  bool completed_cyclic = decode_item_with_timeout(data, /*item=*/1, std::chrono::seconds(20), err_cyclic);
  REQUIRE(completed_cyclic);
  REQUIRE(err_cyclic.code == heif_error_Invalid_input);
}

// A nested grid (a grid whose tile is itself a grid) violates MIAF's derivation
// chain (ISO/IEC 23000-22 clause 7.3.11: a grid input must be a coded image).
// The graph is acyclic, so the cycle check passes; verify_decodable() rejects it
// only because of the MIAF derivation constraints, which apply here because the
// file carries the 'miaf' brand.
static std::vector<Item> nested_grid_items() {
  const std::vector<uint8_t> pixels(64 * 64, 0x7F);
  std::vector<Item> items;
  //           id  type    w   h   alpha dimg auxl data
  items.push_back({1, "grid", 64, 64, false, {2}, {}, image_grid(1, 1, 64, 64)});  // grid over ...
  items.push_back({2, "grid", 64, 64, false, {3}, {}, image_grid(1, 1, 64, 64)});  // ... a grid (invalid)
  items.push_back({3, "mski", 64, 64, false, {},  {}, pixels});                     // coded base
  return items;
}

TEST_CASE("MIAF: a nested grid is rejected when the file has the 'miaf' brand") {
  auto data = build_file(nested_grid_items(), /*primary=*/1, /*with_miaf_brand=*/true);

  heif_error err{};
  bool completed = decode_item_with_timeout(data, /*item=*/1, std::chrono::seconds(20), err);

  REQUIRE(completed);
  REQUIRE(err.code == heif_error_Invalid_input);
}

// The same nested grid, without the 'miaf' brand, is not subject to the MIAF
// derivation constraints. It is acyclic and structurally decodable, so it is
// not rejected by verify_decodable(). (This documents that the MIAF check is
// brand-gated; a future security-limits flag will be able to force it on.)
TEST_CASE("MIAF: without the 'miaf' brand a nested grid is not structurally rejected") {
  auto data = build_file(nested_grid_items(), /*primary=*/1, /*with_miaf_brand=*/false);

  heif_error err{};
  bool completed = decode_item_with_timeout(data, /*item=*/1, std::chrono::seconds(20), err);

  REQUIRE(completed);
  REQUIRE(err.code == heif_error_Ok);
}
