/*
  libheif Box equality unit tests

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
#include "box.h"

TEST_CASE("box equals") {
  std::shared_ptr<Box_ispe> ispe1 = std::make_shared<Box_ispe>();
  ispe1->set_size(100, 200);

  std::shared_ptr<Box_ispe> ispe2 = std::make_shared<Box_ispe>();
  ispe2->set_size(100, 250);
  
  std::shared_ptr<Box_ispe> ispe3 = std::make_shared<Box_ispe>();
  ispe3->set_size(100, 200);
  
  std::shared_ptr<Box_clap> clap = std::make_shared<Box_clap>();
  clap->set(100, 200, 150, 250);

  REQUIRE(Box::equal(ispe1, ispe2) == false);
  REQUIRE(Box::equal(ispe1, ispe3) == true);
  REQUIRE(Box::equal(ispe3, ispe1) == true);
  REQUIRE(Box::equal(ispe1, 0) == false);
  REQUIRE(Box::equal(clap, ispe1) == false);
  REQUIRE(Box::equal(0, ispe1) == false);
}

TEST_CASE("add_box") {
  std::shared_ptr<Box_ispe> ispe1 = std::make_shared<Box_ispe>();
  ispe1->set_size(100, 200);

  std::shared_ptr<Box_ispe> ispe2 = std::make_shared<Box_ispe>();
  ispe2->set_size(100, 250);
  
  std::shared_ptr<Box_ispe> ispe3 = std::make_shared<Box_ispe>();
  ispe3->set_size(100, 200);
  
  std::shared_ptr<Box_ipco> ipco = std::make_shared<Box_ipco>();
  REQUIRE(ipco->find_or_append_child_box(ispe1) == 0);
  REQUIRE(ipco->find_or_append_child_box(ispe2) == 1);
  REQUIRE(ipco->find_or_append_child_box(ispe3) == 0);
}
