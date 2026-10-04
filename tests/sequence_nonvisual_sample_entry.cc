/*
  libheif regression test for GHSA-8p2c-9wv4-rx4p: a visual sequence track whose
  'stsd' carries a non-visual sample entry (e.g. an 'mp4a' audio entry) that a
  chunk is mapped to via 'stsc'. Such a chunk gets no decoder in Track::load().
  The decode loop guarded the decoder pointer only with assert(), so on an NDEBUG
  build this was a NULL-pointer dereference through a virtual call. The file must
  now be rejected with heif_error_Invalid_input at read time, not crash.

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

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "sequence_file_builder.h"

#include <cstdint>
#include <vector>

using namespace seqfile;

namespace {

// A minimal but structurally valid 'hvc1' VisualSampleEntry. The 78-byte visual
// header is followed by a minimal 'hvcC' configuration record (no NAL arrays),
// which is enough for Decoder::alloc_for_sequence_sample_description_box() to
// construct an HEVC decoder object. No codec plugin is needed: the file is
// rejected during Track::load(), before any decoding is attempted.
std::vector<uint8_t> make_hvc1_entry()
{
  std::vector<uint8_t> vse;
  for (int i = 0; i < 6; i++) vse.push_back(0); // SampleEntry reserved
  put16(vse, 1);                                // data_reference_index
  put16(vse, 0);                                // pre_defined
  put16(vse, 0);                                // reserved
  for (int i = 0; i < 3; i++) put32(vse, 0);    // pre_defined
  put16(vse, 4);                                // width
  put16(vse, 4);                                // height
  put32(vse, 0x00480000);                       // horizresolution 72 dpi
  put32(vse, 0x00480000);                       // vertresolution 72 dpi
  put32(vse, 0);                                // reserved
  put16(vse, 1);                                // frame_count
  for (int i = 0; i < 32; i++) vse.push_back(0);// compressorname (length-prefixed, empty)
  put16(vse, 0x0018);                           // depth
  put16(vse, 0xFFFF);                           // pre_defined (-1)

  // HEVCDecoderConfigurationRecord, 23 bytes, zero NAL arrays.
  std::vector<uint8_t> hvcc;
  hvcc.push_back(1);        // configuration_version
  hvcc.push_back(0);        // profile_space / tier / profile_idc
  put32(hvcc, 0);           // general_profile_compatibility_flags
  for (int i = 0; i < 6; i++) hvcc.push_back(0); // general_constraint_indicator_flags
  hvcc.push_back(0);        // general_level_idc
  put16(hvcc, 0);           // min_spatial_segmentation_idc
  hvcc.push_back(0);        // parallelism_type
  hvcc.push_back(1);        // chroma_format (4:2:0)
  hvcc.push_back(0);        // bit_depth_luma_minus8
  hvcc.push_back(0);        // bit_depth_chroma_minus8
  put16(hvcc, 0);           // avg_frame_rate
  hvcc.push_back(0);        // constant_frame_rate / temporal layers / length_size
  hvcc.push_back(0);        // num_of_arrays

  append(vse, box("hvcC", hvcc));
  return box("hvc1", vse);
}

// A non-visual sample entry of the given 4cc (a bare SampleEntry: 6 reserved
// bytes + data_reference_index). Any 4cc the box factory does not know becomes a
// generic Box, which does not cast to Box_VisualSampleEntry.
std::vector<uint8_t> make_nonvisual_entry(const char* type)
{
  std::vector<uint8_t> payload;
  for (int i = 0; i < 6; i++) payload.push_back(0);
  put16(payload, 1); // data_reference_index
  return box(type, payload);
}

// Build a 'pict' sequence with two samples in two chunks. The 'stsd' holds the
// hvc1 entry (index 1) plus 'second_entry' (index 2). Chunk 1 maps to sample
// description index 1; chunk 2 maps to 'chunk2_sdi'.
std::vector<uint8_t> build_two_entry_visual_sequence(const std::vector<uint8_t>& second_entry,
                                                     uint32_t chunk2_sdi)
{
  std::vector<uint8_t> ftyp_payload;
  put_fourcc(ftyp_payload, "msf1");
  put32(ftyp_payload, 0);
  put_fourcc(ftyp_payload, "msf1");
  put_fourcc(ftyp_payload, "iso8");
  std::vector<uint8_t> ftyp = box("ftyp", ftyp_payload);

  // mvhd (version 0)
  std::vector<uint8_t> mvhd_payload;
  put32(mvhd_payload, 0);        // version 0, flags 0
  put32(mvhd_payload, 0);        // creation_time
  put32(mvhd_payload, 0);        // modification_time
  put32(mvhd_payload, 1000);     // timescale
  put32(mvhd_payload, 2000);     // duration
  put32(mvhd_payload, 0x00010000);
  put16(mvhd_payload, 0x0100);
  put16(mvhd_payload, 0);
  put32(mvhd_payload, 0);
  put32(mvhd_payload, 0);
  for (int i = 0; i < 9; i++) put32(mvhd_payload, 0);
  for (int i = 0; i < 6; i++) put32(mvhd_payload, 0);
  put32(mvhd_payload, 2);        // next_track_ID
  std::vector<uint8_t> mvhd = box("mvhd", mvhd_payload);

  // tkhd (version 0)
  std::vector<uint8_t> tkhd_payload;
  put32(tkhd_payload, 0x00000007);
  put32(tkhd_payload, 0);
  put32(tkhd_payload, 0);
  put32(tkhd_payload, 1);        // track_ID
  put32(tkhd_payload, 0);
  put32(tkhd_payload, 2000);     // duration
  put32(tkhd_payload, 0);
  put32(tkhd_payload, 0);
  put16(tkhd_payload, 0);
  put16(tkhd_payload, 0);
  put16(tkhd_payload, 0);
  put16(tkhd_payload, 0);
  for (int i = 0; i < 9; i++) put32(tkhd_payload, 0);
  put32(tkhd_payload, 0x00040000); // width 4.0
  put32(tkhd_payload, 0x00040000); // height 4.0
  std::vector<uint8_t> tkhd = box("tkhd", tkhd_payload);

  // mdhd (version 0)
  std::vector<uint8_t> mdhd_payload;
  put32(mdhd_payload, 0);
  put32(mdhd_payload, 0);
  put32(mdhd_payload, 0);
  put32(mdhd_payload, 1000);     // timescale
  put32(mdhd_payload, 2000);     // duration
  put16(mdhd_payload, 0x55c4);   // language 'und'
  put16(mdhd_payload, 0);
  std::vector<uint8_t> mdhd = box("mdhd", mdhd_payload);

  // hdlr 'pict'
  std::vector<uint8_t> hdlr_payload;
  put32(hdlr_payload, 0);
  put32(hdlr_payload, 0);
  put_fourcc(hdlr_payload, "pict");
  put32(hdlr_payload, 0);
  put32(hdlr_payload, 0);
  put32(hdlr_payload, 0);
  hdlr_payload.push_back(0);
  std::vector<uint8_t> hdlr = box("hdlr", hdlr_payload);

  // vmhd
  std::vector<uint8_t> vmhd_payload;
  put32(vmhd_payload, 0x00000001);
  put16(vmhd_payload, 0);
  for (int i = 0; i < 3; i++) put16(vmhd_payload, 0);
  std::vector<uint8_t> vmhd = box("vmhd", vmhd_payload);

  // stsd with two entries
  std::vector<uint8_t> stsd_payload;
  put32(stsd_payload, 0);
  put32(stsd_payload, 2); // entry_count
  append(stsd_payload, make_hvc1_entry());
  append(stsd_payload, second_entry);
  std::vector<uint8_t> stsd = box("stsd", stsd_payload);

  // stts: two samples
  std::vector<uint8_t> stts_payload;
  put32(stts_payload, 0);
  put32(stts_payload, 1);
  put32(stts_payload, 2);     // sample_count
  put32(stts_payload, 1000);  // sample_delta
  std::vector<uint8_t> stts = box("stts", stts_payload);

  // stsc: chunk 1 -> sdi 1, chunk 2 -> chunk2_sdi
  std::vector<uint8_t> stsc_payload;
  put32(stsc_payload, 0);
  put32(stsc_payload, 2);
  put32(stsc_payload, 1); put32(stsc_payload, 1); put32(stsc_payload, 1);
  put32(stsc_payload, 2); put32(stsc_payload, 1); put32(stsc_payload, chunk2_sdi);
  std::vector<uint8_t> stsc = box("stsc", stsc_payload);

  // stsz: fixed size, two samples
  std::vector<uint8_t> stsz_payload;
  put32(stsz_payload, 0);
  put32(stsz_payload, 16);  // fixed sample size
  put32(stsz_payload, 2);   // sample_count
  std::vector<uint8_t> stsz = box("stsz", stsz_payload);

  // Assemble, then patch the two chunk offsets to point into the mdat payload.
  auto make_stco = [](uint32_t off0, uint32_t off1) {
    std::vector<uint8_t> p;
    put32(p, 0);
    put32(p, 2);
    put32(p, off0);
    put32(p, off1);
    return box("stco", p);
  };
  auto assemble = [&](const std::vector<uint8_t>& stco) {
    std::vector<uint8_t> stbl = box("stbl", concat({stsd, stts, stsc, stsz, stco}));
    std::vector<uint8_t> minf = box("minf", concat({vmhd, stbl}));
    std::vector<uint8_t> mdia = box("mdia", concat({mdhd, hdlr, minf}));
    std::vector<uint8_t> trak = box("trak", concat({tkhd, mdia}));
    return box("moov", concat({mvhd, trak}));
  };

  std::vector<uint8_t> moov0 = assemble(make_stco(0, 0));
  uint32_t mdat_payload_offset = static_cast<uint32_t>(ftyp.size() + moov0.size() + 8);
  std::vector<uint8_t> moov = assemble(make_stco(mdat_payload_offset, mdat_payload_offset + 16));

  std::vector<uint8_t> mdat = box("mdat", std::vector<uint8_t>(32, 0xA5));

  return concat({ftyp, moov, mdat});
}

heif_error read_sequence(const std::vector<uint8_t>& file)
{
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  heif_error err = heif_context_read_from_memory_without_copy(
      ctx, file.data(), file.size(), nullptr);
  heif_context_free(ctx);
  return err;
}

} // namespace

TEST_CASE("sequence with non-visual sample entry in chunk is rejected")
{
  // Chunk 2 maps to the second 'stsd' entry, which is not a VisualSampleEntry.
  // Before the fix this reached a decoder-less chunk and crashed (NULL deref on
  // an NDEBUG build, assert on a debug build). It must now be a clean error.
  SECTION("mp4a audio entry")
  {
    heif_error err = read_sequence(build_two_entry_visual_sequence(make_nonvisual_entry("mp4a"), 2));
    REQUIRE(err.code == heif_error_Invalid_input);
  }

  // The defect is codec-independent: any non-visual second entry triggers it.
  SECTION("unknown four-cc entry")
  {
    heif_error err = read_sequence(build_two_entry_visual_sequence(make_nonvisual_entry("zzzz"), 2));
    REQUIRE(err.code == heif_error_Invalid_input);
  }
}

TEST_CASE("sequence that maps every chunk to the visual entry still loads")
{
  // Same file, but chunk 2 maps to the visual 'hvc1' entry (index 1) instead of
  // the non-visual one. This is the matched control: it isolates the non-visual
  // entry as the cause. Both chunks get a decoder, so the track loads without
  // error (no frame is decoded here, so no codec plugin is required).
  heif_error err = read_sequence(build_two_entry_visual_sequence(make_nonvisual_entry("mp4a"), 1));
  REQUIRE(err.code == heif_error_Ok);
}
