/*
  libheif integration tests for the 'stsc' chunk lookup.

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

// Regression tests for GHSA-74mj-m37q-h3q9.
//
// Track::load() looks up the 'stsc' entry of every chunk. Box_stsc::get_chunk() scanned
// the table linearly, which made opening a file quadratic in its size. It is now a binary
// search, which requires 'first_chunk' values that do not decrease; parse() checks this.
//
// All chunks of the test files point to the start of the sample data, and sample i of
// the mdat payload holds the byte 0xA0 + (i & 0x0f). So the first byte of each raw sample
// is 0xA0 + its index within its chunk, which shows how the chunks were mapped to entries.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "sequence_file_builder.h"

#include <cstdint>
#include <vector>

using namespace seqfile;

namespace {

SequenceFileParams params_for(const std::vector<Stsc_entry>& entries, uint32_t num_chunks, uint32_t num_samples)
{
  SequenceFileParams p;
  p.mvhd_v1 = false;
  p.mvhd_duration = num_samples;
  p.mdhd_duration = num_samples;
  p.with_editlist = false;
  p.stts = {{num_samples, 1}};
  p.num_samples = num_samples;
  p.sample_size = 1;
  p.stsc_entries = entries;
  p.num_chunks = num_chunks;
  return p;
}

struct Context
{
  heif_context* ctx = heif_context_alloc();

  Context() = default;
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  ~Context() { heif_context_free(ctx); }
};

heif_error read(Context& c, const std::vector<uint8_t>& file)
{
  REQUIRE(c.ctx != nullptr);
  return heif_context_read_from_memory(c.ctx, file.data(), file.size(), nullptr);
}

// Index of each sample within its chunk, in sample order.
std::vector<int> positions_in_chunks(heif_context* ctx)
{
  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  std::vector<int> positions;
  for (;;) {
    heif_raw_sequence_sample* sample = nullptr;
    heif_error err = heif_track_get_next_raw_sequence_sample(track, &sample);
    if (err.code == heif_error_End_of_sequence) {
      break;
    }
    REQUIRE(err.code == heif_error_Ok);

    size_t size = 0;
    const uint8_t* data = heif_raw_sequence_sample_get_data(sample, &size);
    REQUIRE(size == 1);
    positions.push_back(data[0] - 0xA0);
    heif_raw_sequence_sample_release(sample);
  }

  heif_track_release(track);
  return positions;
}

} // namespace


TEST_CASE("'stsc' runs map chunks to the entry they start in")
{
  // chunks 1-2: 1 sample each, chunks 3-5: 2 samples each
  Context c;
  REQUIRE(read(c, build_sequence_file(params_for({{1, 1, 1}, {3, 2, 1}}, 5, 8))).code == heif_error_Ok);
  REQUIRE(positions_in_chunks(c.ctx) == std::vector<int>{0, 0, 0, 1, 0, 1, 0, 1});
}


TEST_CASE("'stsc' with one entry per chunk")
{
  // chunk k has (k % 3) + 1 samples
  std::vector<Stsc_entry> entries;
  std::vector<int> expected;
  uint32_t num_samples = 0;
  for (uint32_t k = 1; k <= 1000; k++) {
    uint32_t n = (k % 3) + 1;
    entries.push_back({k, n, 1});
    for (uint32_t j = 0; j < n; j++) expected.push_back(static_cast<int>(j));
    num_samples += n;
  }

  Context c;
  REQUIRE(read(c, build_sequence_file(params_for(entries, 1000, num_samples))).code == heif_error_Ok);
  REQUIRE(positions_in_chunks(c.ctx) == expected);
}


TEST_CASE("'stsc' entries with equal first_chunk: the last one of the run applies")
{
  // the entry with 5 samples per chunk describes an empty run
  Context c;
  REQUIRE(read(c, build_sequence_file(params_for({{1, 1, 1}, {3, 5, 1}, {3, 2, 1}}, 5, 8))).code == heif_error_Ok);
  REQUIRE(positions_in_chunks(c.ctx) == std::vector<int>{0, 0, 0, 1, 0, 1, 0, 1});
}


TEST_CASE("'stsc' with decreasing first_chunk is rejected")
{
  // The former linear scan accepted this table: it mapped chunks 1-3 to the first entry and
  // chunks 4-5 to the last one (7 samples), and never used the entry that starts at chunk 4.
  Context c;
  heif_error err = read(c, build_sequence_file(params_for({{1, 1, 1}, {4, 2, 1}, {3, 2, 1}}, 5, 7)));
  REQUIRE(err.code == heif_error_Invalid_input);
}
