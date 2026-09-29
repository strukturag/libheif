/*
  libheif unit tests - NAL unit splitter

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
#include "codecs/decoder.h"

#include <cstdint>
#include <vector>

TEST_CASE("split_nal_units: two well-formed NAL units") {
  std::vector<uint8_t> buf{
      0x00, 0x00, 0x00, 0x03, 0xAA, 0xBB, 0xCC,   // NAL of 3 bytes
      0x00, 0x00, 0x00, 0x02, 0xDD, 0xEE};        // NAL of 2 bytes
  auto units = split_nal_units_4byte_length_prefixed(buf.data(), buf.size());
  REQUIRE(units.size() == 2);
  REQUIRE(units[0].second == 3);
  REQUIRE(units[0].first[0] == 0xAA);
  REQUIRE(units[1].second == 2);
  REQUIRE(units[1].first[0] == 0xDD);
}

TEST_CASE("split_nal_units: length running past the end stops cleanly") {
  // First NAL is fine; second declares 100 bytes but only 2 remain.
  std::vector<uint8_t> buf{
      0x00, 0x00, 0x00, 0x01, 0xAA,
      0x00, 0x00, 0x00, 0x64, 0xBB, 0xCC};
  auto units = split_nal_units_4byte_length_prefixed(buf.data(), buf.size());
  REQUIRE(units.size() == 1);
  REQUIRE(units[0].second == 1);
  REQUIRE(units[0].first[0] == 0xAA);
}

TEST_CASE("split_nal_units: zero-length NAL is skipped, parsing continues") {
  std::vector<uint8_t> buf{
      0x00, 0x00, 0x00, 0x00,                     // zero-length NAL
      0x00, 0x00, 0x00, 0x01, 0x7E};              // 1-byte NAL
  auto units = split_nal_units_4byte_length_prefixed(buf.data(), buf.size());
  REQUIRE(units.size() == 1);
  REQUIRE(units[0].second == 1);
  REQUIRE(units[0].first[0] == 0x7E);
}

TEST_CASE("split_nal_units: a lone truncated length prefix yields nothing") {
  std::vector<uint8_t> buf{0x00, 0x00, 0x00};  // fewer than 4 bytes
  auto units = split_nal_units_4byte_length_prefixed(buf.data(), buf.size());
  REQUIRE(units.empty());
}

TEST_CASE("split_nal_units: empty input yields nothing") {
  auto units = split_nal_units_4byte_length_prefixed(nullptr, 0);
  REQUIRE(units.empty());
}
