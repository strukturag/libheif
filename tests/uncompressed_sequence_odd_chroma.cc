/*
  libheif integration test for GHSA-4h82-g446-83fm.

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

// Regression test for GHSA-4h82-g446-83fm: heap-buffer-overflow read when
// decoding an uncompressed ('uncv') HEIF *sequence* frame with YCbCr 4:2:0
// subsampling and an odd declared luma height.
//
// Two independent defects combined here:
//
//  1. Chroma-geometry mismatch (the advisory's root cause). The sequence
//     decode path builds the HeifPixelImage via the fallback branch of
//     UncompressedImageCodec::create_image() (it does not clone pre-populated
//     component descriptions). That branch sized the Cb/Cr planes with FLOOR
//     division of the luma dimensions, while the RGB conversion
//     (Op_YCbCr420_to_RGB24) indexes chroma with y/2 for every luma row and so
//     needs ceil(height/2) rows. For height 129 the Cb/Cr plane was allocated
//     64 rows tall but read on row 64 -> a 1-byte heap out-of-bounds read.
//
//  2. Component-mapping defect (why the OOB was masked in recent releases).
//     The same fallback loop compared desc_idx against the *live*
//     get_component_descriptions().size(). add_component() grows that list, so
//     after creating the Y plane the next iteration wrongly took the
//     pre-populated branch and re-referenced Y instead of creating Cb, dropping
//     the Cb plane. The conversion then failed early ("Internal error") before
//     reaching the vulnerable read, hiding defect 1.
//
// The fix rounds the fallback chroma allocation up (defect 1) and anchors the
// branch decision to the description count captured before the loop (defect 2).
// This test builds a minimal 128x129 4:2:0 'uncv' sequence by hand and decodes
// it to RGB through the public heif_track_decode_next_image() API. Under
// AddressSanitizer the unfixed library aborts on the out-of-bounds read; the
// fixed library decodes a correctly sized RGB frame.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"

#include <cstdint>
#include <vector>

namespace {

void put16(std::vector<uint8_t>& v, uint16_t x)
{
  v.push_back(static_cast<uint8_t>(x >> 8));
  v.push_back(static_cast<uint8_t>(x));
}

void put32(std::vector<uint8_t>& v, uint32_t x)
{
  v.push_back(static_cast<uint8_t>(x >> 24));
  v.push_back(static_cast<uint8_t>(x >> 16));
  v.push_back(static_cast<uint8_t>(x >> 8));
  v.push_back(static_cast<uint8_t>(x));
}

void put_fourcc(std::vector<uint8_t>& v, const char* s)
{
  v.push_back(static_cast<uint8_t>(s[0]));
  v.push_back(static_cast<uint8_t>(s[1]));
  v.push_back(static_cast<uint8_t>(s[2]));
  v.push_back(static_cast<uint8_t>(s[3]));
}

std::vector<uint8_t> box(const char* type, const std::vector<uint8_t>& payload)
{
  std::vector<uint8_t> b;
  put32(b, static_cast<uint32_t>(8 + payload.size()));
  put_fourcc(b, type);
  b.insert(b.end(), payload.begin(), payload.end());
  return b;
}

std::vector<uint8_t> concat(std::initializer_list<std::vector<uint8_t>> parts)
{
  std::vector<uint8_t> out;
  for (const auto& p : parts) {
    out.insert(out.end(), p.begin(), p.end());
  }
  return out;
}

constexpr uint16_t WIDTH  = 128;
constexpr uint16_t HEIGHT = 129; // odd -> floor chroma height 64, ceil 65

// Build a minimal, structurally valid HEIF image sequence with a single 'pict'
// track whose sole sample is a 128x129 YCbCr 4:2:0 uncompressed ('uncv') frame
// in component-interleave layout (Y plane, then Cb plane, then Cr plane).
std::vector<uint8_t> build_uncv_sequence_file()
{
  // --- ftyp
  std::vector<uint8_t> ftyp_p;
  put_fourcc(ftyp_p, "msf1");
  put32(ftyp_p, 0);
  put_fourcc(ftyp_p, "msf1");
  put_fourcc(ftyp_p, "iso8");
  std::vector<uint8_t> ftyp = box("ftyp", ftyp_p);

  // --- mvhd (v0)
  std::vector<uint8_t> mvhd_p;
  put32(mvhd_p, 0);              // version/flags
  put32(mvhd_p, 0);              // creation_time
  put32(mvhd_p, 0);              // modification_time
  put32(mvhd_p, 1000);           // timescale
  put32(mvhd_p, 1);              // duration
  put32(mvhd_p, 0x00010000);     // rate
  put16(mvhd_p, 0x0100);         // volume
  put16(mvhd_p, 0);
  put32(mvhd_p, 0);
  put32(mvhd_p, 0);
  for (uint32_t m : {0x00010000u,0u,0u,0u,0x00010000u,0u,0u,0u,0x40000000u}) put32(mvhd_p, m);
  for (int i = 0; i < 6; i++) put32(mvhd_p, 0);
  put32(mvhd_p, 2);              // next_track_ID
  std::vector<uint8_t> mvhd = box("mvhd", mvhd_p);

  // --- tkhd (v0)
  std::vector<uint8_t> tkhd_p;
  put32(tkhd_p, 0x00000007);     // flags: enabled | in_movie | in_preview
  put32(tkhd_p, 0);
  put32(tkhd_p, 0);
  put32(tkhd_p, 1);              // track_ID
  put32(tkhd_p, 0);
  put32(tkhd_p, 1);              // duration
  put32(tkhd_p, 0);
  put32(tkhd_p, 0);
  put16(tkhd_p, 0);              // layer
  put16(tkhd_p, 0);              // alternate_group
  put16(tkhd_p, 0);              // volume
  put16(tkhd_p, 0);
  for (uint32_t m : {0x00010000u,0u,0u,0u,0x00010000u,0u,0u,0u,0x40000000u}) put32(tkhd_p, m);
  put32(tkhd_p, static_cast<uint32_t>(WIDTH)  << 16); // width  (16.16)
  put32(tkhd_p, static_cast<uint32_t>(HEIGHT) << 16); // height (16.16)
  std::vector<uint8_t> tkhd = box("tkhd", tkhd_p);

  // --- mdhd (v0)
  std::vector<uint8_t> mdhd_p;
  put32(mdhd_p, 0);
  put32(mdhd_p, 0);
  put32(mdhd_p, 0);
  put32(mdhd_p, 1000);           // timescale
  put32(mdhd_p, 1);              // duration
  put16(mdhd_p, 0x55c4);         // language 'und'
  put16(mdhd_p, 0);
  std::vector<uint8_t> mdhd = box("mdhd", mdhd_p);

  // --- hdlr ('pict')
  std::vector<uint8_t> hdlr_p;
  put32(hdlr_p, 0);
  put32(hdlr_p, 0);
  put_fourcc(hdlr_p, "pict");
  put32(hdlr_p, 0);
  put32(hdlr_p, 0);
  put32(hdlr_p, 0);
  hdlr_p.push_back(0);
  std::vector<uint8_t> hdlr = box("hdlr", hdlr_p);

  // --- vmhd
  std::vector<uint8_t> vmhd_p;
  put32(vmhd_p, 0x00000001);
  put16(vmhd_p, 0);
  put16(vmhd_p, 0);
  put16(vmhd_p, 0);
  put16(vmhd_p, 0);
  std::vector<uint8_t> vmhd = box("vmhd", vmhd_p);

  // --- cmpd (plain box): Y, Cb, Cr
  std::vector<uint8_t> cmpd_p;
  put32(cmpd_p, 3);
  put16(cmpd_p, 1); // Y
  put16(cmpd_p, 2); // Cb
  put16(cmpd_p, 3); // Cr
  std::vector<uint8_t> cmpd = box("cmpd", cmpd_p);

  // --- uncC (fullbox v0): component interleave, 4:2:0, 8-bit
  std::vector<uint8_t> uncC_p;
  put32(uncC_p, 0);              // version/flags
  put32(uncC_p, 0);              // profile
  put32(uncC_p, 3);              // component_count
  for (uint16_t idx = 0; idx < 3; idx++) {
    put16(uncC_p, idx);          // component_index
    uncC_p.push_back(7);         // component_bit_depth_minus_1 (8 bit)
    uncC_p.push_back(0);         // component_format (unsigned int)
    uncC_p.push_back(0);         // component_align_size
  }
  uncC_p.push_back(2);           // sampling_type = 4:2:0
  uncC_p.push_back(0);           // interleave_type = component
  uncC_p.push_back(0);           // block_size
  uncC_p.push_back(0);           // flags
  put32(uncC_p, 0);              // pixel_size
  put32(uncC_p, 0);              // row_align_size
  put32(uncC_p, 0);              // tile_align_size
  put32(uncC_p, 0);              // num_tile_cols_minus_one
  put32(uncC_p, 0);              // num_tile_rows_minus_one
  std::vector<uint8_t> uncC = box("uncC", uncC_p);

  // --- uncv VisualSampleEntry (78-byte header + cmpd + uncC)
  std::vector<uint8_t> vse;
  for (int i = 0; i < 6; i++) vse.push_back(0); // reserved
  put16(vse, 1);                 // data_reference_index
  put16(vse, 0);                 // pre_defined
  put16(vse, 0);                 // reserved
  put32(vse, 0); put32(vse, 0); put32(vse, 0); // pre_defined2[3]
  put16(vse, WIDTH);
  put16(vse, HEIGHT);
  put32(vse, 0x00480000);        // horizresolution 72 dpi
  put32(vse, 0x00480000);        // vertresolution 72 dpi
  put32(vse, 0);                 // reserved
  put16(vse, 1);                 // frame_count
  for (int i = 0; i < 32; i++) vse.push_back(0); // compressorname
  put16(vse, 0x0018);            // depth
  put16(vse, 0xFFFF);            // pre_defined3 (-1)
  std::vector<uint8_t> uncv = box("uncv", concat({vse, cmpd, uncC}));

  // --- stsd
  std::vector<uint8_t> stsd_p;
  put32(stsd_p, 0);
  put32(stsd_p, 1);
  stsd_p.insert(stsd_p.end(), uncv.begin(), uncv.end());
  std::vector<uint8_t> stsd = box("stsd", stsd_p);

  // --- stts
  std::vector<uint8_t> stts_p;
  put32(stts_p, 0);
  put32(stts_p, 1);
  put32(stts_p, 1);              // sample_count
  put32(stts_p, 1);              // sample_delta
  std::vector<uint8_t> stts = box("stts", stts_p);

  // --- stsc
  std::vector<uint8_t> stsc_p;
  put32(stsc_p, 0);
  put32(stsc_p, 1);
  put32(stsc_p, 1);              // first_chunk
  put32(stsc_p, 1);              // samples_per_chunk
  put32(stsc_p, 1);              // sample_description_index
  std::vector<uint8_t> stsc = box("stsc", stsc_p);

  const uint32_t sample_size =
      static_cast<uint32_t>(WIDTH) * HEIGHT +               // Y
      2u * (WIDTH / 2) * (HEIGHT / 2);                      // Cb + Cr (floor chroma in the stream)

  // --- stsz (single fixed sample size)
  std::vector<uint8_t> stsz_p;
  put32(stsz_p, 0);
  put32(stsz_p, sample_size);    // fixed sample size
  put32(stsz_p, 1);              // sample_count
  std::vector<uint8_t> stsz = box("stsz", stsz_p);

  auto assemble = [&](uint32_t chunk_offset) {
    std::vector<uint8_t> stco_p;
    put32(stco_p, 0);
    put32(stco_p, 1);
    put32(stco_p, chunk_offset);
    std::vector<uint8_t> stco = box("stco", stco_p);

    std::vector<uint8_t> stbl = box("stbl", concat({stsd, stts, stsc, stsz, stco}));
    std::vector<uint8_t> minf = box("minf", concat({vmhd, stbl}));
    std::vector<uint8_t> mdia = box("mdia", concat({mdhd, hdlr, minf}));
    std::vector<uint8_t> trak = box("trak", concat({tkhd, mdia}));
    return box("moov", concat({mvhd, trak}));
  };

  std::vector<uint8_t> moov0 = assemble(0);
  uint32_t mdat_payload_off = static_cast<uint32_t>(ftyp.size() + moov0.size() + 8 /* mdat header */);
  std::vector<uint8_t> moov = assemble(mdat_payload_off);

  // --- mdat: Y plane, then Cb plane, then Cr plane (floor-sized chroma)
  std::vector<uint8_t> mdat_payload;
  mdat_payload.reserve(sample_size);
  for (uint32_t y = 0; y < HEIGHT; y++)
    for (uint32_t x = 0; x < WIDTH; x++)
      mdat_payload.push_back(static_cast<uint8_t>((x + y) & 0xFF));
  for (int plane = 0; plane < 2; plane++)
    for (uint32_t y = 0; y < HEIGHT / 2u; y++)
      for (uint32_t x = 0; x < WIDTH / 2u; x++)
        mdat_payload.push_back(0x80);
  REQUIRE(mdat_payload.size() == sample_size);
  std::vector<uint8_t> mdat = box("mdat", mdat_payload);

  return concat({ftyp, moov, mdat});
}

} // namespace


TEST_CASE("uncv 4:2:0 sequence with odd luma height decodes without OOB")
{
  std::vector<uint8_t> file = build_uncv_sequence_file();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(heif_context_has_sequence(ctx) == 1);

  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  uint16_t tw = 0, th = 0;
  REQUIRE(heif_track_get_image_resolution(track, &tw, &th).code == heif_error_Ok);
  REQUIRE(tw == WIDTH);
  REQUIRE(th == HEIGHT);

  // Decode the single frame to interleaved RGB. This exercises
  // Op_YCbCr420_to_RGB24, the sink of the out-of-bounds read. With the unfixed
  // library this aborts under AddressSanitizer; with the fix it must return a
  // correctly sized RGB image (and, thanks to the component-mapping fix, all
  // three YCbCr planes are present so the conversion no longer fails early).
  heif_image* img = nullptr;
  heif_error derr = heif_track_decode_next_image(track, &img,
                                                 heif_colorspace_RGB,
                                                 heif_chroma_interleaved_RGB,
                                                 nullptr);
  REQUIRE(derr.code == heif_error_Ok);
  REQUIRE(img != nullptr);

  REQUIRE(heif_image_get_width(img, heif_channel_interleaved) == WIDTH);
  REQUIRE(heif_image_get_height(img, heif_channel_interleaved) == HEIGHT);

  heif_image_release(img);
  heif_track_release(track);
  heif_context_free(ctx);
}
