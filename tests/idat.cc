/*
  libheif Item Data Box (idat) unit tests

  MIT License

  Copyright (c) 2023 Brad Hards <bradh@frogmouth.net>

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
#include <cstdint>
#include <iostream>

TEST_CASE("idat bad") {
  std::vector<uint8_t> testData{0x00, 0x00, 0x00, 0x00, 'i',
                                'd',  'a',  't',  0x65};
  auto reader = std::make_shared<StreamReader_memory>(testData.data(),
                                                      testData.size(), false);

  BitstreamRange range(reader, testData.size());
  for (;;) {
    std::shared_ptr<Box> box;
    Error error = Box::read(range, &box, heif_get_global_security_limits());
    if (error != Error::Ok || range.error()) {
      break;
    }

    box->get_type();
    box->get_type_string();
    Indent indent;
    box->dump(indent);
  }
}
