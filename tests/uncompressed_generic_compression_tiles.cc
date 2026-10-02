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

// Regression tests for GHSA-6fqc-p7r8-2g36.
//
// The uncC tiling of an 'unci' image describes how the uncompressed data is
// stored. Whether a tile can be decoded on its own depends on the compressed units
// of the generic compression (cmpC, icef):
//
// - Units that span several tiles (the full item, the full image of a component)
//   force the decoder to decompress the item completely to get the data of any
//   tile. The decoder did this once for each tile, which needs time proportional
//   to num_tiles * item_size. A 4 kB file with 256x256 tiles kept a CPU busy for
//   minutes.
//
//   Such an item is now decompressed once when the image is decoded, and the image
//   is exposed as a single tile in heif_image_tiling. Because the decompression
//   cannot be observed through the security limits, the tests read the file
//   through a heif_reader that counts the bytes read from the item data: they have
//   to be read exactly once.
//
// - Units that are parts of a tile (rows, pixels) are listed in the order of the
//   uncompressed data, so each tile is a run of consecutive units. These images
//   keep their tiling, and decoding a tile only reads the units of this tile.
//
// The decompressed size is known in both cases. More or less data is rejected.
//
// The files use deflate generic compression, so the tests only run when the
// library was built with zlib (guarded in tests/CMakeLists.txt).

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

// A 4x4 RGB image with 2x2 tiles of 2x2 pixels.
constexpr uint32_t kWidth = 4;
constexpr uint32_t kHeight = 4;
constexpr uint32_t kTileColumns = 2;
constexpr uint32_t kTileRows = 2;
constexpr uint32_t kTileWidth = kWidth / kTileColumns;
constexpr uint32_t kTileHeight = kHeight / kTileRows;
constexpr uint32_t kNumComponents = 3;
constexpr uint32_t kNumTiles = kTileColumns * kTileRows;
constexpr uint32_t kTileDataSize = kTileWidth * kTileHeight * kNumComponents;

constexpr uint8_t kInterleaveComponent = 0;
constexpr uint8_t kInterleavePixel = 1;
constexpr uint8_t kInterleaveTileComponent = 4;

constexpr uint8_t kUnitFullItem = 0;
constexpr uint8_t kUnitImage = 1;
constexpr uint8_t kUnitRow = 3;
constexpr uint8_t kUnitPixel = 4;

// Each sample of the image has a different value.
uint8_t pixel_value(uint32_t component, uint32_t x, uint32_t y) {
  return static_cast<uint8_t>((component + 1) * 50 + y * kWidth + x);
}

void append_tile_component(std::vector<uint8_t>& out, uint32_t tile, uint32_t component) {
  uint32_t x0 = (tile % kTileColumns) * kTileWidth;
  uint32_t y0 = (tile / kTileColumns) * kTileHeight;

  for (uint32_t y = 0; y < kTileHeight; y++) {
    for (uint32_t x = 0; x < kTileWidth; x++) {
      out.push_back(pixel_value(component, x0 + x, y0 + y));
    }
  }
}

// The uncompressed data of the image in the order in which the file stores it.
std::vector<uint8_t> make_image_data(uint8_t interleave_type) {
  std::vector<uint8_t> data;

  switch (interleave_type) {
    case kInterleaveComponent:
      for (uint32_t tile = 0; tile < kNumTiles; tile++) {
        for (uint32_t c = 0; c < kNumComponents; c++) {
          append_tile_component(data, tile, c);
        }
      }
      break;

    case kInterleavePixel:
      for (uint32_t tile = 0; tile < kNumTiles; tile++) {
        uint32_t x0 = (tile % kTileColumns) * kTileWidth;
        uint32_t y0 = (tile / kTileColumns) * kTileHeight;

        for (uint32_t y = 0; y < kTileHeight; y++) {
          for (uint32_t x = 0; x < kTileWidth; x++) {
            for (uint32_t c = 0; c < kNumComponents; c++) {
              data.push_back(pixel_value(c, x0 + x, y0 + y));
            }
          }
        }
      }
      break;

    case kInterleaveTileComponent:
      for (uint32_t c = 0; c < kNumComponents; c++) {
        for (uint32_t tile = 0; tile < kNumTiles; tile++) {
          append_tile_component(data, tile, c);
        }
      }
      break;

    default:
      REQUIRE(false);
  }

  return data;
}

// Split the data into pieces of 'unit_size' bytes.
std::vector<std::vector<uint8_t>> split_into_units(const std::vector<uint8_t>& data, size_t unit_size) {
  std::vector<std::vector<uint8_t>> units;

  for (size_t pos = 0; pos < data.size(); pos += unit_size) {
    size_t end = std::min(pos + unit_size, data.size());
    units.emplace_back(data.begin() + pos, data.begin() + end);
  }

  return units;
}

// Wrap the data into a raw deflate stream consisting of a single stored block.
std::vector<uint8_t> deflate_stored(const std::vector<uint8_t>& data) {
  auto len = static_cast<uint16_t>(data.size());
  auto nlen = static_cast<uint16_t>(~len);

  std::vector<uint8_t> out;
  out.push_back(0x01); // BFINAL=1, BTYPE=00 (stored)
  out.push_back(static_cast<uint8_t>(len & 0xFF));
  out.push_back(static_cast<uint8_t>(len >> 8));
  out.push_back(static_cast<uint8_t>(nlen & 0xFF));
  out.push_back(static_cast<uint8_t>(nlen >> 8));
  append(out, data);
  return out;
}

struct UnciFileSpec {
  const char* description = "";

  uint8_t interleave_type = kInterleaveComponent;

  // cmpC compressed_unit_type
  uint8_t unit_type = kUnitFullItem;

  // Without an icef box, there has to be exactly one unit.
  bool with_icef = false;

  // The uncompressed content of the compressed units.
  std::vector<std::vector<uint8_t>> units;
};

struct UnciFile {
  std::vector<uint8_t> data;

  // The item data is at the end of the file.
  size_t item_size = 0;

  // The sizes of the compressed units in the item data.
  std::vector<size_t> compressed_unit_sizes;
};

// Build an 'unci' image with deflate generic compression. The item data is stored
// in 'idat', which is the last box of the file.
UnciFile build_heif_unci(const UnciFileSpec& spec) {
  UnciFile file;

  std::vector<uint8_t> item_data;
  for (const auto& unit : spec.units) {
    std::vector<uint8_t> compressed = deflate_stored(unit);
    file.compressed_unit_sizes.push_back(compressed.size());
    append(item_data, compressed);
  }

  file.item_size = item_data.size();

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

  std::vector<uint8_t> infe_payload;
  put_u16_be(infe_payload, 1);
  put_u16_be(infe_payload, 0);
  append_fourcc(infe_payload, "unci");
  append_cstr(infe_payload, "");
  auto infe = make_box("infe", infe_payload, /*full=*/true, /*version=*/2);

  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, 1);
  append(iinf_payload, infe);
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // ispe
  std::vector<uint8_t> ispe_payload;
  put_u32_be(ispe_payload, kWidth);
  put_u32_be(ispe_payload, kHeight);
  auto ispe = make_box("ispe", ispe_payload, /*full=*/true);

  // cmpd
  std::vector<uint8_t> cmpd_payload;
  put_u32_be(cmpd_payload, kNumComponents);
  put_u16_be(cmpd_payload, heif_cmpd_component_type_red);
  put_u16_be(cmpd_payload, heif_cmpd_component_type_green);
  put_u16_be(cmpd_payload, heif_cmpd_component_type_blue);
  auto cmpd = make_box("cmpd", cmpd_payload);

  // uncC (v0): 8-bit components
  std::vector<uint8_t> uncC_payload;
  put_u32_be(uncC_payload, 0);           // profile
  put_u32_be(uncC_payload, kNumComponents);
  for (uint16_t c = 0; c < kNumComponents; c++) {
    put_u16_be(uncC_payload, c);         // component_index
    uncC_payload.push_back(7);           // component_bit_depth_minus_one -> 8 bit
    uncC_payload.push_back(0);           // component_format (unsigned)
    uncC_payload.push_back(0);           // component_align_size
  }
  uncC_payload.push_back(0);             // sampling_type (no subsampling)
  uncC_payload.push_back(spec.interleave_type);
  uncC_payload.push_back(0);             // block_size
  uncC_payload.push_back(0);             // flags
  put_u32_be(uncC_payload, 0);           // pixel_size
  put_u32_be(uncC_payload, 0);           // row_align_size
  put_u32_be(uncC_payload, 0);           // tile_align_size
  put_u32_be(uncC_payload, kTileColumns - 1);
  put_u32_be(uncC_payload, kTileRows - 1);
  auto uncC = make_box("uncC", uncC_payload, /*full=*/true);

  // cmpC: deflate
  std::vector<uint8_t> cmpC_payload;
  append_fourcc(cmpC_payload, "defl");
  cmpC_payload.push_back(spec.unit_type);
  auto cmpC = make_box("cmpC", cmpC_payload, /*full=*/true);

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, ispe);
  append(ipco_payload, cmpd);
  append(ipco_payload, uncC);
  append(ipco_payload, cmpC);

  uint8_t num_properties = 4;

  if (spec.with_icef) {
    // icef: implied unit offsets, 32-bit unit sizes
    std::vector<uint8_t> icef_payload;
    icef_payload.push_back(3 << 2);      // unit_offset_code = 0, unit_size_code = 3
    put_u32_be(icef_payload, static_cast<uint32_t>(file.compressed_unit_sizes.size()));
    for (size_t size : file.compressed_unit_sizes) {
      put_u32_be(icef_payload, static_cast<uint32_t>(size));
    }
    append(ipco_payload, make_box("icef", icef_payload, /*full=*/true));

    num_properties = 5;
  }
  else {
    REQUIRE(spec.units.size() == 1);
  }

  auto ipco = make_box("ipco", ipco_payload);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, 1); // entry_count
  put_u16_be(ipma_payload, 1); // item_ID 1
  ipma_payload.push_back(num_properties); // association_count
  for (uint8_t i = 1; i <= num_properties; i++) {
    ipma_payload.push_back(0x80 | i);     // essential
  }
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  auto idat = make_box("idat", item_data);

  // iloc (version 1): item 1 stored in idat (construction_method=1).
  std::vector<uint8_t> iloc_payload;
  put_u16_be(iloc_payload, (4 << 12) | (4 << 8) | (0 << 4) | 0); // offset_size=4, length_size=4
  put_u16_be(iloc_payload, 1);          // item_count
  put_u16_be(iloc_payload, 1);          // item_ID
  put_u16_be(iloc_payload, 0x0001);     // construction_method=1 (idat)
  put_u16_be(iloc_payload, 0);          // data_reference_index
  put_u16_be(iloc_payload, 1);          // extent_count
  put_u32_be(iloc_payload, 0);          // extent_offset (within idat)
  put_u32_be(iloc_payload, static_cast<uint32_t>(item_data.size())); // extent_length
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);

  // idat has to be the last box: the tests locate the item data at the end of the file.
  std::vector<uint8_t> meta_payload;
  append(meta_payload, hdlr);
  append(meta_payload, pitm);
  append(meta_payload, iinf);
  append(meta_payload, iprp);
  append(meta_payload, iloc);
  append(meta_payload, idat);
  auto meta = make_box("meta", meta_payload, /*full=*/true);

  append(file.data, ftyp);
  append(file.data, meta);
  return file;
}


// A heif_reader on a memory buffer that counts the bytes read from the item data.
struct CountingReader {
  const std::vector<uint8_t>* data = nullptr;
  int64_t position = 0;

  int64_t watched_start = 0;
  int64_t watched_end = 0;
  uint64_t watched_bytes_read = 0;
};

int64_t reader_get_position(void* userdata) {
  return static_cast<CountingReader*>(userdata)->position;
}

int reader_read(void* dst, size_t size, void* userdata) {
  auto* reader = static_cast<CountingReader*>(userdata);
  auto file_size = static_cast<int64_t>(reader->data->size());
  auto read_end = reader->position + static_cast<int64_t>(size);

  if (read_end > file_size) {
    return 1;
  }

  memcpy(dst, reader->data->data() + reader->position, size);

  int64_t overlap_start = std::max(reader->position, reader->watched_start);
  int64_t overlap_end = std::min(read_end, reader->watched_end);
  if (overlap_end > overlap_start) {
    reader->watched_bytes_read += static_cast<uint64_t>(overlap_end - overlap_start);
  }

  reader->position = read_end;
  return 0;
}

int reader_seek(int64_t position, void* userdata) {
  auto* reader = static_cast<CountingReader*>(userdata);
  if (position < 0 || position > static_cast<int64_t>(reader->data->size())) {
    return 1;
  }

  reader->position = position;
  return 0;
}

heif_reader_grow_status reader_wait_for_file_size(int64_t target_size, void* userdata) {
  auto* reader = static_cast<CountingReader*>(userdata);
  return (target_size <= static_cast<int64_t>(reader->data->size()))
             ? heif_reader_grow_status_size_reached
             : heif_reader_grow_status_size_beyond_eof;
}


// Opens the primary image of a file through the counting reader.
class OpenedImage {
public:
  explicit OpenedImage(const UnciFile& file) {
    m_counting_reader.data = &file.data;
    m_counting_reader.watched_start = static_cast<int64_t>(file.data.size() - file.item_size);
    m_counting_reader.watched_end = static_cast<int64_t>(file.data.size());

    m_reader.reader_api_version = 1;
    m_reader.get_position = reader_get_position;
    m_reader.read = reader_read;
    m_reader.seek = reader_seek;
    m_reader.wait_for_file_size = reader_wait_for_file_size;

    m_ctx = heif_context_alloc();
    REQUIRE(m_ctx != nullptr);

    heif_error err = heif_context_read_from_reader(m_ctx, &m_reader, &m_counting_reader, nullptr);
    INFO("read error: " << err.message);
    REQUIRE(err.code == heif_error_Ok);

    err = heif_context_get_primary_image_handle(m_ctx, &m_handle);
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(m_handle != nullptr);
  }

  ~OpenedImage() {
    heif_image_handle_release(m_handle);
    heif_context_free(m_ctx);
  }

  OpenedImage(const OpenedImage&) = delete;
  OpenedImage& operator=(const OpenedImage&) = delete;

  heif_image_handle* handle() const { return m_handle; }

  heif_image_tiling tiling() const {
    heif_image_tiling tiling{};
    heif_error err = heif_image_handle_get_image_tiling(m_handle, 1, &tiling);
    REQUIRE(err.code == heif_error_Ok);
    return tiling;
  }

  // The error message is owned by the context, so it is only valid as long as this object lives.
  heif_error decode_image(heif_image** out_img) {
    m_counting_reader.watched_bytes_read = 0;
    return heif_decode_image(m_handle, out_img, heif_colorspace_RGB, heif_chroma_444, nullptr);
  }

  heif_error decode_tile(heif_image** out_img, uint32_t tile_x, uint32_t tile_y, bool ignore_transformations = false) {
    heif_decoding_options* options = heif_decoding_options_alloc();
    options->ignore_transformations = ignore_transformations;

    m_counting_reader.watched_bytes_read = 0;
    heif_error err = heif_image_handle_decode_image_tile(m_handle, out_img, heif_colorspace_RGB, heif_chroma_444,
                                                         options, tile_x, tile_y);
    heif_decoding_options_free(options);
    return err;
  }

  // The number of bytes of the item data that the last decoding call has read.
  uint64_t item_bytes_read() const { return m_counting_reader.watched_bytes_read; }

private:
  CountingReader m_counting_reader;
  heif_reader m_reader{};
  heif_context* m_ctx = nullptr;
  heif_image_handle* m_handle = nullptr;
};


// Check that the image shows the area of the test image that starts at (x0;y0).
void check_pixels(const heif_image* img, uint32_t x0, uint32_t y0, uint32_t width, uint32_t height) {
  const heif_channel channels[kNumComponents] = {heif_channel_R, heif_channel_G, heif_channel_B};

  for (uint32_t c = 0; c < kNumComponents; c++) {
    REQUIRE(heif_image_get_width(img, channels[c]) == static_cast<int>(width));
    REQUIRE(heif_image_get_height(img, channels[c]) == static_cast<int>(height));

    int stride = 0;
    const uint8_t* plane = heif_image_get_plane_readonly(img, channels[c], &stride);
    REQUIRE(plane != nullptr);

    for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
        INFO("component " << c << ", pixel (" << x << "," << y << ")");
        REQUIRE(static_cast<int>(plane[y * stride + x]) == static_cast<int>(pixel_value(c, x0 + x, y0 + y)));
      }
    }
  }
}

} // namespace


TEST_CASE("unci image with compressed units spanning several tiles is decompressed once") {
  std::vector<UnciFileSpec> specs;

  {
    UnciFileSpec spec;
    spec.description = "full item without icef";
    spec.units = {make_image_data(kInterleaveComponent)};
    specs.push_back(spec);
  }

  {
    UnciFileSpec spec;
    spec.description = "full item with icef";
    spec.with_icef = true;
    spec.units = {make_image_data(kInterleaveComponent)};
    specs.push_back(spec);
  }

  {
    UnciFileSpec spec;
    spec.description = "one unit for the full image of each component";
    spec.interleave_type = kInterleaveTileComponent;
    spec.unit_type = kUnitImage;
    spec.with_icef = true;
    spec.units = split_into_units(make_image_data(kInterleaveTileComponent), kWidth * kHeight);
    specs.push_back(spec);
  }

  {
    // The rows of a tile are not contiguous in the uncompressed data.
    UnciFileSpec spec;
    spec.description = "row units with tile-component interleave";
    spec.interleave_type = kInterleaveTileComponent;
    spec.unit_type = kUnitRow;
    spec.with_icef = true;
    spec.units = split_into_units(make_image_data(kInterleaveTileComponent), kTileWidth);
    specs.push_back(spec);
  }

  {
    // Three units for four tiles
    UnciFileSpec spec;
    spec.description = "row units that cannot be assigned to the tiles";
    spec.interleave_type = kInterleavePixel;
    spec.unit_type = kUnitRow;
    spec.with_icef = true;
    spec.units = split_into_units(make_image_data(kInterleavePixel), kWidth * kHeight);
    REQUIRE(spec.units.size() == 3);
    specs.push_back(spec);
  }

  for (const UnciFileSpec& spec : specs) {
    INFO("compressed units: " << spec.description);

    UnciFile file = build_heif_unci(spec);
    OpenedImage image(file);

    // --- The uncC tiles cannot be decoded independently, so the image is a single tile.

    heif_image_tiling tiling = image.tiling();
    REQUIRE(tiling.num_columns == 1);
    REQUIRE(tiling.num_rows == 1);
    REQUIRE(tiling.tile_width == kWidth);
    REQUIRE(tiling.tile_height == kHeight);
    REQUIRE(tiling.image_width == kWidth);
    REQUIRE(tiling.image_height == kHeight);

    // --- Decoding the image reads (and decompresses) the item once, not once per uncC tile.

    heif_image* img = nullptr;
    heif_error err = image.decode_image(&img);
    INFO("decode error: " << err.message);
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(img != nullptr);
    REQUIRE(image.item_bytes_read() == file.item_size);
    check_pixels(img, 0, 0, kWidth, kHeight);
    heif_image_release(img);

    // --- The single tile is the whole image.

    for (bool ignore_transformations : {false, true}) {
      INFO("ignore_transformations: " << ignore_transformations);

      img = nullptr;
      err = image.decode_tile(&img, 0, 0, ignore_transformations);
      INFO("tile decode error: " << err.message);
      REQUIRE(err.code == heif_error_Ok);
      REQUIRE(img != nullptr);
      REQUIRE(image.item_bytes_read() == file.item_size);
      check_pixels(img, 0, 0, kWidth, kHeight);
      heif_image_release(img);

      // --- The uncC tiles are not accessible.

      img = nullptr;
      err = image.decode_tile(&img, 1, 0, ignore_transformations);
      REQUIRE(err.code == heif_error_Usage_error);
      REQUIRE(img == nullptr);

      err = image.decode_tile(&img, 0, 1, ignore_transformations);
      REQUIRE(err.code == heif_error_Usage_error);
      REQUIRE(img == nullptr);
    }
  }
}


TEST_CASE("unci image with compressed units that are parts of a tile keeps its tiles") {
  std::vector<UnciFileSpec> specs;

  {
    // Two units per tile
    UnciFileSpec spec;
    spec.description = "row units with pixel interleave";
    spec.interleave_type = kInterleavePixel;
    spec.unit_type = kUnitRow;
    spec.units = split_into_units(make_image_data(kInterleavePixel), kTileWidth * kNumComponents);
    specs.push_back(spec);
  }

  {
    // Six units per tile: two rows for each of the three components
    UnciFileSpec spec;
    spec.description = "row units with component interleave";
    spec.interleave_type = kInterleaveComponent;
    spec.unit_type = kUnitRow;
    spec.units = split_into_units(make_image_data(kInterleaveComponent), kTileWidth);
    specs.push_back(spec);
  }

  {
    // Four units per tile
    UnciFileSpec spec;
    spec.description = "pixel units with pixel interleave";
    spec.interleave_type = kInterleavePixel;
    spec.unit_type = kUnitPixel;
    spec.units = split_into_units(make_image_data(kInterleavePixel), kNumComponents);
    specs.push_back(spec);
  }

  for (UnciFileSpec& spec : specs) {
    INFO("compressed units: " << spec.description);

    spec.with_icef = true;

    UnciFile file = build_heif_unci(spec);
    OpenedImage image(file);

    REQUIRE(file.compressed_unit_sizes.size() % kNumTiles == 0);
    size_t units_per_tile = file.compressed_unit_sizes.size() / kNumTiles;

    heif_image_tiling tiling = image.tiling();
    REQUIRE(tiling.num_columns == kTileColumns);
    REQUIRE(tiling.num_rows == kTileRows);
    REQUIRE(tiling.tile_width == kTileWidth);
    REQUIRE(tiling.tile_height == kTileHeight);

    // --- Decoding a tile only reads the compressed units of this tile.

    for (uint32_t ty = 0; ty < kTileRows; ty++) {
      for (uint32_t tx = 0; tx < kTileColumns; tx++) {
        INFO("tile (" << tx << "," << ty << ")");

        uint32_t tile_idx = ty * kTileColumns + tx;

        size_t tile_units_size = 0;
        for (size_t i = 0; i < units_per_tile; i++) {
          tile_units_size += file.compressed_unit_sizes[tile_idx * units_per_tile + i];
        }

        heif_image* img = nullptr;
        heif_error err = image.decode_tile(&img, tx, ty);
        INFO("tile decode error: " << err.message);
        REQUIRE(err.code == heif_error_Ok);
        REQUIRE(img != nullptr);
        REQUIRE(image.item_bytes_read() == tile_units_size);
        check_pixels(img, tx * kTileWidth, ty * kTileHeight, kTileWidth, kTileHeight);
        heif_image_release(img);
      }
    }

    // --- A tile position outside of the tiling is rejected.

    heif_image* img = nullptr;
    heif_error err = image.decode_tile(&img, kTileColumns, 0, /*ignore_transformations=*/true);
    REQUIRE(err.code == heif_error_Usage_error);
    REQUIRE(img == nullptr);

    // --- Decoding the whole image reads each unit once.

    err = image.decode_image(&img);
    INFO("decode error: " << err.message);
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(img != nullptr);
    REQUIRE(image.item_bytes_read() == file.item_size);
    check_pixels(img, 0, 0, kWidth, kHeight);
    heif_image_release(img);
  }
}


TEST_CASE("unci item that is decompressed as a whole must have the size of the image data") {
  for (bool with_icef : {false, true}) {
    INFO("with icef: " << with_icef);

    std::vector<uint8_t> image_data = make_image_data(kInterleaveComponent);

    UnciFileSpec spec;
    spec.with_icef = with_icef;

    // --- one byte too much

    std::vector<uint8_t> oversized_data = image_data;
    oversized_data.push_back(0);
    spec.units = {oversized_data};

    {
      UnciFile file = build_heif_unci(spec);
      OpenedImage image(file);

      heif_image* img = nullptr;
      heif_error err = image.decode_image(&img);
      REQUIRE(err.code == heif_error_Invalid_input);
      REQUIRE(err.subcode == heif_suberror_Decompression_invalid_data);
      REQUIRE(img == nullptr);
    }

    // --- one byte missing

    std::vector<uint8_t> short_data = image_data;
    short_data.pop_back();
    spec.units = {short_data};

    {
      UnciFile file = build_heif_unci(spec);
      OpenedImage image(file);

      heif_image* img = nullptr;
      heif_error err = image.decode_image(&img);
      REQUIRE(err.code == heif_error_Invalid_input);
      REQUIRE(err.subcode == heif_suberror_End_of_data);
      REQUIRE(img == nullptr);
    }
  }
}


TEST_CASE("unci compressed units of a tile must have the size of the tile") {
  // Row units with pixel interleave: two units per tile.
  UnciFileSpec spec;
  spec.interleave_type = kInterleavePixel;
  spec.unit_type = kUnitRow;
  spec.with_icef = true;

  const std::vector<std::vector<uint8_t>> units = split_into_units(make_image_data(kInterleavePixel),
                                                                    kTileWidth * kNumComponents);
  REQUIRE(units.size() == 2 * kNumTiles);
  REQUIRE(units[0].size() + units[1].size() == kTileDataSize);

  // --- the second unit of the first tile has one byte too much

  spec.units = units;
  spec.units[1].push_back(0);

  {
    UnciFile file = build_heif_unci(spec);
    OpenedImage image(file);

    heif_image* img = nullptr;
    heif_error err = image.decode_tile(&img, 0, 0);
    REQUIRE(err.code == heif_error_Invalid_input);
    REQUIRE(err.subcode == heif_suberror_Decompression_invalid_data);
    REQUIRE(img == nullptr);

    // The other tiles do not depend on these units.
    err = image.decode_tile(&img, 1, 0);
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(img != nullptr);
    check_pixels(img, kTileWidth, 0, kTileWidth, kTileHeight);
    heif_image_release(img);

    img = nullptr;
    err = image.decode_image(&img);
    REQUIRE(err.code == heif_error_Invalid_input);
    REQUIRE(img == nullptr);
  }

  // --- the second unit of the first tile has one byte missing

  spec.units = units;
  spec.units[1].pop_back();

  {
    UnciFile file = build_heif_unci(spec);
    OpenedImage image(file);

    heif_image* img = nullptr;
    heif_error err = image.decode_tile(&img, 0, 0);
    REQUIRE(err.code == heif_error_Invalid_input);
    REQUIRE(err.subcode == heif_suberror_End_of_data);
    REQUIRE(img == nullptr);

    err = image.decode_tile(&img, 1, 0);
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(img != nullptr);
    check_pixels(img, kTileWidth, 0, kTileWidth, kTileHeight);
    heif_image_release(img);
  }
}
