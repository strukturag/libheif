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

// A 'saiz' box may describe aux info for fewer samples than the track contains
// (only the opposite, more than the track, is rejected at load). Samples beyond
// the 'saiz' range carry no aux info of that type. SampleAuxInfoReader::get_sample_info()
// enforced that in its variable-size branch but not in the contiguous constant-size
// branch, where it computed an offset for the undescribed sample and read
// default_sample_info_size bytes from an unrelated file region (or failed on EOF).

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "sequence_file_builder.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace seqfile;

namespace {

struct LoadedTrack
{
  heif_context* ctx = nullptr;
  heif_track* track = nullptr;

  ~LoadedTrack()
  {
    if (track) heif_track_release(track);
    if (ctx) heif_context_free(ctx);
  }
};

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

heif_raw_sequence_sample* next_sample(heif_track* track)
{
  heif_raw_sequence_sample* sample = nullptr;
  heif_error err = heif_track_get_next_raw_sequence_sample(track, &sample);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(sample != nullptr);
  return sample;
}

} // namespace


TEST_CASE("samples beyond a short constant-size 'saiz' carry no content ID")
{
  // Two samples, but the 'saiz' describes only the first. The aux-info chunk in
  // 'mdat' therefore holds a single content ID, directly followed by the end of
  // the file. The second sample must read back as "no content ID", not as a read
  // of the bytes past the aux-info chunk.
  SequenceFileParams p;
  p.mvhd_v1 = false;
  p.mvhd_duration = 20;
  p.mdhd_duration = 20;
  p.with_editlist = false;
  p.stts = {{2, 10}};
  p.num_samples = 2;
  p.sample_size = 4;

  SampleAuxInfo aux;
  aux.type = "suid";
  aux.data = {'A', 0}; // utf8string "A"
  aux.sample_count = 1; // 'saiz' covers sample 0 only
  p.aux_infos = {aux};

  LoadedTrack t;
  load(build_sequence_file(p), t);

  heif_raw_sequence_sample* s0 = next_sample(t.track);
  const char* id0 = heif_raw_sequence_sample_get_gimi_sample_content_id(s0);
  REQUIRE(id0 != nullptr);
  REQUIRE(std::string(id0) == "A");
  heif_string_release(id0);
  heif_raw_sequence_sample_release(s0);

  // The second sample is not described by the 'saiz'. Before the fix this read
  // past the one-entry aux-info chunk and failed with heif_error_Invalid_input;
  // next_sample() requires a clean heif_error_Ok. Its content ID is empty.
  heif_raw_sequence_sample* s1 = next_sample(t.track);
  const char* id1 = heif_raw_sequence_sample_get_gimi_sample_content_id(s1);
  REQUIRE(id1 != nullptr);
  REQUIRE(std::string(id1).empty());
  heif_string_release(id1);
  heif_raw_sequence_sample_release(s1);

  heif_raw_sequence_sample* sample = nullptr;
  heif_error err = heif_track_get_next_raw_sequence_sample(t.track, &sample);
  REQUIRE(err.code == heif_error_End_of_sequence);
  REQUIRE(sample == nullptr);
}
