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

// Test for uncompressed ('uncv') sequence frames with uncC tiles.
//
// The decoder fetches the data of each tile as a byte range of the sample.
// DataExtent::read_data(offset, size) ignored the requested range for samples
// and returned the whole sample. Each tile was therefore decoded from the start
// of the sample, so that all tiles showed the content of the first tile, and the
// whole sample was read from the file once for each tile.
//
// The test builds a 4x4 RGB frame with 2x2 tiles in which each sample value is
// different, decodes it and checks all pixels. It reads the file through a
// heif_reader that counts the bytes read from the sample data: they have to be
// read exactly once.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "test_utils.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr uint16_t kWidth = 4;
constexpr uint16_t kHeight = 4;
constexpr uint32_t kTileColumns = 2;
constexpr uint32_t kTileRows = 2;
constexpr uint32_t kTileWidth = kWidth / kTileColumns;
constexpr uint32_t kTileHeight = kHeight / kTileRows;
constexpr uint32_t kNumComponents = 3;
constexpr uint32_t kSampleSize = kWidth * kHeight * kNumComponents;

// Each sample value of the frame is different.
uint8_t pixel_value(uint32_t component, uint32_t x, uint32_t y) {
  return static_cast<uint8_t>((component + 1) * 50 + y * kWidth + x);
}

void append_matrix(std::vector<uint8_t>& out) {
  for (uint32_t m : {0x00010000u, 0u, 0u, 0u, 0x00010000u, 0u, 0u, 0u, 0x40000000u}) {
    put_u32_be(out, m);
  }
}

// Build an image sequence with a single 'pict' track. Its only sample is a 4x4 RGB
// frame with 2x2 tiles in pixel interleave. The sample data is in 'mdat', which is
// the last box of the file.
std::vector<uint8_t> build_tiled_uncv_sequence() {
  std::vector<uint8_t> ftyp_payload;
  append_fourcc(ftyp_payload, "msf1");
  put_u32_be(ftyp_payload, 0);
  append_fourcc(ftyp_payload, "msf1");
  append_fourcc(ftyp_payload, "iso8");
  auto ftyp = make_box("ftyp", ftyp_payload);

  // mvhd (v0)
  std::vector<uint8_t> mvhd_payload;
  put_u32_be(mvhd_payload, 0);          // creation_time
  put_u32_be(mvhd_payload, 0);          // modification_time
  put_u32_be(mvhd_payload, 1000);       // timescale
  put_u32_be(mvhd_payload, 1);          // duration
  put_u32_be(mvhd_payload, 0x00010000); // rate
  put_u16_be(mvhd_payload, 0x0100);     // volume
  put_u16_be(mvhd_payload, 0);
  put_u32_be(mvhd_payload, 0);
  put_u32_be(mvhd_payload, 0);
  append_matrix(mvhd_payload);
  for (int i = 0; i < 6; i++) {
    put_u32_be(mvhd_payload, 0);
  }
  put_u32_be(mvhd_payload, 2);          // next_track_ID
  auto mvhd = make_box("mvhd", mvhd_payload, /*full=*/true);

  // tkhd (v0), flags: enabled | in_movie | in_preview
  std::vector<uint8_t> tkhd_payload;
  put_u32_be(tkhd_payload, 0);
  put_u32_be(tkhd_payload, 0);
  put_u32_be(tkhd_payload, 1);          // track_ID
  put_u32_be(tkhd_payload, 0);
  put_u32_be(tkhd_payload, 1);          // duration
  put_u32_be(tkhd_payload, 0);
  put_u32_be(tkhd_payload, 0);
  put_u16_be(tkhd_payload, 0);          // layer
  put_u16_be(tkhd_payload, 0);          // alternate_group
  put_u16_be(tkhd_payload, 0);          // volume
  put_u16_be(tkhd_payload, 0);
  append_matrix(tkhd_payload);
  put_u32_be(tkhd_payload, static_cast<uint32_t>(kWidth) << 16);  // width (16.16)
  put_u32_be(tkhd_payload, static_cast<uint32_t>(kHeight) << 16); // height (16.16)
  auto tkhd = make_box("tkhd", tkhd_payload, /*full=*/true, /*version=*/0, /*flags=*/7);

  // mdhd (v0)
  std::vector<uint8_t> mdhd_payload;
  put_u32_be(mdhd_payload, 0);
  put_u32_be(mdhd_payload, 0);
  put_u32_be(mdhd_payload, 1000);       // timescale
  put_u32_be(mdhd_payload, 1);          // duration
  put_u16_be(mdhd_payload, 0x55c4);     // language 'und'
  put_u16_be(mdhd_payload, 0);
  auto mdhd = make_box("mdhd", mdhd_payload, /*full=*/true);

  std::vector<uint8_t> hdlr_payload;
  put_u32_be(hdlr_payload, 0);
  append_fourcc(hdlr_payload, "pict");
  put_u32_be(hdlr_payload, 0);
  put_u32_be(hdlr_payload, 0);
  put_u32_be(hdlr_payload, 0);
  hdlr_payload.push_back(0);
  auto hdlr = make_box("hdlr", hdlr_payload, /*full=*/true);

  std::vector<uint8_t> vmhd_payload;
  put_u16_be(vmhd_payload, 0);
  put_u16_be(vmhd_payload, 0);
  put_u16_be(vmhd_payload, 0);
  put_u16_be(vmhd_payload, 0);
  auto vmhd = make_box("vmhd", vmhd_payload, /*full=*/true, /*version=*/0, /*flags=*/1);

  // cmpd
  std::vector<uint8_t> cmpd_payload;
  put_u32_be(cmpd_payload, kNumComponents);
  put_u16_be(cmpd_payload, heif_cmpd_component_type_red);
  put_u16_be(cmpd_payload, heif_cmpd_component_type_green);
  put_u16_be(cmpd_payload, heif_cmpd_component_type_blue);
  auto cmpd = make_box("cmpd", cmpd_payload);

  // uncC (v0): 8-bit components, pixel interleave, 2x2 tiles
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
  uncC_payload.push_back(1);             // interleave_type (pixel)
  uncC_payload.push_back(0);             // block_size
  uncC_payload.push_back(0);             // flags
  put_u32_be(uncC_payload, 0);           // pixel_size
  put_u32_be(uncC_payload, 0);           // row_align_size
  put_u32_be(uncC_payload, 0);           // tile_align_size
  put_u32_be(uncC_payload, kTileColumns - 1);
  put_u32_be(uncC_payload, kTileRows - 1);
  auto uncC = make_box("uncC", uncC_payload, /*full=*/true);

  // uncv VisualSampleEntry
  std::vector<uint8_t> uncv_payload;
  for (int i = 0; i < 6; i++) {
    uncv_payload.push_back(0);           // reserved
  }
  put_u16_be(uncv_payload, 1);           // data_reference_index
  put_u16_be(uncv_payload, 0);           // pre_defined
  put_u16_be(uncv_payload, 0);           // reserved
  put_u32_be(uncv_payload, 0);           // pre_defined
  put_u32_be(uncv_payload, 0);
  put_u32_be(uncv_payload, 0);
  put_u16_be(uncv_payload, kWidth);
  put_u16_be(uncv_payload, kHeight);
  put_u32_be(uncv_payload, 0x00480000);  // horizresolution 72 dpi
  put_u32_be(uncv_payload, 0x00480000);  // vertresolution 72 dpi
  put_u32_be(uncv_payload, 0);           // reserved
  put_u16_be(uncv_payload, 1);           // frame_count
  for (int i = 0; i < 32; i++) {
    uncv_payload.push_back(0);           // compressorname
  }
  put_u16_be(uncv_payload, 0x0018);      // depth
  put_u16_be(uncv_payload, 0xFFFF);      // pre_defined (-1)
  append(uncv_payload, cmpd);
  append(uncv_payload, uncC);
  auto uncv = make_box("uncv", uncv_payload);

  std::vector<uint8_t> stsd_payload;
  put_u32_be(stsd_payload, 1);           // entry_count
  append(stsd_payload, uncv);
  auto stsd = make_box("stsd", stsd_payload, /*full=*/true);

  std::vector<uint8_t> stts_payload;
  put_u32_be(stts_payload, 1);           // entry_count
  put_u32_be(stts_payload, 1);           // sample_count
  put_u32_be(stts_payload, 1);           // sample_delta
  auto stts = make_box("stts", stts_payload, /*full=*/true);

  std::vector<uint8_t> stsc_payload;
  put_u32_be(stsc_payload, 1);           // entry_count
  put_u32_be(stsc_payload, 1);           // first_chunk
  put_u32_be(stsc_payload, 1);           // samples_per_chunk
  put_u32_be(stsc_payload, 1);           // sample_description_index
  auto stsc = make_box("stsc", stsc_payload, /*full=*/true);

  std::vector<uint8_t> stsz_payload;
  put_u32_be(stsz_payload, kSampleSize); // fixed sample size
  put_u32_be(stsz_payload, 1);           // sample_count
  auto stsz = make_box("stsz", stsz_payload, /*full=*/true);

  auto assemble_moov = [&](uint32_t chunk_offset) {
    std::vector<uint8_t> stco_payload;
    put_u32_be(stco_payload, 1);         // entry_count
    put_u32_be(stco_payload, chunk_offset);
    auto stco = make_box("stco", stco_payload, /*full=*/true);

    std::vector<uint8_t> stbl_payload;
    append(stbl_payload, stsd);
    append(stbl_payload, stts);
    append(stbl_payload, stsc);
    append(stbl_payload, stsz);
    append(stbl_payload, stco);

    std::vector<uint8_t> minf_payload;
    append(minf_payload, vmhd);
    append(minf_payload, make_box("stbl", stbl_payload));

    std::vector<uint8_t> mdia_payload;
    append(mdia_payload, mdhd);
    append(mdia_payload, hdlr);
    append(mdia_payload, make_box("minf", minf_payload));

    std::vector<uint8_t> trak_payload;
    append(trak_payload, tkhd);
    append(trak_payload, make_box("mdia", mdia_payload));

    std::vector<uint8_t> moov_payload;
    append(moov_payload, mvhd);
    append(moov_payload, make_box("trak", trak_payload));
    return make_box("moov", moov_payload);
  };

  // The chunk offset does not change the size of the moov box.
  auto mdat_payload_offset = static_cast<uint32_t>(ftyp.size() + assemble_moov(0).size() + 8 /* mdat header */);
  auto moov = assemble_moov(mdat_payload_offset);

  // mdat: the tiles in raster order, each in pixel interleave
  std::vector<uint8_t> sample;
  for (uint32_t tile_y = 0; tile_y < kTileRows; tile_y++) {
    for (uint32_t tile_x = 0; tile_x < kTileColumns; tile_x++) {
      for (uint32_t y = 0; y < kTileHeight; y++) {
        for (uint32_t x = 0; x < kTileWidth; x++) {
          for (uint32_t c = 0; c < kNumComponents; c++) {
            sample.push_back(pixel_value(c, tile_x * kTileWidth + x, tile_y * kTileHeight + y));
          }
        }
      }
    }
  }
  REQUIRE(sample.size() == kSampleSize);

  std::vector<uint8_t> file;
  append(file, ftyp);
  append(file, moov);
  append(file, make_box("mdat", sample));
  return file;
}


// A heif_reader on a memory buffer that counts the bytes read from the sample data.
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

} // namespace


TEST_CASE("uncv sequence frame with tiles decodes each tile from its own data") {
  std::vector<uint8_t> file = build_tiled_uncv_sequence();

  // The sample data is at the end of the file.
  CountingReader counting_reader;
  counting_reader.data = &file;
  counting_reader.watched_start = static_cast<int64_t>(file.size() - kSampleSize);
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
  REQUIRE(heif_context_has_sequence(ctx) == 1);

  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  // Only count what the decoding reads.
  counting_reader.watched_bytes_read = 0;

  heif_image* img = nullptr;
  err = heif_track_decode_next_image(track, &img, heif_colorspace_RGB, heif_chroma_444, nullptr);
  INFO("decode error: " << err.message);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(img != nullptr);

  const heif_channel channels[kNumComponents] = {heif_channel_R, heif_channel_G, heif_channel_B};

  for (uint32_t c = 0; c < kNumComponents; c++) {
    REQUIRE(heif_image_get_width(img, channels[c]) == kWidth);
    REQUIRE(heif_image_get_height(img, channels[c]) == kHeight);

    int stride = 0;
    const uint8_t* plane = heif_image_get_plane_readonly(img, channels[c], &stride);
    REQUIRE(plane != nullptr);

    for (uint32_t y = 0; y < kHeight; y++) {
      for (uint32_t x = 0; x < kWidth; x++) {
        INFO("component " << c << ", pixel (" << x << "," << y << ")");
        REQUIRE(static_cast<int>(plane[y * stride + x]) == static_cast<int>(pixel_value(c, x, y)));
      }
    }
  }

  // Before the fix, the whole sample was read once for each tile.
  REQUIRE(counting_reader.watched_bytes_read == kSampleSize);

  heif_image_release(img);
  heif_track_release(track);
  heif_context_free(ctx);
}
