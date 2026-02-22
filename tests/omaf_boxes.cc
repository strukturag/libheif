/*
  libheif OMAF (ISO/IEC 23090-2) unit tests

  MIT License

  Copyright (c) 2026 Brad Hards <bradh@frogmouth.net>

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
#include "omaf_boxes.h"
#include "error.h"
#include <cstdint>
#include <iostream>
#include <memory>


TEST_CASE("prfr") {
  std::vector<uint8_t> byteArray{0x00, 0x00, 0x00, 0x0d, 0x70, 0x72, 0x66, 0x72, 0x00, 0x00, 0x00, 0x00, 0x01};

  auto reader = std::make_shared<StreamReader_memory>(byteArray.data(),
                                                      byteArray.size(), false);

  BitstreamRange range(reader, byteArray.size());
  std::shared_ptr<Box> box;
  Error error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error == Error::Ok);
  REQUIRE(range.error() == 0);

  REQUIRE(box->get_short_type() == fourcc("prfr"));
  REQUIRE(box->get_type_string() == "prfr");
  std::shared_ptr<Box_prfr> prfr = std::dynamic_pointer_cast<Box_prfr>(box);
  REQUIRE(prfr->get_image_projection() == heif_image_projection::cube_map);
  Indent indent;
  std::string dumpResult = box->dump(indent);
  REQUIRE(dumpResult == "Box: prfr ----- (Projection Format)\n"
                        "size: 13   (header size: 12)\n"
                        "projection_type: 1\n");

  StreamWriter writer;
  Error err = prfr->write(writer);
  REQUIRE(err.error_code == heif_error_Ok);
  const std::vector<uint8_t> bytes = writer.get_data();
  REQUIRE(bytes == byteArray);
}
