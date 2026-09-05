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

// Regression test for GHSA-8fmq-r4pf-7m57: a decoder deadlock (DoS) reachable
// in a default build. The cycle guard that protects derived-image ('dimg')
// edges did not cover the auxiliary alpha ('auxl') edge, because the guard's
// insert happens inside decode_compressed_image() which receives the traversal
// state by value, while the alpha edge is followed one level up in
// ImageItem::decode_image() using that frame's own (un-updated) state. Two
// items whose alpha references form a cycle therefore re-entered decode_image()
// on an item whose non-recursive m_decode_mutex was still held, deadlocking the
// decode thread forever.
//
// The images are 'mski' masks so no codec plugin is required. Because a
// regression re-introduces a permanent hang (not a slow-but-bounded decode),
// the decode is run on a worker thread guarded by a timeout: a regression fails
// the test quickly instead of hanging the whole suite.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

// One item in the synthetic file. Every item is a decodable 8x8 'mski' mask
// (ispe + mskC + 64 bytes of idat). `is_alpha` items additionally carry an
// 'auxC' with the MIAF alpha aux-type; `auxl_to` lists the master images this
// item is the alpha channel of (an 'auxl' iref points aux -> master).
struct Item {
  uint16_t id = 0;
  bool is_alpha = false;
  std::vector<uint16_t> auxl_to;
  std::vector<uint8_t> data;
};

// Assemble a minimal, self-contained HEIF file from an explicit item list.
// ipco property indices (1-based, in box order): 1 = ispe(8x8), 2 = mskC(8bpp),
// 3 = auxC(alpha).
std::vector<uint8_t> build_file(const std::vector<Item>& items, uint16_t primary_id) {
  const uint32_t W = 8, H = 8;

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

  std::vector<uint8_t> pitm_payload;
  put_u16_be(pitm_payload, primary_id);
  auto pitm = make_box("pitm", pitm_payload, /*full=*/true);

  // iinf
  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, static_cast<uint16_t>(items.size()));
  for (const auto& it : items) {
    std::vector<uint8_t> infe_payload;
    put_u16_be(infe_payload, it.id);
    put_u16_be(infe_payload, 0);
    append_fourcc(infe_payload, "mski");
    infe_payload.push_back(0);
    append(iinf_payload, make_box("infe", infe_payload, /*full=*/true, /*version=*/2));
  }
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // iprp / ipco
  std::vector<uint8_t> ispe_payload;
  put_u32_be(ispe_payload, W);
  put_u32_be(ispe_payload, H);
  auto ispe = make_box("ispe", ispe_payload, /*full=*/true);

  std::vector<uint8_t> mskC_payload;
  mskC_payload.push_back(8);  // bits_per_pixel
  auto mskC = make_box("mskC", mskC_payload, /*full=*/true);

  std::vector<uint8_t> auxC_payload;
  append_cstr(auxC_payload, "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha");  // MIAF alpha
  auto auxC = make_box("auxC", auxC_payload, /*full=*/true);

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, ispe);   // property 1
  append(ipco_payload, mskC);   // property 2
  append(ipco_payload, auxC);   // property 3
  auto ipco = make_box("ipco", ipco_payload);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, static_cast<uint32_t>(items.size()));  // entry_count
  for (const auto& it : items) {
    put_u16_be(ipma_payload, it.id);
    if (it.is_alpha) {
      ipma_payload.push_back(3);          // association_count
      ipma_payload.push_back(0x80 | 1);   // essential, ispe
      ipma_payload.push_back(0x80 | 2);   // essential, mskC
      ipma_payload.push_back(0x80 | 3);   // essential, auxC
    }
    else {
      ipma_payload.push_back(2);
      ipma_payload.push_back(0x80 | 1);   // essential, ispe
      ipma_payload.push_back(0x80 | 2);   // essential, mskC
    }
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
    if (!it.data.empty()) {
      extents.push_back({it.id, static_cast<uint32_t>(idat_payload.size()),
                         static_cast<uint32_t>(it.data.size())});
      append(idat_payload, it.data);
    }
  }
  auto idat = make_box("idat", idat_payload);

  std::vector<uint8_t> iloc_payload;
  iloc_payload.push_back((4 << 4) | 4);   // offset_size=4, length_size=4
  iloc_payload.push_back((0 << 4) | 0);   // base_offset_size=0, index_size=0
  put_u16_be(iloc_payload, static_cast<uint16_t>(extents.size()));  // item_count
  for (const auto& e : extents) {
    put_u16_be(iloc_payload, e.id);
    put_u16_be(iloc_payload, 0x0001);     // construction_method=1 (idat)
    put_u16_be(iloc_payload, 0);          // data_reference_index
    put_u16_be(iloc_payload, 1);          // extent_count
    put_u32_be(iloc_payload, e.off);      // extent_offset (within idat)
    put_u32_be(iloc_payload, e.len);      // extent_length
  }
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);

  // iref: one 'auxl' entry per alpha item, from the aux item to its master(s).
  std::vector<uint8_t> iref_children;
  for (const auto& it : items) {
    if (!it.auxl_to.empty()) {
      std::vector<uint8_t> auxl_payload;
      put_u16_be(auxl_payload, it.id);
      put_u16_be(auxl_payload, static_cast<uint16_t>(it.auxl_to.size()));
      for (uint16_t to : it.auxl_to) { put_u16_be(auxl_payload, to); }
      append(iref_children, make_box("auxl", auxl_payload));
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

// Decode the primary image and return the resulting error. Self-contained so it
// can run on a detached worker thread without touching caller-owned state.
heif_error decode_primary_blocking(const std::vector<uint8_t>& data) {
  heif_context* ctx = heif_context_alloc();

  heif_error err = heif_context_read_from_memory_without_copy(
      ctx, data.data(), data.size(), nullptr);
  if (err.code != heif_error_Ok) {
    heif_context_free(ctx);
    return err;
  }

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
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

// Run decode_primary_blocking() with a timeout. Returns false on timeout (a
// regression: the decode deadlocked). The worker holds its shared state through
// a shared_ptr so that detaching it on timeout leaves no dangling references.
bool decode_primary_with_timeout(const std::vector<uint8_t>& data,
                                 std::chrono::seconds timeout,
                                 heif_error& out_err) {
  struct Job {
    std::vector<uint8_t> data;
    std::promise<heif_error> prom;
  };
  auto job = std::make_shared<Job>();
  job->data = data;
  auto fut = job->prom.get_future();

  std::thread worker([job]() {
    job->prom.set_value(decode_primary_blocking(job->data));
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


// The core DoS. Three 'mski' items: item1 (primary) has alpha=item2; item2 and
// item3 are each other's alpha (auxl item2->item3 and item3->item2), forming a
// cycle. Without the fix, decode_image() re-enters item2 while item2's
// m_decode_mutex is still held and deadlocks forever. With the fix, the alpha
// edge participates in the cycle guard, so the decode returns a cyclic-reference
// error essentially instantly.
TEST_CASE("alpha cycle: cyclic alpha auxl references do not deadlock") {
  std::vector<Item> items;
  const std::vector<uint8_t> pixels(64, 0x7F);
  //           id  is_alpha  auxl_to (masters)   data
  items.push_back({1, false, {},        pixels});  // primary, master of item2
  items.push_back({2, true,  {1, 3},    pixels});  // alpha of item1 and item3
  items.push_back({3, true,  {2},       pixels});  // alpha of item2  -> cycle 2<->3

  auto data = build_file(items, /*primary=*/1);

  heif_error err{};
  bool completed = decode_primary_with_timeout(data, std::chrono::seconds(15), err);

  REQUIRE(completed);                       // fails fast if the deadlock regresses
  REQUIRE(err.code != heif_error_Ok);       // the cycle is reported as an error
  REQUIRE(err.code == heif_error_Invalid_input);
}

// Control: a normal image with a single (non-cyclic) alpha aux image still
// decodes, proving the added cycle-guard insert does not reject legitimate
// alpha channels.
TEST_CASE("alpha cycle: a normal alpha aux image still decodes") {
  std::vector<Item> items;
  const std::vector<uint8_t> pixels(64, 0x7F);
  items.push_back({1, false, {},   pixels});  // primary
  items.push_back({2, true,  {1},  pixels});  // alpha of item1, no further alpha

  auto data = build_file(items, /*primary=*/1);

  heif_error err{};
  bool completed = decode_primary_with_timeout(data, std::chrono::seconds(15), err);

  REQUIRE(completed);
  REQUIRE(err.code == heif_error_Ok);
}
