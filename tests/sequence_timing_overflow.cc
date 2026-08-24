/*
  libheif integration tests for sequence sample-timing overflow.

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

// Regression tests for GHSA-xw34-mjcp-jqh8: an ISOBMFF edit list in "repeat"
// mode combined with the movie-header duration amplifies a single physical
// sample into an astronomically large *logical* output count. Before the fix,
// Track::init_sample_timing_table() left m_num_output_samples (uint64_t) at that
// value while the decode/raw-output loops count with a uint32_t, so
// end_of_sequence_reached() could never become true and decoding never
// terminated (variants V4/V5). The physical-sample security limit
// (max_sequence_frames) was also never applied to the amplified logical count
// (variant V6).
//
// These tests build minimal HEIF sequence files by hand. A 'urim' metadata
// track is used so the raw-sample API path exercises the timing table without
// needing any codec plugin.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"

#include <cstdint>
#include <limits>
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

void put64(std::vector<uint8_t>& v, uint64_t x)
{
  for (int i = 7; i >= 0; i--) {
    v.push_back(static_cast<uint8_t>(x >> (i * 8)));
  }
}

void put_fourcc(std::vector<uint8_t>& v, const char* s)
{
  v.push_back(static_cast<uint8_t>(s[0]));
  v.push_back(static_cast<uint8_t>(s[1]));
  v.push_back(static_cast<uint8_t>(s[2]));
  v.push_back(static_cast<uint8_t>(s[3]));
}

// Wrap a payload in a box: [uint32 size][fourcc type][payload].
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

struct Stts_entry
{
  uint32_t sample_count;
  uint32_t sample_delta;
};

struct SequenceFileParams
{
  bool     mvhd_v1 = true;
  uint64_t mvhd_duration = std::numeric_limits<uint64_t>::max(); // indefinite sentinel
  uint32_t timescale = 1000;

  uint64_t mdhd_duration = 1;

  bool     with_editlist = true;
  uint64_t elst_segment_duration = 1; // must equal mdhd_duration to match the repeat pattern

  std::vector<Stts_entry> stts = {{1, 1}};

  uint32_t num_samples = 1;
  uint32_t sample_size = 1;
};

// Build a minimal, structurally valid HEIF sequence file containing a single
// 'urim' metadata track, following the box tree that Track::load() requires.
std::vector<uint8_t> build_sequence_file(const SequenceFileParams& p)
{
  // --- ftyp
  std::vector<uint8_t> ftyp_payload;
  put_fourcc(ftyp_payload, "msf1"); // major brand: HEIF image sequence
  put32(ftyp_payload, 0);           // minor version
  put_fourcc(ftyp_payload, "msf1");
  put_fourcc(ftyp_payload, "isom");
  std::vector<uint8_t> ftyp = box("ftyp", ftyp_payload);

  // --- mvhd
  std::vector<uint8_t> mvhd_payload;
  if (p.mvhd_v1) {
    put32(mvhd_payload, 0x01000000); // version 1, flags 0
    put64(mvhd_payload, 0);          // creation_time
    put64(mvhd_payload, 0);          // modification_time
    put32(mvhd_payload, p.timescale);
    put64(mvhd_payload, p.mvhd_duration);
  }
  else {
    put32(mvhd_payload, 0x00000000); // version 0, flags 0
    put32(mvhd_payload, 0);          // creation_time
    put32(mvhd_payload, 0);          // modification_time
    put32(mvhd_payload, p.timescale);
    put32(mvhd_payload, static_cast<uint32_t>(p.mvhd_duration));
  }
  put32(mvhd_payload, 0x00010000); // rate 1.0
  put16(mvhd_payload, 0x0100);     // volume
  put16(mvhd_payload, 0);          // reserved
  put32(mvhd_payload, 0);          // reserved
  put32(mvhd_payload, 0);          // reserved
  for (int i = 0; i < 9; i++) put32(mvhd_payload, 0); // matrix
  for (int i = 0; i < 6; i++) put32(mvhd_payload, 0); // pre_defined
  put32(mvhd_payload, 2);          // next_track_ID
  std::vector<uint8_t> mvhd = box("mvhd", mvhd_payload);

  // --- tkhd (version 1)
  std::vector<uint8_t> tkhd_payload;
  put32(tkhd_payload, 0x01000007); // version 1, flags = enabled|in_movie|in_preview
  put64(tkhd_payload, 0);          // creation_time
  put64(tkhd_payload, 0);          // modification_time
  put32(tkhd_payload, 1);          // track_ID
  put32(tkhd_payload, 0);          // reserved
  put64(tkhd_payload, 0);          // duration
  put64(tkhd_payload, 0);          // reserved
  put16(tkhd_payload, 0);          // layer
  put16(tkhd_payload, 0);          // alternate_group
  put16(tkhd_payload, 0);          // volume
  put16(tkhd_payload, 0);          // reserved
  for (int i = 0; i < 9; i++) put32(tkhd_payload, 0); // matrix
  put32(tkhd_payload, 0);          // width
  put32(tkhd_payload, 0);          // height
  std::vector<uint8_t> tkhd = box("tkhd", tkhd_payload);

  // --- edts / elst (version 1, flags = repeat)
  std::vector<uint8_t> edts;
  if (p.with_editlist) {
    std::vector<uint8_t> elst_payload;
    put32(elst_payload, 0x01000001); // version 1, flags = Repeat_EditList
    put32(elst_payload, 1);          // entry_count
    put64(elst_payload, p.elst_segment_duration);
    put64(elst_payload, 0);          // media_time
    put16(elst_payload, 1);          // media_rate_integer
    put16(elst_payload, 0);          // media_rate_fraction
    edts = box("edts", box("elst", elst_payload));
  }

  // --- mdhd (version 0)
  std::vector<uint8_t> mdhd_payload;
  put32(mdhd_payload, 0x00000000); // version 0, flags 0
  put32(mdhd_payload, 0);          // creation_time
  put32(mdhd_payload, 0);          // modification_time
  put32(mdhd_payload, p.timescale);
  put32(mdhd_payload, static_cast<uint32_t>(p.mdhd_duration));
  put16(mdhd_payload, 0x55c4);     // language ('und')
  put16(mdhd_payload, 0);          // pre_defined
  std::vector<uint8_t> mdhd = box("mdhd", mdhd_payload);

  // --- hdlr (handler type 'meta')
  std::vector<uint8_t> hdlr_payload;
  put32(hdlr_payload, 0x00000000); // version 0, flags 0
  put32(hdlr_payload, 0);          // pre_defined
  put_fourcc(hdlr_payload, "meta");
  put32(hdlr_payload, 0);          // reserved
  put32(hdlr_payload, 0);          // reserved
  put32(hdlr_payload, 0);          // reserved
  hdlr_payload.push_back(0);       // name (empty, null-terminated)
  std::vector<uint8_t> hdlr = box("hdlr", hdlr_payload);

  // --- nmhd (null media header for metadata tracks)
  std::vector<uint8_t> nmhd_payload;
  put32(nmhd_payload, 0x00000000); // version 0, flags 0
  std::vector<uint8_t> nmhd = box("nmhd", nmhd_payload);

  // --- stsd with a single 'urim' (URI meta) sample entry
  std::vector<uint8_t> urim_payload;
  for (int i = 0; i < 6; i++) urim_payload.push_back(0); // SampleEntry reserved
  put16(urim_payload, 1);                                // data_reference_index
  std::vector<uint8_t> urim = box("urim", urim_payload);

  std::vector<uint8_t> stsd_payload;
  put32(stsd_payload, 0x00000000); // version 0, flags 0
  put32(stsd_payload, 1);          // entry_count
  stsd_payload.insert(stsd_payload.end(), urim.begin(), urim.end());
  std::vector<uint8_t> stsd = box("stsd", stsd_payload);

  // --- stts
  std::vector<uint8_t> stts_payload;
  put32(stts_payload, 0x00000000); // version 0, flags 0
  put32(stts_payload, static_cast<uint32_t>(p.stts.size()));
  for (const auto& e : p.stts) {
    put32(stts_payload, e.sample_count);
    put32(stts_payload, e.sample_delta);
  }
  std::vector<uint8_t> stts = box("stts", stts_payload);

  // --- stsc: all samples in a single chunk
  std::vector<uint8_t> stsc_payload;
  put32(stsc_payload, 0x00000000); // version 0, flags 0
  put32(stsc_payload, 1);          // entry_count
  put32(stsc_payload, 1);          // first_chunk
  put32(stsc_payload, p.num_samples); // samples_per_chunk
  put32(stsc_payload, 1);          // sample_description_index
  std::vector<uint8_t> stsc = box("stsc", stsc_payload);

  // --- stsz: fixed sample size
  std::vector<uint8_t> stsz_payload;
  put32(stsz_payload, 0x00000000);  // version 0, flags 0
  put32(stsz_payload, p.sample_size); // fixed sample size (non-zero -> no per-sample array)
  put32(stsz_payload, p.num_samples); // sample_count
  std::vector<uint8_t> stsz = box("stsz", stsz_payload);

  // --- stco: single chunk offset, patched below to point at the mdat payload
  auto make_stco = [](uint32_t offset) {
    std::vector<uint8_t> stco_payload;
    put32(stco_payload, 0x00000000); // version 0, flags 0
    put32(stco_payload, 1);          // entry_count
    put32(stco_payload, offset);     // chunk offset
    return box("stco", stco_payload);
  };

  auto assemble = [&](const std::vector<uint8_t>& stco) {
    std::vector<uint8_t> stbl = box("stbl", concat({stsd, stts, stsc, stsz, stco}));
    std::vector<uint8_t> minf = box("minf", concat({nmhd, stbl}));
    std::vector<uint8_t> mdia = box("mdia", concat({mdhd, hdlr, minf}));
    std::vector<uint8_t> trak = box("trak", concat({tkhd, edts, mdia}));
    std::vector<uint8_t> moov = box("moov", concat({mvhd, trak}));
    return moov;
  };

  // Box sizes are independent of the offset value, so we can size the file with a
  // placeholder, then patch the real offset in one pass.
  std::vector<uint8_t> moov0 = assemble(make_stco(0));
  uint32_t mdat_payload_offset = static_cast<uint32_t>(ftyp.size() + moov0.size() + 8 /* mdat header */);
  std::vector<uint8_t> moov = assemble(make_stco(mdat_payload_offset));

  // --- mdat: num_samples * sample_size bytes of arbitrary content
  std::vector<uint8_t> mdat_payload(static_cast<size_t>(p.num_samples) * p.sample_size);
  for (size_t i = 0; i < mdat_payload.size(); i++) {
    mdat_payload[i] = static_cast<uint8_t>(0xA0 + (i & 0x0f));
  }
  std::vector<uint8_t> mdat = box("mdat", mdat_payload);

  return concat({ftyp, moov, mdat});
}

} // namespace


TEST_CASE("repeat edit list with indefinite duration terminates")
{
  // A single physical sample plus an indefinite-duration repeat edit list.
  // Before the fix this inflated m_num_output_samples to ~UINT64_MAX, and the
  // uint32_t output counter could never reach it -> non-terminating loop.
  SequenceFileParams p;
  p.mvhd_v1 = true;
  p.mvhd_duration = std::numeric_limits<uint64_t>::max(); // indefinite sentinel
  p.mdhd_duration = 1;
  p.with_editlist = true;
  p.elst_segment_duration = 1;
  p.stts = {{1, 1}};
  p.num_samples = 1;

  std::vector<uint8_t> file = build_sequence_file(p);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  // Lower the frame limit so the (now bounded) output count is small and the test
  // runs quickly. This is exactly the limit that must cap the repeat-amplified
  // logical sample count.
  heif_security_limits* limits = heif_context_get_security_limits(ctx);
  const uint32_t kFrameLimit = 100;
  limits->max_sequence_frames = kFrameLimit;

  heif_error err = heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(heif_context_has_sequence(ctx) == 1);

  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  // The file still advertises "infinite" repetition to the caller...
  REQUIRE(heif_track_get_number_of_repetitions(track) ==
          heif_sequence_track_number_of_repetitions_infinite);

  // ...but iterating the raw samples must terminate. It is bounded by the
  // frame limit. Cap the loop well above that so a regression fails (rather than
  // hanging): with the bug, End_of_sequence is never reached.
  const int kIterationCap = 100000;
  int count = 0;
  bool reached_end = false;
  for (; count < kIterationCap; count++) {
    heif_raw_sequence_sample* sample = nullptr;
    heif_error serr = heif_track_get_next_raw_sequence_sample(track, &sample);
    if (serr.code == heif_error_End_of_sequence) {
      reached_end = true;
      break;
    }
    REQUIRE(serr.code == heif_error_Ok);
    REQUIRE(sample != nullptr);
    heif_raw_sequence_sample_release(sample);
  }

  REQUIRE(reached_end);
  REQUIRE(count == static_cast<int>(kFrameLimit));

  heif_track_release(track);
  heif_context_free(ctx);
}


TEST_CASE("repeat edit list with finite over-uint32 multiplier terminates")
{
  // Non-sentinel duration whose multiplier exceeds UINT32_MAX (variant V5): the
  // repeat is finite but the logical count overflows the uint32_t output counter.
  SequenceFileParams p;
  p.mvhd_v1 = true;
  p.mvhd_duration = (uint64_t{1} << 32) + 1; // 0x100000001, > UINT32_MAX, not the sentinel
  p.mdhd_duration = 1;
  p.with_editlist = true;
  p.elst_segment_duration = 1;
  p.stts = {{1, 1}};
  p.num_samples = 1;

  std::vector<uint8_t> file = build_sequence_file(p);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_security_limits* limits = heif_context_get_security_limits(ctx);
  const uint32_t kFrameLimit = 50;
  limits->max_sequence_frames = kFrameLimit;

  heif_error err = heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  const int kIterationCap = 100000;
  int count = 0;
  bool reached_end = false;
  for (; count < kIterationCap; count++) {
    heif_raw_sequence_sample* sample = nullptr;
    heif_error serr = heif_track_get_next_raw_sequence_sample(track, &sample);
    if (serr.code == heif_error_End_of_sequence) {
      reached_end = true;
      break;
    }
    REQUIRE(serr.code == heif_error_Ok);
    heif_raw_sequence_sample_release(sample);
  }

  REQUIRE(reached_end);
  REQUIRE(count == static_cast<int>(kFrameLimit));

  heif_track_release(track);
  heif_context_free(ctx);
}


TEST_CASE("multi-entry stts yields correct per-sample durations")
{
  // Guards the prefix-sum + binary-search rewrite of Box_stts::get_sample_duration():
  // the run-length coded 'stts' must resolve to the same per-sample durations as the
  // original linear scan, including across entry boundaries. Uses a plain track (no
  // edit list -> single playback).
  SequenceFileParams p;
  p.mvhd_v1 = false;
  p.mvhd_duration = 6;
  p.mdhd_duration = 6;
  p.with_editlist = false;
  p.stts = {{2, 10}, {1, 20}, {3, 30}}; // durations: 10, 10, 20, 30, 30, 30
  p.num_samples = 6;

  std::vector<uint8_t> file = build_sequence_file(p);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  std::vector<uint32_t> expected_durations = {10, 10, 20, 30, 30, 30};
  std::vector<uint32_t> got_durations;

  for (int i = 0; i < 20; i++) {
    heif_raw_sequence_sample* sample = nullptr;
    heif_error serr = heif_track_get_next_raw_sequence_sample(track, &sample);
    if (serr.code == heif_error_End_of_sequence) {
      break;
    }
    REQUIRE(serr.code == heif_error_Ok);
    got_durations.push_back(heif_raw_sequence_sample_get_duration(sample));
    heif_raw_sequence_sample_release(sample);
  }

  REQUIRE(got_durations == expected_durations);

  heif_track_release(track);
  heif_context_free(ctx);
}


TEST_CASE("per-track timeline/chunk memory is bounded by max_total_memory")
{
  // A tiny file that declares a large sample count via a single run-length 'stts'
  // entry plus a fixed_sample_size 'stsz'. The per-track presentation timeline
  // (~48 B/sample) and the chunk sample-range table (~16 B/sample) must be accounted
  // against max_total_memory. Before the fix they bypassed MemoryHandle, so a small
  // file could force hundreds of MB per track undetected (GHSA-xw34-mjcp-jqh8, V2/V3).
  const uint32_t N = 500000; // ~24 MB timeline + ~8 MB chunk ranges; file stays tiny

  SequenceFileParams p;
  p.mvhd_v1 = false;
  p.mvhd_duration = N;
  p.mdhd_duration = N;
  p.with_editlist = false;
  p.stts = {{N, 1}}; // one run-length entry describes all N samples
  p.num_samples = N;
  p.sample_size = 1;

  std::vector<uint8_t> file = build_sequence_file(p);

  // With ample memory the file is structurally valid and reads fine.
  {
    heif_context* ctx = heif_context_alloc();
    REQUIRE(ctx != nullptr);
    heif_error err = heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr);
    REQUIRE(err.code == heif_error_Ok);
    heif_context_free(ctx);
  }

  // With a small max_total_memory the per-track tables must trip the limit. The
  // 'stts'/'stsz'/'stsc'/'stco' tables are all tiny here, so the only allocation
  // large enough to exceed 4 MB is one of the newly-tracked per-track tables.
  {
    heif_context* ctx = heif_context_alloc();
    REQUIRE(ctx != nullptr);

    heif_security_limits* limits = heif_context_get_security_limits(ctx);
    limits->max_total_memory = 4 * 1024 * 1024; // 4 MB, far below the ~32 MB needed

    heif_error err = heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr);
    REQUIRE(err.code == heif_error_Memory_allocation_error);
    REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);

    heif_context_free(ctx);
  }
}
