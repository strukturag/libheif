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

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "image/pixelimage.h"

#include <cstdint>

// fill_channel() writes the value with the width of the plane's storage word. It used to
// support only 8-bit and 16-bit planes: a wider plane was written as 16-bit words, which
// filled only a part of each row.
TEST_CASE( "fill_channel for all plane widths" )
{
  auto* limits = heif_get_global_security_limits();
  const uint32_t w = 5;
  const uint32_t h = 3;

  struct Case { int bit_depth; uint64_t value; };

  // 24 and 48 bits are stored in the next larger word (32 and 64 bits).
  for (const Case& c : {Case{8, 0xA5}, Case{10, 1023}, Case{16, 0xFFFF}, Case{24, 0xABCDEF},
                        Case{32, 0xFFFFFFFFu}, Case{48, 0xABCDEF012345ull},
                        Case{64, 0xFFFFFFFFFFFFFFFFull}, Case{64, 0x0123456789ABCDEFull}}) {
    INFO("bit depth " << c.bit_depth);

    HeifPixelImage image;
    image.create(w, h, heif_colorspace_custom, heif_chroma_planar);
    REQUIRE(!image.add_channel(heif_channel_Y, w, h, c.bit_depth, limits, heif_component_datatype_unsigned_integer));
    REQUIRE(!image.fill_channel(heif_channel_Y, c.value));

    size_t stride;
    const uint8_t* data = image.get_channel_memory(heif_channel_Y, &stride);
    const size_t bytes_per_sample = image.get_storage_bits_per_pixel(heif_channel_Y) / 8;

    for (uint32_t y = 0; y < h; y++) {
      for (uint32_t x = 0; x < w; x++) {
        uint64_t sample = 0;
        switch (bytes_per_sample) {
          case 1: sample = data[y * stride + x]; break;
          case 2: sample = reinterpret_cast<const uint16_t*>(data + y * stride)[x]; break;
          case 4: sample = reinterpret_cast<const uint32_t*>(data + y * stride)[x]; break;
          case 8: sample = reinterpret_cast<const uint64_t*>(data + y * stride)[x]; break;
          default: FAIL("unexpected sample size");
        }
        REQUIRE(sample == c.value);
      }
    }
  }

  SECTION("all components of an interleaved plane") {
    HeifPixelImage image;
    image.create(w, h, heif_colorspace_RGB, heif_chroma_interleaved_RGB);
    REQUIRE(!image.fill_new_channel(heif_channel_interleaved, 0x7F, w, h, 8, limits));

    size_t stride;
    const uint8_t* data = image.get_channel_memory(heif_channel_interleaved, &stride);
    for (uint32_t y = 0; y < h; y++) {
      for (uint32_t x = 0; x < 3 * w; x++) {
        REQUIRE(data[y * stride + x] == 0x7F);
      }
    }
  }

  SECTION("planes with more than 64 bits and missing planes are refused") {
    HeifPixelImage image;
    image.create(w, h, heif_colorspace_custom, heif_chroma_planar);
    REQUIRE(!image.add_channel(heif_channel_Y, w, h, 128, limits, heif_component_datatype_complex_number));

    CHECK(image.fill_channel(heif_channel_Y, 0));
    CHECK(image.fill_channel(heif_channel_Alpha, 0));
  }
}
