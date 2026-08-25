/*
  libheif clean aperture (clap) zero-size unit tests

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
#include "box.h"
#include <cmath>
#include <limits>

// The checked Fraction factories replace the former Fraction(uint32_t, uint32_t)
// constructor, which only assert()ed its value range (GHSA-gh5q-69gg-c964).

TEST_CASE("Fraction::from_unsigned") {
  auto f = Fraction::from_unsigned(0x7FFFFFFF, 2);
  REQUIRE(f);
  REQUIRE(std::abs(f->to_double() - 0x7FFFFFFF / 2.0) < 1.0);

  REQUIRE(Fraction::from_unsigned(0, 1));
  REQUIRE(Fraction::from_unsigned(0x7FFFFFFF, 0x7FFFFFFF));

  REQUIRE(!Fraction::from_unsigned(0x80000000, 1));
  REQUIRE(!Fraction::from_unsigned(1, 0x80000000));
  REQUIRE(!Fraction::from_unsigned(0xFFFFFFFF, 2));
  REQUIRE(!Fraction::from_unsigned(1, 0));
}

TEST_CASE("Fraction::from_signed") {
  const int64_t max = std::numeric_limits<int32_t>::max();
  const int64_t min = std::numeric_limits<int32_t>::min();

  auto f = Fraction::from_signed(-5, 3);
  REQUIRE(f);
  REQUIRE(f->numerator == -5);
  REQUIRE(f->denominator == 3);

  REQUIRE(Fraction::from_signed(max, 1));
  REQUIRE(Fraction::from_signed(min, 1));
  REQUIRE(Fraction::from_signed(1, max));

  REQUIRE(!Fraction::from_signed(max + 1, 1));
  REQUIRE(!Fraction::from_signed(min - 1, 1));
  REQUIRE(!Fraction::from_signed(1, max + 1));
  REQUIRE(!Fraction::from_signed(1, min - 1));

  auto zero_den = Fraction::from_signed(1, 0);
  REQUIRE(!zero_den);
  REQUIRE(zero_den.error().error_code == heif_error_Invalid_input);
  REQUIRE(zero_den.error().sub_error_code == heif_suberror_Invalid_fractional_number);
}
