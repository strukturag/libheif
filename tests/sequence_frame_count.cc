/*
  libheif integration tests: number of frames in a sequence track.

  MIT License

  Copyright (c) 2026 James Gilbertson

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

constexpr uint32_t num_samples = 5;

std::vector<uint8_t> sequence_file(uint32_t repetitions)
{
  SequenceFileParams p;
  p.num_samples = num_samples;
  p.stts = {{num_samples, 1}};
  p.mdhd_duration = num_samples;

  p.with_editlist = (repetitions > 1);
  p.elst_segment_duration = p.mdhd_duration;
  p.mvhd_duration = p.mdhd_duration * repetitions;
  return build_sequence_file(p);
}

void require_counts_match_samples_read(uint32_t repetitions)
{
  auto file = sequence_file(repetitions);

  auto ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  REQUIRE(heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr).code == heif_error_Ok);

  auto track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  // the counts come from the track's metadata, so check them before reading any samples
  REQUIRE(heif_track_get_number_of_frames(track) == num_samples);
  REQUIRE(heif_track_get_number_of_repetitions(track) == repetitions);

  uint32_t samples_read = 0;
  heif_raw_sequence_sample* sample;
  while (heif_track_get_next_raw_sequence_sample(track, &sample).code == heif_error_Ok) {
    heif_raw_sequence_sample_release(sample);
    samples_read++;
  }

  REQUIRE(samples_read == num_samples * repetitions);

  heif_track_release(track);
  heif_context_free(ctx);
}

} // namespace


TEST_CASE("frame count of a track without repetition")
{
  require_counts_match_samples_read(1);
}


TEST_CASE("frame count excludes repetition")
{
  require_counts_match_samples_read(3);
}
