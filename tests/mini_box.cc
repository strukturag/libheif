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
#include <bitstream.h>
#include <box.h>
#include "error.h"
#include <logging.h>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <mini.h>
#include "test_utils.h"
#include "test-config.h"
#include <file_layout.h>

TEST_CASE("mini")
{
  std::vector<uint8_t> byteArray{
      0x00, 0x00, 0x00, 0x10, 0x66, 0x74, 0x79, 0x70,
      0x6d, 0x69, 0x66, 0x33, 0x61, 0x76, 0x69, 0x66,
      0x00, 0x00, 0x00, 0x4a, 0x6d, 0x69, 0x6e, 0x69,
      0x08, 0x18, 0x00, 0xff, 0x01, 0xfe, 0xe0, 0x03,
      0x40, 0x81, 0x20, 0x00, 0x00, 0x12, 0x00, 0x0a,
      0x09, 0x38, 0x1d, 0xff, 0xff, 0xd8, 0x40, 0x43,
      0x41, 0xa4, 0x32, 0x26, 0x11, 0x90, 0x01, 0x86,
      0x18, 0x61, 0x00, 0xb4, 0x83, 0x5a, 0x70, 0x50,
      0x8b, 0xe5, 0x7d, 0xf5, 0xc7, 0xd3, 0x6e, 0x92,
      0xea, 0x80, 0x01, 0x50, 0x91, 0xc4, 0x06, 0xa3,
      0xe1, 0xca, 0x44, 0x43, 0xe7, 0xb8, 0x67, 0x43,
      0xea, 0x80};

  auto reader = std::make_shared<StreamReader_memory>(byteArray.data(),
                                                      byteArray.size(), false);

  BitstreamRange range(reader, byteArray.size());
  std::shared_ptr<Box> box;
  Error error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error == Error::Ok);
  REQUIRE(range.error() == 0);

  REQUIRE(box->get_short_type() == fourcc("ftyp"));
  REQUIRE(box->get_type_string() == "ftyp");
  std::shared_ptr<Box_ftyp> ftyp = std::dynamic_pointer_cast<Box_ftyp>(box);
  REQUIRE(ftyp->get_major_brand() == fourcc("mif3"));
  REQUIRE(ftyp->get_minor_version() == fourcc("avif"));
  REQUIRE(ftyp->list_brands().size() == 0);
  Indent indent;
  std::string dumpResult = box->dump(indent);
  REQUIRE(dumpResult == "Box: ftyp ----- (File Type)\n"
                        "size: 16   (header size: 8)\n"
                        "major brand: mif3\n"
                        "minor version: avif\n"
                        "compatible brands: \n");

  error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error == Error::Ok);
  REQUIRE(range.error() == 0);

  REQUIRE(box->get_short_type() == fourcc("mini"));
  REQUIRE(box->get_type_string() == "mini");
  std::shared_ptr<Box_mini> mini = std::dynamic_pointer_cast<Box_mini>(box);
  REQUIRE(mini->get_exif_flag() == false);
  REQUIRE(mini->get_xmp_flag() == false);
  REQUIRE(mini->get_bit_depth() == 8);
  REQUIRE(mini->get_colour_primaries() == 1);
  REQUIRE(mini->get_transfer_characteristics() == 13);
  REQUIRE(mini->get_matrix_coefficients() == 6);
  REQUIRE(mini->get_width() == 256);
  REQUIRE(mini->get_height() == 256);
  REQUIRE(mini->get_main_item_codec_config().size() == 4);
  REQUIRE((int)(mini->get_main_item_codec_config().data()[0]) == 0x81);
  REQUIRE((int)(mini->get_main_item_codec_config().data()[1]) == 0x20);
  REQUIRE((int)(mini->get_main_item_codec_config().data()[2]) == 0x00);
  REQUIRE((int)(mini->get_main_item_codec_config().data()[3]) == 0x00);
  dumpResult = box->dump(indent);
  REQUIRE(dumpResult == "Box: mini -----\n"
                        "size: 74   (header size: 8)\n"
                        "version: 0\n"
                        "explicit_codec_types_flag: 0\n"
                        "float_flag: 0\n"
                        "full_range_flag: 1\n"
                        "alpha_flag: 0\n"
                        "explicit_cicp_flag: 0\n"
                        "hdr_flag: 0\n"
                        "icc_flag: 0\n"
                        "exif_flag: 0\n"
                        "xmp_flag: 0\n"
                        "chroma_subsampling: 3\n"
                        "orientation: 1\n"
                        "width: 256\n"
                        "height: 256\n"
                        "bit_depth: 8\n"
                        "colour_primaries: 1\n"
                        "transfer_characteristics: 13\n"
                        "matrix_coefficients: 6\n"
                        "main_item_code_config size: 4\n"
                        "main_item_data offset: 37, size: 53\n");
}

TEST_CASE("check mini+alpha version")
{
  auto istr = std::unique_ptr<std::istream>(new std::ifstream(tests_data_directory + "/simple_osm_tile_alpha.avif", std::ios::binary));
  auto reader = std::make_shared<StreamReader_istream>(std::move(istr));
  FileLayout file;
  Error err = file.read(reader, heif_get_global_security_limits());
  REQUIRE(err.error_code == heif_error_Ok);

  std::shared_ptr<Box_mini> mini = file.get_mini_box();
  REQUIRE(mini->get_exif_flag() == false);
  REQUIRE(mini->get_xmp_flag() == false);
  REQUIRE(mini->get_bit_depth() == 8);
  REQUIRE(mini->get_colour_primaries() == 2);
  REQUIRE(mini->get_transfer_characteristics() == 2);
  REQUIRE(mini->get_matrix_coefficients() == 6);
  REQUIRE(mini->get_width() == 256);
  REQUIRE(mini->get_height() == 256);
  REQUIRE(mini->get_main_item_codec_config().size() == 4);
  REQUIRE((int)(mini->get_main_item_codec_config().data()[0]) == 0x81);
  REQUIRE((int)(mini->get_main_item_codec_config().data()[1]) == 0x20);
  REQUIRE((int)(mini->get_main_item_codec_config().data()[2]) == 0x00);
  REQUIRE((int)(mini->get_main_item_codec_config().data()[3]) == 0x00);
  Indent indent;
  std::string dumpResult = mini->dump(indent);
  REQUIRE(dumpResult == "Box: mini -----\n"
                        "size: 788   (header size: 8)\n"
                        "version: 0\n"
                        "explicit_codec_types_flag: 0\n"
                        "float_flag: 0\n"
                        "full_range_flag: 1\n"
                        "alpha_flag: 1\n"
                        "explicit_cicp_flag: 0\n"
                        "hdr_flag: 0\n"
                        "icc_flag: 1\n"
                        "exif_flag: 0\n"
                        "xmp_flag: 0\n"
                        "chroma_subsampling: 3\n"
                        "orientation: 1\n"
                        "width: 256\n"
                        "height: 256\n"
                        "bit_depth: 8\n"
                        "alpha_is_premultiplied: 0\n"
                        "colour_primaries: 2\n"
                        "transfer_characteristics: 2\n"
                        "matrix_coefficients: 6\n"
                        "alpha_item_code_config size: 4\n"
                        "main_item_code_config size: 4\n"
                        "icc_data size: 672\n"
                        "alpha_item_data offset: 717, size: 34\n"
                        "main_item_data offset: 751, size: 53\n");
}

TEST_CASE("check mini+exif+xmp version")
{
  auto istr = std::unique_ptr<std::istream>(new std::ifstream(tests_data_directory + "/simple_osm_tile_meta.avif", std::ios::binary));
  auto reader = std::make_shared<StreamReader_istream>(std::move(istr));
  FileLayout file;
  Error err = file.read(reader, heif_get_global_security_limits());
  REQUIRE(err.error_code == heif_error_Ok);

  std::shared_ptr<Box_mini> mini = file.get_mini_box();
  REQUIRE(mini->get_exif_flag() == true);
  REQUIRE(mini->get_xmp_flag() == true);
  REQUIRE(mini->get_bit_depth() == 8);
  REQUIRE(mini->get_colour_primaries() == 2);
  REQUIRE(mini->get_transfer_characteristics() == 2);
  REQUIRE(mini->get_matrix_coefficients() == 6);
  REQUIRE(mini->get_width() == 256);
  REQUIRE(mini->get_height() == 256);
  REQUIRE(mini->get_main_item_codec_config().size() == 4);
  REQUIRE((int)(mini->get_main_item_codec_config().data()[0]) == 0x81);
  REQUIRE((int)(mini->get_main_item_codec_config().data()[1]) == 0x20);
  REQUIRE((int)(mini->get_main_item_codec_config().data()[2]) == 0x00);
  REQUIRE((int)(mini->get_main_item_codec_config().data()[3]) == 0x00);
  Indent indent;
  std::string dumpResult = mini->dump(indent);
  REQUIRE(dumpResult == "Box: mini -----\n"
                        "size: 4388   (header size: 8)\n"
                        "version: 0\n"
                        "explicit_codec_types_flag: 0\n"
                        "float_flag: 0\n"
                        "full_range_flag: 1\n"
                        "alpha_flag: 0\n"
                        "explicit_cicp_flag: 0\n"
                        "hdr_flag: 0\n"
                        "icc_flag: 1\n"
                        "exif_flag: 1\n"
                        "xmp_flag: 1\n"
                        "chroma_subsampling: 3\n"
                        "orientation: 1\n"
                        "width: 256\n"
                        "height: 256\n"
                        "bit_depth: 8\n"
                        "colour_primaries: 2\n"
                        "transfer_characteristics: 2\n"
                        "matrix_coefficients: 6\n"
                        "main_item_code_config size: 4\n"
                        "icc_data size: 672\n"
                        "main_item_data offset: 717, size: 53\n"
                        "exif_data offset: 770, size: 208\n"
                        "xmp_data offset: 978, size: 3426\n");
}


TEST_CASE("check heif mini")
{
  auto istr = std::unique_ptr<std::istream>(new std::ifstream(tests_data_directory + "/lightning_mini.heif", std::ios::binary));
  auto reader = std::make_shared<StreamReader_istream>(std::move(istr));
  FileLayout file;
  Error err = file.read(reader, heif_get_global_security_limits());
  REQUIRE(err.error_code == heif_error_Ok);

  std::shared_ptr<Box_mini> mini = file.get_mini_box();
  REQUIRE(mini->get_exif_flag() == false);
  REQUIRE(mini->get_xmp_flag() == false);
  REQUIRE(mini->get_bit_depth() == 8);
  REQUIRE(mini->get_colour_primaries() == 1);
  REQUIRE(mini->get_transfer_characteristics() == 13);
  REQUIRE(mini->get_matrix_coefficients() == 6);
  REQUIRE(mini->get_width() == 128);
  REQUIRE(mini->get_height() == 128);
  REQUIRE(mini->get_main_item_codec_config().size() == 112);
  Indent indent;
  std::string dumpResult = mini->dump(indent);
  REQUIRE(dumpResult == "Box: mini -----\n"
                        "size: 4710   (header size: 8)\n"
                        "version: 0\n"
                        "explicit_codec_types_flag: 0\n"
                        "float_flag: 0\n"
                        "full_range_flag: 1\n"
                        "alpha_flag: 0\n"
                        "explicit_cicp_flag: 0\n"
                        "hdr_flag: 0\n"
                        "icc_flag: 0\n"
                        "exif_flag: 0\n"
                        "xmp_flag: 0\n"
                        "chroma_subsampling: 1\n"
                        "orientation: 1\n"
                        "width: 128\n"
                        "height: 128\n"
                        "chroma_is_horizontally_centered: 0\n"
                        "chroma_is_vertically_centered: 0\n"
                        "bit_depth: 8\n"
                        "colour_primaries: 1\n"
                        "transfer_characteristics: 13\n"
                        "matrix_coefficients: 6\n"
                        "main_item_code_config size: 112\n"
                        "main_item_data offset: 144, size: 4582\n");
}

