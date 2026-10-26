/*
  libheif unit tests for text items (internal API)

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
#include "security_limits.h"
#include "text.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>


TEST_CASE("retained text data remains charged to the memory budget") {
  heif_security_limits limits = global_security_limits;
  limits.max_total_memory = 48;
  TotalMemoryTracker tracker(&limits);

  TextItem second;
  std::vector<uint8_t> second_data(32, 'b');
  {
    TextItem first;
    std::vector<uint8_t> first_data(32, 'a');
    REQUIRE_FALSE(first.parse(std::move(first_data), &limits));
    auto first_text = first.get_item_text();
    REQUIRE(first_text);
    REQUIRE(*first_text == std::string(32, 'a'));

    Error err = second.parse(std::move(second_data), &limits);
    REQUIRE(err.error_code == heif_error_Memory_allocation_error);
    REQUIRE(err.sub_error_code == heif_suberror_Security_limit_exceeded);
  }

  REQUIRE_FALSE(second.parse(std::move(second_data), &limits));
}
