/*
  libheif HEVC SPS parsing unit tests

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
#include "codecs/hevc_boxes.h"
#include "codecs/decoder.h"
#include "error.h"
#include <cstdint>
#include <vector>

// SPS NAL unit (with emulation prevention bytes) taken from the hvcC box of
// tests/data/rainbow-451x461.heic. x265 padded the 451x461 input to a coded
// size of 456x464 and signals a conformance window that crops it to 452x462.
static const std::vector<uint8_t> rainbow_sps{
    0x42, 0x01, 0x01, 0x03, 0x70, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x03, 0x00, 0x3f, 0xa0, 0x0e, 0x48, 0x07, 0x47, 0x75,
    0x96, 0xea, 0x49, 0x29, 0xae, 0x6e, 0x02, 0x1a, 0x0c, 0x08, 0x00, 0x00,
    0x03, 0x00, 0xc8, 0x00, 0x00, 0x03, 0x00, 0x08, 0x40};


TEST_CASE("SPS conformance window yields visible and coded size")
{
  HEVCDecoderConfigurationRecord config;
  uint32_t width = 0, height = 0;
  ImageSize coded{};

  Error err = parse_sps_for_hvcC_configuration(rainbow_sps.data(), rainbow_sps.size(),
                                               &config, &width, &height, &coded);
  REQUIRE(!err);

  CHECK(config.chroma_format == 1);
  CHECK(config.bit_depth_luma == 8);
  CHECK(config.bit_depth_chroma == 8);

  CHECK(width == 452);
  CHECK(height == 462);
  CHECK(coded.width == 456);
  CHECK(coded.height == 464);
}


TEST_CASE("SPS chroma_format_idc out of range is rejected")
{
  // Same SPS with chroma_format_idc changed from 1 (uvlc '010') to 4
  // (uvlc '00101'). The byte at index 18 holds sps_seq_parameter_set_id and
  // the start of chroma_format_idc: '1 010 ....' becomes '1 00101 ..'.
  std::vector<uint8_t> sps = rainbow_sps;
  REQUIRE((sps[18] & 0xF0) == 0xA0);
  sps[18] = 0x94 | (sps[18] & 0x03);

  HEVCDecoderConfigurationRecord config;
  uint32_t width = 0, height = 0;
  ImageSize coded{};

  Error err = parse_sps_for_hvcC_configuration(sps.data(), sps.size(),
                                               &config, &width, &height, &coded);
  REQUIRE(err);
  CHECK(err.error_code == heif_error_Invalid_input);
  CHECK(err.sub_error_code == heif_suberror_Invalid_parameter_value);
  CHECK(err.message.find("chroma_format_idc") != std::string::npos);
}
