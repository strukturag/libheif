/*
  libheif integration tests for reading the URI of a 'urim' sample entry.

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

// Regression tests for GHSA-r9cg-w542-g7hj.
//
// heif_track_get_urim_sample_entry_uri_of_first_cluster() wrote *out_uri only on
// success. heif-dec -S and heif-info printed and released the output pointer even
// after an error, so a 'urim' sample entry without 'uri ' child box made them use
// an uninitialized pointer. The function now sets *out_uri to NULL on entry.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "sequence_file_builder.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace seqfile;

namespace {

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

// 'urim' sample entry with a 'uri ' child box holding the given URI.
std::vector<uint8_t> urim_with_uri(const char* uri)
{
  std::vector<uint8_t> uri_payload;
  put32(uri_payload, 0); // version, flags
  uri_payload.insert(uri_payload.end(), uri, uri + strlen(uri) + 1);

  std::vector<uint8_t> urim_payload;
  for (int i = 0; i < 6; i++) urim_payload.push_back(0); // SampleEntry reserved
  put16(urim_payload, 1);                                // data_reference_index
  append(urim_payload, box("uri ", uri_payload));
  return box("urim", urim_payload);
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

void load(const std::vector<uint8_t>& file, LoadedTrack& out)
{
  out.ctx = heif_context_alloc();
  REQUIRE(out.ctx != nullptr);

  heif_error err = heif_context_read_from_memory(out.ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  out.track = heif_context_get_track(out.ctx, 0);
  REQUIRE(out.track != nullptr);
  REQUIRE(heif_track_get_sample_entry_type_of_first_cluster(out.track) == heif_fourcc('u', 'r', 'i', 'm'));
}

} // namespace


TEST_CASE("'urim' without 'uri' box returns an error and a NULL URI")
{
  LoadedTrack t;
  load(build_sequence_file(base_params()), t); // default sample entry: 'urim' without children

  int dummy;
  const char* uri = reinterpret_cast<const char*>(&dummy); // must be overwritten
  heif_error err = heif_track_get_urim_sample_entry_uri_of_first_cluster(t.track, &uri);
  REQUIRE(err.code == heif_error_Invalid_input);
  REQUIRE(uri == nullptr);

  // releasing the NULL output, as a careless caller would, is harmless
  heif_string_release(uri);

  // a NULL output argument is tolerated
  err = heif_track_get_urim_sample_entry_uri_of_first_cluster(t.track, nullptr);
  REQUIRE(err.code == heif_error_Invalid_input);
}


TEST_CASE("'urim' with 'uri' box returns the URI")
{
  SequenceFileParams p = base_params();
  p.sample_entry = urim_with_uri("urn:example:test");

  LoadedTrack t;
  load(build_sequence_file(p), t);

  const char* uri = nullptr;
  heif_error err = heif_track_get_urim_sample_entry_uri_of_first_cluster(t.track, &uri);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(uri != nullptr);
  REQUIRE(strcmp(uri, "urn:example:test") == 0);
  heif_string_release(uri);
}
