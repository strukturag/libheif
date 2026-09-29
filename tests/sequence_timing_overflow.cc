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
// These tests use the hand-built sequence files from sequence_file_builder.h. A 'urim' metadata
// track is used so the raw-sample API path exercises the timing table without
// needing any codec plugin.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "sequence_file_builder.h"

#include <cstdint>
#include <limits>
#include <vector>

using namespace seqfile;


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
