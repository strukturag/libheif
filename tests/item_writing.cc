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

#include <array>


TEST_CASE("generic item writers return the allocated item IDs")
{
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  const std::array<uint8_t, 2> data = {0x12, 0x34};
  heif_item_id seed_id = 0;
  heif_item_id item_id = 0;
  heif_item_id compressed_mime_id = 0;
  heif_item_id uri_id = 0;

  heif_error err = heif_context_add_mime_item(ctx, "application/octet-stream",
                                              heif_metadata_compression_off,
                                              data.data(), data.size(), &seed_id);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_add_item(ctx, "test", data.data(), data.size(), &item_id);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_add_precompressed_mime_item(ctx, "application/octet-stream", "identity",
                                                 data.data(), data.size(), &compressed_mime_id);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_add_uri_item(ctx, "urn:example:test", data.data(), data.size(), &uri_id);
  REQUIRE(err.code == heif_error_Ok);

  REQUIRE(seed_id != 0);
  REQUIRE(item_id != seed_id);
  REQUIRE(compressed_mime_id != seed_id);
  REQUIRE(compressed_mime_id != item_id);
  REQUIRE(uri_id != seed_id);
  REQUIRE(uri_id != item_id);
  REQUIRE(uri_id != compressed_mime_id);

  REQUIRE(heif_item_get_item_type(ctx, item_id) == heif_fourcc('t', 'e', 's', 't'));
  REQUIRE(heif_item_get_item_type(ctx, compressed_mime_id) == heif_fourcc('m', 'i', 'm', 'e'));
  REQUIRE(heif_item_get_item_type(ctx, uri_id) == heif_fourcc('u', 'r', 'i', ' '));

  heif_context_free(ctx);
}
