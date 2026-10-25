/*
  libheif regression test: a visual sequence track whose samples contain no picture,
  combined with an edit list in "repeat" mode. The decoder accepts the samples, but it
  never outputs a frame. A single heif_track_decode_next_image() call then pushed the
  same samples again and again, up to max_sequence_frames times, before it gave up.

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
#include "libheif/heif_plugin.h"
#include "libheif/heif_sequences.h"
#include "sequence_file_builder.h"

#include <cstdint>
#include <limits>
#include <vector>

using namespace seqfile;

namespace {

// --- A mock HEVC decoder plugin, so that the test does not depend on the installed codecs
//     and can count how often data is pushed. It ignores the data. It outputs one frame for
//     each 'mock_output_divisor' pushed samples after an initial latency, or no frames at all.

const int no_output = -1;

int mock_latency = no_output;
int mock_output_divisor = 1;
int mock_output_multiplier = 1;
int mock_num_pushed = 0;
int mock_num_output = 0;
bool mock_flushed = false;

const heif_error mock_ok = {heif_error_Ok, heif_suberror_Unspecified, "Success"};

const char* mock_plugin_name() { return "mock decoder"; }

int mock_does_support_format(heif_compression_format format)
{
  return (format == heif_compression_HEVC || format == heif_compression_JPEG) ? 1000 : 0;
}

int mock_does_support_format2(const heif_decoder_plugin_compressed_format_description* desc)
{
  return mock_does_support_format(desc->format);
}

heif_error mock_new_decoder(void** decoder)
{
  *decoder = &mock_num_pushed; // any non-NULL pointer
  return mock_ok;
}

heif_error mock_new_decoder2(void** decoder, const heif_decoder_plugin_options*)
{
  return mock_new_decoder(decoder);
}

void mock_free_decoder(void*) {}

heif_error mock_push_data2(void*, const void*, size_t, uintptr_t)
{
  mock_num_pushed++;
  return mock_ok;
}

heif_error mock_push_data(void* decoder, const void* data, size_t size)
{
  return mock_push_data2(decoder, data, size, 0);
}

heif_error mock_flush_data(void*)
{
  mock_flushed = true;
  return mock_ok;
}

heif_error mock_decode_next_image2(void*, heif_image** out_img, uintptr_t* out_user_data, const heif_security_limits*)
{
  *out_img = nullptr;

  int latency = mock_flushed ? 0 : mock_latency;
  if (mock_latency == no_output || mock_num_pushed <= latency ||
      mock_num_output >= (mock_num_pushed - latency) * mock_output_multiplier / mock_output_divisor) {
    return mock_ok;
  }

  heif_error err = heif_image_create(4, 4, heif_colorspace_monochrome, heif_chroma_monochrome, out_img);
  if (err.code) {
    return err;
  }

  err = heif_image_add_plane(*out_img, heif_channel_Y, 4, 4, 8);
  if (err.code) {
    return err;
  }

  if (out_user_data) {
    *out_user_data = 0;
  }

  mock_num_output++;
  return mock_ok;
}

heif_error mock_decode_next_image(void* decoder, heif_image** out_img, const heif_security_limits* limits)
{
  return mock_decode_next_image2(decoder, out_img, nullptr, limits);
}

heif_error mock_decode_image(void* decoder, heif_image** out_img)
{
  return mock_decode_next_image2(decoder, out_img, nullptr, nullptr);
}

void mock_set_strict_decoding(void*, int) {}

const heif_decoder_plugin mock_decoder_plugin{
  5,
  mock_plugin_name,
  nullptr, // init_plugin
  nullptr, // deinit_plugin
  mock_does_support_format,
  mock_new_decoder,
  mock_free_decoder,
  mock_push_data,
  mock_decode_image,
  mock_set_strict_decoding,
  "mock",
  mock_decode_next_image,
  LIBHEIF_MAKE_VERSION(1, 21, 0),
  mock_does_support_format2,
  mock_new_decoder2,
  mock_push_data2,
  mock_flush_data,
  mock_decode_next_image2
};

void start_mock_decoder(int latency, int output_divisor = 1, int output_multiplier = 1)
{
  static bool registered = false;
  if (!registered) {
    REQUIRE(heif_register_decoder_plugin(&mock_decoder_plugin).code == heif_error_Ok);
    registered = true;
  }

  mock_latency = latency;
  mock_output_divisor = output_divisor;
  mock_output_multiplier = output_multiplier;
  mock_num_pushed = 0;
  mock_num_output = 0;
  mock_flushed = false;
}


// A minimal visual sample entry. For HEVC, include an hvcC box without parameter sets.
std::vector<uint8_t> make_visual_entry(const char* type, bool with_hevc_config)
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

  if (with_hevc_config) {
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
    hvcc.push_back(3);        // constant_frame_rate / temporal layers / length_size
    hvcc.push_back(0);        // num_of_arrays
    append(vse, box("hvcC", hvcc));
  }

  return box(type, vse);
}

// A 'pict' track with 'num_samples' samples that is repeated endlessly.
std::vector<uint8_t> build_endless_visual_sequence(uint32_t num_samples)
{
  SequenceFileParams p;
  p.mvhd_duration = std::numeric_limits<uint64_t>::max(); // indefinite sentinel
  p.mdhd_duration = num_samples;
  p.elst_segment_duration = num_samples;
  p.stts = {{num_samples, 1}};
  p.num_samples = num_samples;
  p.handler_type = "pict";
  p.sample_entry = make_visual_entry("hvc1", true);
  // A length-prefixed HEVC access unit containing only an AUD, no picture.
  p.sample_bytes = {0, 0, 0, 2, 0x46, 0x01};

  return build_sequence_file(p);
}

std::vector<uint8_t> build_endless_jpeg_sequence()
{
  SequenceFileParams p;
  p.mvhd_duration = std::numeric_limits<uint64_t>::max();
  p.mdhd_duration = 1;
  p.elst_segment_duration = 1;
  p.stts = {{1, 1}};
  p.handler_type = "pict";
  p.sample_entry = make_visual_entry("mjpg", false);
  // Enough JPEG SOF data for the coded-size check; the mock decoder ignores it.
  p.sample_bytes = {0xff, 0xd8, 0xff, 0xc0, 0x00, 0x0b, 0x08,
                    0x00, 0x04, 0x00, 0x04, 0x01, 0x01, 0x11,
                    0x00, 0xff, 0xd9};
  return build_sequence_file(p);
}

std::vector<uint8_t> build_endless_hevc_then_jpeg_sequence()
{
  SequenceFileParams p;
  p.mvhd_duration = std::numeric_limits<uint64_t>::max();
  p.mdhd_duration = 12;
  p.elst_segment_duration = 12;
  p.stts = {{12, 1}};
  p.num_samples = 12;
  p.first_chunk_num_samples = 10;
  p.handler_type = "pict";
  p.sample_entry = make_visual_entry("hvc1", true);
  p.second_sample_entry = make_visual_entry("mjpg", false);
  // Both sample descriptions use 17-byte samples because stsz has a fixed size.
  p.sample_bytes = {0, 0, 0, 13, 0x46, 0x01, 0, 0, 0,
                    0, 0, 0, 0, 0, 0, 0, 0};
  p.second_sample_bytes = {0xff, 0xd8, 0xff, 0xc0, 0x00, 0x0b, 0x08,
                           0x00, 0x04, 0x00, 0x04, 0x01, 0x01, 0x11,
                           0x00, 0xff, 0xd9};
  return build_sequence_file(p);
}

} // namespace


TEST_CASE("repeated samples without decoder output are not pushed endlessly")
{
  const uint32_t num_samples = 3;
  std::vector<uint8_t> file = build_endless_visual_sequence(num_samples);

  start_mock_decoder(no_output);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  // Lower the frame limit (default: 18 million) so that a regression fails quickly.
  heif_context_get_security_limits(ctx)->max_sequence_frames = 100000;

  heif_error err = heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);
  REQUIRE(heif_track_get_number_of_repetitions(track) ==
          heif_sequence_track_number_of_repetitions_infinite);

  heif_image* img = nullptr;
  err = heif_track_decode_next_image(track, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
  INFO("error: " << err.message << ", subcode: " << err.subcode << ", pushed: " << mock_num_pushed);
  REQUIRE(err.code == heif_error_Invalid_input);
  REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);
  REQUIRE(img == nullptr);

  // The decoder has seen all samples, but only a small number of repetitions.
  // Before the fix, this was max_sequence_frames.
  REQUIRE(mock_num_pushed == 32);

  // Asking again does not start over.
  int num_pushed_in_first_call = mock_num_pushed;
  err = heif_track_decode_next_image(track, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
  REQUIRE(err.code == heif_error_Invalid_input);
  REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);
  REQUIRE(mock_num_pushed == num_pushed_in_first_call);

  heif_track_release(track);
  heif_context_free(ctx);
}


TEST_CASE("JPEG sequence stops after one sample without output")
{
  std::vector<uint8_t> file = build_endless_jpeg_sequence();
  start_mock_decoder(no_output);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  REQUIRE(heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr).code == heif_error_Ok);
  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  heif_image* img = nullptr;
  heif_error err = heif_track_decode_next_image(track, &img, heif_colorspace_undefined,
                                                 heif_chroma_undefined, nullptr);
  REQUIRE(err.code == heif_error_Invalid_input);
  REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);
  REQUIRE(img == nullptr);
  REQUIRE(mock_num_pushed == 1);

  heif_track_release(track);
  heif_context_free(ctx);
}


TEST_CASE("HEVC debt does not exhaust the JPEG budget at a codec change")
{
  std::vector<uint8_t> file = build_endless_hevc_then_jpeg_sequence();
  start_mock_decoder(no_output);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  REQUIRE(heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr).code == heif_error_Ok);
  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  heif_image* img = nullptr;
  heif_error err = heif_track_decode_next_image(track, &img, heif_colorspace_undefined,
                                                 heif_chroma_undefined, nullptr);
  REQUIRE(err.code == heif_error_Invalid_input);
  REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);
  REQUIRE(img == nullptr);
  // Ten HEVC samples and one JPEG sample are accepted. The next JPEG sample
  // exceeds JPEG's own budget of one; HEVC's outstanding debt is not charged to it.
  REQUIRE(mock_num_pushed == 11);

  heif_track_release(track);
  heif_context_free(ctx);
}


TEST_CASE("decoder latency may exceed the length of the repeated segment")
{
  // The first frame comes out only after the single sample was repeated many times.
  std::vector<uint8_t> file = build_endless_visual_sequence(1);

  start_mock_decoder(24);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  for (int i = 0; i < 200; i++) {
    heif_image* img = nullptr;
    err = heif_track_decode_next_image(track, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
    INFO("error: " << err.message << ", subcode: " << err.subcode << ", pushed: " << mock_num_pushed);
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(img != nullptr);
    heif_image_release(img);
  }

  heif_track_release(track);
  heif_context_free(ctx);
}


TEST_CASE("one non-displayed sample per decoded frame stays within the work budget")
{
  std::vector<uint8_t> file = build_endless_visual_sequence(1);
  start_mock_decoder(0, 2);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  REQUIRE(heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr).code == heif_error_Ok);
  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  for (int i = 0; i < 300; i++) {
    heif_image* img = nullptr;
    heif_error err = heif_track_decode_next_image(track, &img, heif_colorspace_undefined,
                                                   heif_chroma_undefined, nullptr);
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(img != nullptr);
    heif_image_release(img);
  }

  REQUIRE(mock_num_pushed == 600);
  heif_track_release(track);
  heif_context_free(ctx);
}


TEST_CASE("extra output frames do not create credit for future samples")
{
  std::vector<uint8_t> file = build_endless_visual_sequence(1);
  start_mock_decoder(0, 1, 2);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  REQUIRE(heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr).code == heif_error_Ok);
  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  for (int i = 0; i < 200; i++) {
    heif_image* img = nullptr;
    heif_error err = heif_track_decode_next_image(track, &img, heif_colorspace_undefined,
                                                   heif_chroma_undefined, nullptr);
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(img != nullptr);
    heif_image_release(img);
  }

  REQUIRE(mock_num_pushed == 100);
  heif_track_release(track);
  heif_context_free(ctx);
}


TEST_CASE("more than one non-displayed sample per frame eventually exhausts the work budget")
{
  std::vector<uint8_t> file = build_endless_visual_sequence(1);
  start_mock_decoder(0, 3);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  REQUIRE(heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr).code == heif_error_Ok);
  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  heif_error err{};
  heif_image* img = nullptr;
  for (int i = 0; i < 300; i++) {
    err = heif_track_decode_next_image(track, &img, heif_colorspace_undefined,
                                       heif_chroma_undefined, nullptr);
    if (err.code != heif_error_Ok) {
      break;
    }
    REQUIRE(img != nullptr);
    heif_image_release(img);
    img = nullptr;
  }

  REQUIRE(err.code == heif_error_Invalid_input);
  REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);
  REQUIRE(img == nullptr);
  REQUIRE(mock_num_pushed <= 2 * mock_num_output + 32);

  int num_pushed_before_retry = mock_num_pushed;
  err = heif_track_decode_next_image(track, &img, heif_colorspace_undefined,
                                     heif_chroma_undefined, nullptr);
  REQUIRE(err.code == heif_error_Invalid_input);
  REQUIRE(mock_num_pushed == num_pushed_before_retry);

  heif_track_release(track);
  heif_context_free(ctx);
}


TEST_CASE("ignoring an edit list stops at the physical sample count")
{
  std::vector<uint8_t> file = build_endless_visual_sequence(3);
  start_mock_decoder(no_output);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  REQUIRE(heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr).code == heif_error_Ok);
  heif_track* track = heif_context_get_track(ctx, 0);
  REQUIRE(track != nullptr);

  heif_decoding_options* options = heif_decoding_options_alloc();
  REQUIRE(options != nullptr);
  options->ignore_sequence_editlist = 1;

  heif_image* img = nullptr;
  heif_error err = heif_track_decode_next_image(track, &img, heif_colorspace_undefined,
                                                 heif_chroma_undefined, options);
  REQUIRE(err.code == heif_error_Decoder_plugin_error);
  REQUIRE(img == nullptr);
  REQUIRE(mock_num_pushed == 3);

  heif_decoding_options_free(options);
  heif_track_release(track);
  heif_context_free(ctx);
}
