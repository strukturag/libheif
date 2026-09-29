/*
  libheif unit tests

  MIT License

  Copyright (c) 2025 Dirk Farin <dirk.farin@gmail.com>

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
#include "test_utils.h"
#include <libheif/heif.h>
#include <libheif/heif_tai_timestamps.h>

TEST_CASE( "image-tai" )
{
  heif_error err{};

  err = heif_init(nullptr);
  REQUIRE(err.code == heif_error_Ok);

  std::string filename = get_tests_output_file_path("tai-1.heic");

  // Query the encoder first: get_encoder_or_skip_test() may abort the test
  // via SKIP(), and anything allocated before it would then be leaked.
  heif_encoder* enc = get_encoder_or_skip_test(heif_compression_HEVC);
  heif_image* img = createImage_RGB_planar();
  heif_context* ctx = heif_context_alloc();

  heif_image_handle* handle;
  err = heif_context_encode_image(ctx, img, enc, nullptr, &handle);
  REQUIRE(err.code == heif_error_Ok);
  heif_item_id itemId = heif_image_handle_get_item_id(handle);

  // add TAI clock info

  heif_tai_clock_info* clock_info = heif_tai_clock_info_alloc();
  clock_info->clock_resolution = 1000;
  clock_info->clock_drift_rate = 123;
  clock_info->clock_type = heif_tai_clock_info_clock_type_synchronized_to_atomic_source;
  clock_info->time_uncertainty = 999;

  err = heif_item_set_property_tai_clock_info(ctx, itemId, clock_info, nullptr);
  REQUIRE(err.code == heif_error_Ok);

  // check that adding a second timestamp leads to an error
  err = heif_item_set_property_tai_clock_info(ctx, itemId, clock_info, nullptr);
  REQUIRE(err.code != heif_error_Ok);

  heif_tai_clock_info_release(clock_info);

  // add TAI timestamp

  heif_tai_timestamp_packet* timestamp = heif_tai_timestamp_packet_alloc();
  timestamp->tai_timestamp = 1234567890;
  timestamp->synchronization_state = 1;
  timestamp->timestamp_generation_failure = 0;
  timestamp->timestamp_is_modified = 0;

  err = heif_item_set_property_tai_timestamp(ctx, itemId, timestamp, nullptr);
  REQUIRE(err.code == heif_error_Ok);

  // check that adding a second timestamp leads to an error
  err = heif_item_set_property_tai_timestamp(ctx, itemId, timestamp, nullptr);
  REQUIRE(err.code != heif_error_Ok);

  heif_tai_timestamp_packet_release(timestamp);

  err = heif_context_write_to_file(ctx, filename.c_str());
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle_release(handle);
  heif_context_free(ctx);
  heif_image_release(img);


  // --- read file

  ctx = heif_context_alloc();
  err = heif_context_read_from_file(ctx, filename.c_str(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);

  itemId = heif_image_handle_get_item_id(handle);

  clock_info = nullptr; // make sure that we are not accidentally using old data
  err = heif_item_get_property_tai_clock_info(ctx, itemId, &clock_info);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(clock_info != nullptr);

  timestamp = nullptr; // make sure that we are not accidentally using old data
  err = heif_item_get_property_tai_timestamp(ctx, itemId, &timestamp);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(timestamp != nullptr);

  REQUIRE(clock_info->clock_resolution == 1000);
  REQUIRE(clock_info->clock_drift_rate == 123);
  REQUIRE(clock_info->clock_type == heif_tai_clock_info_clock_type_synchronized_to_atomic_source);
  REQUIRE(clock_info->time_uncertainty == 999);
  heif_tai_clock_info_release(clock_info);

  REQUIRE(timestamp->tai_timestamp == 1234567890);
  REQUIRE(timestamp->synchronization_state == 1);
  REQUIRE(timestamp->timestamp_generation_failure == 0);
  REQUIRE(timestamp->timestamp_is_modified == 0);
  heif_tai_timestamp_packet_release(timestamp);

  // check whether we can get the timestamp also from the decoded image

  err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
  REQUIRE(err.code == heif_error_Ok);

  timestamp = nullptr; // make sure that we are not accidentally using old data
  err = heif_image_get_tai_timestamp(img, &timestamp);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(timestamp != nullptr);

  REQUIRE(timestamp->tai_timestamp == 1234567890);
  REQUIRE(timestamp->synchronization_state == 1);
  REQUIRE(timestamp->timestamp_generation_failure == 0);
  REQUIRE(timestamp->timestamp_is_modified == 0);
  heif_tai_timestamp_packet_release(timestamp);
}

// Pick a codec for which we have both an encoder and a decoder so that the
// test can encode, read back, decode and re-encode.
static heif_compression_format pick_roundtrip_format_or_skip()
{
  for (heif_compression_format format : {heif_compression_AV1,
                                         heif_compression_HEVC,
                                         heif_compression_uncompressed}) {
    if (heif_have_encoder_for_format(format) && heif_have_decoder_for_format(format)) {
      return format;
    }
  }

  SKIP("No codec with both encoder and decoder available, skipping test");
  return heif_compression_undefined;
}


static void require_tai_timestamp(heif_tai_timestamp_packet* tai, uint64_t expected)
{
  REQUIRE(tai != nullptr);
  REQUIRE(tai->tai_timestamp == expected);
  REQUIRE(tai->synchronization_state == 1);
  REQUIRE(tai->timestamp_generation_failure == 0);
  REQUIRE(tai->timestamp_is_modified == 1);
  heif_tai_timestamp_packet_release(tai);
}


// Regression test for GHSA-qwpf-5wf7-r996.
//
// Encoding an image that carries a TAI timestamp copied the ImageDescription
// of the source image into the ImageItem through a sliced temporary. The
// temporary shared the raw heif_tai_timestamp_packet pointer with the source
// image and freed it when it went out of scope, leaving both the item and
// the source image with a dangling pointer (use-after-free while generating
// the 'itai' property, double free at teardown).
TEST_CASE( "image-tai-encode-keeps-timestamp" )
{
  heif_error err{};

  err = heif_init(nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_compression_format format = pick_roundtrip_format_or_skip();

  const uint64_t expected_timestamp = 0x0123456789ABCDEFULL;

  heif_image* img = createImage_RGB_planar();

  heif_tai_timestamp_packet* tai = heif_tai_timestamp_packet_alloc();
  tai->tai_timestamp = expected_timestamp;
  tai->synchronization_state = 1;
  tai->timestamp_generation_failure = 0;
  tai->timestamp_is_modified = 1;
  err = heif_image_set_tai_timestamp(img, tai);
  REQUIRE(err.code == heif_error_Ok);
  heif_tai_timestamp_packet_release(tai);

  heif_context* ctx = heif_context_alloc();
  heif_encoder* enc = nullptr;
  err = heif_context_get_encoder_for_format(ctx, format, &enc);
  REQUIRE(err.code == heif_error_Ok);

  // --- encode: the timestamp is copied from the image into the item

  heif_image_handle* handle = nullptr;
  err = heif_context_encode_image(ctx, img, enc, nullptr, &handle);
  REQUIRE(err.code == heif_error_Ok);

  // the encoded item carries the timestamp as an 'itai' property

  heif_item_id itemId = heif_image_handle_get_item_id(handle);
  tai = nullptr;
  err = heif_item_get_property_tai_timestamp(ctx, itemId, &tai);
  REQUIRE(err.code == heif_error_Ok);
  require_tai_timestamp(tai, expected_timestamp);

  // the source image must still own its own, intact timestamp

  tai = nullptr;
  err = heif_image_get_tai_timestamp(img, &tai);
  REQUIRE(err.code == heif_error_Ok);
  require_tai_timestamp(tai, expected_timestamp);

  // encoding the same image a second time must work as well

  heif_image_handle* handle2 = nullptr;
  err = heif_context_encode_image(ctx, img, enc, nullptr, &handle2);
  REQUIRE(err.code == heif_error_Ok);
  heif_image_handle_release(handle2);

  std::string filename = get_tests_output_file_path("tai-transcode.heif");
  err = heif_context_write_to_file(ctx, filename.c_str());
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle_release(handle);
  heif_encoder_release(enc);
  heif_context_free(ctx);
  heif_image_release(img);


  // --- transcode: decode the file and encode the decoded image again

  ctx = heif_context_alloc();
  err = heif_context_read_from_file(ctx, filename.c_str(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);

  img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle_release(handle);
  heif_context_free(ctx);

  // the decoded image has the timestamp attached

  tai = nullptr;
  err = heif_image_get_tai_timestamp(img, &tai);
  REQUIRE(err.code == heif_error_Ok);
  require_tai_timestamp(tai, expected_timestamp);

  ctx = heif_context_alloc();
  err = heif_context_get_encoder_for_format(ctx, format, &enc);
  REQUIRE(err.code == heif_error_Ok);

  handle = nullptr;
  err = heif_context_encode_image(ctx, img, enc, nullptr, &handle);
  REQUIRE(err.code == heif_error_Ok);

  itemId = heif_image_handle_get_item_id(handle);
  tai = nullptr;
  err = heif_item_get_property_tai_timestamp(ctx, itemId, &tai);
  REQUIRE(err.code == heif_error_Ok);
  require_tai_timestamp(tai, expected_timestamp);

  tai = nullptr;
  err = heif_image_get_tai_timestamp(img, &tai);
  REQUIRE(err.code == heif_error_Ok);
  require_tai_timestamp(tai, expected_timestamp);

  heif_image_handle_release(handle);
  heif_encoder_release(enc);
  heif_context_free(ctx);
  heif_image_release(img);
}
