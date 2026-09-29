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

// An encoder is opened once per sequence and configured from the first frame:
// Encoder_HEVC / Encoder_AVC / Encoder_AVIF / Encoder_VVC::encode_sequence_frame()
// call the plugin's start_sequence_encoding() only while the encoder is not
// running yet. Every later frame goes straight to encode_sequence_frame(), and
// nothing in the sequence API requires the frames to agree on a bit depth.
//
// The plugins latch the depth at that point. x265 is the worst case: it hands
// libx265 the plane pointers of the current frame together with a pic->bitDepth
// taken from the first one, so a 10 bit first frame followed by an 8 bit frame
// made libx265 walk a one byte per sample plane at two bytes per sample. That is
// a heap out-of-bounds read, and it reproduces under valgrind on the unfixed code
// as an "Invalid read" inside x265_10bit::Encoder::encode(). aom refused such a
// frame with an error of its own that named no cause, and rav1e and SVT-AV1
// quietly encoded the mismatch.
//
// The fix is check_sequence_frame_bit_depth() in the plugins, so every frame of
// a sequence has to carry the depth the encoder was opened with. This test pins
// both directions of the mismatch, and that a uniform sequence still encodes.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "test_utils.h"

#include <cstdint>
#include <cstring>

namespace {

// Large enough that the over-read of the unfixed code runs past the end of the
// plane allocation instead of staying inside its stride padding.
constexpr uint32_t WIDTH = 512;
constexpr uint32_t HEIGHT = 512;

heif_image* make_image(int bit_depth)
{
  heif_image* img = nullptr;
  REQUIRE(heif_image_create(WIDTH, HEIGHT, heif_colorspace_YCbCr, heif_chroma_420, &img).code == heif_error_Ok);
  REQUIRE(img != nullptr);

  REQUIRE(heif_image_add_plane(img, heif_channel_Y, WIDTH, HEIGHT, bit_depth).code == heif_error_Ok);
  REQUIRE(heif_image_add_plane(img, heif_channel_Cb, WIDTH / 2, HEIGHT / 2, bit_depth).code == heif_error_Ok);
  REQUIRE(heif_image_add_plane(img, heif_channel_Cr, WIDTH / 2, HEIGHT / 2, bit_depth).code == heif_error_Ok);

  for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}) {
    uint32_t h = (channel == heif_channel_Y) ? HEIGHT : HEIGHT / 2;
    uint32_t w = (channel == heif_channel_Y) ? WIDTH : WIDTH / 2;

    size_t stride = 0;
    uint8_t* p = heif_image_get_plane2(img, channel, &stride);
    REQUIRE(p != nullptr);

    // Mid grey, so that a frame read at the wrong sample width is visibly wrong
    // rather than accidentally plausible.
    uint16_t value = static_cast<uint16_t>(1 << (bit_depth - 1));

    for (uint32_t y = 0; y < h; y++) {
      if (bit_depth > 8) {
        auto* row = reinterpret_cast<uint16_t*>(p + y * stride);
        for (uint32_t x = 0; x < w; x++) {
          row[x] = value;
        }
      }
      else {
        memset(p + y * stride, static_cast<int>(value), w);
      }
    }
  }

  heif_image_set_duration(img, 1);

  return img;
}

struct TwoFrameResult
{
  // False when this build cannot encode the first depth at all (a libx265 without
  // high bit depth support, for instance). The sequence then never reached a state
  // in which two frames could disagree, and there is nothing to assert.
  bool first_frame_encoded = false;

  heif_error second_frame_error = heif_error{heif_error_Ok, heif_suberror_Unspecified, nullptr};
};

// Encode 'first_depth' and then 'second_depth' into one track and report what the
// second frame returned.
TwoFrameResult encode_two_frames(heif_compression_format format, int first_depth, int second_depth)
{
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_encoder* encoder = nullptr;
  REQUIRE(heif_context_get_encoder_for_format(ctx, format, &encoder).code == heif_error_Ok);
  REQUIRE(encoder != nullptr);

  heif_track* track = nullptr;
  REQUIRE(heif_context_add_visual_sequence_track(ctx, static_cast<uint16_t>(WIDTH), static_cast<uint16_t>(HEIGHT),
                                                 heif_track_type_video, nullptr, nullptr, &track).code == heif_error_Ok);
  REQUIRE(track != nullptr);

  TwoFrameResult result;

  heif_image* first = make_image(first_depth);
  heif_error err = heif_track_encode_sequence_image(track, first, encoder, nullptr);
  heif_image_release(first);

  result.first_frame_encoded = (err.code == heif_error_Ok);

  if (result.first_frame_encoded) {
    heif_image* second = make_image(second_depth);
    result.second_frame_error = heif_track_encode_sequence_image(track, second, encoder, nullptr);
    heif_image_release(second);
  }

  heif_encoder_release(encoder);
  heif_context_free(ctx);

  return result;
}

void require_mismatch_refused(heif_compression_format format, int first_depth, int second_depth)
{
  TwoFrameResult result = encode_two_frames(format, first_depth, second_depth);

  if (!result.first_frame_encoded) {
    return;
  }

  const heif_error& err = result.second_frame_error;

  INFO("second frame error (" << err.code << "/" << err.subcode << "): "
                              << (err.message ? err.message : "(null)"));

  // Silently succeeding is the failure mode of the unfixed x265 plugin: it read
  // past the end of the second frame's planes and encoded whatever it found.
  REQUIRE(err.code != heif_error_Ok);

  // An encoder may bail out for a reason of its own, but when it is our check
  // that fires, it has to report the bit depth as the reason.
  if (err.code == heif_error_Encoder_plugin_error) {
    REQUIRE(err.subcode == heif_suberror_Unsupported_bit_depth);
  }
}

void require_uniform_accepted(heif_compression_format format, int depth)
{
  TwoFrameResult result = encode_two_frames(format, depth, depth);

  REQUIRE(result.first_frame_encoded);

  const heif_error& err = result.second_frame_error;

  INFO("second frame error (" << err.code << "/" << err.subcode << "): "
                              << (err.message ? err.message : "(null)"));
  REQUIRE(err.code == heif_error_Ok);
}

} // namespace

// One section per codec, so that a failure in one does not stop the other from
// running: the AV1 encoders report the mismatch as an error of their own, while
// it is the HEVC leg that reads out of bounds without the fix.
TEST_CASE("a sequence frame must carry the bit depth of the first frame")
{
  SECTION("AV1")
  {
    if (!heif_have_encoder_for_format(heif_compression_AV1)) {
      SKIP("Skipping because no AV1 encoder is compiled.");
    }

    // 10 bit first: this is the direction that made x265 read past the end of the
    // 8 bit planes of the second frame.
    require_mismatch_refused(heif_compression_AV1, 10, 8);

    // 8 bit first: the mirror case, where the second frame's planes are wider than
    // what the encoder was opened for.
    require_mismatch_refused(heif_compression_AV1, 8, 10);
  }

  SECTION("HEVC")
  {
    if (!heif_have_encoder_for_format(heif_compression_HEVC)) {
      SKIP("Skipping because no HEVC encoder is compiled.");
    }

    require_mismatch_refused(heif_compression_HEVC, 10, 8);
    require_mismatch_refused(heif_compression_HEVC, 8, 10);
  }
}

TEST_CASE("a sequence of frames with one bit depth still encodes")
{
  // The check must not reject what it is supposed to let through.
  SECTION("AV1")
  {
    if (!heif_have_encoder_for_format(heif_compression_AV1)) {
      SKIP("Skipping because no AV1 encoder is compiled.");
    }

    require_uniform_accepted(heif_compression_AV1, 8);
  }

  SECTION("HEVC")
  {
    if (!heif_have_encoder_for_format(heif_compression_HEVC)) {
      SKIP("Skipping because no HEVC encoder is compiled.");
    }

    require_uniform_accepted(heif_compression_HEVC, 8);
  }
}
