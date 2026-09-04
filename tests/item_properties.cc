/*
  libheif integration tests for item property ids

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
#include "libheif/heif_items.h"
#include "libheif/heif_properties.h"
#include "test_utils.h"
#include <cstdint>
#include <vector>


// The 'heif_property_id' returned by heif_item_add_*_property() has to be usable with the
// property getters. The property boxes of all items are stored in a single, file-wide 'ipco'
// box, while the getters address the properties of one item. As soon as the file holds more
// properties than the item we add to, the two numberings differ. Using an 'ipco' index as
// property id then reads past the end of the item's property list.

TEST_CASE("property id round trip with several items") {
  heif_init(nullptr);
  heif_context* ctx = heif_context_alloc();

  const std::vector<uint8_t> payload1{'i', 't', 'e', 'm', '1'};
  const std::vector<uint8_t> payload2{'i', 't', 'e', 'm', '2'};

  heif_item_id item1, item2;
  heif_error err;

  err = heif_context_add_item(ctx, "mime", payload1.data(), (int) payload1.size(), &item1);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_context_add_item(ctx, "mime", payload2.data(), (int) payload2.size(), &item2);
  REQUIRE(err.code == heif_error_Ok);

  // Add a property to the second item first, so that the 'ipco' box holds a property that the
  // first item does not associate.

  const std::vector<uint8_t> body2{0x02, 0x02, 0x02, 0x02};
  heif_property_id propertyId2;
  err = heif_item_add_raw_property(ctx, item2, heif_fourcc('p', 'r', 'p', '2'), nullptr,
                                   body2.data(), body2.size(), 0, &propertyId2);
  REQUIRE(err.code == heif_error_Ok);

  const std::vector<uint8_t> body1{0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
  heif_property_id propertyId1;
  err = heif_item_add_raw_property(ctx, item1, heif_fourcc('p', 'r', 'p', '1'), nullptr,
                                   body1.data(), body1.size(), 0, &propertyId1);
  REQUIRE(err.code == heif_error_Ok);

  // Both items hold exactly one property, hence both ids have to be 1. Before this was fixed,
  // 'propertyId1' was 2 (the position in 'ipco'), which read past the end of item1's list.

  REQUIRE(propertyId1 == 1);
  REQUIRE(propertyId2 == 1);

  // The ids the enumerator hands out have to match the ids we got when adding.

  heif_property_id enumerated[8];
  int n = heif_item_get_properties_of_type(ctx, item1, heif_item_property_type_invalid, enumerated, 8);
  REQUIRE(n == 1);
  REQUIRE(enumerated[0] == propertyId1);

  // Read the properties back through the ids that were returned when adding them.

  size_t size = 0;
  err = heif_item_get_property_raw_size(ctx, item1, propertyId1, &size);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(size == body1.size());

  std::vector<uint8_t> readback(size);
  err = heif_item_get_property_raw_data(ctx, item1, propertyId1, readback.data());
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(readback == body1);

  err = heif_item_get_property_raw_size(ctx, item2, propertyId2, &size);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(size == body2.size());

  readback.resize(size);
  err = heif_item_get_property_raw_data(ctx, item2, propertyId2, readback.data());
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(readback == body2);

  // An id past the end of the item's property list has to be rejected instead of reading
  // out of bounds.

  err = heif_item_get_property_raw_size(ctx, item1, propertyId1 + 1, &size);
  REQUIRE(err.code == heif_error_Usage_error);

  heif_context_free(ctx);
  heif_deinit();
}


// The same divergence occurs for a plain image with a thumbnail: the thumbnail contributes its
// own codec configuration and 'ispe' to the shared 'ipco' box.

TEST_CASE("property id round trip with thumbnail") {
  heif_init(nullptr);
  // Query the encoder before allocating anything, so that a skipped test leaks nothing.
  heif_encoder* encoder = get_encoder_or_skip_test(heif_compression_HEVC);
  heif_image* input_image = createImage_RGB_planar();
  heif_context* ctx = heif_context_alloc();

  heif_image_handle* image_handle;
  heif_error err = heif_context_encode_image(ctx, input_image, encoder, nullptr, &image_handle);
  UNSCOPED_INFO("heif_context_encode_image: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* thumbnail_handle = nullptr;
  err = heif_context_encode_thumbnail(ctx, input_image, image_handle, encoder, nullptr, 16,
                                      &thumbnail_handle);
  UNSCOPED_INFO("heif_context_encode_thumbnail: " << err.message);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(thumbnail_handle != nullptr);

  heif_item_id itemId;
  err = heif_context_get_primary_image_ID(ctx, &itemId);
  REQUIRE(err.code == heif_error_Ok);

  int n_before = heif_item_get_properties_of_type(ctx, itemId, heif_item_property_type_invalid,
                                                  nullptr, 0);
  REQUIRE(n_before > 0);

  const std::vector<uint8_t> body{0xfa, 0xde, 0x99, 0x04};
  heif_property_id propertyId;
  err = heif_item_add_raw_property(ctx, itemId, heif_fourcc('p', 'r', 'p', 'x'), nullptr,
                                   body.data(), body.size(), 0, &propertyId);
  REQUIRE(err.code == heif_error_Ok);

  // The property was appended to the item's property list, so its id is the new list length.
  REQUIRE(propertyId == (heif_property_id) (n_before + 1));

  size_t size = 0;
  err = heif_item_get_property_raw_size(ctx, itemId, propertyId, &size);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(size == body.size());

  std::vector<uint8_t> readback(size);
  err = heif_item_get_property_raw_data(ctx, itemId, propertyId, readback.data());
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(readback == body);

  heif_image_handle_release(thumbnail_handle);
  heif_image_handle_release(image_handle);
  heif_encoder_release(encoder);
  heif_image_release(input_image);
  heif_context_free(ctx);
  heif_deinit();
}


// Adding properties to a context that was read from a file is not supported: the item data
// still lives in the input file and the context cannot be written out again. This has to be
// refused rather than half-succeeding.

TEST_CASE("adding a property to a loaded context is refused") {
  heif_init(nullptr);
  heif_encoder* encoder = get_encoder_or_skip_test(heif_compression_HEVC);
  heif_context* ctx = heif_context_alloc();

  heif_image* input_image = createImage_RGB_planar();
  heif_image_handle* image_handle;
  heif_error err = heif_context_encode_image(ctx, input_image, encoder, nullptr, &image_handle);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_write_to_file(ctx, "property_ids.heif");
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle_release(image_handle);
  heif_image_release(input_image);
  heif_encoder_release(encoder);
  heif_context_free(ctx);

  heif_context* read_ctx = heif_context_alloc();
  err = heif_context_read_from_file(read_ctx, "property_ids.heif", nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_item_id itemId;
  err = heif_context_get_primary_image_ID(read_ctx, &itemId);
  REQUIRE(err.code == heif_error_Ok);

  const std::vector<uint8_t> body{0x01, 0x02, 0x03, 0x04};
  heif_property_id propertyId = 0;
  err = heif_item_add_raw_property(read_ctx, itemId, heif_fourcc('p', 'r', 'p', 'y'), nullptr,
                                   body.data(), body.size(), 0, &propertyId);
  REQUIRE(err.code == heif_error_Unsupported_feature);

  heif_context_free(read_ctx);
  heif_deinit();
}
