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

// Regression tests for three defects found with the HEIF conformance file C021,
// an 'iovl' that composites the same coded image twice: at (0,0) and, on top of
// it, at (-640,-360) on a canvas of half the image size.
//
// 1. Box_iref rejected every 'dimg' entry that lists the same item twice
//    ("'iref' has double references"). Neither ISO/IEC 14496-12 nor 23008-12
//    forbids this, and an overlay needs it to place one image at two positions,
//    because the offsets are paired with the references.
// 2. ImageOverlay::parse() sign-extended 16-bit offsets incorrectly, so every
//    negative offset in the common 16-bit form placed the image far outside the
//    canvas, where it was silently skipped.
// 3. HeifPixelImage::overlay() mis-clipped input images with negative offsets:
//    a layer whose negative offset was at least half its size was not drawn at
//    all, and smaller negative offsets were truncated.
//
// The files are built by hand from an 'mski' base image, which needs no codec,
// so the tests run in every build configuration. Each test decodes the base
// image and the overlay through the public API and compares the overlay with a
// straightforward reference composition of the decoded base.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

const uint32_t BASE_W = 8;
const uint32_t BASE_H = 8;

const heif_item_id BASE_ID = 1;
const heif_item_id IOVL_ID = 2;

struct Layer {
  int32_t x;
  int32_t y;
};


// ImageOverlay payload (ISO/IEC 23008-12 6.6.2.2): version, flags (bit 0
// selects 32-bit instead of 16-bit size and offset fields), background RGBA
// (opaque white), canvas size, and one signed (x,y) offset per input image.
std::vector<uint8_t> make_overlay_spec(uint16_t canvas_w, uint16_t canvas_h, const std::vector<Layer>& layers,
                                       bool long_fields)
{
  auto put_field = [long_fields](std::vector<uint8_t>& out, int32_t v) {
    if (long_fields) {
      put_u32_be(out, static_cast<uint32_t>(v));
    }
    else {
      put_u16_be(out, static_cast<uint16_t>(static_cast<int16_t>(v)));
    }
  };

  std::vector<uint8_t> s;
  s.push_back(0);
  s.push_back(long_fields ? 1 : 0);
  for (int i = 0; i < 4; i++) { put_u16_be(s, 0xFFFF); }
  put_field(s, canvas_w);
  put_field(s, canvas_h);
  for (const auto& l : layers) {
    put_field(s, l.x);
    put_field(s, l.y);
  }
  return s;
}


// A file with two items: item 1 is an 8x8 'mski' base image whose pixel value is
// 8*y+x, item 2 is the primary 'iovl' that references the base once per layer.
// With two or more layers, the 'dimg' entry therefore lists item 1 repeatedly.
std::vector<uint8_t> build_file(uint16_t canvas_w, uint16_t canvas_h, const std::vector<Layer>& layers, bool long_fields)
{
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
  put_u16_be(pitm_payload, static_cast<uint16_t>(IOVL_ID));
  auto pitm = make_box("pitm", pitm_payload, /*full=*/true);

  // iinf
  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, 2);
  for (const auto& item : {std::make_pair(BASE_ID, "mski"), std::make_pair(IOVL_ID, "iovl")}) {
    std::vector<uint8_t> infe_payload;
    put_u16_be(infe_payload, static_cast<uint16_t>(item.first));
    put_u16_be(infe_payload, 0);
    append_fourcc(infe_payload, item.second);
    infe_payload.push_back(0);
    append(iinf_payload, make_box("infe", infe_payload, /*full=*/true, /*version=*/2));
  }
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // iprp: property 1 = ispe of the base, 2 = mskC, 3 = ispe of the canvas
  std::vector<uint8_t> ispe_base_payload;
  put_u32_be(ispe_base_payload, BASE_W);
  put_u32_be(ispe_base_payload, BASE_H);
  auto ispe_base = make_box("ispe", ispe_base_payload, /*full=*/true);

  std::vector<uint8_t> mskC_payload;
  mskC_payload.push_back(8);  // bits_per_pixel
  auto mskC = make_box("mskC", mskC_payload, /*full=*/true);

  std::vector<uint8_t> ispe_canvas_payload;
  put_u32_be(ispe_canvas_payload, canvas_w);
  put_u32_be(ispe_canvas_payload, canvas_h);
  auto ispe_canvas = make_box("ispe", ispe_canvas_payload, /*full=*/true);

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, ispe_base);
  append(ipco_payload, mskC);
  append(ipco_payload, ispe_canvas);
  auto ipco = make_box("ipco", ipco_payload);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, 2);                  // entry_count
  put_u16_be(ipma_payload, static_cast<uint16_t>(BASE_ID));
  ipma_payload.push_back(2);                    // association_count
  ipma_payload.push_back(0x80 | 1);             // essential, ispe (base)
  ipma_payload.push_back(0x80 | 2);             // essential, mskC
  put_u16_be(ipma_payload, static_cast<uint16_t>(IOVL_ID));
  ipma_payload.push_back(1);
  ipma_payload.push_back(0x80 | 3);             // essential, ispe (canvas)
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // idat: base pixels, then the overlay spec
  std::vector<uint8_t> base_data(BASE_W * BASE_H);
  for (uint32_t i = 0; i < BASE_W * BASE_H; i++) {
    base_data[i] = static_cast<uint8_t>(i);
  }
  auto spec = make_overlay_spec(canvas_w, canvas_h, layers, long_fields);

  std::vector<uint8_t> idat_payload;
  append(idat_payload, base_data);
  append(idat_payload, spec);
  auto idat = make_box("idat", idat_payload);

  std::vector<uint8_t> iloc_payload;
  iloc_payload.push_back((4 << 4) | 4);         // offset_size=4, length_size=4
  iloc_payload.push_back((0 << 4) | 0);         // base_offset_size=0, index_size=0
  put_u16_be(iloc_payload, 2);                  // item_count
  struct Extent { heif_item_id id; uint32_t off; uint32_t len; };
  for (const Extent& e : {Extent{BASE_ID, 0, static_cast<uint32_t>(base_data.size())},
                          Extent{IOVL_ID, static_cast<uint32_t>(base_data.size()), static_cast<uint32_t>(spec.size())}}) {
    put_u16_be(iloc_payload, static_cast<uint16_t>(e.id));
    put_u16_be(iloc_payload, 0x0001);           // reserved(12) + construction_method=1 (idat)
    put_u16_be(iloc_payload, 0);                // data_reference_index
    put_u16_be(iloc_payload, 1);                // extent_count
    put_u32_be(iloc_payload, e.off);
    put_u32_be(iloc_payload, e.len);
  }
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);

  // iref: one 'dimg' entry listing the base once per layer
  std::vector<uint8_t> dimg_payload;
  put_u16_be(dimg_payload, static_cast<uint16_t>(IOVL_ID));
  put_u16_be(dimg_payload, static_cast<uint16_t>(layers.size()));
  for (size_t i = 0; i < layers.size(); i++) {
    put_u16_be(dimg_payload, static_cast<uint16_t>(BASE_ID));
  }
  auto iref = make_box("iref", make_box("dimg", dimg_payload), /*full=*/true);

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


struct Pixels {
  uint32_t w = 0;
  uint32_t h = 0;
  std::vector<uint8_t> rgb;  // interleaved, tightly packed
};

Pixels decode_rgb(heif_image_handle* handle)
{
  heif_image* img = nullptr;
  heif_error err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);
  REQUIRE(err.code == heif_error_Ok);

  Pixels px;
  px.w = static_cast<uint32_t>(heif_image_get_width(img, heif_channel_interleaved));
  px.h = static_cast<uint32_t>(heif_image_get_height(img, heif_channel_interleaved));

  size_t stride = 0;
  const uint8_t* p = heif_image_get_plane_readonly2(img, heif_channel_interleaved, &stride);
  REQUIRE(p != nullptr);

  px.rgb.resize(static_cast<size_t>(px.w) * px.h * 3);
  for (uint32_t y = 0; y < px.h; y++) {
    memcpy(&px.rgb[static_cast<size_t>(y) * px.w * 3], p + y * stride, static_cast<size_t>(px.w) * 3);
  }

  heif_image_release(img);
  return px;
}

Pixels decode_item(heif_context* ctx, heif_item_id id)
{
  heif_image_handle* handle = nullptr;
  REQUIRE(heif_context_get_image_handle(ctx, id, &handle).code == heif_error_Ok);
  Pixels px = decode_rgb(handle);
  heif_image_handle_release(handle);
  return px;
}

Pixels decode_primary(heif_context* ctx)
{
  heif_image_handle* handle = nullptr;
  REQUIRE(heif_context_get_primary_image_handle(ctx, &handle).code == heif_error_Ok);
  Pixels px = decode_rgb(handle);
  heif_image_handle_release(handle);
  return px;
}


// Reference composition: an opaque white canvas with the base image painted at
// every layer offset in order, pixels outside the canvas dropped.
std::vector<uint8_t> composite(const Pixels& base, uint32_t canvas_w, uint32_t canvas_h, const std::vector<Layer>& layers)
{
  std::vector<uint8_t> out(static_cast<size_t>(canvas_w) * canvas_h * 3, 255);

  for (const auto& l : layers) {
    for (uint32_t y = 0; y < base.h; y++) {
      for (uint32_t x = 0; x < base.w; x++) {
        int64_t cx = static_cast<int64_t>(l.x) + x;
        int64_t cy = static_cast<int64_t>(l.y) + y;
        if (cx < 0 || cy < 0 || cx >= canvas_w || cy >= canvas_h) {
          continue;
        }

        memcpy(&out[(static_cast<size_t>(cy) * canvas_w + static_cast<size_t>(cx)) * 3],
               &base.rgb[(static_cast<size_t>(y) * base.w + x) * 3],
               3);
      }
    }
  }

  return out;
}


// Compare pixel by pixel. (A REQUIRE on the whole vectors would print hundreds
// of bytes on failure, and the first mismatch is what matters.)
void require_same_pixels(const Pixels& actual, const std::vector<uint8_t>& expected)
{
  REQUIRE(actual.rgb.size() == expected.size());

  for (size_t i = 0; i < expected.size(); i++) {
    if (actual.rgb[i] != expected[i]) {
      size_t pixel = i / 3;
      uint32_t x = static_cast<uint32_t>(pixel % actual.w);
      uint32_t y = static_cast<uint32_t>(pixel / actual.w);

      std::string actual_row, expected_row;
      for (uint32_t xx = 0; xx < actual.w; xx++) {
        size_t idx = (static_cast<size_t>(y) * actual.w + xx) * 3;
        actual_row += std::to_string(actual.rgb[idx]) + " ";
        expected_row += std::to_string(expected[idx]) + " ";
      }

      INFO("first mismatch at pixel (" << x << "," << y << "), channel " << (i % 3));
      INFO("decoded row " << y << ":  " << actual_row);
      INFO("expected row " << y << ": " << expected_row);
      REQUIRE(static_cast<int>(actual.rgb[i]) == static_cast<int>(expected[i]));
    }
  }
}


void check_composition_with_field_size(uint16_t canvas_w, uint16_t canvas_h, const std::vector<Layer>& layers,
                                       bool long_fields)
{
  INFO("canvas " << canvas_w << "x" << canvas_h << ", " << layers.size() << " layer(s), first offset ("
                 << layers[0].x << "," << layers[0].y << "), " << (long_fields ? 32 : 16) << "-bit fields");

  auto data = build_file(canvas_w, canvas_h, layers, long_fields);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  heif_error err = heif_context_read_from_memory_without_copy(ctx, data.data(), data.size(), nullptr);
  INFO("read error: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  Pixels base = decode_item(ctx, BASE_ID);
  REQUIRE(base.w == BASE_W);
  REQUIRE(base.h == BASE_H);

  Pixels canvas = decode_primary(ctx);
  REQUIRE(canvas.w == canvas_w);
  REQUIRE(canvas.h == canvas_h);
  require_same_pixels(canvas, composite(base, canvas_w, canvas_h, layers));

  heif_context_free(ctx);
}

// Check the composition with both overlay field sizes. Negative 16-bit offsets
// were mis-read as huge negative values (the sign extension was only correct
// for 32-bit fields), which also made the image disappear from the canvas.
void check_composition(uint16_t canvas_w, uint16_t canvas_h, const std::vector<Layer>& layers)
{
  check_composition_with_field_size(canvas_w, canvas_h, layers, false);
  check_composition_with_field_size(canvas_w, canvas_h, layers, true);
}

} // namespace


TEST_CASE("overlay offsets: negative offset of at least half the image size") {
  // Used to be dropped entirely.
  check_composition(8, 8, {{-4, -4}});
  check_composition(8, 8, {{-6, -2}});
  check_composition(8, 8, {{0, -5}});
}

TEST_CASE("overlay offsets: small negative offset") {
  // Used to be truncated.
  check_composition(8, 8, {{-2, -3}});
  check_composition(8, 8, {{-1, 0}});
}

TEST_CASE("overlay offsets: positive offset partially outside the canvas") {
  check_composition(8, 8, {{5, 6}});
  check_composition(8, 8, {{7, 7}});
}

TEST_CASE("overlay offsets: input image completely outside the canvas") {
  check_composition(8, 8, {{-8, 0}});
  check_composition(8, 8, {{0, 8}});
  check_composition(4, 4, {{-8, -8}});
  check_composition(4, 4, {{4, 0}});
}

TEST_CASE("overlay offsets: canvas smaller than the input image") {
  check_composition(4, 4, {{0, 0}});
  check_composition(4, 4, {{-2, -2}});
  check_composition(4, 4, {{-4, -4}});
}

TEST_CASE("overlay offsets: the same input image placed twice (C021 structure)") {
  // The 'dimg' entry lists item 1 twice. These files were rejected while
  // parsing with "'iref' has double references".
  check_composition(8, 8, {{0, 0}, {-4, -4}});
  check_composition(4, 4, {{0, 0}, {-4, -4}});
  check_composition(8, 8, {{-4, -4}, {0, 0}});
  check_composition(8, 8, {{0, 0}, {2, 2}, {-6, -6}});
}

TEST_CASE("overlay offsets: overlay written through the API may reference one image twice") {
  heif_encoder* encoder = get_encoder_or_skip_test(heif_compression_mask);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_image* base = nullptr;
  REQUIRE(heif_image_create(BASE_W, BASE_H, heif_colorspace_monochrome, heif_chroma_monochrome, &base).code == heif_error_Ok);
  REQUIRE(heif_image_add_plane(base, heif_channel_Y, BASE_W, BASE_H, 8).code == heif_error_Ok);
  size_t stride = 0;
  uint8_t* p = heif_image_get_plane2(base, heif_channel_Y, &stride);
  for (uint32_t y = 0; y < BASE_H; y++) {
    for (uint32_t x = 0; x < BASE_W; x++) {
      p[y * stride + x] = static_cast<uint8_t>(BASE_W * y + x);
    }
  }

  heif_image_handle* base_handle = nullptr;
  REQUIRE(heif_context_encode_image(ctx, base, encoder, nullptr, &base_handle).code == heif_error_Ok);
  heif_item_id base_id = heif_image_handle_get_item_id(base_handle);

  const std::vector<Layer> layers = {{0, 0}, {-4, -4}};
  heif_item_id ids[2] = {base_id, base_id};
  int32_t offsets[4] = {0, 0, -4, -4};
  uint16_t background[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};

  heif_image_handle* iovl_handle = nullptr;
  REQUIRE(heif_context_add_overlay_image(ctx, 8, 8, 2, ids, offsets, background, &iovl_handle).code == heif_error_Ok);
  REQUIRE(heif_context_set_primary_image(ctx, iovl_handle).code == heif_error_Ok);

  // Writing used to fail here with "'iref' has double references".
  std::string path = get_tests_output_file_path("overlay_same_image_twice.heif");
  heif_error err = heif_context_write_to_file(ctx, path.c_str());
  INFO("write error: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle_release(iovl_handle);
  heif_image_handle_release(base_handle);
  heif_image_release(base);
  heif_encoder_release(encoder);
  heif_context_free(ctx);

  // Read the file back and check the composition.
  ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  REQUIRE(heif_context_read_from_file(ctx, path.c_str(), nullptr).code == heif_error_Ok);

  Pixels base_px = decode_item(ctx, base_id);
  REQUIRE(base_px.w == BASE_W);
  REQUIRE(base_px.h == BASE_H);

  Pixels canvas = decode_primary(ctx);
  REQUIRE(canvas.w == 8);
  REQUIRE(canvas.h == 8);
  require_same_pixels(canvas, composite(base_px, 8, 8, layers));

  heif_context_free(ctx);
}
