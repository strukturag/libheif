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
#include "codecs/decoder.h"
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

TEST_CASE("Reject invalid chroma format in AVC SPS") {
  const std::vector<uint8_t> sps{
      0x20, 0x2c, 0x20, 0x20, 0x00, 0x20, 0x20, 0x00, 0x20, 0x20, 0x20, 0x20,
      0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
  Box_avcC::configuration configuration;
  uint32_t width = 0;
  uint32_t height = 0;

  Error error = parse_sps_for_avcC_configuration(
      sps.data(), sps.size(), &configuration, &width, &height);

  REQUIRE(error.error_code == heif_error_Invalid_input);
}

TEST_CASE("Reject invalid code in AVC SPS scaling list") {
  std::vector<uint8_t> sps{
      0x67, 0x64, 0x00, 0x1f, 0xad, 0x80, 0x00, 0x00, 0x00, 0x3f};
  sps.insert(sps.end(), 40, 0xff);
  Box_avcC::configuration configuration;
  uint32_t width = 0;
  uint32_t height = 0;

  Error error = parse_sps_for_avcC_configuration(
      sps.data(), sps.size(), &configuration, &width, &height);

  REQUIRE(error.error_code == heif_error_Invalid_input);
}


// Regression test for https://github.com/strukturag/libheif/issues/1866:
// the SPS frame cropping offsets are in chroma-dependent crop units
// (CropUnitX/CropUnitY), not in luma samples.
TEST_CASE("AVC SPS conformance window crop units") {

  SECTION("4:2:0 progressive, crop units are 2 luma samples") {
    // SPS generated by x264 for a 150x150 image: coded size 160x160,
    // frame_crop_right_offset = frame_crop_bottom_offset = 5 units = 10 pixels.
    const std::vector<uint8_t> sps{
        0x67, 0x4d, 0x40, 0x0b, 0xec, 0xc1, 0x42, 0xbc,
        0xd3, 0x42, 0x00, 0x00, 0x03, 0x00, 0x32, 0x00,
        0x00, 0x03, 0x00, 0x04, 0x1e, 0x28, 0x53, 0x34};

    Box_avcC::configuration configuration;
    uint32_t width = 0;
    uint32_t height = 0;
    ImageSize coded_size{};

    Error error = parse_sps_for_avcC_configuration(
        sps.data(), sps.size(), &configuration, &width, &height, &coded_size);

    REQUIRE(error == Error::Ok);
    REQUIRE(configuration.chroma_format == heif_chroma_420);
    REQUIRE(coded_size.width == 160);
    REQUIRE(coded_size.height == 160);
    REQUIRE(width == 150);
    REQUIRE(height == 150);
  }

  SECTION("4:4:4 progressive, crop units are 1 luma sample") {
    // High profile, chroma_format_idc = 3, coded size 160x160,
    // frame_crop_right_offset = frame_crop_bottom_offset = 5 units = 5 pixels.
    const std::vector<uint8_t> sps{
        0x67, 0x64, 0x00, 0x1f, 0x91, 0x96, 0x82, 0x85, 0x79, 0xa6, 0x40};

    Box_avcC::configuration configuration;
    uint32_t width = 0;
    uint32_t height = 0;
    ImageSize coded_size{};

    Error error = parse_sps_for_avcC_configuration(
        sps.data(), sps.size(), &configuration, &width, &height, &coded_size);

    REQUIRE(error == Error::Ok);
    REQUIRE(configuration.chroma_format == heif_chroma_444);
    REQUIRE(coded_size.width == 160);
    REQUIRE(coded_size.height == 160);
    REQUIRE(width == 155);
    REQUIRE(height == 155);
  }

  SECTION("4:2:0 interlaced, map units count double, vertical crop unit is 4") {
    // Main profile, frame_mbs_only_flag = 0, 5 map units = 160 pixels frame height,
    // frame_crop_right_offset = 5 units = 10 pixels,
    // frame_crop_bottom_offset = 2 units = 8 pixels.
    const std::vector<uint8_t> sps{
        0x67, 0x4d, 0x40, 0x1e, 0xda, 0x0a, 0x29, 0xcd, 0x68};

    Box_avcC::configuration configuration;
    uint32_t width = 0;
    uint32_t height = 0;
    ImageSize coded_size{};

    Error error = parse_sps_for_avcC_configuration(
        sps.data(), sps.size(), &configuration, &width, &height, &coded_size);

    REQUIRE(error == Error::Ok);
    REQUIRE(coded_size.width == 160);
    REQUIRE(coded_size.height == 160);
    REQUIRE(width == 150);
    REQUIRE(height == 152);
  }
}
