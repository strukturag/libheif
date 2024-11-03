/*
  libheif bitstream unit tests

  MIT License

  Copyright (c) 2024 Brad Hards <bradh@frogmouth.net>

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
#include "error.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <bitstream.h>


TEST_CASE("read bits") {
  std::vector<uint8_t> byteArray{0x7f, 0xf1, 0b01000001, 0b10000111, 0b10001111};
  BitReader uut(byteArray.data(), (int)byteArray.size());
  uint32_t byte0 = uut.get_bits(8);
  REQUIRE(byte0 == 0x7f);
  uint32_t byte1_high = uut.get_bits(4);
  REQUIRE(byte1_high == 0x0f);
  uint32_t byte1_low = uut.get_bits(4);
  REQUIRE(byte1_low == 0x01);
  uint32_t byte2_partial1 = uut.get_bits(3);
  REQUIRE(byte2_partial1 == 0x02);
  uint32_t byte2_partial2 = uut.get_bits(3);
  REQUIRE(byte2_partial2 == 0x00);
  uint32_t byte2_3_overlap = uut.get_bits(11);
  REQUIRE(byte2_3_overlap == 0b0000001100001111);
}

TEST_CASE("read uint8") {
  std::vector<uint8_t> byteArray{0x7f, 0xf1, 0b01000001, 0b10000111, 0b10001111};
  BitReader uut(byteArray.data(), (int)byteArray.size());
  uint8_t byte0 = uut.get_bits8(8);
  REQUIRE(byte0 == 0x7f);
  uint8_t byte1_high = uut.get_bits8(4);
  REQUIRE(byte1_high == 0x0f);
  uint8_t byte1_low = uut.get_bits8(4);
  REQUIRE(byte1_low == 0x01);
  uint8_t byte2_partial1 = uut.get_bits8(3);
  REQUIRE(byte2_partial1 == 0x02);
  uint8_t byte2_partial2 = uut.get_bits8(3);
  REQUIRE(byte2_partial2 == 0x00);
  uint8_t byte2_3_overlap = uut.get_bits8(8);
  REQUIRE((int)byte2_3_overlap == 0b1100001);
}

TEST_CASE("read uint32") {
  std::vector<uint8_t> byteArray{0x7f, 0b11110001, 0b01000001, 0b10000111, 0b10001111};
  BitReader uut(byteArray.data(), (int)byteArray.size());
  uint32_t byte0 = uut.get_bits32(8);
  REQUIRE(byte0 == 0x7f);
  uint32_t byte1_high = uut.get_bits(1);
  REQUIRE(byte1_high == 0x01);
  uint32_t overlap = uut.get_bits32(30);
  REQUIRE(overlap == 0b111000101000001100001111000111);
}

TEST_CASE("read float") {
  std::vector<uint8_t> byteArray{0x40, 0x00, 0x00, 0x00};
  std::shared_ptr<StreamReader_memory> stream = std::make_shared<StreamReader_memory>(byteArray.data(), (int)byteArray.size(), false);
  BitstreamRange uut(stream, byteArray.size(), nullptr);
  float f = uut.read_float32();
  REQUIRE(f == 2.0);
}
