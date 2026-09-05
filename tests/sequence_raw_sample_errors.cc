/*
  libheif integration tests for the error paths of the raw sequence sample API.

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

// Regression tests for GHSA-4rv4-953r-p24q.
//
// Track::get_next_sample_raw_data() allocated the heif_raw_sequence_sample, holding
// a full copy of the sample payload, before reading the sample auxiliary
// information ('saiz'/'saio'). Four error returns in that aux-info stage dropped
// the pointer: the Result<T*> error variant cannot carry it, so the caller never
// received anything it could release. A file with many tracks aliasing one payload
// range leaked that payload once per track and per call. The same C entry point
// also lacked exception_guard(), so a failing allocation of the file-controlled
// sample size aborted the host process instead of returning heif_error_out_of_memory.
//
// The leak itself is only visible under LeakSanitizer. These tests pin down the
// caller-visible contract on each of the four error paths (a clean heif_error,
// *out_sample untouched, the call repeatable) and exercise the success path so the
// payload move in the fix is covered. Run this binary under ASan/LSan to check the
// allocation side: before the fix, every repetition in expect_clean_failure()
// leaked one sample object plus one payload copy.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "libheif/heif_tai_timestamps.h"
#include "sequence_file_builder.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace seqfile;

namespace {

// One 'urim' metadata track with a single 64-byte sample and no edit list.
SequenceFileParams base_params()
{
  SequenceFileParams p;
  p.mvhd_v1 = false;
  p.mvhd_duration = 10;
  p.mdhd_duration = 10;
  p.with_editlist = false;
  p.stts = {{1, 10}};
  p.num_samples = 1;
  p.sample_size = 64;
  return p;
}

struct LoadedTrack
{
  heif_context* ctx = nullptr;
  heif_track* track = nullptr;

  LoadedTrack() = default;
  LoadedTrack(const LoadedTrack&) = delete;
  LoadedTrack& operator=(const LoadedTrack&) = delete;

  ~LoadedTrack()
  {
    if (track) heif_track_release(track);
    if (ctx) heif_context_free(ctx);
  }
};

// Read the file and fetch its first track. Loading must succeed: all the malformed
// aux-info variants below are only detected when the sample itself is read.
void load(const std::vector<uint8_t>& file, LoadedTrack& out)
{
  out.ctx = heif_context_alloc();
  REQUIRE(out.ctx != nullptr);

  heif_error err = heif_context_read_from_memory(out.ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(heif_context_has_sequence(out.ctx) == 1);

  out.track = heif_context_get_track(out.ctx, 0);
  REQUIRE(out.track != nullptr);
}

// Every call on a malformed file must fail with a clean error and leave the output
// pointer untouched. The sample index does not advance on error, so repeating the
// call exercises the same path again (and, before the fix, leaked again).
void expect_clean_failure(heif_track* track, int repetitions = 20)
{
  for (int i = 0; i < repetitions; i++) {
    heif_raw_sequence_sample* sample = nullptr;
    heif_error err = heif_track_get_next_raw_sequence_sample(track, &sample);
    REQUIRE(err.code == heif_error_Invalid_input);
    REQUIRE(sample == nullptr);
  }
}

} // namespace


TEST_CASE("raw sample with non-terminated 'suid' content ID fails cleanly")
{
  SequenceFileParams p = base_params();
  p.aux_infos = {{"suid", {'A'}}}; // utf8string without NUL terminator

  LoadedTrack t;
  load(build_sequence_file(p), t);
  expect_clean_failure(t.track);
}


TEST_CASE("raw sample with embedded NUL in 'suid' content ID fails cleanly")
{
  SequenceFileParams p = base_params();
  p.aux_infos = {{"suid", {'A', 0, 'B', 0}}};

  LoadedTrack t;
  load(build_sequence_file(p), t);
  expect_clean_failure(t.track);
}


TEST_CASE("raw sample with 'saio' offset past end of file fails cleanly")
{
  SequenceFileParams p = base_params();
  SampleAuxInfo aux;
  aux.type = "suid";
  aux.data = {'A', 0};
  aux.offset_past_eof = true;
  p.aux_infos = {aux};

  LoadedTrack t;
  load(build_sequence_file(p), t);
  expect_clean_failure(t.track);
}


TEST_CASE("raw sample with wrong-sized 'stai' TAI timestamp fails cleanly")
{
  SequenceFileParams p = base_params();
  p.aux_infos = {{"stai", {1, 2, 3, 4, 5}}}; // a TAI timestamp payload must be exactly 9 bytes

  LoadedTrack t;
  load(build_sequence_file(p), t);
  expect_clean_failure(t.track);
}


TEST_CASE("raw sample with well-formed 'suid' and 'stai' aux info succeeds")
{
  // Control: the same file structure with valid aux-info payloads must deliver the
  // sample together with its content ID and TAI timestamp. This also covers the
  // payload move in Track::get_next_sample_raw_data(): the sample bytes must arrive
  // intact.
  SequenceFileParams p = base_params();
  p.aux_infos = {
      {"suid", {'u', 'r', 'n', ':', 'x', 0}},
      {"stai", {0x00, 0x00, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78, 0x00}}
  };

  LoadedTrack t;
  load(build_sequence_file(p), t);

  heif_raw_sequence_sample* sample = nullptr;
  heif_error err = heif_track_get_next_raw_sequence_sample(t.track, &sample);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(sample != nullptr);

  size_t size = 0;
  const uint8_t* data = heif_raw_sequence_sample_get_data(sample, &size);
  REQUIRE(data != nullptr);
  REQUIRE(size == 64);
  for (size_t i = 0; i < size; i++) {
    REQUIRE(data[i] == static_cast<uint8_t>(0xA0 + (i & 0x0f))); // pattern written by the builder
  }
  REQUIRE(heif_raw_sequence_sample_get_duration(sample) == 10);

  const char* content_id = heif_raw_sequence_sample_get_gimi_sample_content_id(sample);
  REQUIRE(content_id != nullptr);
  REQUIRE(std::string(content_id) == "urn:x");
  heif_string_release(content_id);

  REQUIRE(heif_raw_sequence_sample_has_tai_timestamp(sample) == 1);
  const heif_tai_timestamp_packet* tai = heif_raw_sequence_sample_get_tai_timestamp(sample);
  REQUIRE(tai != nullptr);
  REQUIRE(tai->tai_timestamp == 0x12345678);

  heif_raw_sequence_sample_release(sample);

  sample = nullptr;
  err = heif_track_get_next_raw_sequence_sample(t.track, &sample);
  REQUIRE(err.code == heif_error_End_of_sequence);
  REQUIRE(sample == nullptr);
}


TEST_CASE("oversized raw sample is rejected with a heif_error")
{
  // The sample buffer is sized from the file's 'stsz'. A size above
  // max_memory_block_size must come back as a heif_error through the C API. (The
  // entry point is additionally wrapped in exception_guard() so that a failing
  // allocation below that limit is reported as heif_error_out_of_memory instead of
  // aborting the process; that path needs an rlimit and is not unit-tested here.)
  SequenceFileParams p = base_params();
  p.sample_size = 4096;

  LoadedTrack t;
  load(build_sequence_file(p), t);

  // Tighten the limit only after loading so that the file read itself is unaffected.
  heif_security_limits* limits = heif_context_get_security_limits(t.ctx);
  limits->max_memory_block_size = 1024;

  heif_raw_sequence_sample* sample = nullptr;
  heif_error err = heif_track_get_next_raw_sequence_sample(t.track, &sample);
  REQUIRE(err.code == heif_error_Memory_allocation_error);
  REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);
  REQUIRE(sample == nullptr);
}
