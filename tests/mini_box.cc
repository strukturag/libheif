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
      0x08, 0x18, 0x80, 0xff, 0x01, 0xfe, 0x20, 0x03,
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

TEST_CASE("mini write round-trip from scratch")
{
  // Construct a Box_mini from scratch using setters
  auto mini = std::make_shared<Box_mini>();
  mini->set_version(0);
  mini->set_explicit_codec_types_flag(false);
  mini->set_float_flag(false);
  mini->set_full_range_flag(true);
  mini->set_alpha_flag(false);
  mini->set_explicit_cicp_flag(false);
  mini->set_hdr_flag(false);
  mini->set_icc_flag(false);
  mini->set_exif_flag(false);
  mini->set_xmp_flag(false);
  mini->set_chroma_subsampling(3);  // 4:4:4
  mini->set_orientation(1);
  mini->set_width(256);
  mini->set_height(256);
  mini->set_bit_depth(8);
  mini->set_colour_primaries(1);
  mini->set_transfer_characteristics(13);
  mini->set_matrix_coefficients(6);

  // Codec config (4 bytes)
  mini->set_main_item_codec_config({0x81, 0x20, 0x00, 0x00});

  // Fake main item data (10 bytes)
  std::vector<uint8_t> fake_data(10, 0xAB);
  mini->set_main_item_data(fake_data);

  // Write
  StreamWriter writer;
  Error error = mini->write(writer);
  REQUIRE(error == Error::Ok);

  // Parse back
  auto written_data = writer.get_data();
  auto reader = std::make_shared<StreamReader_memory>(written_data.data(), written_data.size(), false);
  BitstreamRange range(reader, written_data.size());

  std::shared_ptr<Box> box;
  error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error == Error::Ok);
  auto mini2 = std::dynamic_pointer_cast<Box_mini>(box);
  REQUIRE(mini2 != nullptr);

  // Compare
  REQUIRE(mini2->get_width() == 256);
  REQUIRE(mini2->get_height() == 256);
  REQUIRE(mini2->get_bit_depth() == 8);
  REQUIRE(mini2->get_icc_flag() == false);
  REQUIRE(mini2->get_exif_flag() == false);
  REQUIRE(mini2->get_xmp_flag() == false);
  REQUIRE(mini2->get_full_range_flag() == true);
  REQUIRE(mini2->get_colour_primaries() == 1);
  REQUIRE(mini2->get_transfer_characteristics() == 13);
  REQUIRE(mini2->get_matrix_coefficients() == 6);
  REQUIRE(mini2->get_orientation() == 1);
  REQUIRE(mini2->get_main_item_codec_config().size() == 4);
  REQUIRE(mini2->get_main_item_codec_config() == std::vector<uint8_t>({0x81, 0x20, 0x00, 0x00}));
  REQUIRE(mini2->get_main_item_data_size() == 10);
}


TEST_CASE("mini write round-trip with alpha and ICC from scratch")
{
  auto mini = std::make_shared<Box_mini>();
  mini->set_version(0);
  mini->set_explicit_codec_types_flag(false);
  mini->set_float_flag(false);
  mini->set_full_range_flag(true);
  mini->set_alpha_flag(true);
  mini->set_explicit_cicp_flag(false);
  mini->set_hdr_flag(false);
  mini->set_icc_flag(true);
  mini->set_exif_flag(false);
  mini->set_xmp_flag(false);
  mini->set_chroma_subsampling(3);
  mini->set_orientation(1);
  mini->set_width(256);
  mini->set_height(256);
  mini->set_bit_depth(8);
  mini->set_alpha_is_premultiplied(false);

  // CICP defaults for ICC: primaries=2, transfer=2, matrix=6
  mini->set_colour_primaries(2);
  mini->set_transfer_characteristics(2);
  mini->set_matrix_coefficients(6);

  mini->set_main_item_codec_config({0x81, 0x20, 0x00, 0x00});
  // Alpha uses same codec config (will be zero-size in bitstream = reuse main)
  mini->set_alpha_item_codec_config({0x81, 0x20, 0x00, 0x00});

  // Fake ICC data
  std::vector<uint8_t> icc_data(100, 0xCC);
  mini->set_icc_data(icc_data);

  // Fake image data
  std::vector<uint8_t> main_data(50, 0xAA);
  mini->set_main_item_data(main_data);
  std::vector<uint8_t> alpha_data(30, 0xBB);
  mini->set_alpha_item_data(alpha_data);

  // Write
  StreamWriter writer;
  Error error = mini->write(writer);
  REQUIRE(error == Error::Ok);

  // Parse back
  auto written_data = writer.get_data();
  auto reader = std::make_shared<StreamReader_memory>(written_data.data(), written_data.size(), false);
  BitstreamRange range(reader, written_data.size());

  std::shared_ptr<Box> box;
  error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error == Error::Ok);
  auto mini2 = std::dynamic_pointer_cast<Box_mini>(box);
  REQUIRE(mini2 != nullptr);

  REQUIRE(mini2->get_width() == 256);
  REQUIRE(mini2->get_height() == 256);
  REQUIRE(mini2->get_bit_depth() == 8);
  REQUIRE(mini2->get_icc_flag() == true);
  REQUIRE(mini2->get_full_range_flag() == true);
  REQUIRE(mini2->get_colour_primaries() == 2);
  REQUIRE(mini2->get_transfer_characteristics() == 2);
  REQUIRE(mini2->get_matrix_coefficients() == 6);
  REQUIRE(mini2->get_icc_data().size() == 100);
  REQUIRE(mini2->get_icc_data() == icc_data);
  REQUIRE(mini2->get_main_item_codec_config() == std::vector<uint8_t>({0x81, 0x20, 0x00, 0x00}));
  REQUIRE(mini2->get_alpha_item_codec_config() == std::vector<uint8_t>({0x81, 0x20, 0x00, 0x00}));
  REQUIRE(mini2->get_main_item_data_size() == 50);
  REQUIRE(mini2->get_alpha_item_data_size() == 30);
}


TEST_CASE("mini write round-trip with exif and xmp from scratch")
{
  auto mini = std::make_shared<Box_mini>();
  mini->set_version(0);
  mini->set_explicit_codec_types_flag(false);
  mini->set_float_flag(false);
  mini->set_full_range_flag(true);
  mini->set_alpha_flag(false);
  mini->set_explicit_cicp_flag(true);
  mini->set_hdr_flag(false);
  mini->set_icc_flag(true);
  mini->set_exif_flag(true);
  mini->set_xmp_flag(true);
  mini->set_chroma_subsampling(1);  // 4:2:0
  mini->set_orientation(1);
  mini->set_width(320);
  mini->set_height(240);
  mini->set_bit_depth(10);
  mini->set_chroma_is_horizontally_centered(true);
  mini->set_chroma_is_vertically_centered(false);
  mini->set_colour_primaries(9);
  mini->set_transfer_characteristics(16);
  mini->set_matrix_coefficients(9);
  mini->set_exif_xmp_compressed_flag(false);

  mini->set_main_item_codec_config({0x81, 0x20, 0x00, 0x00});

  std::vector<uint8_t> icc_data(200, 0xDD);
  mini->set_icc_data(icc_data);

  std::vector<uint8_t> main_data(100, 0xAA);
  mini->set_main_item_data(main_data);

  std::vector<uint8_t> exif_data(80, 0xEE);
  mini->set_exif_data(exif_data);

  std::vector<uint8_t> xmp_data(150, 0xFF);
  mini->set_xmp_data(xmp_data);

  // Write
  StreamWriter writer;
  Error error = mini->write(writer);
  REQUIRE(error == Error::Ok);

  // Parse back
  auto written_data = writer.get_data();
  auto reader = std::make_shared<StreamReader_memory>(written_data.data(), written_data.size(), false);
  BitstreamRange range(reader, written_data.size());

  std::shared_ptr<Box> box;
  error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error == Error::Ok);
  auto mini2 = std::dynamic_pointer_cast<Box_mini>(box);
  REQUIRE(mini2 != nullptr);

  REQUIRE(mini2->get_width() == 320);
  REQUIRE(mini2->get_height() == 240);
  REQUIRE(mini2->get_bit_depth() == 10);
  REQUIRE(mini2->get_icc_flag() == true);
  REQUIRE(mini2->get_exif_flag() == true);
  REQUIRE(mini2->get_xmp_flag() == true);
  REQUIRE(mini2->get_colour_primaries() == 9);
  REQUIRE(mini2->get_transfer_characteristics() == 16);
  REQUIRE(mini2->get_matrix_coefficients() == 9);
  REQUIRE(mini2->get_orientation() == 1);
  REQUIRE(mini2->get_icc_data().size() == 200);
  REQUIRE(mini2->get_icc_data() == icc_data);
  REQUIRE(mini2->get_main_item_data_size() == 100);
  REQUIRE(mini2->get_exif_item_data_size() == 80);
  REQUIRE(mini2->get_xmp_item_data_size() == 150);
}


TEST_CASE("mini write round-trip small dimensions")
{
  // Test with small dimensions (7-bit, no large_dimensions_flag)
  auto mini = std::make_shared<Box_mini>();
  mini->set_version(0);
  mini->set_explicit_codec_types_flag(false);
  mini->set_float_flag(false);
  mini->set_full_range_flag(true);
  mini->set_alpha_flag(false);
  mini->set_explicit_cicp_flag(false);
  mini->set_hdr_flag(false);
  mini->set_icc_flag(false);
  mini->set_exif_flag(false);
  mini->set_xmp_flag(false);
  mini->set_chroma_subsampling(1);
  mini->set_orientation(3);
  mini->set_width(64);
  mini->set_height(48);
  mini->set_bit_depth(8);
  mini->set_chroma_is_horizontally_centered(true);
  mini->set_chroma_is_vertically_centered(true);
  mini->set_colour_primaries(1);
  mini->set_transfer_characteristics(13);
  mini->set_matrix_coefficients(6);

  mini->set_main_item_codec_config({0x81, 0x20, 0x00, 0x00});
  mini->set_main_item_data(std::vector<uint8_t>(20, 0x42));

  StreamWriter writer;
  Error error = mini->write(writer);
  REQUIRE(error == Error::Ok);

  auto written_data = writer.get_data();
  auto reader = std::make_shared<StreamReader_memory>(written_data.data(), written_data.size(), false);
  BitstreamRange range(reader, written_data.size());

  std::shared_ptr<Box> box;
  error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error == Error::Ok);
  auto mini2 = std::dynamic_pointer_cast<Box_mini>(box);
  REQUIRE(mini2 != nullptr);

  REQUIRE(mini2->get_width() == 64);
  REQUIRE(mini2->get_height() == 48);
  REQUIRE(mini2->get_orientation() == 3);
  REQUIRE(mini2->get_bit_depth() == 8);
  REQUIRE(mini2->get_main_item_data_size() == 20);
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
                        "size: 1923   (header size: 8)\n"
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
                        "alpha_item_data offset: 717, size: 219\n"
                        "main_item_data offset: 936, size: 1003\n");
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
                        "size: 6294   (header size: 8)\n"
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
                        "main_item_data offset: 717, size: 1003\n"
                        "exif_data offset: 1720, size: 314\n"
                        "xmp_data offset: 2034, size: 4276\n");
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
  REQUIRE(mini->get_width() == 256);
  REQUIRE(mini->get_height() == 256);
  REQUIRE(mini->get_main_item_codec_config().size() == 113);
  Indent indent;
  std::string dumpResult = mini->dump(indent);
  REQUIRE(dumpResult == "Box: mini -----\n"
                        "size: 19229   (header size: 8)\n"
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
                        "width: 256\n"
                        "height: 256\n"
                        "chroma_is_horizontally_centered: 0\n"
                        "chroma_is_vertically_centered: 0\n"
                        "bit_depth: 8\n"
                        "colour_primaries: 1\n"
                        "transfer_characteristics: 13\n"
                        "matrix_coefficients: 6\n"
                        "main_item_code_config size: 113\n"
                        "main_item_data offset: 147, size: 19098\n");
}


// --- Orientation round-trip tests ---
//
// Exercise Box_mini::compute_orientation_from_properties() — the static helper
// that walks a list of irot/imir property boxes and composes them via
// heif_orientation_concat() to recover the cumulative EXIF orientation.
// The order in ipma is not fixed across files, so the recovery must work
// regardless of whether irot or imir appears first.

namespace {

std::shared_ptr<Box_irot> make_irot(int rotation_ccw)
{
  auto b = std::make_shared<Box_irot>();
  b->set_rotation_ccw(rotation_ccw);
  return b;
}

std::shared_ptr<Box_imir> make_imir(heif_transform_mirror_direction dir)
{
  auto b = std::make_shared<Box_imir>();
  b->set_mirror_direction(dir);
  return b;
}

// A non-transform property to verify it's ignored.
std::shared_ptr<Box> make_ispe()
{
  auto b = std::make_shared<Box_ispe>();
  b->set_size(64, 64);
  return b;
}

} // namespace

TEST_CASE("Box_mini::compute_orientation_from_properties — single boxes")
{
  // Empty -> normal
  REQUIRE(Box_mini::compute_orientation_from_properties({}) == heif_orientation_normal);

  // Non-transform properties are ignored.
  REQUIRE(Box_mini::compute_orientation_from_properties({make_ispe()}) == heif_orientation_normal);

  // Single irot at every supported angle (Box_irot stores rotation in
  // counter-clockwise degrees; EXIF labels rotations clockwise).
  REQUIRE(Box_mini::compute_orientation_from_properties({make_irot(0)})   == heif_orientation_normal);
  REQUIRE(Box_mini::compute_orientation_from_properties({make_irot(180)}) == heif_orientation_rotate_180);
  REQUIRE(Box_mini::compute_orientation_from_properties({make_irot(270)}) == heif_orientation_rotate_90_cw);  // 270 ccw == 90 cw
  REQUIRE(Box_mini::compute_orientation_from_properties({make_irot(90)})  == heif_orientation_rotate_270_cw); // 90 ccw == 270 cw

  // Single imir in each direction.
  REQUIRE(Box_mini::compute_orientation_from_properties({make_imir(heif_transform_mirror_direction_horizontal)})
          == heif_orientation_flip_horizontally);
  REQUIRE(Box_mini::compute_orientation_from_properties({make_imir(heif_transform_mirror_direction_vertical)})
          == heif_orientation_flip_vertically);
}

TEST_CASE("Box_mini::compute_orientation_from_properties — irot followed by imir")
{
  // This is the canonical order emitted by HeifFile::add_orientation_properties:
  // rotation first, then mirror.

  using H = std::pair<std::vector<std::shared_ptr<Box>>, heif_orientation>;
  std::vector<H> cases = {
      // The two combinations add_orientation_properties produces (orientations
      // 5 and 7 in the EXIF mapping).
      { {make_irot(270), make_imir(heif_transform_mirror_direction_horizontal)},
        heif_orientation_rotate_90_cw_then_flip_horizontally },                 // 5
      { {make_irot(270), make_imir(heif_transform_mirror_direction_vertical)},
        heif_orientation_rotate_90_cw_then_flip_vertically },                   // 7

      // A few more rotation+mirror pairs that exercise different table cells.
      { {make_irot(180), make_imir(heif_transform_mirror_direction_horizontal)},
        heif_orientation_flip_vertically },                                     // 4
      { {make_irot(180), make_imir(heif_transform_mirror_direction_vertical)},
        heif_orientation_flip_horizontally },                                   // 2
      { {make_irot(90),  make_imir(heif_transform_mirror_direction_horizontal)},
        heif_orientation_rotate_90_cw_then_flip_vertically },                   // 7
  };

  for (auto& [transforms, expected] : cases) {
    INFO("expected orientation = " << expected);
    REQUIRE(Box_mini::compute_orientation_from_properties(transforms) == expected);
  }
}

TEST_CASE("Box_mini::compute_orientation_from_properties — imir followed by irot")
{
  // add_orientation_properties always emits irot-then-imir, but a file
  // produced by other tools could place them in the reverse order. The
  // heif_orientation_concat() composition must give the right answer for
  // that order too. These expected values come from the concat table in
  // heif_encoding.cc: concat(imir, irot).

  using H = std::pair<std::vector<std::shared_ptr<Box>>, heif_orientation>;
  std::vector<H> cases = {
      // imir(H) then irot(270 ccw / 90 cw) = concat(2, 6) = 7
      { {make_imir(heif_transform_mirror_direction_horizontal), make_irot(270)},
        heif_orientation_rotate_90_cw_then_flip_vertically },                   // 7

      // imir(H) then irot(180) = concat(2, 3) = 4
      { {make_imir(heif_transform_mirror_direction_horizontal), make_irot(180)},
        heif_orientation_flip_vertically },                                     // 4

      // imir(V) then irot(270 ccw / 90 cw) = concat(4, 6) = 5
      { {make_imir(heif_transform_mirror_direction_vertical),   make_irot(270)},
        heif_orientation_rotate_90_cw_then_flip_horizontally },                 // 5

      // imir(V) then irot(90 ccw / 270 cw) = concat(4, 8) = 7 again (different path)
      { {make_imir(heif_transform_mirror_direction_vertical),   make_irot(90)},
        heif_orientation_rotate_90_cw_then_flip_vertically },                   // 7
  };

  for (auto& [transforms, expected] : cases) {
    INFO("expected orientation = " << expected);
    REQUIRE(Box_mini::compute_orientation_from_properties(transforms) == expected);
  }
}

TEST_CASE("Box_mini::compute_orientation_from_properties — mixed with non-transform properties")
{
  // Non-transform properties between transforms must not affect the result.
  std::vector<std::shared_ptr<Box>> props = {
      make_ispe(),
      make_irot(270),
      make_ispe(),
      make_imir(heif_transform_mirror_direction_horizontal),
      make_ispe(),
  };
  REQUIRE(Box_mini::compute_orientation_from_properties(props)
          == heif_orientation_rotate_90_cw_then_flip_horizontally); // 5
}
