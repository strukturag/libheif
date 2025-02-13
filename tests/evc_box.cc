/*
  libheif EVC unit tests

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
#include "codecs/evc_boxes.h"
#include "error.h"
#include <cstdint>
#include <iostream>
#include <memory>

TEST_CASE("evcC") {
  std::vector<uint8_t> byteArray{
      0x00, 0x00, 0x00, 0x3d, 0x65, 0x76, 0x63, 0x43,
      0x01, 0x02, 0xd7, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x52, 0x01, 0x40, 0x00, 0xf0,
      0x03, 0x02, 0x98, 0x00, 0x01, 0x00, 0x15, 0x32,
      0x00, 0x80, 0x6b, 0x80, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x20, 0x0a, 0x08, 0x0f, 0x16,
      0xc0, 0x00, 0x54, 0x00, 0x99, 0x00, 0x01, 0x00,
      0x04, 0x34, 0x00, 0xfb, 0x00};

  auto reader = std::make_shared<StreamReader_memory>(byteArray.data(),
                                                      byteArray.size(), false);

  BitstreamRange range(reader, byteArray.size());
  std::shared_ptr<Box> box;
  Error error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error == Error::Ok);
  REQUIRE(range.error() == 0);

  REQUIRE(box->get_short_type() == fourcc("evcC"));
  REQUIRE(box->get_type_string() == "evcC");
  std::shared_ptr<Box_evcC> evcC = std::dynamic_pointer_cast<Box_evcC>(box);
  Box_evcC::configuration configuration = evcC->get_configuration();
  REQUIRE(configuration.configurationVersion == 1);
  REQUIRE(configuration.profile_idc == 2);
  // TODO: this value looks off
  REQUIRE(configuration.level_idc == 215);
  REQUIRE(configuration.toolset_idc_h == 0);
  REQUIRE(configuration.toolset_idc_l == 0);
  REQUIRE(configuration.chroma_format_idc == 1);
  REQUIRE(configuration.bit_depth_luma == 10);
  REQUIRE(configuration.bit_depth_chroma == 10);
  REQUIRE(configuration.pic_width_in_luma_samples == 320);
  REQUIRE(configuration.pic_height_in_luma_samples == 240);
  REQUIRE(configuration.lengthSize == 4);
  Indent indent;
  std::string dumpResult = box->dump(indent);
  REQUIRE(dumpResult == "Box: evcC -----\n"
                        "size: 61   (header size: 8)\n"
                        "configurationVersion: 1\n"
                        "profile_idc: 2 (Baseline Still)\n"
                        "level_idc: 215\n"
                        "toolset_idc_h: 0\n"
                        "toolset_idc_l: 0\n"
                        "chroma_format_idc: 1 (4:2:0)\n"
                        "bit_depth_luma: 10\n"
                        "bit_depth_chroma: 10\n"
                        "pic_width_in_luma_samples: 320\n"
                        "pic_height_in_luma_samples: 240\n"
                        "length_size: 4\n"
                        "<array>\n"
                        "| array_completeness: true\n"
                        "| NAL_unit_type: 24 (SPS_NUT)\n"
                        "| 32 00 80 6b 80 00 00 00 00 00 00 00 20 0a 08 0f 16 c0 00 54 00 \n"
                        "<array>\n"
                        "| array_completeness: true\n"
                        "| NAL_unit_type: 25 (PPS_NUT)\n"
                        "| 34 00 fb 00 \n");

  StreamWriter writer;
  Error err = evcC->write(writer);
  REQUIRE(err.error_code == heif_error_Ok);
  const std::vector<uint8_t> bytes = writer.get_data();
  REQUIRE(bytes == byteArray);
}
