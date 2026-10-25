/*
  libheif coded-picture budget tests

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

#include <initializer_list>
#include <vector>


static std::vector<uint8_t> nal_stream(std::initializer_list<std::vector<uint8_t>> units)
{
  std::vector<uint8_t> stream;
  for (const auto& unit : units) {
    stream.insert(stream.end(), {0, 0, 0, static_cast<uint8_t>(unit.size())});
    stream.insert(stream.end(), unit.begin(), unit.end());
  }
  return stream;
}


TEST_CASE("NAL picture count distinguishes slices from pictures")
{
  auto hevc = count_nal_coded_pictures(nal_stream({{0x28, 0x01, 0x80},
                                                   {0x28, 0x01, 0x00},
                                                   {0x28, 0x01, 0x80}}),
                                        heif_compression_HEVC);
  REQUIRE(hevc);
  CHECK(*hevc == 2);

  auto avc = count_nal_coded_pictures(nal_stream({{0x65, 0x80}, {0x65, 0x40}, {0x65, 0x80}}),
                                       heif_compression_AVC);
  REQUIRE(avc);
  CHECK(*avc == 2);

  auto vvc = count_nal_coded_pictures(nal_stream({{0x00, 0x99, 0x00},
                                                  {0x00, 0x39, 0x00},
                                                  {0x00, 0x39, 0x80}}),
                                       heif_compression_VVC);
  REQUIRE(vvc);
  CHECK(*vvc == 2);
}


TEST_CASE("AVC picture count rejects embedded start codes and incomplete framing")
{
  auto embedded = count_nal_coded_pictures(nal_stream({{0x65, 0x80, 0x00, 0x00, 0x01, 0x65, 0x80}}),
                                            heif_compression_AVC);
  CHECK(embedded.is_error());

  auto truncated = count_nal_coded_pictures({0, 0, 0, 12, 0x65, 0x80},
                                             heif_compression_AVC);
  CHECK(truncated.is_error());
}
