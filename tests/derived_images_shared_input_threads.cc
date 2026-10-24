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

// Derived images (overlay, 'iden', grid) report the bit depth of their first coded input
// image. They also ask for it while they are decoded (canvas of the overlay, check of the
// decoded bit depth). A JPEG 2000 item has to read its codestream to answer, which it does
// through the decoder object that is also used for decoding the item.
//
// When derived images are the tiles of a grid, they are decoded by parallel tile threads.
// If they share an input image, one thread asked for its bit depth while another thread was
// decoding it and replaced the cached input data of the decoder: heap-use-after-free.
// The input image's bit-depth query now takes its decoding mutex.
//
// The files of this test are a grid whose tiles cycle through four derived images, which
// all have the same JPEG 2000 item as input. As this is a race, the test relies on a
// sanitizer build (ASan or TSan) to report a regression reliably.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint16_t TILE_SIZE = 64;
constexpr uint8_t ROWS = 8;
constexpr uint8_t COLUMNS = 8;

constexpr uint16_t GRID_ID = 1;
constexpr uint16_t J2K_ID = 2;
const std::vector<uint16_t> derived_ids{10, 11, 12, 13};


heif_error write_to_vector(heif_context*, const void* data, size_t size, void* userdata)
{
  auto* out = static_cast<std::vector<uint8_t>*>(userdata);
  const auto* bytes = static_cast<const uint8_t*>(data);
  out->insert(out->end(), bytes, bytes + size);
  return heif_error{heif_error_Ok, heif_suberror_Unspecified, "Success"};
}


std::vector<uint8_t> encode_jpeg2000_image()
{
  heif_encoder* encoder = get_encoder_or_skip_test(heif_compression_JPEG2000);

  heif_image* img = nullptr;
  heif_error err = heif_image_create(TILE_SIZE, TILE_SIZE, heif_colorspace_YCbCr, heif_chroma_444, &img);
  REQUIRE(err.code == heif_error_Ok);

  fill_new_plane(img, heif_channel_Y, TILE_SIZE, TILE_SIZE);
  fill_new_plane(img, heif_channel_Cb, TILE_SIZE, TILE_SIZE);
  fill_new_plane(img, heif_channel_Cr, TILE_SIZE, TILE_SIZE);

  heif_context* ctx = heif_context_alloc();
  err = heif_context_encode_image(ctx, img, encoder, nullptr, nullptr);
  INFO("encode: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  std::vector<uint8_t> file;
  heif_writer writer;
  writer.writer_api_version = 1;
  writer.write = write_to_vector;
  err = heif_context_write(ctx, &writer, &file);
  REQUIRE(err.code == heif_error_Ok);

  heif_encoder_release(encoder);
  heif_context_free(ctx);
  heif_image_release(img);

  return file;
}


// Returns the first box of the given type, including its header.
std::vector<uint8_t> find_box(const std::vector<uint8_t>& file, const char fourcc[4])
{
  auto pos = std::search(file.begin() + 4, file.end(), fourcc, fourcc + 4);
  REQUIRE(pos != file.end());

  auto start = pos - 4;
  size_t size = (size_t(start[0]) << 24) | (size_t(start[1]) << 16) | (size_t(start[2]) << 8) | size_t(start[3]);
  REQUIRE(size >= 8);
  REQUIRE(size <= size_t(file.end() - start));

  return {start, start + size};
}


std::vector<uint8_t> image_grid(uint8_t rows, uint8_t columns)
{
  std::vector<uint8_t> data{0, 0, uint8_t(rows - 1), uint8_t(columns - 1)};
  put_u16_be(data, columns * TILE_SIZE);
  put_u16_be(data, rows * TILE_SIZE);
  return data;
}


// An overlay with a canvas of the tile size and one input image at offset (0;0).
std::vector<uint8_t> image_overlay()
{
  std::vector<uint8_t> data{0, 0};
  const uint16_t background_color[] = {0, 0, 0, 0xFFFF};
  for (uint16_t v : background_color) {
    put_u16_be(data, v);
  }
  put_u16_be(data, TILE_SIZE);
  put_u16_be(data, TILE_SIZE);
  put_u16_be(data, 0);
  put_u16_be(data, 0);
  return data;
}


// Item 1 is the grid, items 10-13 are derived images of type 'derived_type' that are used in
// turn as the grid tiles, item 2 is the JPEG 2000 image that all of them are derived from.
std::vector<uint8_t> build_file(const std::vector<uint8_t>& jpeg2000_file, const std::string& derived_type)
{
  struct Item
  {
    uint16_t id;
    std::string type;
    std::vector<uint8_t> properties;
    std::vector<uint16_t> dimg;
    std::vector<uint8_t> data;
  };

  // properties: 1 = size of the grid, 2 = size of a tile, 3 = j2kH

  std::vector<uint8_t> mdat = find_box(jpeg2000_file, "mdat");

  std::vector<Item> items;
  items.push_back({GRID_ID, "grid", {1}, {}, image_grid(ROWS, COLUMNS)});
  items.push_back({J2K_ID, "j2k1", {2, 0x80 | 3}, {}, {mdat.begin() + 8, mdat.end()}});

  for (int i = 0; i < ROWS * COLUMNS; i++) {
    items[0].dimg.push_back(derived_ids[i % derived_ids.size()]);
  }

  for (uint16_t id : derived_ids) {
    std::vector<uint8_t> data;
    if (derived_type == "iovl") {
      data = image_overlay();
    }
    else if (derived_type == "grid") {
      data = image_grid(1, 1);
    }

    items.push_back({id, derived_type, {2}, {J2K_ID}, data});
  }


  std::vector<uint8_t> ftyp_payload;
  append_fourcc(ftyp_payload, "mif1");
  put_u32_be(ftyp_payload, 0);
  append_fourcc(ftyp_payload, "mif1");
  append_fourcc(ftyp_payload, "j2ki");

  std::vector<uint8_t> hdlr_payload;
  put_u32_be(hdlr_payload, 0);
  append_fourcc(hdlr_payload, "pict");
  hdlr_payload.resize(hdlr_payload.size() + 13);

  std::vector<uint8_t> pitm_payload;
  put_u16_be(pitm_payload, GRID_ID);

  std::vector<uint8_t> iinf_payload, ipma_payload, iref_payload, iloc_payload, idat_payload;
  put_u16_be(iinf_payload, uint16_t(items.size()));
  put_u32_be(ipma_payload, uint32_t(items.size()));
  iloc_payload.push_back(0x44); // offset_size=4, length_size=4
  iloc_payload.push_back(0);
  put_u16_be(iloc_payload, uint16_t(items.size()));

  for (const Item& item : items) {
    std::vector<uint8_t> infe_payload;
    put_u16_be(infe_payload, item.id);
    put_u16_be(infe_payload, 0);
    append_fourcc(infe_payload, item.type.c_str());
    infe_payload.push_back(0);
    append(iinf_payload, make_box("infe", infe_payload, true, 2, item.id == GRID_ID ? 0 : 1 /* hidden */));

    put_u16_be(ipma_payload, item.id);
    ipma_payload.push_back(uint8_t(item.properties.size()));
    append(ipma_payload, item.properties);

    if (!item.dimg.empty()) {
      std::vector<uint8_t> dimg_payload;
      put_u16_be(dimg_payload, item.id);
      put_u16_be(dimg_payload, uint16_t(item.dimg.size()));
      for (uint16_t to_id : item.dimg) {
        put_u16_be(dimg_payload, to_id);
      }
      append(iref_payload, make_box("dimg", dimg_payload));
    }

    put_u16_be(iloc_payload, item.id);
    put_u16_be(iloc_payload, 1); // construction_method=1 (idat)
    put_u16_be(iloc_payload, 0);
    put_u16_be(iloc_payload, 1); // extent_count
    put_u32_be(iloc_payload, uint32_t(idat_payload.size()));
    put_u32_be(iloc_payload, uint32_t(item.data.size()));
    append(idat_payload, item.data);
  }

  std::vector<uint8_t> grid_ispe_payload, tile_ispe_payload;
  put_u32_be(grid_ispe_payload, COLUMNS * TILE_SIZE);
  put_u32_be(grid_ispe_payload, ROWS * TILE_SIZE);
  put_u32_be(tile_ispe_payload, TILE_SIZE);
  put_u32_be(tile_ispe_payload, TILE_SIZE);

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, make_box("ispe", grid_ispe_payload, true));
  append(ipco_payload, make_box("ispe", tile_ispe_payload, true));
  append(ipco_payload, find_box(jpeg2000_file, "j2kH"));

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, make_box("ipco", ipco_payload));
  append(iprp_payload, make_box("ipma", ipma_payload, true));

  std::vector<uint8_t> meta_payload;
  append(meta_payload, make_box("hdlr", hdlr_payload, true));
  append(meta_payload, make_box("pitm", pitm_payload, true));
  append(meta_payload, make_box("iloc", iloc_payload, true, 1));
  append(meta_payload, make_box("iinf", iinf_payload, true));
  append(meta_payload, make_box("iref", iref_payload, true));
  append(meta_payload, make_box("iprp", iprp_payload));
  append(meta_payload, make_box("idat", idat_payload));

  std::vector<uint8_t> file = make_box("ftyp", ftyp_payload);
  append(file, make_box("meta", meta_payload, true));
  return file;
}

} // namespace


TEST_CASE("derived images on grid tile threads share a JPEG 2000 input image")
{
  if (!heif_have_decoder_for_format(heif_compression_JPEG2000)) {
    SKIP("Decoder for JPEG 2000 not found, skipping test");
  }

  std::string derived_type = GENERATE("iovl", "iden", "grid");
  INFO("grid tiles are '" << derived_type << "' items");

  std::vector<uint8_t> file = build_file(encode_jpeg2000_image(), derived_type);

  heif_context* ctx = heif_context_alloc();
  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  INFO("read: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(heif_image_handle_get_luma_bits_per_pixel(handle) == 8);

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
  INFO("decode: " << err.message);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(heif_image_get_primary_width(img) == COLUMNS * TILE_SIZE);
  REQUIRE(heif_image_get_primary_height(img) == ROWS * TILE_SIZE);

  heif_image_release(img);
  heif_image_handle_release(handle);
  heif_context_free(ctx);
}
