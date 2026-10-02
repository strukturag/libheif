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

// Tests for decoding the tiles of an image that is combined with another image.
//
// The alpha channel of an image is a separate image item with a tiling of its own.
// When a tile is decoded, the same tile of the alpha image is decoded and attached.
// This only works when both images have the same tiling:
//
// - When the tilings differ, a tile of the alpha image covers another area than the
//   tile of the color image. The alpha tile was scaled to the size of the color
//   tile, which gave a wrong alpha channel. Such an image is now exposed as a
//   single tile, which is the whole image.
//
// - The tile position was converted from the transformed (e.g. rotated) image to
//   the coded image twice: by the color image and again by the alpha image. With a
//   rotation by 180 degrees, the alpha channel came from the opposite tile.
//
// An 'iden' image is exposed as a single tile, but it passed the tile request on
// to the image it is derived from. When that image has tiles, only its first tile
// was decoded, which then was rejected for its size.
//
// The tiles of a 'grid' image can have alpha images of their own (libheif writes
// grids with alpha like this). Decoding a single tile of the grid skipped the
// alpha image of the tile, so that the tile had no alpha channel.
//
// The tests build files with 'unci' image items without generic compression in
// memory. Each sample of the color image and of the alpha image has a different
// value.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace {

struct ItemSpec {
  uint32_t width = 4;
  uint32_t height = 4;
  uint32_t tile_columns = 2;
  uint32_t tile_rows = 2;
};

struct FileSpec {
  ItemSpec color;
  ItemSpec alpha;

  // Both images are rotated by 180 degrees ('irot').
  bool rotate_180 = false;

  // Both images are cropped to the 2x2 pixels in the center ('clap').
  bool clap = false;

  // The primary image is an 'iden' image derived from the color image.
  // The color image has no alpha image in this case.
  bool primary_is_iden = false;
};

constexpr uint32_t kNumColorComponents = 3;

uint8_t color_value(const FileSpec& spec, uint32_t component, uint32_t x, uint32_t y) {
  return static_cast<uint8_t>((component + 1) * 50 + y * spec.color.width + x);
}

uint8_t alpha_value(const FileSpec& spec, uint32_t x, uint32_t y) {
  return static_cast<uint8_t>(200 + y * spec.alpha.width + x);
}

// The item data of an 'unci' image with pixel interleave: the tiles in raster order.
template <typename ValueFunction>
std::vector<uint8_t> make_item_data(const ItemSpec& item, uint32_t num_components, ValueFunction value) {
  uint32_t tile_width = item.width / item.tile_columns;
  uint32_t tile_height = item.height / item.tile_rows;

  std::vector<uint8_t> data;
  for (uint32_t tile_y = 0; tile_y < item.tile_rows; tile_y++) {
    for (uint32_t tile_x = 0; tile_x < item.tile_columns; tile_x++) {
      for (uint32_t y = 0; y < tile_height; y++) {
        for (uint32_t x = 0; x < tile_width; x++) {
          for (uint32_t c = 0; c < num_components; c++) {
            data.push_back(value(c, tile_x * tile_width + x, tile_y * tile_height + y));
          }
        }
      }
    }
  }

  return data;
}

std::vector<uint8_t> make_ispe(const ItemSpec& item) {
  std::vector<uint8_t> payload;
  put_u32_be(payload, item.width);
  put_u32_be(payload, item.height);
  return make_box("ispe", payload, /*full=*/true);
}

std::vector<uint8_t> make_cmpd(const std::vector<uint16_t>& component_types) {
  std::vector<uint8_t> payload;
  put_u32_be(payload, static_cast<uint32_t>(component_types.size()));
  for (uint16_t type : component_types) {
    put_u16_be(payload, type);
  }
  return make_box("cmpd", payload);
}

// uncC (v0): 8-bit components, pixel interleave
std::vector<uint8_t> make_uncC(const ItemSpec& item, uint16_t num_components) {
  std::vector<uint8_t> payload;
  put_u32_be(payload, 0);           // profile
  put_u32_be(payload, num_components);
  for (uint16_t c = 0; c < num_components; c++) {
    put_u16_be(payload, c);         // component_index
    payload.push_back(7);           // component_bit_depth_minus_one -> 8 bit
    payload.push_back(0);           // component_format (unsigned)
    payload.push_back(0);           // component_align_size
  }
  payload.push_back(0);             // sampling_type (no subsampling)
  payload.push_back(1);             // interleave_type (pixel)
  payload.push_back(0);             // block_size
  payload.push_back(0);             // flags
  put_u32_be(payload, 0);           // pixel_size
  put_u32_be(payload, 0);           // row_align_size
  put_u32_be(payload, 0);           // tile_align_size
  put_u32_be(payload, item.tile_columns - 1);
  put_u32_be(payload, item.tile_rows - 1);
  return make_box("uncC", payload, /*full=*/true);
}

std::vector<uint8_t> make_infe(uint16_t item_id, const char* type, bool hidden) {
  std::vector<uint8_t> payload;
  put_u16_be(payload, item_id);
  put_u16_be(payload, 0);           // item_protection_index
  append_fourcc(payload, type);
  append_cstr(payload, "");
  return make_box("infe", payload, /*full=*/true, /*version=*/2, /*flags=*/hidden ? 1 : 0);
}

std::vector<uint8_t> make_reference(const char* type, uint16_t from_item, uint16_t to_item) {
  std::vector<uint8_t> payload;
  put_u16_be(payload, from_item);
  put_u16_be(payload, 1);           // reference_count
  put_u16_be(payload, to_item);
  return make_box(type, payload);
}

constexpr uint16_t kColorItem = 1;
constexpr uint16_t kAlphaItem = 2;
constexpr uint16_t kIdenItem = 3;

// Build a file with an 'unci' color image and an 'unci' alpha image. The item data
// is stored in 'idat'.
std::vector<uint8_t> build_heif(const FileSpec& spec) {
  std::vector<uint8_t> color_data = make_item_data(spec.color, kNumColorComponents,
                                                   [&](uint32_t c, uint32_t x, uint32_t y) {
                                                     return color_value(spec, c, x, y);
                                                   });
  std::vector<uint8_t> alpha_data = make_item_data(spec.alpha, 1,
                                                   [&](uint32_t, uint32_t x, uint32_t y) {
                                                     return alpha_value(spec, x, y);
                                                   });

  const bool with_alpha = !spec.primary_is_iden;

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
  put_u16_be(pitm_payload, spec.primary_is_iden ? kIdenItem : kColorItem);
  auto pitm = make_box("pitm", pitm_payload, /*full=*/true);

  // --- iinf

  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, 2);      // entry_count
  append(iinf_payload, make_infe(kColorItem, "unci", /*hidden=*/false));
  if (with_alpha) {
    append(iinf_payload, make_infe(kAlphaItem, "unci", /*hidden=*/true));
  }
  else {
    append(iinf_payload, make_infe(kIdenItem, "iden", /*hidden=*/false));
  }
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // --- iref

  std::vector<uint8_t> iref_payload;
  if (with_alpha) {
    append(iref_payload, make_reference("auxl", kAlphaItem, kColorItem));
  }
  else {
    append(iref_payload, make_reference("dimg", kIdenItem, kColorItem));
  }
  auto iref = make_box("iref", iref_payload, /*full=*/true);

  // --- ipco. The comments give the (1-based) property indices.

  std::vector<uint8_t> auxC_payload;
  append_cstr(auxC_payload, "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha");

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, make_ispe(spec.color));                         // 1
  append(ipco_payload, make_cmpd({heif_cmpd_component_type_red,
                                  heif_cmpd_component_type_green,
                                  heif_cmpd_component_type_blue}));    // 2
  append(ipco_payload, make_uncC(spec.color, kNumColorComponents));    // 3
  append(ipco_payload, make_ispe(spec.alpha));                         // 4
  append(ipco_payload, make_cmpd({heif_cmpd_component_type_monochrome})); // 5
  append(ipco_payload, make_uncC(spec.alpha, 1));                      // 6
  append(ipco_payload, make_box("auxC", auxC_payload, /*full=*/true)); // 7

  // The transformations are the same for the color image and the alpha image.
  std::vector<uint8_t> transformation_properties;

  uint8_t num_properties = 7;

  if (spec.rotate_180) {
    append(ipco_payload, make_box("irot", {2}));
    transformation_properties.push_back(++num_properties);
  }

  if (spec.clap) {
    std::vector<uint8_t> clap_payload;
    put_u32_be(clap_payload, 2);    // cleanApertureWidthN
    put_u32_be(clap_payload, 1);    // cleanApertureWidthD
    put_u32_be(clap_payload, 2);    // cleanApertureHeightN
    put_u32_be(clap_payload, 1);    // cleanApertureHeightD
    put_u32_be(clap_payload, 0);    // horizOffN
    put_u32_be(clap_payload, 1);    // horizOffD
    put_u32_be(clap_payload, 0);    // vertOffN
    put_u32_be(clap_payload, 1);    // vertOffD
    append(ipco_payload, make_box("clap", clap_payload));
    transformation_properties.push_back(++num_properties);
  }

  auto ipco = make_box("ipco", ipco_payload);

  // --- ipma

  auto append_associations = [&](std::vector<uint8_t>& out, uint16_t item_id,
                                 std::vector<uint8_t> properties, bool with_transformations) {
    if (with_transformations) {
      append(properties, transformation_properties);
    }

    put_u16_be(out, item_id);
    out.push_back(static_cast<uint8_t>(properties.size()));
    for (uint8_t property : properties) {
      bool essential = (property != 7); // all but auxC
      out.push_back(static_cast<uint8_t>((essential ? 0x80 : 0) | property));
    }
  };

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, 2);      // entry_count
  append_associations(ipma_payload, kColorItem, {1, 2, 3}, true);
  if (with_alpha) {
    append_associations(ipma_payload, kAlphaItem, {4, 5, 6, 7}, true);
  }
  else {
    append_associations(ipma_payload, kIdenItem, {1}, false);
  }
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // --- idat and iloc (version 1): the items are stored in idat (construction_method=1).

  std::vector<uint8_t> idat_payload = color_data;
  if (with_alpha) {
    append(idat_payload, alpha_data);
  }
  auto idat = make_box("idat", idat_payload);

  auto append_item_location = [](std::vector<uint8_t>& out, uint16_t item_id, size_t offset, size_t length) {
    put_u16_be(out, item_id);
    put_u16_be(out, 0x0001);        // construction_method=1 (idat)
    put_u16_be(out, 0);             // data_reference_index
    put_u16_be(out, 1);             // extent_count
    put_u32_be(out, static_cast<uint32_t>(offset));
    put_u32_be(out, static_cast<uint32_t>(length));
  };

  std::vector<uint8_t> iloc_payload;
  put_u16_be(iloc_payload, (4 << 12) | (4 << 8) | (0 << 4) | 0); // offset_size=4, length_size=4
  put_u16_be(iloc_payload, with_alpha ? 2 : 1); // item_count
  append_item_location(iloc_payload, kColorItem, 0, color_data.size());
  if (with_alpha) {
    append_item_location(iloc_payload, kAlphaItem, color_data.size(), alpha_data.size());
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


// Build a file with a 2x2 'grid' image. Each tile is an 'unci' color image of 2x2 pixels
// with an 'unci' alpha image of its own. The image content is the same as for the
// default FileSpec.
std::vector<uint8_t> build_heif_grid_with_tile_alpha() {
  const FileSpec spec;

  ItemSpec tile;
  tile.width = 2;
  tile.height = 2;
  tile.tile_columns = 1;
  tile.tile_rows = 1;

  constexpr uint16_t kGridItem = 1;
  constexpr uint16_t kFirstColorTile = 2;
  constexpr uint16_t kFirstAlphaTile = 6;
  constexpr uint16_t kNumTiles = 4;

  // --- item data: the grid, the color tiles, the alpha tiles

  std::vector<std::vector<uint8_t>> item_data;

  std::vector<uint8_t> grid_data;
  grid_data.push_back(0);           // version
  grid_data.push_back(0);           // flags
  grid_data.push_back(1);           // rows_minus_one
  grid_data.push_back(1);           // columns_minus_one
  put_u16_be(grid_data, static_cast<uint16_t>(spec.color.width));  // output_width
  put_u16_be(grid_data, static_cast<uint16_t>(spec.color.height)); // output_height
  item_data.push_back(grid_data);

  for (uint32_t t = 0; t < kNumTiles; t++) {
    item_data.push_back(make_item_data(tile, kNumColorComponents,
                                       [&](uint32_t c, uint32_t x, uint32_t y) {
                                         return color_value(spec, c, (t % 2) * 2 + x, (t / 2) * 2 + y);
                                       }));
  }

  for (uint32_t t = 0; t < kNumTiles; t++) {
    item_data.push_back(make_item_data(tile, 1,
                                       [&](uint32_t, uint32_t x, uint32_t y) {
                                         return alpha_value(spec, (t % 2) * 2 + x, (t / 2) * 2 + y);
                                       }));
  }

  const auto num_items = static_cast<uint16_t>(item_data.size());

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
  put_u16_be(pitm_payload, kGridItem);
  auto pitm = make_box("pitm", pitm_payload, /*full=*/true);

  // --- iinf

  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, num_items);
  append(iinf_payload, make_infe(kGridItem, "grid", /*hidden=*/false));
  for (uint16_t t = 0; t < 2 * kNumTiles; t++) {
    append(iinf_payload, make_infe(static_cast<uint16_t>(kFirstColorTile + t), "unci", /*hidden=*/true));
  }
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // --- iref

  std::vector<uint8_t> dimg_payload;
  put_u16_be(dimg_payload, kGridItem);
  put_u16_be(dimg_payload, kNumTiles);
  for (uint16_t t = 0; t < kNumTiles; t++) {
    put_u16_be(dimg_payload, static_cast<uint16_t>(kFirstColorTile + t));
  }

  std::vector<uint8_t> iref_payload;
  append(iref_payload, make_box("dimg", dimg_payload));
  for (uint16_t t = 0; t < kNumTiles; t++) {
    append(iref_payload, make_reference("auxl",
                                        static_cast<uint16_t>(kFirstAlphaTile + t),
                                        static_cast<uint16_t>(kFirstColorTile + t)));
  }
  auto iref = make_box("iref", iref_payload, /*full=*/true);

  // --- ipco. The comments give the (1-based) property indices.

  std::vector<uint8_t> auxC_payload;
  append_cstr(auxC_payload, "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha");

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, make_ispe(spec.color));                         // 1 (grid)
  append(ipco_payload, make_ispe(tile));                               // 2 (tiles)
  append(ipco_payload, make_cmpd({heif_cmpd_component_type_red,
                                  heif_cmpd_component_type_green,
                                  heif_cmpd_component_type_blue}));    // 3
  append(ipco_payload, make_uncC(tile, kNumColorComponents));          // 4
  append(ipco_payload, make_cmpd({heif_cmpd_component_type_monochrome})); // 5
  append(ipco_payload, make_uncC(tile, 1));                            // 6
  append(ipco_payload, make_box("auxC", auxC_payload, /*full=*/true)); // 7
  auto ipco = make_box("ipco", ipco_payload);

  // --- ipma

  auto append_associations = [](std::vector<uint8_t>& out, uint16_t item_id, const std::vector<uint8_t>& properties) {
    put_u16_be(out, item_id);
    out.push_back(static_cast<uint8_t>(properties.size()));
    for (uint8_t property : properties) {
      bool essential = (property != 7); // all but auxC
      out.push_back(static_cast<uint8_t>((essential ? 0x80 : 0) | property));
    }
  };

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, num_items);
  append_associations(ipma_payload, kGridItem, {1});
  for (uint16_t t = 0; t < kNumTiles; t++) {
    append_associations(ipma_payload, static_cast<uint16_t>(kFirstColorTile + t), {2, 3, 4});
  }
  for (uint16_t t = 0; t < kNumTiles; t++) {
    append_associations(ipma_payload, static_cast<uint16_t>(kFirstAlphaTile + t), {2, 5, 6, 7});
  }
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // --- idat and iloc (version 1): the items are stored in idat (construction_method=1).

  std::vector<uint8_t> idat_payload;

  std::vector<uint8_t> iloc_payload;
  put_u16_be(iloc_payload, (4 << 12) | (4 << 8) | (0 << 4) | 0); // offset_size=4, length_size=4
  put_u16_be(iloc_payload, num_items);
  for (uint16_t i = 0; i < num_items; i++) {
    put_u16_be(iloc_payload, static_cast<uint16_t>(kGridItem + i)); // item_ID
    put_u16_be(iloc_payload, 0x0001); // construction_method=1 (idat)
    put_u16_be(iloc_payload, 0);      // data_reference_index
    put_u16_be(iloc_payload, 1);      // extent_count
    put_u32_be(iloc_payload, static_cast<uint32_t>(idat_payload.size()));
    put_u32_be(iloc_payload, static_cast<uint32_t>(item_data[i].size()));

    append(idat_payload, item_data[i]);
  }
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);
  auto idat = make_box("idat", idat_payload);

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


// Build a file with an 'iovl' image of 6x4 pixels that overlays two 'unci' color images
// of 4x4 pixels with 2x2 tiles each. Both have the content of the default FileSpec.
// The second image is placed two pixels to the right of the first one.
std::vector<uint8_t> build_heif_overlay(bool rotate_90) {
  const FileSpec spec;

  ItemSpec canvas;
  canvas.width = 6;
  canvas.height = 4;

  constexpr uint16_t kOverlayItem = 1;
  constexpr uint16_t kFirstLayer = 2;
  constexpr uint16_t kNumLayers = 2;

  // --- item data: the overlay, the two layers

  std::vector<std::vector<uint8_t>> item_data;

  std::vector<uint8_t> overlay_data;
  overlay_data.push_back(0);        // version
  overlay_data.push_back(0);        // flags: 16 bit fields
  for (int i = 0; i < 4; i++) {
    put_u16_be(overlay_data, 0);    // canvas_fill_value
  }
  put_u16_be(overlay_data, static_cast<uint16_t>(canvas.width));  // output_width
  put_u16_be(overlay_data, static_cast<uint16_t>(canvas.height)); // output_height
  put_u16_be(overlay_data, 0);      // first layer: horizontal_offset
  put_u16_be(overlay_data, 0);      // vertical_offset
  put_u16_be(overlay_data, 2);      // second layer: horizontal_offset
  put_u16_be(overlay_data, 0);      // vertical_offset
  item_data.push_back(overlay_data);

  for (uint32_t i = 0; i < kNumLayers; i++) {
    item_data.push_back(make_item_data(spec.color, kNumColorComponents,
                                       [&](uint32_t c, uint32_t x, uint32_t y) {
                                         return color_value(spec, c, x, y);
                                       }));
  }

  const auto num_items = static_cast<uint16_t>(item_data.size());

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
  put_u16_be(pitm_payload, kOverlayItem);
  auto pitm = make_box("pitm", pitm_payload, /*full=*/true);

  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, num_items);
  append(iinf_payload, make_infe(kOverlayItem, "iovl", /*hidden=*/false));
  for (uint16_t i = 0; i < kNumLayers; i++) {
    append(iinf_payload, make_infe(static_cast<uint16_t>(kFirstLayer + i), "unci", /*hidden=*/true));
  }
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  std::vector<uint8_t> dimg_payload;
  put_u16_be(dimg_payload, kOverlayItem);
  put_u16_be(dimg_payload, kNumLayers);
  for (uint16_t i = 0; i < kNumLayers; i++) {
    put_u16_be(dimg_payload, static_cast<uint16_t>(kFirstLayer + i));
  }

  std::vector<uint8_t> iref_payload;
  append(iref_payload, make_box("dimg", dimg_payload));
  auto iref = make_box("iref", iref_payload, /*full=*/true);

  // --- ipco. The comments give the (1-based) property indices.

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, make_ispe(canvas));                             // 1 (overlay)
  append(ipco_payload, make_ispe(spec.color));                         // 2 (layers)
  append(ipco_payload, make_cmpd({heif_cmpd_component_type_red,
                                  heif_cmpd_component_type_green,
                                  heif_cmpd_component_type_blue}));    // 3
  append(ipco_payload, make_uncC(spec.color, kNumColorComponents));    // 4
  append(ipco_payload, make_box("irot", {1}));                         // 5
  auto ipco = make_box("ipco", ipco_payload);

  // --- ipma

  auto append_associations = [](std::vector<uint8_t>& out, uint16_t item_id, const std::vector<uint8_t>& properties) {
    put_u16_be(out, item_id);
    out.push_back(static_cast<uint8_t>(properties.size()));
    for (uint8_t property : properties) {
      out.push_back(static_cast<uint8_t>(0x80 | property)); // essential
    }
  };

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, num_items);
  if (rotate_90) {
    append_associations(ipma_payload, kOverlayItem, {1, 5});
  }
  else {
    append_associations(ipma_payload, kOverlayItem, {1});
  }
  for (uint16_t i = 0; i < kNumLayers; i++) {
    append_associations(ipma_payload, static_cast<uint16_t>(kFirstLayer + i), {2, 3, 4});
  }
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // --- idat and iloc (version 1): the items are stored in idat (construction_method=1).

  std::vector<uint8_t> idat_payload;

  std::vector<uint8_t> iloc_payload;
  put_u16_be(iloc_payload, (4 << 12) | (4 << 8) | (0 << 4) | 0); // offset_size=4, length_size=4
  put_u16_be(iloc_payload, num_items);
  for (uint16_t i = 0; i < num_items; i++) {
    put_u16_be(iloc_payload, static_cast<uint16_t>(kOverlayItem + i)); // item_ID
    put_u16_be(iloc_payload, 0x0001); // construction_method=1 (idat)
    put_u16_be(iloc_payload, 0);      // data_reference_index
    put_u16_be(iloc_payload, 1);      // extent_count
    put_u32_be(iloc_payload, static_cast<uint32_t>(idat_payload.size()));
    put_u32_be(iloc_payload, static_cast<uint32_t>(item_data[i].size()));

    append(idat_payload, item_data[i]);
  }
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);
  auto idat = make_box("idat", idat_payload);

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


// Opens the primary image of a file.
class OpenedImage {
public:
  explicit OpenedImage(const FileSpec& spec) : OpenedImage(build_heif(spec)) {}

  explicit OpenedImage(std::vector<uint8_t> file) : m_file(std::move(file)) {
    m_ctx = heif_context_alloc();
    REQUIRE(m_ctx != nullptr);

    heif_error err = heif_context_read_from_memory_without_copy(m_ctx, m_file.data(), m_file.size(), nullptr);
    INFO("read error: " << err.message);
    REQUIRE(err.code == heif_error_Ok);

    err = heif_context_get_primary_image_handle(m_ctx, &m_handle);
    INFO("primary image error: " << err.message);
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(m_handle != nullptr);
  }

  ~OpenedImage() {
    heif_image_handle_release(m_handle);
    heif_context_free(m_ctx);
  }

  OpenedImage(const OpenedImage&) = delete;
  OpenedImage& operator=(const OpenedImage&) = delete;

  // The tiling of the coded image, without the image transformations.
  heif_image_tiling tiling() const {
    heif_image_tiling tiling{};
    heif_error err = heif_image_handle_get_image_tiling(m_handle, 0, &tiling);
    REQUIRE(err.code == heif_error_Ok);
    return tiling;
  }

  // The error message is owned by the context, so it is only valid as long as this object lives.
  heif_error decode_image(heif_image** out_img) {
    return heif_decode_image(m_handle, out_img, heif_colorspace_RGB, heif_chroma_444, nullptr);
  }

  heif_error decode_tile(heif_image** out_img, uint32_t tile_x, uint32_t tile_y, bool ignore_transformations = false) {
    heif_decoding_options* options = heif_decoding_options_alloc();
    options->ignore_transformations = ignore_transformations;

    heif_error err = heif_image_handle_decode_image_tile(m_handle, out_img, heif_colorspace_RGB, heif_chroma_444,
                                                         options, tile_x, tile_y);
    heif_decoding_options_free(options);
    return err;
  }

private:
  std::vector<uint8_t> m_file;
  heif_context* m_ctx = nullptr;
  heif_image_handle* m_handle = nullptr;
};


// Check that the image shows the area of the test image that starts at (x0;y0).
// The position refers to the image after the rotation, but before the cropping.
void check_pixels(const heif_image* img, const FileSpec& spec, bool rotated, bool with_alpha,
                  uint32_t x0, uint32_t y0, uint32_t width, uint32_t height) {
  const heif_channel channels[kNumColorComponents + 1] = {heif_channel_R, heif_channel_G, heif_channel_B,
                                                          heif_channel_Alpha};

  REQUIRE((heif_image_has_channel(img, heif_channel_Alpha) != 0) == with_alpha);

  for (uint32_t c = 0; c < kNumColorComponents + (with_alpha ? 1 : 0); c++) {
    REQUIRE(heif_image_get_width(img, channels[c]) == static_cast<int>(width));
    REQUIRE(heif_image_get_height(img, channels[c]) == static_cast<int>(height));

    int stride = 0;
    const uint8_t* plane = heif_image_get_plane_readonly(img, channels[c], &stride);
    REQUIRE(plane != nullptr);

    for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
        // the position in the coded color image
        uint32_t coded_x = x0 + x;
        uint32_t coded_y = y0 + y;
        if (rotated) {
          coded_x = spec.color.width - 1 - coded_x;
          coded_y = spec.color.height - 1 - coded_y;
        }

        int expected;
        if (c < kNumColorComponents) {
          expected = color_value(spec, c, coded_x, coded_y);
        }
        else {
          // The alpha image may have a lower resolution.
          expected = alpha_value(spec,
                                 coded_x * spec.alpha.width / spec.color.width,
                                 coded_y * spec.alpha.height / spec.color.height);
        }

        INFO("channel " << c << ", pixel (" << x << "," << y << ")");
        REQUIRE(static_cast<int>(plane[y * stride + x]) == expected);
      }
    }
  }
}

} // namespace


TEST_CASE("tiles of an image with an alpha image in the same tiling") {
  for (bool rotate : {false, true}) {
    for (bool half_resolution_alpha : {false, true}) {
      INFO("rotate by 180 degrees: " << rotate);
      INFO("alpha with half resolution: " << half_resolution_alpha);

      FileSpec spec;
      spec.rotate_180 = rotate;

      if (half_resolution_alpha) {
        // The same number of tiles, which cover the same area as the tiles of the color image.
        spec.alpha.width = 2;
        spec.alpha.height = 2;
      }

      OpenedImage image(spec);

      heif_image_tiling tiling = image.tiling();
      REQUIRE(tiling.num_columns == 2);
      REQUIRE(tiling.num_rows == 2);
      REQUIRE(tiling.tile_width == 2);
      REQUIRE(tiling.tile_height == 2);

      for (uint32_t ty = 0; ty < 2; ty++) {
        for (uint32_t tx = 0; tx < 2; tx++) {
          INFO("tile (" << tx << "," << ty << ")");

          heif_image* img = nullptr;
          heif_error err = image.decode_tile(&img, tx, ty);
          INFO("tile decode error: " << err.message);
          REQUIRE(err.code == heif_error_Ok);
          REQUIRE(img != nullptr);
          check_pixels(img, spec, rotate, true, tx * 2, ty * 2, 2, 2);
          heif_image_release(img);
        }
      }

      heif_image* img = nullptr;
      heif_error err = image.decode_image(&img);
      INFO("decode error: " << err.message);
      REQUIRE(err.code == heif_error_Ok);
      REQUIRE(img != nullptr);
      check_pixels(img, spec, rotate, true, 0, 0, 4, 4);
      heif_image_release(img);
    }
  }
}


TEST_CASE("image with an alpha image in a different tiling is a single tile") {
  for (bool tiled_color : {false, true}) {
    for (bool rotate : {false, true}) {
      INFO((tiled_color ? "color image with tiles, alpha image without" : "alpha image with tiles, color image without"));
      INFO("rotate by 180 degrees: " << rotate);

      FileSpec spec;
      spec.rotate_180 = rotate;

      ItemSpec& untiled = (tiled_color ? spec.alpha : spec.color);
      untiled.tile_columns = 1;
      untiled.tile_rows = 1;

      OpenedImage image(spec);

      heif_image_tiling tiling = image.tiling();
      REQUIRE(tiling.num_columns == 1);
      REQUIRE(tiling.num_rows == 1);
      REQUIRE(tiling.tile_width == 4);
      REQUIRE(tiling.tile_height == 4);
      REQUIRE(tiling.image_width == 4);
      REQUIRE(tiling.image_height == 4);

      // --- The single tile is the whole image.

      for (bool ignore_transformations : {false, true}) {
        INFO("ignore_transformations: " << ignore_transformations);

        heif_image* img = nullptr;
        heif_error err = image.decode_tile(&img, 0, 0, ignore_transformations);
        INFO("tile decode error: " << err.message);
        REQUIRE(err.code == heif_error_Ok);
        REQUIRE(img != nullptr);
        check_pixels(img, spec, rotate && !ignore_transformations, true, 0, 0, 4, 4);
        heif_image_release(img);

        // --- The tiles of the color image are not accessible.

        img = nullptr;
        err = image.decode_tile(&img, 1, 0, ignore_transformations);
        REQUIRE(err.code == heif_error_Usage_error);
        REQUIRE(img == nullptr);

        err = image.decode_tile(&img, 0, 1, ignore_transformations);
        REQUIRE(err.code == heif_error_Usage_error);
        REQUIRE(img == nullptr);
      }

      heif_image* img = nullptr;
      heif_error err = image.decode_image(&img);
      INFO("decode error: " << err.message);
      REQUIRE(err.code == heif_error_Ok);
      REQUIRE(img != nullptr);
      check_pixels(img, spec, rotate, true, 0, 0, 4, 4);
      heif_image_release(img);
    }
  }
}


TEST_CASE("single tile of an image with an alpha image in a different tiling is not cropped") {
  // A tile is not cropped by 'clap'. The crop is described by the offsets in heif_image_tiling.
  // This also has to hold for the alpha image, which is decoded as a whole.
  FileSpec spec;
  spec.clap = true;
  spec.alpha.tile_columns = 1;
  spec.alpha.tile_rows = 1;

  OpenedImage image(spec);

  heif_image_tiling tiling = image.tiling();
  REQUIRE(tiling.num_columns == 1);
  REQUIRE(tiling.num_rows == 1);
  REQUIRE(tiling.tile_width == 4);
  REQUIRE(tiling.tile_height == 4);

  heif_image* img = nullptr;
  heif_error err = image.decode_tile(&img, 0, 0);
  INFO("tile decode error: " << err.message);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(img != nullptr);
  check_pixels(img, spec, false, true, 0, 0, 4, 4);
  heif_image_release(img);

  // The whole image is cropped to the 2x2 pixels in the center.
  img = nullptr;
  err = image.decode_image(&img);
  INFO("decode error: " << err.message);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(img != nullptr);
  check_pixels(img, spec, false, true, 1, 1, 2, 2);
  heif_image_release(img);
}


TEST_CASE("single tile of an 'iden' image derived from an image with tiles") {
  FileSpec spec;
  spec.primary_is_iden = true;

  OpenedImage image(spec);

  heif_image_tiling tiling = image.tiling();
  REQUIRE(tiling.num_columns == 1);
  REQUIRE(tiling.num_rows == 1);
  REQUIRE(tiling.tile_width == 4);
  REQUIRE(tiling.tile_height == 4);

  heif_image* img = nullptr;
  heif_error err = image.decode_tile(&img, 0, 0);
  INFO("tile decode error: " << err.message);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(img != nullptr);
  check_pixels(img, spec, false, false, 0, 0, 4, 4);
  heif_image_release(img);

  img = nullptr;
  err = image.decode_image(&img);
  INFO("decode error: " << err.message);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(img != nullptr);
  check_pixels(img, spec, false, false, 0, 0, 4, 4);
  heif_image_release(img);
}


TEST_CASE("tiles of a grid image with an alpha image for each tile") {
  const FileSpec spec; // only describes the image content

  OpenedImage image(build_heif_grid_with_tile_alpha());

  heif_image_tiling tiling = image.tiling();
  REQUIRE(tiling.num_columns == 2);
  REQUIRE(tiling.num_rows == 2);
  REQUIRE(tiling.tile_width == 2);
  REQUIRE(tiling.tile_height == 2);

  for (uint32_t ty = 0; ty < 2; ty++) {
    for (uint32_t tx = 0; tx < 2; tx++) {
      INFO("tile (" << tx << "," << ty << ")");

      heif_image* img = nullptr;
      heif_error err = image.decode_tile(&img, tx, ty);
      INFO("tile decode error: " << err.message);
      REQUIRE(err.code == heif_error_Ok);
      REQUIRE(img != nullptr);
      check_pixels(img, spec, false, true, tx * 2, ty * 2, 2, 2);
      heif_image_release(img);
    }
  }

  heif_image* img = nullptr;
  heif_error err = image.decode_image(&img);
  INFO("decode error: " << err.message);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(img != nullptr);
  check_pixels(img, spec, false, true, 0, 0, 4, 4);
  heif_image_release(img);
}


TEST_CASE("overlay of images with tiles is a single tile") {
  const FileSpec spec; // only describes the content of the overlaid images

  for (bool rotate : {false, true}) {
    INFO("rotate by 90 degrees: " << rotate);

    OpenedImage image(build_heif_overlay(rotate));

    // The overlaid images have 2x2 tiles each. They are placed at different positions,
    // so their tiles do not form a tiling of the overlay image.
    heif_image_tiling tiling = image.tiling();
    REQUIRE(tiling.num_columns == 1);
    REQUIRE(tiling.num_rows == 1);
    REQUIRE(tiling.tile_width == 6);
    REQUIRE(tiling.tile_height == 4);

    heif_image* full = nullptr;
    heif_error err = image.decode_image(&full);
    INFO("decode error: " << err.message);
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(full != nullptr);

    const int width = heif_image_get_width(full, heif_channel_R);
    const int height = heif_image_get_height(full, heif_channel_R);
    REQUIRE(width == (rotate ? 4 : 6));
    REQUIRE(height == (rotate ? 6 : 4));

    heif_image* tile = nullptr;
    err = image.decode_tile(&tile, 0, 0);
    INFO("tile decode error: " << err.message);
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(tile != nullptr);

    // --- The single tile is the whole image.

    const heif_channel channels[kNumColorComponents] = {heif_channel_R, heif_channel_G, heif_channel_B};

    for (uint32_t c = 0; c < kNumColorComponents; c++) {
      REQUIRE(heif_image_get_width(tile, channels[c]) == width);
      REQUIRE(heif_image_get_height(tile, channels[c]) == height);

      int full_stride = 0;
      int tile_stride = 0;
      const uint8_t* full_plane = heif_image_get_plane_readonly(full, channels[c], &full_stride);
      const uint8_t* tile_plane = heif_image_get_plane_readonly(tile, channels[c], &tile_stride);
      REQUIRE(full_plane != nullptr);
      REQUIRE(tile_plane != nullptr);

      for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
          INFO("channel " << c << ", pixel (" << x << "," << y << ")");
          REQUIRE(static_cast<int>(tile_plane[y * tile_stride + x]) == static_cast<int>(full_plane[y * full_stride + x]));

          if (!rotate) {
            // The second image is placed two pixels to the right and covers the first one.
            int expected = (x >= 2) ? color_value(spec, c, x - 2, y) : color_value(spec, c, x, y);
            REQUIRE(static_cast<int>(full_plane[y * full_stride + x]) == expected);
          }
        }
      }
    }

    heif_image_release(tile);
    heif_image_release(full);

    for (bool ignore_transformations : {false, true}) {
      heif_image* img = nullptr;
      err = image.decode_tile(&img, 1, 0, ignore_transformations);
      REQUIRE(err.code == heif_error_Usage_error);
      REQUIRE(img == nullptr);
    }
  }
}
