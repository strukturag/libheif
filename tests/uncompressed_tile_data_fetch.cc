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

// Regression tests for GHSA-fcmw-5764-7rq8.
//
// An 'unci' image with generic compression and one compressed unit per image
// tile (cmpC compressed_unit_type == image_tile) combined with tile-component
// interleave (uncC interleave_type == 4) made unc_decoder::fetch_tile_data()
// fetch the tile data once per component. Since the image_tile branch of
// get_compressed_image_data_uncompressed() returns the whole unit irrespective
// of the requested range, the same unit was read and decompressed once for each
// component and all copies were concatenated: the tile buffer grew to
// num_components times the unit size, none of it charged against the memory
// limits. With 256 components and a unit that inflates to 20 MiB, a 22 kB file
// requested more than 5 GiB.
//
// The fix has four parts, each with a test below:
//
// 1. The unit is fetched once per tile. Because the excess memory was never
//    accounted, it cannot be observed through the security limits. The test
//    reads the file through a heif_reader that counts the bytes read from the
//    compressed unit: they have to be read exactly once. It also checks the
//    decoded pixels, as the unit holds the planes of all components.
//
// 2. The decompressor is told the size of the tile and stops with an error as
//    soon as the unit yields more data than that.
//
// 3. A unit that yields less data than the tile is rejected, too.
//
// 4. The tile data is charged to the memory budget for as long as it is kept.
//    This is independent of the generic compression, so the test uses an image
//    without compression. A large row alignment pads each 16 byte row to 64 kiB.
//    This makes the tile data much larger than the decoded image, so that it
//    exceeds a memory budget that the decoded image itself fits into.
//
// The compressed files use deflate generic compression, so the tests only run
// when the library was built with zlib (guarded in tests/CMakeLists.txt).

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

struct UnciFileSpec {
  uint32_t width = 2;
  uint32_t height = 2;

  // 'cmpd' component types. All components have 8 bits.
  std::vector<uint16_t> component_types = {heif_cmpd_component_type_red,
                                           heif_cmpd_component_type_green,
                                           heif_cmpd_component_type_blue};

  uint8_t interleave_type = 4; // tile-component
  uint32_t row_align_size = 0;

  // Whether 'item_data' is a single deflate compressed unit of type image_tile.
  bool deflate_tile_unit = true;

  std::vector<uint8_t> item_data;
};

// Tile data of the default 2x2 RGB image in tile-component interleave:
// the R plane, the G plane, the B plane.
const std::vector<uint8_t> kTileData = {
    10, 11, 12, 13,   // R
    20, 21, 22, 23,   // G
    30, 31, 32, 33};  // B

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

// Build an 'unci' image with a single tile. The item data is stored in 'idat',
// which is the last box of the file.
std::vector<uint8_t> build_heif_unci(const UnciFileSpec& spec) {
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

  auto num_components = static_cast<uint16_t>(spec.component_types.size());

  // ispe
  std::vector<uint8_t> ispe_payload;
  put_u32_be(ispe_payload, spec.width);
  put_u32_be(ispe_payload, spec.height);
  auto ispe = make_box("ispe", ispe_payload, /*full=*/true);

  // cmpd
  std::vector<uint8_t> cmpd_payload;
  put_u32_be(cmpd_payload, num_components);
  for (uint16_t type : spec.component_types) {
    put_u16_be(cmpd_payload, type);
  }
  auto cmpd = make_box("cmpd", cmpd_payload);

  // uncC (v0): 8-bit components, a single tile.
  std::vector<uint8_t> uncC_payload;
  put_u32_be(uncC_payload, 0);           // profile
  put_u32_be(uncC_payload, num_components);
  for (uint16_t c = 0; c < num_components; c++) {
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
  put_u32_be(uncC_payload, spec.row_align_size);
  put_u32_be(uncC_payload, 0);           // tile_align_size
  put_u32_be(uncC_payload, 0);           // num_tile_cols_minus_one
  put_u32_be(uncC_payload, 0);           // num_tile_rows_minus_one
  auto uncC = make_box("uncC", uncC_payload, /*full=*/true);

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, ispe);
  append(ipco_payload, cmpd);
  append(ipco_payload, uncC);

  uint8_t num_properties = 3;

  if (spec.deflate_tile_unit) {
    // cmpC: deflate, compressed_unit_type = image_tile (2).
    std::vector<uint8_t> cmpC_payload;
    append_fourcc(cmpC_payload, "defl");
    cmpC_payload.push_back(2);
    append(ipco_payload, make_box("cmpC", cmpC_payload, /*full=*/true));

    // icef: one unit at the implied offset 0 with a 32-bit size field.
    std::vector<uint8_t> icef_payload;
    icef_payload.push_back(3 << 2);      // unit_offset_code = 0, unit_size_code = 3
    put_u32_be(icef_payload, 1);         // num_compressed_units
    put_u32_be(icef_payload, static_cast<uint32_t>(spec.item_data.size()));
    append(ipco_payload, make_box("icef", icef_payload, /*full=*/true));

    num_properties = 5;
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

  auto idat = make_box("idat", spec.item_data);

  // iloc (version 1): item 1 stored in idat (construction_method=1).
  std::vector<uint8_t> iloc_payload;
  put_u16_be(iloc_payload, (4 << 12) | (4 << 8) | (0 << 4) | 0); // offset_size=4, length_size=4
  put_u16_be(iloc_payload, 1);          // item_count
  put_u16_be(iloc_payload, 1);          // item_ID
  put_u16_be(iloc_payload, 0x0001);     // construction_method=1 (idat)
  put_u16_be(iloc_payload, 0);          // data_reference_index
  put_u16_be(iloc_payload, 1);          // extent_count
  put_u32_be(iloc_payload, 0);          // extent_offset (within idat)
  put_u32_be(iloc_payload, static_cast<uint32_t>(spec.item_data.size())); // extent_length
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);

  // idat has to be the last box: a test locates the item data at the end of the file.
  std::vector<uint8_t> meta_payload;
  append(meta_payload, hdlr);
  append(meta_payload, pitm);
  append(meta_payload, iinf);
  append(meta_payload, iprp);
  append(meta_payload, iloc);
  append(meta_payload, idat);
  auto meta = make_box("meta", meta_payload, /*full=*/true);

  std::vector<uint8_t> file;
  append(file, ftyp);
  append(file, meta);
  return file;
}


// A heif_reader on a memory buffer that counts the bytes read from one byte range.
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


// Read the file from memory and decode its primary image without any color conversion.
// 'max_total_memory' replaces the default memory budget unless it is 0.
heif_error decode_primary_image(const std::vector<uint8_t>& file, uint64_t max_total_memory = 0) {
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  if (max_total_memory != 0) {
    heif_context_get_security_limits(ctx)->max_total_memory = max_total_memory;
  }

  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  INFO("read error: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
  REQUIRE((img != nullptr) == (err.code == heif_error_Ok));

  // The error message is owned by the context. The tests only compare the error codes.
  err.message = "";

  heif_image_release(img);
  heif_image_handle_release(handle);
  heif_context_free(ctx);

  return err;
}

} // namespace

TEST_CASE("unci image_tile compressed unit is fetched once for tile-component interleave") {
  UnciFileSpec spec;
  spec.item_data = deflate_stored(kTileData);
  std::vector<uint8_t> file = build_heif_unci(spec);

  CountingReader counting_reader;
  counting_reader.data = &file;
  counting_reader.watched_start = static_cast<int64_t>(file.size() - spec.item_data.size());
  counting_reader.watched_end = static_cast<int64_t>(file.size());

  heif_reader reader{};
  reader.reader_api_version = 1;
  reader.get_position = reader_get_position;
  reader.read = reader_read;
  reader.seek = reader_seek;
  reader.wait_for_file_size = reader_wait_for_file_size;

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_reader(ctx, &reader, &counting_reader, nullptr);
  INFO("read error: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  // Only count what the decoding reads.
  counting_reader.watched_bytes_read = 0;

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_444, nullptr);
  INFO("decode error: " << err.message);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(img != nullptr);

  // Before the fix, the unit was read (and decompressed) once per component.
  REQUIRE(counting_reader.watched_bytes_read == spec.item_data.size());

  const heif_channel channels[3] = {heif_channel_R, heif_channel_G, heif_channel_B};

  for (uint32_t c = 0; c < 3; c++) {
    REQUIRE(heif_image_get_width(img, channels[c]) == static_cast<int>(spec.width));
    REQUIRE(heif_image_get_height(img, channels[c]) == static_cast<int>(spec.height));

    int stride = 0;
    const uint8_t* plane = heif_image_get_plane_readonly(img, channels[c], &stride);
    REQUIRE(plane != nullptr);

    for (uint32_t y = 0; y < spec.height; y++) {
      for (uint32_t x = 0; x < spec.width; x++) {
        INFO("component " << c << ", pixel (" << x << "," << y << ")");
        REQUIRE(static_cast<int>(plane[y * stride + x]) ==
                static_cast<int>(kTileData[c * spec.width * spec.height + y * spec.width + x]));
      }
    }
  }

  heif_image_release(img);
  heif_image_handle_release(handle);
  heif_context_free(ctx);
}


TEST_CASE("unci image_tile compressed unit larger than the tile is rejected") {
  std::vector<uint8_t> oversized_tile_data = kTileData;
  oversized_tile_data.push_back(0);

  UnciFileSpec spec;
  spec.item_data = deflate_stored(oversized_tile_data);

  heif_error err = decode_primary_image(build_heif_unci(spec));
  REQUIRE(err.code == heif_error_Invalid_input);
  REQUIRE(err.subcode == heif_suberror_Decompression_invalid_data);
}


TEST_CASE("unci image_tile compressed unit smaller than the tile is rejected") {
  std::vector<uint8_t> short_tile_data = kTileData;
  short_tile_data.pop_back();

  UnciFileSpec spec;
  spec.item_data = deflate_stored(short_tile_data);

  heif_error err = decode_primary_image(build_heif_unci(spec));
  REQUIRE(err.code == heif_error_Invalid_input);
  REQUIRE(err.subcode == heif_suberror_End_of_data);
}


TEST_CASE("unci tile data is charged to the memory budget") {
  // Each row of a component has 16 bytes of pixel data and is padded to 64 kiB.
  // The decoded image has three planes of 256 bytes. The tile data has 1 MiB for
  // each of the three components.
  constexpr uint32_t size = 16;
  constexpr uint32_t row_size = 65536;
  constexpr uint32_t num_components = 3;

  UnciFileSpec spec;
  spec.width = size;
  spec.height = size;
  spec.row_align_size = row_size;
  spec.deflate_tile_unit = false;
  spec.item_data.assign(size * row_size * num_components, 0);

  // component interleave (single read) and tile-component interleave (one read per component)
  for (uint8_t interleave_type : {uint8_t{0}, uint8_t{4}}) {
    INFO("interleave_type: " << static_cast<int>(interleave_type));

    spec.interleave_type = interleave_type;
    std::vector<uint8_t> file = build_heif_unci(spec);

    // The image decodes with the default limits.
    heif_error err = decode_primary_image(file);
    REQUIRE(err.code == heif_error_Ok);

    // A budget of 2 MiB is plenty for the decoded image, but not enough for the tile data.
    err = decode_primary_image(file, 2 * 1024 * 1024);
    REQUIRE(err.code == heif_error_Memory_allocation_error);
    REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);
  }
}
