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

// Regression test for a null-pointer dereference reachable from a fuzzer
// (OSS-Fuzz / CIFuzz). An item whose type libheif cannot handle (here 'lhv1',
// layered HEVC) is represented internally by an ImageItem_Error placeholder.
// These placeholders used to be constructed with a null HeifContext, so any
// code that dereferenced their context crashed. ImageItem::decode_image() gained
// a verify_decodable() pre-pass that calls get_file() (-> context->get_heif_file())
// before decoding, so decoding such an item segfaulted.
//
// The depth-image handle getter does not reject items with an item-error, so an
// error item attached as a depth auxiliary can be handed to heif_decode_image()
// and reach the crash. The fix gives error items their real context, so decoding
// one returns its error instead of dereferencing null.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <vector>

namespace {

// A minimal file: item 1 is a decodable 16x16 'mski' mask (the primary); item 2
// is an 'lhv1' item (unsupported -> ImageItem_Error) declared as the depth
// auxiliary of item 1 via 'auxl' + an 'auxC' depth type.
std::vector<uint8_t> build_file_with_error_depth_item() {
  const uint32_t W = 16, H = 16;

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
  put_u16_be(pitm_payload, 1);
  auto pitm = make_box("pitm", pitm_payload, /*full=*/true);

  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, 2);
  {
    std::vector<uint8_t> infe1;
    put_u16_be(infe1, 1);
    put_u16_be(infe1, 0);
    append_fourcc(infe1, "mski");
    infe1.push_back(0);
    append(iinf_payload, make_box("infe", infe1, /*full=*/true, /*version=*/2));

    std::vector<uint8_t> infe2;
    put_u16_be(infe2, 2);
    put_u16_be(infe2, 0);
    append_fourcc(infe2, "lhv1");   // unsupported -> ImageItem_Error
    infe2.push_back(0);
    append(iinf_payload, make_box("infe", infe2, /*full=*/true, /*version=*/2));
  }
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // ipco: 1=ispe, 2=mskC, 3=auxC(depth)
  std::vector<uint8_t> ispe_payload;
  put_u32_be(ispe_payload, W);
  put_u32_be(ispe_payload, H);
  auto ispe = make_box("ispe", ispe_payload, /*full=*/true);

  std::vector<uint8_t> mskC_payload;
  mskC_payload.push_back(8);
  auto mskC = make_box("mskC", mskC_payload, /*full=*/true);

  std::vector<uint8_t> auxC_payload;
  append_cstr(auxC_payload, "urn:mpeg:mpegB:cicp:systems:auxiliary:depth");
  auto auxC = make_box("auxC", auxC_payload, /*full=*/true);

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, ispe);
  append(ipco_payload, mskC);
  append(ipco_payload, auxC);
  auto ipco = make_box("ipco", ipco_payload);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, 2);
  put_u16_be(ipma_payload, 1);            // item 1: ispe + mskC
  ipma_payload.push_back(2);
  ipma_payload.push_back(0x80 | 1);
  ipma_payload.push_back(0x80 | 2);
  put_u16_be(ipma_payload, 2);            // item 2: ispe + auxC(depth)
  ipma_payload.push_back(2);
  ipma_payload.push_back(0x80 | 1);
  ipma_payload.push_back(0x80 | 3);
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  std::vector<uint8_t> idat_payload(W * H + 16, 0x7F);   // item 1 mask + item 2 dummy
  auto idat = make_box("idat", idat_payload);

  std::vector<uint8_t> iloc_payload;
  iloc_payload.push_back((4 << 4) | 4);
  iloc_payload.push_back(0);
  put_u16_be(iloc_payload, 2);
  // item 1
  put_u16_be(iloc_payload, 1);
  put_u16_be(iloc_payload, 0x0001);   // idat
  put_u16_be(iloc_payload, 0);
  put_u16_be(iloc_payload, 1);
  put_u32_be(iloc_payload, 0);
  put_u32_be(iloc_payload, W * H);
  // item 2
  put_u16_be(iloc_payload, 2);
  put_u16_be(iloc_payload, 0x0001);
  put_u16_be(iloc_payload, 0);
  put_u16_be(iloc_payload, 1);
  put_u32_be(iloc_payload, W * H);
  put_u32_be(iloc_payload, 16);
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);

  // iref: 'auxl' from item 2 (aux) to item 1 (master)
  std::vector<uint8_t> auxl_payload;
  put_u16_be(auxl_payload, 2);
  put_u16_be(auxl_payload, 1);
  put_u16_be(auxl_payload, 1);
  auto iref = make_box("iref", make_box("auxl", auxl_payload), /*full=*/true);

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


// Decoding an error item (reached here through its depth-image handle) must
// return that item's error, not crash on a null context.
TEST_CASE("error item: decoding an unsupported depth aux item does not crash") {
  auto data = build_file_with_error_depth_item();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory_without_copy(ctx, data.data(), data.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* primary = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &primary);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(primary != nullptr);

  int num_depth = heif_image_handle_get_number_of_depth_images(primary);
  REQUIRE(num_depth == 1);

  heif_item_id depth_id = 0;
  heif_image_handle_get_list_of_depth_image_IDs(primary, &depth_id, 1);

  heif_image_handle* depth_handle = nullptr;
  err = heif_image_handle_get_depth_image_handle(primary, depth_id, &depth_handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(depth_handle != nullptr);

  // The crash was here: decode_image() -> verify_decodable() -> get_file() on a
  // null context. Reaching the assert at all proves it no longer segfaults.
  heif_image* img = nullptr;
  err = heif_decode_image(depth_handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
  REQUIRE(err.code != heif_error_Ok);   // the item's own error is surfaced
  if (img) { heif_image_release(img); }

  heif_image_handle_release(depth_handle);
  heif_image_handle_release(primary);
  heif_context_free(ctx);
}
