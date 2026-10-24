/*
  libheifio Exif IFD loop unit tests

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

// The Exif tag lookup follows ExifIFD pointer entries (tag 0x8769) recursively.
// An IFD whose entries all point back to itself must not cost N^depth work.

#include "catch_amalgamated.hpp"
#include "heifio/exif.h"

#include <cstdint>
#include <vector>

static void put16(std::vector<uint8_t>& v, uint16_t x)
{
  v.push_back((uint8_t) (x >> 8));
  v.push_back((uint8_t) (x & 0xFF));
}

static void put32(std::vector<uint8_t>& v, uint32_t x)
{
  put16(v, (uint16_t) (x >> 16));
  put16(v, (uint16_t) (x & 0xFFFF));
}

// One IFD entry with a 4-byte inline value.
static void put_entry(std::vector<uint8_t>& v, uint16_t tag, uint16_t type, uint32_t count, uint32_t value)
{
  put16(v, tag);
  put16(v, type);
  put32(v, count);
  put32(v, value);
}

static std::vector<uint8_t> tiff_header()
{
  std::vector<uint8_t> v;
  v.push_back('M');
  v.push_back('M');
  put16(v, 42);
  put32(v, 8); // IFD0 at offset 8
  return v;
}


TEST_CASE("exif IFD entries that point back to their own IFD")
{
  const int entry_count = 2000;

  std::vector<uint8_t> exif = tiff_header();
  put16(exif, entry_count);
  for (int i = 0; i < entry_count; i++) {
    put_entry(exif, 0x8769 /* ExifIFD pointer */, 4 /* LONG */, 1, 8 /* IFD0 itself */);
  }
  put32(exif, 0); // no next IFD

  // Without a bound on the total number of scanned entries, this would take entry_count^5 steps.
  REQUIRE(read_exif_orientation_tag(exif.data(), (uint32_t) exif.size()) == 1);

  modify_exif_orientation_tag_if_it_exists(exif.data(), (uint32_t) exif.size(), 3);
  overwrite_exif_image_size_if_it_exists(exif.data(), (uint32_t) exif.size(), 100, 200);
}


TEST_CASE("empty exif IFD whose next pointer points to itself")
{
  std::vector<uint8_t> exif = tiff_header();
  put16(exif, 0);
  put32(exif, 8); // next IFD is this IFD

  REQUIRE(read_exif_orientation_tag(exif.data(), (uint32_t) exif.size()) == 1);
}


TEST_CASE("exif orientation after a cyclic IFD pointer is still found")
{
  std::vector<uint8_t> exif = tiff_header();
  put16(exif, 2);
  put_entry(exif, 0x8769 /* ExifIFD pointer */, 4 /* LONG */, 1, 8 /* IFD0 itself */);
  put_entry(exif, 0x0112 /* Orientation */, 3 /* SHORT */, 1, 6u << 16);
  put32(exif, 0);

  REQUIRE(read_exif_orientation_tag(exif.data(), (uint32_t) exif.size()) == 6);
}


TEST_CASE("exif orientation beyond five IFDs is found")
{
  std::vector<uint8_t> exif = tiff_header();
  for (uint32_t i = 0; i < 7; i++) {
    put16(exif, 0);
    put32(exif, 8 + (i + 1) * 6);
  }
  put16(exif, 1);
  put_entry(exif, 0x0112 /* Orientation */, 3 /* SHORT */, 1, 7u << 16);
  put32(exif, 0);

  REQUIRE(read_exif_orientation_tag(exif.data(), (uint32_t) exif.size()) == 7);
}


TEST_CASE("deep cyclic exif IFD pointer does not exhaust the call stack")
{
  std::vector<uint8_t> exif = tiff_header();
  put16(exif, 1);
  put_entry(exif, 0x8769 /* ExifIFD pointer */, 4 /* LONG */, 1, 8 /* IFD0 itself */);
  put32(exif, 0);
  exif.resize(18 * 10000);

  REQUIRE(read_exif_orientation_tag(exif.data(), (uint32_t) exif.size()) == 1);
}


TEST_CASE("exif orientation in IFD0 is found")
{
  std::vector<uint8_t> exif = tiff_header();
  put16(exif, 2);
  put_entry(exif, 0x8769 /* ExifIFD pointer */, 4 /* LONG */, 1, 8 + 2 + 2 * 12 + 4);
  put_entry(exif, 0x0112 /* Orientation */, 3 /* SHORT */, 1, 6u << 16);
  put32(exif, 0); // no next IFD

  // Exif IFD
  put16(exif, 1);
  put_entry(exif, 0xA002 /* PixelXDimension */, 4 /* LONG */, 1, 10);
  put32(exif, 0);

  REQUIRE(read_exif_orientation_tag(exif.data(), (uint32_t) exif.size()) == 6);
}


TEST_CASE("exif tag in the Exif IFD is found")
{
  std::vector<uint8_t> exif = tiff_header();
  put16(exif, 1);
  put_entry(exif, 0x8769 /* ExifIFD pointer */, 4 /* LONG */, 1, 8 + 2 + 1 * 12 + 4);
  put32(exif, 0); // no next IFD

  // Exif IFD with the orientation tag (not where it belongs, but it has to be reachable through the pointer)
  const size_t exifIFD_pos = exif.size();
  put16(exif, 1);
  put_entry(exif, 0x0112 /* Orientation */, 3 /* SHORT */, 1, 8u << 16);
  put32(exif, 0);

  REQUIRE(read_exif_orientation_tag(exif.data(), (uint32_t) exif.size()) == 8);

  modify_exif_orientation_tag_if_it_exists(exif.data(), (uint32_t) exif.size(), 1);
  REQUIRE(exif[exifIFD_pos + 2 + 8] == 0);
  REQUIRE(exif[exifIFD_pos + 2 + 9] == 1);
  REQUIRE(read_exif_orientation_tag(exif.data(), (uint32_t) exif.size()) == 1);
}
