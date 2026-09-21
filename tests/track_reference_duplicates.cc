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

// A track reference type box may list the same track ID more than once.
// ISO/IEC 14496-12 does not forbid it, no consumer in libheif depends on the
// entries being unique, and a redundant entry is no reason to reject a file.
// Box_tref used to refuse such a box on both the read and the write path
// ("'tref' has double references"), so calling heif_track_add_reference_to_track()
// twice with the same target made the whole file unwritable, and a file written
// by another muxer with a repeated ID could not be opened.
//
// The test uses URI metadata tracks with raw samples, so it needs no codec.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"

#include <cstdint>
#include <vector>

namespace {

heif_error mem_writer(heif_context*, const void* data, size_t size, void* userdata)
{
  auto* out = static_cast<std::vector<uint8_t>*>(userdata);
  const auto* p = static_cast<const uint8_t*>(data);
  out->insert(out->end(), p, p + size);
  return heif_error{heif_error_Ok, heif_suberror_Unspecified, nullptr};
}

// Add a metadata track with one raw sample so that the track is complete.
heif_track* add_metadata_track(heif_context* ctx, const char* uri)
{
  heif_track* track = nullptr;
  heif_error err = heif_context_add_uri_metadata_sequence_track(ctx, uri, nullptr, &track);
  INFO("add_uri_metadata_sequence_track: " << (err.message ? err.message : ""));
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(track != nullptr);

  const uint8_t payload[] = {1, 2, 3, 4};
  heif_raw_sequence_sample* sample = heif_raw_sequence_sample_alloc();
  REQUIRE(sample != nullptr);
  REQUIRE(heif_raw_sequence_sample_set_data(sample, payload, sizeof(payload)).code == heif_error_Ok);
  heif_raw_sequence_sample_set_duration(sample, 1);
  REQUIRE(heif_track_add_raw_sequence_sample(track, sample).code == heif_error_Ok);
  heif_raw_sequence_sample_release(sample);

  return track;
}

} // namespace


TEST_CASE("a track reference type box may list the same track twice")
{
  const uint32_t ref_type = heif_track_reference_type_description;

  // --- write a file in which track B references track A twice

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_track* track_a = add_metadata_track(ctx, "urn:test:a");
  heif_track* track_b = add_metadata_track(ctx, "urn:test:b");
  const uint32_t id_a = heif_track_get_id(track_a);
  const uint32_t id_b = heif_track_get_id(track_b);
  REQUIRE(id_a != id_b);

  heif_track_add_reference_to_track(track_b, ref_type, track_a);
  heif_track_add_reference_to_track(track_b, ref_type, track_a);

  std::vector<uint8_t> file;
  heif_writer writer{};
  writer.writer_api_version = 1;
  writer.write = mem_writer;
  heif_error err = heif_context_write(ctx, &writer, &file);
  INFO("write: " << (err.message ? err.message : ""));
  REQUIRE(err.code == heif_error_Ok);   // used to fail with "'tref' has double references"

  heif_track_release(track_a);
  heif_track_release(track_b);
  heif_context_free(ctx);

  // --- read it back

  ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  err = heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr);
  INFO("read: " << (err.message ? err.message : ""));
  REQUIRE(err.code == heif_error_Ok);   // used to fail while parsing the 'tref' box
  REQUIRE(heif_context_has_sequence(ctx) == 1);

  track_a = heif_context_get_track(ctx, id_a);
  track_b = heif_context_get_track(ctx, id_b);
  REQUIRE(track_a != nullptr);
  REQUIRE(track_b != nullptr);

  // Both entries are reported as written.
  REQUIRE(heif_track_get_number_of_track_reference_types(track_b) == 1);
  REQUIRE(heif_track_get_number_of_track_reference_of_type(track_b, ref_type) == 2);

  uint32_t targets[2] = {0, 0};
  REQUIRE(heif_track_get_references_from_track(track_b, ref_type, targets) == 2);
  REQUIRE(targets[0] == id_a);
  REQUIRE(targets[1] == id_a);

  // The reverse lookup lists the referring track once, not once per entry.
  uint32_t referring[4] = {0, 0, 0, 0};
  REQUIRE(heif_track_find_referring_tracks(track_a, ref_type, referring, 4) == 1);
  REQUIRE(referring[0] == id_b);

  REQUIRE(heif_track_get_number_of_track_reference_of_type(track_a, ref_type) == 0);

  heif_track_release(track_a);
  heif_track_release(track_b);
  heif_context_free(ctx);
}
