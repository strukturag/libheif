/*
  libheif integration tests for generic item writing.

  MIT License

  Copyright (c) 2026 Greg Benz

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
#include "test_utils.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>


static std::vector<heif_item_id> get_item_ids(const heif_context* ctx)
{
  int n = heif_context_get_number_of_items(ctx);
  std::vector<heif_item_id> ids(n);
  REQUIRE(heif_context_get_list_of_item_IDs(ctx, ids.data(), n) == n);
  return ids;
}


static std::vector<uint8_t> read_item_data(const heif_context* ctx, heif_item_id id,
                                           heif_metadata_compression* out_compression = nullptr)
{
  uint8_t* data = nullptr;
  size_t size = 0;
  heif_error err = heif_item_get_item_data(ctx, id, out_compression, &data, &size);
  REQUIRE(err.code == heif_error_Ok);

  std::vector<uint8_t> result(data, data + size);
  heif_release_item_data(ctx, &data);
  return result;
}


TEST_CASE("generic item writers return the allocated item IDs")
{
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  const std::array<uint8_t, 2> data = {0x12, 0x34};
  heif_item_id seed_id = 0;
  heif_item_id item_id = 0;
  heif_item_id compressed_mime_id = 0;
  heif_item_id uri_id = 0;

  // The bug this guards against (PR #1885) made the writers store the boolean "success"
  // value 1 instead of the item ID. The seed item takes ID 1 first, so that a regression
  // to the old behaviour cannot be mistaken for a correct ID below.
  heif_error err = heif_context_add_mime_item(ctx, "application/octet-stream",
                                              heif_metadata_compression_off,
                                              data.data(), data.size(), &seed_id);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_add_item(ctx, "test", data.data(), data.size(), &item_id);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_add_precompressed_mime_item(ctx, "application/octet-stream", "",
                                                 data.data(), data.size(), &compressed_mime_id);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_add_uri_item(ctx, "urn:example:test", data.data(), data.size(), &uri_id);
  REQUIRE(err.code == heif_error_Ok);

  // IDs are handed out sequentially, starting at 1 (tests/region.cc relies on this as well).
  REQUIRE(seed_id == 1);
  REQUIRE(item_id == 2);
  REQUIRE(compressed_mime_id == 3);
  REQUIRE(uri_id == 4);

  REQUIRE(heif_item_get_item_type(ctx, seed_id) == heif_item_type_mime);
  REQUIRE(heif_item_get_item_type(ctx, item_id) == heif_fourcc('t', 'e', 's', 't'));
  REQUIRE(heif_item_get_item_type(ctx, compressed_mime_id) == heif_item_type_mime);
  REQUIRE(heif_item_get_item_type(ctx, uri_id) == heif_item_type_uri);

  REQUIRE(std::string(heif_item_get_uri_item_uri_type(ctx, uri_id)) == "urn:example:test");

  std::vector<heif_item_id> ids = get_item_ids(ctx);
  std::sort(ids.begin(), ids.end());
  REQUIRE(ids == std::vector<heif_item_id>{1, 2, 3, 4});

  // out_item_id may be NULL: the item is still added
  err = heif_context_add_mime_item(ctx, "text/plain", heif_metadata_compression_off,
                                   data.data(), data.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(heif_context_get_number_of_items(ctx) == 5);

  heif_context_free(ctx);
}


TEST_CASE("item writers reject invalid arguments without adding an item")
{
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  const std::array<uint8_t, 4> data = {1, 2, 3, 4};
  heif_item_id id = 0;
  heif_error err;

  // NULL strings
  err = heif_context_add_mime_item(ctx, nullptr, heif_metadata_compression_off, data.data(), 4, &id);
  REQUIRE(err.code == heif_error_Usage_error);
  err = heif_context_add_precompressed_mime_item(ctx, nullptr, "", data.data(), 4, &id);
  REQUIRE(err.code == heif_error_Usage_error);
  err = heif_context_add_precompressed_mime_item(ctx, "application/octet-stream", nullptr, data.data(), 4, &id);
  REQUIRE(err.code == heif_error_Usage_error);
  err = heif_context_add_uri_item(ctx, nullptr, data.data(), 4, &id);
  REQUIRE(err.code == heif_error_Usage_error);
  err = heif_context_add_item(ctx, nullptr, data.data(), 4, &id);
  REQUIRE(err.code == heif_error_Usage_error);
  err = heif_context_add_item(ctx, "toolong", data.data(), 4, &id);
  REQUIRE(err.code == heif_error_Usage_error);

  // negative size
  err = heif_context_add_item(ctx, "test", data.data(), -1, &id);
  REQUIRE(err.code == heif_error_Usage_error);
  err = heif_context_add_mime_item(ctx, "text/plain", heif_metadata_compression_off, data.data(), -1, &id);
  REQUIRE(err.code == heif_error_Usage_error);
  err = heif_context_add_precompressed_mime_item(ctx, "text/plain", "", data.data(), -1, &id);
  REQUIRE(err.code == heif_error_Usage_error);
  err = heif_context_add_uri_item(ctx, "urn:example:test", data.data(), -1, &id);
  REQUIRE(err.code == heif_error_Usage_error);

  // NULL data with non-zero size
  err = heif_context_add_item(ctx, "test", nullptr, 4, &id);
  REQUIRE(err.code == heif_error_Usage_error);
  err = heif_context_add_mime_item(ctx, "text/plain", heif_metadata_compression_off, nullptr, 4, &id);
  REQUIRE(err.code == heif_error_Usage_error);
  err = heif_context_add_precompressed_mime_item(ctx, "text/plain", "", nullptr, 4, &id);
  REQUIRE(err.code == heif_error_Usage_error);
  err = heif_context_add_uri_item(ctx, "urn:example:test", nullptr, 4, &id);
  REQUIRE(err.code == heif_error_Usage_error);

  REQUIRE(heif_context_get_number_of_items(ctx) == 0);

  // NULL data with size 0 is an empty item
  err = heif_context_add_item(ctx, "test", nullptr, 0, &id);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(heif_context_get_number_of_items(ctx) == 1);
  REQUIRE(read_item_data(ctx, id).empty());

  heif_context_free(ctx);
}


TEST_CASE("item data can be read back from a context that is being written")
{
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  const std::vector<uint8_t> payload = {'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'};
  heif_item_id id = 0;
  heif_error err;
  heif_metadata_compression compression;

  err = heif_context_add_mime_item(ctx, "text/plain", heif_metadata_compression_off,
                                   payload.data(), (int) payload.size(), &id);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(read_item_data(ctx, id, &compression) == payload);
  REQUIRE(compression == heif_metadata_compression_off);
  REQUIRE(std::string(heif_item_get_mime_item_content_type(ctx, id)) == "text/plain");

  err = heif_context_add_item(ctx, "test", payload.data(), (int) payload.size(), &id);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(read_item_data(ctx, id) == payload);

  err = heif_context_add_uri_item(ctx, "urn:example:test", payload.data(), (int) payload.size(), &id);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(read_item_data(ctx, id) == payload);

  // "identity" is the RFC 2616 no-op content coding
  err = heif_context_add_precompressed_mime_item(ctx, "text/plain", "identity",
                                                 payload.data(), (int) payload.size(), &id);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(read_item_data(ctx, id) == payload);

  // compressed mime item: either it round-trips, or (without the compressor) it is rejected cleanly
  int num_items = heif_context_get_number_of_items(ctx);
  err = heif_context_add_mime_item(ctx, "text/plain", heif_metadata_compression_deflate,
                                   payload.data(), (int) payload.size(), &id);
  if (heif_metadata_compression_method_supported(heif_metadata_compression_deflate)) {
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(std::string(heif_item_get_mime_item_content_encoding(ctx, id)) == "deflate");
    REQUIRE(read_item_data(ctx, id) == payload);

    std::vector<uint8_t> compressed = read_item_data(ctx, id, &compression);
    REQUIRE(compression == heif_metadata_compression_deflate);
    REQUIRE(compressed != payload);
  }
  else {
    REQUIRE(err.code != heif_error_Ok);
    REQUIRE(heif_context_get_number_of_items(ctx) == num_items);
  }

  heif_context_free(ctx);
}


TEST_CASE("items added to a loaded file get fresh IDs")
{
  heif_context* ctx = get_context_for_test_file("rainbow-451x461.heic");

  std::vector<heif_item_id> existing_ids = get_item_ids(ctx);
  REQUIRE(!existing_ids.empty());

  std::vector<uint32_t> existing_types;
  for (heif_item_id id : existing_ids) {
    existing_types.push_back(heif_item_get_item_type(ctx, id));
  }

  const std::vector<uint8_t> payload = {'n', 'e', 'w'};
  heif_item_id mime_id = 0, uri_id = 0, generic_id = 0;
  heif_error err;

  err = heif_context_add_mime_item(ctx, "text/plain", heif_metadata_compression_off,
                                   payload.data(), (int) payload.size(), &mime_id);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_context_add_uri_item(ctx, "urn:example:test", payload.data(), (int) payload.size(), &uri_id);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_context_add_item(ctx, "test", payload.data(), (int) payload.size(), &generic_id);
  REQUIRE(err.code == heif_error_Ok);

  // The new IDs must not collide with the items read from the file (the ID allocator used to
  // restart at 1 and silently replaced the primary image) ...
  for (heif_item_id new_id : {mime_id, uri_id, generic_id}) {
    REQUIRE(std::find(existing_ids.begin(), existing_ids.end(), new_id) == existing_ids.end());
  }
  REQUIRE(mime_id != uri_id);
  REQUIRE(uri_id != generic_id);
  REQUIRE(heif_context_get_number_of_items(ctx) == (int) existing_ids.size() + 3);

  // ... and the existing items must be untouched
  for (size_t i = 0; i < existing_ids.size(); i++) {
    REQUIRE(heif_item_get_item_type(ctx, existing_ids[i]) == existing_types[i]);
  }

  heif_image_handle* handle = get_primary_image_handle(ctx);
  REQUIRE(heif_image_handle_get_width(handle) == 451);
  heif_image_handle_release(handle);

  // the new data is readable even though the context has an input file that does not contain it
  REQUIRE(read_item_data(ctx, mime_id) == payload);
  REQUIRE(read_item_data(ctx, uri_id) == payload);
  REQUIRE(read_item_data(ctx, generic_id) == payload);

  // Writing back a context that was read from a file is not implemented (the image data
  // would not be copied). This must be reported as an error, not crash or write a
  // truncated file.
  std::string path = get_tests_output_file_path("item_writing_loaded_context.heic");
  err = heif_context_write_to_file(ctx, path.c_str());
  REQUIRE(err.code != heif_error_Ok);

  heif_context_free(ctx);
}


TEST_CASE("items survive a round trip through a written file")
{
  heif_context* ctx = heif_context_alloc();
  heif_encoder* enc = get_encoder_or_skip_test(heif_compression_HEVC);

  heif_image* img = createImage_RGB_planar();
  const int image_width = heif_image_get_width(img, heif_channel_R);

  heif_image_handle* handle = nullptr;
  heif_error err = heif_context_encode_image(ctx, img, enc, nullptr, &handle);
  REQUIRE(err.code == heif_error_Ok);
  heif_item_id image_id = heif_image_handle_get_item_id(handle);
  heif_image_handle_release(handle);
  heif_image_release(img);
  heif_encoder_release(enc);

  const std::vector<uint8_t> payload = {'n', 'e', 'w'};
  const std::vector<uint8_t> opaque = {0xde, 0xad, 0xbe, 0xef};
  heif_item_id mime_id = 0, uri_id = 0, generic_id = 0, identity_id = 0, unknown_id = 0;

  err = heif_context_add_mime_item(ctx, "text/plain", heif_metadata_compression_off,
                                   payload.data(), (int) payload.size(), &mime_id);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_context_add_uri_item(ctx, "urn:example:test", payload.data(), (int) payload.size(), &uri_id);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_context_add_item(ctx, "test", payload.data(), (int) payload.size(), &generic_id);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_context_add_precompressed_mime_item(ctx, "text/plain", "identity",
                                                 payload.data(), (int) payload.size(), &identity_id);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_context_add_precompressed_mime_item(ctx, "application/octet-stream", "x-unknown-coding",
                                                 opaque.data(), (int) opaque.size(), &unknown_id);
  REQUIRE(err.code == heif_error_Ok);

  std::vector<heif_item_id> ids = {image_id, mime_id, uri_id, generic_id, identity_id, unknown_id};
  std::sort(ids.begin(), ids.end());
  REQUIRE(std::unique(ids.begin(), ids.end()) == ids.end());

  std::string path = get_tests_output_file_path("item_writing_roundtrip.heic");
  err = heif_context_write_to_file(ctx, path.c_str());
  REQUIRE(err.code == heif_error_Ok);
  heif_context_free(ctx);

  // --- read back (must succeed although one item has an undecodable content_encoding)

  ctx = get_context_for_local_file(path);
  REQUIRE(heif_context_get_number_of_items(ctx) == 6);

  // the item IDs are preserved
  REQUIRE(heif_item_get_item_type(ctx, mime_id) == heif_item_type_mime);
  REQUIRE(std::string(heif_item_get_mime_item_content_type(ctx, mime_id)) == "text/plain");
  REQUIRE(std::string(heif_item_get_mime_item_content_encoding(ctx, mime_id)) == "");
  REQUIRE(read_item_data(ctx, mime_id) == payload);

  REQUIRE(heif_item_get_item_type(ctx, uri_id) == heif_item_type_uri);
  REQUIRE(std::string(heif_item_get_uri_item_uri_type(ctx, uri_id)) == "urn:example:test");
  REQUIRE(read_item_data(ctx, uri_id) == payload);

  REQUIRE(heif_item_get_item_type(ctx, generic_id) == heif_fourcc('t', 'e', 's', 't'));
  REQUIRE(read_item_data(ctx, generic_id) == payload);

  REQUIRE(std::string(heif_item_get_mime_item_content_encoding(ctx, identity_id)) == "identity");
  REQUIRE(read_item_data(ctx, identity_id) == payload);

  // the undecodable item: raw data is available, decoding is refused
  REQUIRE(std::string(heif_item_get_mime_item_content_encoding(ctx, unknown_id)) == "x-unknown-coding");
  heif_metadata_compression compression;
  REQUIRE(read_item_data(ctx, unknown_id, &compression) == opaque);
  REQUIRE(compression == heif_metadata_compression_unknown);

  uint8_t* data = nullptr;
  size_t size = 0;
  err = heif_item_get_item_data(ctx, unknown_id, nullptr, &data, &size);
  REQUIRE(err.code != heif_error_Ok);
  REQUIRE(data == nullptr);

  // the image is intact
  heif_item_id primary_id = 0;
  REQUIRE(heif_context_get_primary_image_ID(ctx, &primary_id).code == heif_error_Ok);
  REQUIRE(primary_id == image_id);

  handle = get_primary_image_handle(ctx);
  REQUIRE(heif_image_handle_get_width(handle) == image_width);
  if (heif_have_decoder_for_format(heif_compression_HEVC)) {
    img = get_primary_image(handle);
    REQUIRE(heif_image_get_primary_width(img) == image_width);
    heif_image_release(img);
  }
  heif_image_handle_release(handle);

  heif_context_free(ctx);
}


TEST_CASE("a context without images or sequences cannot be written")
{
  const std::array<uint8_t, 2> data = {0x12, 0x34};
  std::string path = get_tests_output_file_path("item_writing_no_images.heic");

  heif_context* ctx = heif_context_alloc();
  heif_item_id id = 0;
  heif_error err = heif_context_add_mime_item(ctx, "text/plain", heif_metadata_compression_off,
                                              data.data(), data.size(), &id);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_context_write_to_file(ctx, path.c_str());
  REQUIRE(err.code == heif_error_Usage_error);
  heif_context_free(ctx);

  ctx = heif_context_alloc();
  err = heif_context_add_item(ctx, "test", data.data(), data.size(), &id);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_context_write_to_file(ctx, path.c_str());
  REQUIRE(err.code == heif_error_Usage_error);
  heif_context_free(ctx);
}
