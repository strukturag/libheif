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

// Regression test for GHSA-jc8f-p23p-5hjg: passing a zero image dimension to
// the clap rounding helpers used to underflow `image_width - 1U` to UINT32_MAX,
// which overflowed the Fraction constructor (assert abort in debug builds,
// corrupt crop in release builds). They must now return 0 without aborting.
TEST_CASE("clap rounding with zero image size") {
  std::shared_ptr<Box_clap> clap = std::make_shared<Box_clap>();
  clap->set(100, 200, 150, 250);  // clap 100x200 inside a 150x250 image

  REQUIRE(clap->left_rounded(0) == 0);
  REQUIRE(clap->right_rounded(0) == 99);    // clapWidth - 1 + left(0)
  REQUIRE(clap->top_rounded(0) == 0);
  REQUIRE(clap->bottom_rounded(0) == 199);  // clapHeight - 1 + top(0)
}
