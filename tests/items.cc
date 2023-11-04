/*
  libheif unit tests

  MIT License

  Copyright (c) 2023 Brad Hards <bradh@frogmouth.net>

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

#include "catch.hpp"
#include "libheif/heif.h"
#include "libheif/heif_items.h"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string.h>

TEST_CASE("add item") {
  std::string filename = "simple_item.heif";
  std::cout << "filename: " << filename << std::endl;
  std::vector<uint8_t> content{0x03, 0x04, 0x02, 0x01, 0xff};
  heif_item_id id;
  {
    heif_context *write_ctx = heif_context_alloc();
    heif_context_set_major_brand(write_ctx, heif_brand2_miaf);
    heif_context_add_compatible_brand(write_ctx, heif_brand2_miaf);
    int num_items = heif_context_get_number_of_items(write_ctx);
    REQUIRE(num_items == 0);
    heif_context_add_item(write_ctx, "unci", content.data(), (int) content.size(), &id);
    struct heif_error res = heif_context_write_to_file(write_ctx, filename.c_str());
    REQUIRE(res.code == heif_error_Ok);
    heif_context_free(write_ctx);
  }
  {
    heif_context *read_ctx = heif_context_alloc();
    heif_context_read_from_file(read_ctx, filename.c_str(), NULL);
    int num_items = heif_context_get_number_of_items(read_ctx);
    REQUIRE(num_items == 1);
    std::vector<heif_item_id> item_ids(num_items);
    int actual_count = heif_context_get_list_of_item_IDs(read_ctx, item_ids.data(), (int)item_ids.size());
    REQUIRE(actual_count == 1);
    REQUIRE(item_ids[0] == id);
    uint32_t item_type = heif_context_get_item_type(read_ctx, id);
    REQUIRE(item_type == heif_fourcc('u', 'n', 'c', 'i'));
    size_t item_data_size;
    heif_error res = heif_context_get_item_data(read_ctx, id, NULL, &item_data_size);
    REQUIRE(res.code == heif_error_Ok);
    REQUIRE(item_data_size == content.size());
    uint8_t *item_data;
    res = heif_context_get_item_data(read_ctx, id, &item_data, &item_data_size);
    REQUIRE(res.code == heif_error_Ok);
    REQUIRE(item_data_size == content.size());
    REQUIRE(item_data[0] == 0x03);
    REQUIRE(item_data[4] == 0xff);
    heif_release_item_data(read_ctx, &item_data);
    heif_context_free(read_ctx);
  }
}
