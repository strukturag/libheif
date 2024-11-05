/*
  libheif AVC (H.264) unit tests

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
#include "codecs/avc_boxes.h"
#include "error.h"
#include <cstdint>
#include <iostream>
#include <memory>

TEST_CASE("avcC") {
  std::vector<uint8_t> byteArray{
      0x00, 0x00, 0x00, 0x34, 0x61, 0x76, 0x63, 0x43, 0x01, 0x42, 0x80,
      0x1e, 0xff, 0xe1, 0x00, 0x1a, 0x67, 0x64, 0x00, 0x28, 0xac, 0x72,
      0x04, 0x40, 0x40, 0x04, 0x1a, 0x10, 0x00, 0x00, 0x03, 0x00, 0x10,
      0x00, 0x00, 0x03, 0x03, 0x20, 0xf1, 0x83, 0x18, 0x46, 0x01, 0x00,
      0x07, 0x68, 0xe8, 0x43, 0x83, 0x92, 0xc8, 0xb0};

  auto reader = std::make_shared<StreamReader_memory>(byteArray.data(),
                                                      byteArray.size(), false);

  BitstreamRange range(reader, byteArray.size());
  std::shared_ptr<Box> box;
  Error error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error == Error::Ok);
  REQUIRE(range.error() == 0);

  REQUIRE(box->get_short_type() == fourcc("avcC"));
  REQUIRE(box->get_type_string() == "avcC");
  std::shared_ptr<Box_avcC> avcC = std::dynamic_pointer_cast<Box_avcC>(box);
  Box_avcC::configuration configuration = avcC->get_configuration();
  REQUIRE(configuration.configuration_version == 1);
  REQUIRE(configuration.AVCProfileIndication == 66);
  REQUIRE(configuration.profile_compatibility == 0x80);
  REQUIRE(configuration.AVCLevelIndication == 30);
  REQUIRE(avcC->getSequenceParameterSets().size() == 1);
  REQUIRE(avcC->getSequenceParameterSets()[0].size() == 0x1a);
  REQUIRE(avcC->getPictureParameterSets().size() == 1);
  REQUIRE(avcC->getPictureParameterSets()[0].size() == 7);
  Indent indent;
  std::string dumpResult = box->dump(indent);
  REQUIRE(dumpResult == "Box: avcC -----\n"
                        "size: 52   (header size: 8)\n"
                        "configuration_version: 1\n"
                        "AVCProfileIndication: 66 (Constrained Baseline)\n"
                        "profile_compatibility: 128\n"
                        "AVCLevelIndication: 30\n"
                        "Chroma format: 4:2:0\n"
                        "Bit depth luma: 8\n"
                        "Bit depth chroma: 8\n"
                        "SPS: 67 64 00 28 ac 72 04 40 40 04 1a 10 00 00 03 00 "
                        "10 00 00 03 03 20 f1 83 18 46 \n"
                        "PPS: 68 e8 43 83 92 c8 b0 \n");

  StreamWriter writer;
  Error err = avcC->write(writer);
  REQUIRE(err.error_code == heif_error_Ok);
  const std::vector<uint8_t> bytes = writer.get_data();
  REQUIRE(bytes == byteArray);
}
