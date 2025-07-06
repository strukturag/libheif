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

  heif_image* img = createImage_RGB_planar();
  heif_encoder* enc = get_encoder_or_skip_test(heif_compression_HEVC);
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