/*
  libheif integration tests for extended type (uuid) boxes

  MIT License

  Copyright (c) 2024 Brad Hards <bradh@frogmouth.net>

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
#include "api_structs.h"
#include "libheif/heif.h"
#include "test-config.h"
#include "test_utils.h"
#include <cstddef>
#include <cstdint>
#include <cstring>


TEST_CASE("make extended type") {
  heif_image *input_image = createImage_RGB_planar();
  heif_init(nullptr);
  heif_context *ctx = heif_context_alloc();
  heif_encoder *encoder;
  struct heif_error err;
  encoder = get_encoder_or_skip_test(heif_compression_HEVC);

  struct heif_encoding_options *options = heif_encoding_options_alloc();

  heif_image_handle *output_image_handle;

  err = heif_context_encode_image(ctx, input_image, encoder, nullptr, &output_image_handle);
  UNSCOPED_INFO("heif_context_encode_image: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  heif_item_id itemId;
  err = heif_context_get_primary_image_ID(ctx, &itemId);
  REQUIRE(err.code == heif_error_Ok);

  const uint8_t uuid[] = {0x13, 0x7a, 0x17, 0x42, 0x75, 0xac, 0x47, 0x47, 0x82, 0xbc, 0x65, 0x95, 0x76, 0xe8, 0x67, 0x5b};
  std::vector<uint8_t> body {0x00, 0x00, 0x00, 0x01, 0xfa, 0xde, 0x99, 0x04};
  heif_property_id propertyId;
  err = heif_item_add_raw_property(ctx, itemId, heif_item_property_type_uuid, &uuid[0], body.data(), body.size(), 0, &propertyId);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(propertyId == 4);
  err = heif_context_write_to_file(ctx, "with_uuid.heif");
  REQUIRE(err.code == heif_error_Ok);

  uint8_t extended_type[16];
  err = heif_item_get_property_uuid_type(ctx, itemId, propertyId, &extended_type[0]);
  REQUIRE(err.code == heif_error_Ok);
  for (int i = 0; i < 16; i++) {
    REQUIRE(extended_type[i] == uuid[i]);
  }

  size_t size = 0;
  err = heif_item_get_property_raw_size(ctx, itemId, propertyId, &size);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(size == 8);

  uint8_t data[8];
  err = heif_item_get_property_raw_data(ctx, itemId, propertyId, &data[0]);
  REQUIRE(err.code == heif_error_Ok);
  for (int i = 0; i < 8; i++) {
    REQUIRE(data[i] == body[i]);
  }

  heif_image_handle_release(output_image_handle);
  heif_encoding_options_free(options);
  heif_encoder_release(encoder);
  heif_image_release(input_image);

  heif_context_free(ctx);
  heif_deinit();
}

