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

// The canvas of a 'grid' image is cloned from the first decoded tile, including the tile's
// alpha plane, and that plane is then filled with "opaque" in case some tiles have no alpha.
// The fill value was computed as (1UL << alpha_bpp) - 1 behind assert(alpha_bpp <= 16).
// An 'unci' tile can have an alpha component with 32, 64 or 128 bits and with signed, float
// or complex samples, so a crafted grid aborted every process whose libheif is built with
// assertions, and shifted by 64 (undefined behaviour) otherwise (GHSA-3gwx-cjv4-jr74).
//
// The opaque fill now works for unsigned integer planes of up to 64 bits. For an alpha plane
// of another kind there is no opaque value: the plane stays zero where a tile has no alpha,
// and the decoded image carries a warning.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint32_t TILE_W = 16;
constexpr uint32_t TILE_H = 16;

struct AlphaFormat
{
  const char* name;
  uint8_t bit_depth;
  uint8_t component_format;  // 0 = unsigned integer, 1 = float, 2 = complex, 3 = signed integer
  std::vector<uint8_t> tile0_sample;  // one alpha sample of the left tile, big-endian
  std::vector<uint8_t> tile1_sample;  // one alpha sample of the right tile, big-endian

  // whether the grid can fill in an opaque value for this kind of plane
  bool has_opaque_value() const { return component_format == 0 && bit_depth <= 64; }

  // a complex sample consists of two numbers
  size_t word_size() const { return (component_format == 2) ? bit_depth / 16 : bit_depth / 8; }
};


std::vector<uint8_t> tile_data(const AlphaFormat& alpha, uint8_t color_value, const std::vector<uint8_t>& alpha_sample)
{
  // component interleave: the R, G and B planes (8 bit), then the alpha plane
  std::vector<uint8_t> data(3 * TILE_W * TILE_H, color_value);
  for (uint32_t i = 0; i < TILE_W * TILE_H; i++) {
    append(data, alpha_sample);
  }
  return data;
}


// A 'grid' of two 'unci' tiles (R, G, B with 8 bits and an alpha component) next to each other.
// With 'truncate_second_tile', the data of the right tile is too short to be decoded.
std::vector<uint8_t> build_grid_file(const AlphaFormat& alpha, bool truncate_second_tile)
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
  put_u16_be(pitm_payload, 1);
  auto pitm = make_box("pitm", pitm_payload, /*full=*/true);

  // items: 1 = grid, 2 and 3 = unci tiles
  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, 3);
  const char* item_types[3] = {"grid", "unci", "unci"};
  for (uint16_t id = 1; id <= 3; id++) {
    std::vector<uint8_t> infe_payload;
    put_u16_be(infe_payload, id);
    put_u16_be(infe_payload, 0);
    append_fourcc(infe_payload, item_types[id - 1]);
    append_cstr(infe_payload, "");
    append(iinf_payload, make_box("infe", infe_payload, /*full=*/true, /*version=*/2));
  }
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  std::vector<uint8_t> dimg_payload;
  put_u16_be(dimg_payload, 1); // from the grid
  put_u16_be(dimg_payload, 2); // two references
  put_u16_be(dimg_payload, 2);
  put_u16_be(dimg_payload, 3);
  auto iref = make_box("iref", make_box("dimg", dimg_payload), /*full=*/true);

  // properties: 1 = ispe of the grid, 2 = ispe of a tile, 3 = cmpd, 4 = uncC
  std::vector<uint8_t> grid_ispe_payload;
  put_u32_be(grid_ispe_payload, 2 * TILE_W);
  put_u32_be(grid_ispe_payload, TILE_H);

  std::vector<uint8_t> tile_ispe_payload;
  put_u32_be(tile_ispe_payload, TILE_W);
  put_u32_be(tile_ispe_payload, TILE_H);

  std::vector<uint8_t> cmpd_payload;
  put_u32_be(cmpd_payload, 4);
  put_u16_be(cmpd_payload, 4); // red
  put_u16_be(cmpd_payload, 5); // green
  put_u16_be(cmpd_payload, 6); // blue
  put_u16_be(cmpd_payload, 7); // alpha

  const uint8_t depths[4] = {8, 8, 8, alpha.bit_depth};
  const uint8_t formats[4] = {0, 0, 0, alpha.component_format};

  std::vector<uint8_t> uncC_payload;
  put_u32_be(uncC_payload, 0); // profile
  put_u32_be(uncC_payload, 4); // component_count
  for (uint16_t idx = 0; idx < 4; idx++) {
    put_u16_be(uncC_payload, idx);                                   // component_index
    uncC_payload.push_back(static_cast<uint8_t>(depths[idx] - 1));   // component_bit_depth_minus_one
    uncC_payload.push_back(formats[idx]);                            // component_format
    uncC_payload.push_back(0);                                       // component_align_size
  }
  uncC_payload.push_back(0);   // sampling_type = no subsampling
  uncC_payload.push_back(0);   // interleave_type = component
  uncC_payload.push_back(0);   // block_size
  uncC_payload.push_back(0);   // flags (big-endian components)
  put_u32_be(uncC_payload, 0); // pixel_size
  put_u32_be(uncC_payload, 0); // row_align_size
  put_u32_be(uncC_payload, 0); // tile_align_size
  put_u32_be(uncC_payload, 0); // num_tile_cols_minus_one
  put_u32_be(uncC_payload, 0); // num_tile_rows_minus_one

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, make_box("ispe", grid_ispe_payload, /*full=*/true));
  append(ipco_payload, make_box("ispe", tile_ispe_payload, /*full=*/true));
  append(ipco_payload, make_box("cmpd", cmpd_payload));
  append(ipco_payload, make_box("uncC", uncC_payload, /*full=*/true));
  auto ipco = make_box("ipco", ipco_payload);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, 3);      // entry_count
  put_u16_be(ipma_payload, 1);      // the grid
  ipma_payload.push_back(1);
  ipma_payload.push_back(0x80 | 1);
  for (uint16_t id = 2; id <= 3; id++) {
    put_u16_be(ipma_payload, id);   // a tile
    ipma_payload.push_back(3);
    ipma_payload.push_back(0x80 | 2);
    ipma_payload.push_back(0x80 | 3);
    ipma_payload.push_back(0x80 | 4);
  }
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // --- item data, all in 'idat'

  std::vector<uint8_t> grid_data;
  grid_data.push_back(0); // version
  grid_data.push_back(0); // flags
  grid_data.push_back(0); // rows_minus_one
  grid_data.push_back(1); // columns_minus_one
  put_u16_be(grid_data, 2 * TILE_W);
  put_u16_be(grid_data, TILE_H);

  std::vector<uint8_t> tile0 = tile_data(alpha, 0x40, alpha.tile0_sample);
  std::vector<uint8_t> tile1 = tile_data(alpha, 0x80, alpha.tile1_sample);
  if (truncate_second_tile) {
    tile1.resize(tile1.size() / 2);
  }

  std::vector<uint8_t> idat_payload;
  append(idat_payload, grid_data);
  append(idat_payload, tile0);
  append(idat_payload, tile1);
  auto idat = make_box("idat", idat_payload);

  const std::vector<uint8_t>* item_data[3] = {&grid_data, &tile0, &tile1};

  std::vector<uint8_t> iloc_payload;
  put_u16_be(iloc_payload, (4 << 12) | (4 << 8)); // offset_size=4, length_size=4
  put_u16_be(iloc_payload, 3);                    // item_count
  uint32_t offset = 0;
  for (uint16_t id = 1; id <= 3; id++) {
    put_u16_be(iloc_payload, id);
    put_u16_be(iloc_payload, 0x0001); // construction_method=1 (idat)
    put_u16_be(iloc_payload, 0);      // data_reference_index
    put_u16_be(iloc_payload, 1);      // extent_count
    put_u32_be(iloc_payload, offset);
    put_u32_be(iloc_payload, static_cast<uint32_t>(item_data[id - 1]->size()));
    offset += static_cast<uint32_t>(item_data[id - 1]->size());
  }
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);

  std::vector<uint8_t> meta_payload;
  append(meta_payload, hdlr);
  append(meta_payload, pitm);
  append(meta_payload, iinf);
  append(meta_payload, iref);
  append(meta_payload, iprp);
  append(meta_payload, iloc);
  append(meta_payload, idat);
  auto meta = make_box("meta", meta_payload, /*full=*/true);

  std::vector<uint8_t> file;
  append(file, ftyp);
  append(file, meta);
  return file;
}


struct Decoded
{
  heif_context* ctx = nullptr;
  heif_image_handle* handle = nullptr;
  heif_image* img = nullptr;
  heif_error_code code = heif_error_Ok;
  std::string message;

  ~Decoded()
  {
    if (img) heif_image_release(img);
    if (handle) heif_image_handle_release(handle);
    if (ctx) heif_context_free(ctx);
  }
};


// The file data has to stay alive as long as the context.
void decode(const std::vector<uint8_t>& file, heif_colorspace colorspace, heif_chroma chroma, Decoded& out)
{
  out.ctx = heif_context_alloc();
  heif_error err = heif_context_read_from_memory_without_copy(out.ctx, file.data(), file.size(), nullptr);
  INFO("read: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_get_primary_image_handle(out.ctx, &out.handle);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_decode_image(out.handle, &out.img, colorspace, chroma, nullptr);
  out.code = err.code;
  out.message = err.message ? err.message : "";
}


// Whether the decoded image has the warning about an alpha plane without an opaque value.
bool has_alpha_warning(heif_image* img)
{
  int n = heif_image_get_decoding_warnings(img, 0, nullptr, 0);
  for (int i = 0; i < n; i++) {
    heif_error warning;
    if (heif_image_get_decoding_warnings(img, i, &warning, 1) == 1 &&
        warning.code == heif_error_Unsupported_feature &&
        std::string(warning.message).find("alpha plane of the grid") != std::string::npos) {
      return true;
    }
  }
  return false;
}


// Compares the alpha samples of one tile area with the bytes of the expected sample
// (given in big-endian order, like in the file). 'word_size' is the size of the units that
// are stored in the byte order of the machine: the whole sample, or one of the two numbers
// of a complex sample.
void check_alpha_area(const heif_image* img, uint32_t x0, const std::vector<uint8_t>& expected_be, size_t word_size)
{
  size_t stride = 0;
  const uint8_t* plane = heif_image_get_plane_readonly2(img, heif_channel_Alpha, &stride);
  REQUIRE(plane != nullptr);

  const size_t bytes_per_sample = expected_be.size();

  std::vector<uint8_t> expected = expected_be;
  const uint16_t probe = 1;
  if (*reinterpret_cast<const uint8_t*>(&probe) == 1) {
    for (size_t i = 0; i < bytes_per_sample; i += word_size) {
      std::reverse(expected.begin() + i, expected.begin() + i + word_size);
    }
  }

  for (uint32_t y = 0; y < TILE_H; y++) {
    for (uint32_t x = x0; x < x0 + TILE_W; x++) {
      INFO("alpha at (" << x << "," << y << ")");
      REQUIRE(memcmp(plane + y * stride + x * bytes_per_sample, expected.data(), bytes_per_sample) == 0);
    }
  }
}

} // namespace


TEST_CASE("grid of unci tiles with alpha planes of all sample types")
{
  const std::vector<AlphaFormat> formats = {
      // unsigned integers: the opaque value is the largest value of the bit depth
      {"8 bit unsigned", 8, 0, {0x12}, {0x56}},
      {"16 bit unsigned", 16, 0, {0x12, 0x34}, {0x56, 0x78}},
      {"32 bit unsigned", 32, 0, {0x01, 0x02, 0x03, 0x04}, {0xF1, 0xF2, 0xF3, 0xF4}},
      {"64 bit unsigned", 64, 0, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}, {0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8}},

      // no opaque value
      {"16 bit signed", 16, 3, {0x12, 0x34}, {0xF6, 0x78}},
      {"32 bit float", 32, 1, {0x3F, 0x80, 0x00, 0x00}, {0x3F, 0x00, 0x00, 0x00}}, // 1.0 and 0.5
      {"64 bit float", 64, 1, {0x3F, 0xF0, 0, 0, 0, 0, 0, 0}, {0x3F, 0xE0, 0, 0, 0, 0, 0, 0}}, // 1.0 and 0.5
      {"128 bit complex", 128, 2, {0x3F, 0xF0, 0, 0, 0, 0, 0, 0, 0x3F, 0xE0, 0, 0, 0, 0, 0, 0},
                                  {0x3F, 0xE0, 0, 0, 0, 0, 0, 0, 0x3F, 0xF0, 0, 0, 0, 0, 0, 0}},
  };

  for (const AlphaFormat& alpha : formats) {
    // Sections in a loop need distinct names, otherwise only the first iteration runs them.
    const std::string name = std::string(alpha.name) + " alpha: ";

    DYNAMIC_SECTION(name << "decoding without conversion returns the alpha plane of the tiles") {
      std::vector<uint8_t> file = build_grid_file(alpha, false);

      Decoded d;
      decode(file, heif_colorspace_undefined, heif_chroma_undefined, d);
      INFO("decode: " << d.message);
      REQUIRE(d.code == heif_error_Ok);
      REQUIRE(d.img != nullptr);

      CHECK(heif_image_get_width(d.img, heif_channel_Alpha) == static_cast<int>(2 * TILE_W));
      CHECK(heif_image_get_height(d.img, heif_channel_Alpha) == static_cast<int>(TILE_H));
      REQUIRE(heif_image_get_bits_per_pixel_range(d.img, heif_channel_Alpha) == alpha.bit_depth);

      check_alpha_area(d.img, 0, alpha.tile0_sample, alpha.word_size());
      check_alpha_area(d.img, TILE_W, alpha.tile1_sample, alpha.word_size());

      size_t stride = 0;
      const uint8_t* red = heif_image_get_plane_readonly2(d.img, heif_channel_R, &stride);
      REQUIRE(red != nullptr);
      CHECK(red[0] == 0x40);
      CHECK(red[TILE_W] == 0x80);

      CHECK(has_alpha_warning(d.img) == !alpha.has_opaque_value());
    }

    DYNAMIC_SECTION(name << "the area of a tile that cannot be decoded is opaque, or zero with a warning") {
      std::vector<uint8_t> file = build_grid_file(alpha, true);

      Decoded d;
      decode(file, heif_colorspace_undefined, heif_chroma_undefined, d);
      INFO("decode: " << d.message);
      REQUIRE(d.code == heif_error_Ok);
      REQUIRE(d.img != nullptr);
      REQUIRE(heif_image_get_bits_per_pixel_range(d.img, heif_channel_Alpha) == alpha.bit_depth);

      check_alpha_area(d.img, 0, alpha.tile0_sample, alpha.word_size());

      // All bits set is the largest value of an unsigned integer whose bit depth fills
      // its storage word, which is the case for all the unsigned formats above.
      const uint8_t default_byte = alpha.has_opaque_value() ? 0xFF : 0x00;
      check_alpha_area(d.img, TILE_W, std::vector<uint8_t>(alpha.bit_depth / 8, default_byte), alpha.word_size());

      CHECK(has_alpha_warning(d.img) == !alpha.has_opaque_value());
    }

    if (alpha.bit_depth > 16) {
      DYNAMIC_SECTION(name << "a conversion is refused with an error") {
        // The color conversion does not support planes of more than 16 bits. This has to be
        // reported as an error. It used to be unreachable: the process aborted before.
        std::vector<uint8_t> file = build_grid_file(alpha, false);

        Decoded d;
        decode(file, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, d);
        INFO("decode: " << d.message);
        CHECK(d.code != heif_error_Ok);
        CHECK(d.img == nullptr);
      }
    }
  }
}
