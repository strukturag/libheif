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

// Box::dump() is a debugging aid. Boxes whose content scales with the file size
// (here the 'sdtp' sample-dependency table, one entry per byte) print four lines
// per entry, and a 'j2kH' container copies that text at every nesting level. A
// ~1 MB file therefore produced a ~200 MB dump string, which timed out the
// box_fuzzer (OSS-Fuzz, GHSA-h4h8-qgvc-m7r2).
//
// dump() now (a) writes into a single stream instead of returning and
// re-concatenating a string at every level, and (b) prints at most
// MAX_DUMP_ENTRIES entries per box unless full_log is set. This test builds a
// 'j2kH' holding an 'sdtp' with many entries and checks that the default dump is
// small while the full dump still contains every entry.

#include "catch_amalgamated.hpp"
#include "box.h"
#include "logging.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace {

void append_u32(std::vector<uint8_t>& v, uint32_t x)
{
  v.push_back(uint8_t(x >> 24));
  v.push_back(uint8_t(x >> 16));
  v.push_back(uint8_t(x >> 8));
  v.push_back(uint8_t(x));
}

// A 'j2kH' box containing an 'sdtp' box with `num_samples` one-byte entries.
std::vector<uint8_t> build_j2kH_with_sdtp(uint32_t num_samples)
{
  std::vector<uint8_t> sdtp;
  append_u32(sdtp, 8 + 4 + num_samples);        // box size
  for (char c : std::string("sdtp")) sdtp.push_back(uint8_t(c));
  append_u32(sdtp, 0);                           // FullBox version/flags
  sdtp.insert(sdtp.end(), num_samples, 0);       // one byte per sample

  std::vector<uint8_t> j2kH;
  append_u32(j2kH, 8 + uint32_t(sdtp.size()));   // box size
  for (char c : std::string("j2kH")) j2kH.push_back(uint8_t(c));
  j2kH.insert(j2kH.end(), sdtp.begin(), sdtp.end());

  return j2kH;
}

std::shared_ptr<Box> read_first_box(const std::vector<uint8_t>& data)
{
  auto reader = std::make_shared<StreamReader_memory>(data.data(), data.size(), false);
  BitstreamRange range(reader, data.size());

  std::shared_ptr<Box> box;
  Error err = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(err == Error::Ok);
  REQUIRE(box);
  return box;
}

} // namespace


TEST_CASE("box dump: verbose boxes are bounded unless full_log is set")
{
  const uint32_t num_samples = 50000;   // would be 4 lines each in the dump
  std::shared_ptr<Box> box = read_first_box(build_j2kH_with_sdtp(num_samples));

  Indent indent_default;
  std::string dflt = box->dump_to_string(indent_default);

  Indent indent_full;
  std::string full = box->dump_to_string(indent_full, true);

  // The default dump stops after MAX_DUMP_ENTRIES entries, so it stays small
  // regardless of the number of samples, and shows the truncation note.
  REQUIRE(dflt.size() < 16 * 1024);
  REQUIRE(dflt.find("more") != std::string::npos);

  // The full dump contains an entry for every sample and is therefore much
  // larger. It must not contain the truncation note.
  REQUIRE(full.size() > num_samples);
  REQUIRE(full.find("more") == std::string::npos);
  REQUIRE(full.find("[" + std::to_string(num_samples - 1) + "]") != std::string::npos);
}
