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

// 'sdtp' stores one byte per sample and reads to the end of the box, so its
// sample count scales with the file size. Like the other sample tables it is
// now bounded by max_sequence_frames at parse time.

#include "catch_amalgamated.hpp"
#include "box.h"
#include "security_limits.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace {

std::vector<uint8_t> sdtp_box(uint32_t num_samples)
{
  std::vector<uint8_t> v;
  uint32_t size = 8 + 4 + num_samples;
  v.push_back(uint8_t(size >> 24));
  v.push_back(uint8_t(size >> 16));
  v.push_back(uint8_t(size >> 8));
  v.push_back(uint8_t(size));
  for (char c : std::string("sdtp")) v.push_back(uint8_t(c));
  v.push_back(0); v.push_back(0); v.push_back(0); v.push_back(0); // version + flags
  v.insert(v.end(), num_samples, 0);
  return v;
}

Error parse_sdtp(uint32_t num_samples, uint32_t max_sequence_frames)
{
  std::vector<uint8_t> data = sdtp_box(num_samples);
  auto reader = std::make_shared<StreamReader_memory>(data.data(), data.size(), false);
  BitstreamRange range(reader, data.size());

  heif_security_limits limits = *heif_get_global_security_limits();
  limits.max_sequence_frames = max_sequence_frames;

  std::shared_ptr<Box> box;
  return Box::read(range, &box, &limits);
}

} // namespace


TEST_CASE("sdtp sample count is bounded by max_sequence_frames")
{
  SECTION("more samples than the limit allows is rejected") {
    Error err = parse_sdtp(1000, 100);
    REQUIRE(err.error_code == heif_error_Memory_allocation_error);
    REQUIRE(err.sub_error_code == heif_suberror_Security_limit_exceeded);
  }

  SECTION("a table within the limit is accepted") {
    Error err = parse_sdtp(50, 100);
    REQUIRE(err.error_code == heif_error_Ok);
  }

  SECTION("the limit disabled (0) accepts any count") {
    Error err = parse_sdtp(1000, 0);
    REQUIRE(err.error_code == heif_error_Ok);
  }
}
