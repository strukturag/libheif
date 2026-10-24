/*
  libheif JPEG coded-size scan tests

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
#include "codecs/jpeg_dec.h"
#include "libheif/heif.h"

#include <cstdint>
#include <memory>
#include <vector>


static std::vector<uint8_t> make_jpeg_header(uint16_t width, uint16_t height)
{
  return {
      0xff, 0xd8,             // SOI
      0xff, 0xc0, 0x00, 0x11, // baseline SOF, segment length 17
      0x08,                   // sample precision
      static_cast<uint8_t>(height >> 8), static_cast<uint8_t>(height),
      static_cast<uint8_t>(width >> 8), static_cast<uint8_t>(width),
      0x03,                   // components
      0x01, 0x22, 0x00,      // Y: 2x2
      0x02, 0x11, 0x01,      // Cb: 1x1
      0x03, 0x11, 0x01,      // Cr: 1x1
      0xff, 0xd9              // EOI
  };
}


TEST_CASE("JPEG coded-size scan rejects an unparseable SOF")
{
  Decoder_JPEG decoder(nullptr);

  const std::vector<uint8_t> malformed{0xff, 0xd8, 0xff, 0xd9};
  auto result = decoder.get_max_coded_image_size(malformed);
  REQUIRE(result.is_error());
  CHECK(result.error().error_code == heif_error_Invalid_input);
}


TEST_CASE("JPEG coded-size scan checks every sequence sample")
{
  Decoder_JPEG decoder(nullptr);

  auto first = decoder.get_max_coded_image_size(make_jpeg_header(64, 48));
  REQUIRE(!first.is_error());
  REQUIRE(first->has_value());
  CHECK((*first)->width == 64);
  CHECK((*first)->height == 48);

  auto second = decoder.get_max_coded_image_size(make_jpeg_header(320, 240));
  REQUIRE(!second.is_error());
  REQUIRE(second->has_value());
  CHECK((*second)->width == 320);
  CHECK((*second)->height == 240);
}
