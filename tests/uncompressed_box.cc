/*
  libheif uncompressed box unit tests

  MIT License

  Copyright (c) 2023 Brad Hards <bradh@frogmouth.net>
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
#include "libheif/heif.h"
#include "codecs/uncompressed/unc_types.h"
#include "codecs/uncompressed/unc_boxes.h"
#include <cstdint>
#include <iostream>


TEST_CASE( "cmpd" )
{
    std::shared_ptr<Box_cmpd> cmpd = std::make_shared<Box_cmpd>();
    REQUIRE(cmpd->get_components().size() == 0);
    Box_cmpd::Component component;
    component.component_type = 1;
    cmpd->add_component(component);
    REQUIRE(cmpd->get_components().size() == 1);
    REQUIRE(cmpd->get_components()[0].component_type == 1);
    REQUIRE(cmpd->get_components()[0].component_type_uri == "");
    REQUIRE(cmpd->get_components()[0].get_component_type_name() == "Y\n");

    StreamWriter writer;
    Error err = cmpd->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> bytes = writer.get_data();
    std::vector<uint8_t> expected = {0x00, 0x00, 0x00, 0x0e, 'c', 'm', 'p', 'd', 0x00, 0x00, 0x00, 0x01, 0x00, 0x01};
    REQUIRE(bytes == expected);

    Indent indent;
    std::string dump_output = cmpd->dump(indent);
    REQUIRE(dump_output == "Box: cmpd -----\nsize: 0   (header size: 0)\ncomponent_type: Y\n");
}

TEST_CASE( "cmpd_multi" )
{
    std::shared_ptr<Box_cmpd> cmpd = std::make_shared<Box_cmpd>();
    REQUIRE(cmpd->get_components().size() == 0);

    Box_cmpd::Component red_component;
    red_component.component_type = 4;
    cmpd->add_component(red_component);

    Box_cmpd::Component green_component;
    green_component.component_type = 5;
    cmpd->add_component(green_component);

    Box_cmpd::Component blue_component;
    blue_component.component_type = 6;
    cmpd->add_component(blue_component);
    REQUIRE(cmpd->get_components().size() == 3);
    REQUIRE(cmpd->get_components()[0].component_type == 4);
    REQUIRE(cmpd->get_components()[0].component_type_uri == "");
    REQUIRE(cmpd->get_components()[0].get_component_type_name() == "red\n");
    REQUIRE(cmpd->get_components()[1].component_type == 5);
    REQUIRE(cmpd->get_components()[1].component_type_uri == "");
    REQUIRE(cmpd->get_components()[1].get_component_type_name() == "green\n");
    REQUIRE(cmpd->get_components()[2].component_type == 6);
    REQUIRE(cmpd->get_components()[2].component_type_uri == "");
    REQUIRE(cmpd->get_components()[2].get_component_type_name() == "blue\n");

    StreamWriter writer;
    Error err = cmpd->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> bytes = writer.get_data();
    std::vector<uint8_t> expected = {0x00, 0x00, 0x00, 0x12, 'c', 'm', 'p', 'd', 0x00, 0x00, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 0x06};
    REQUIRE(bytes == expected);

    Indent indent;
    std::string dump_output = cmpd->dump(indent);
    REQUIRE(dump_output == "Box: cmpd -----\nsize: 0   (header size: 0)\ncomponent_type: red\ncomponent_type: green\ncomponent_type: blue\n");
}

TEST_CASE( "cmpd_custom" )
{
    std::shared_ptr<Box_cmpd> cmpd = std::make_shared<Box_cmpd>();
    REQUIRE(cmpd->get_components().size() == 0);

    Box_cmpd::Component component0;
    component0.component_type = 0x8000;
    component0.component_type_uri = "http://example.com/custom_component_uri";
    cmpd->add_component(component0);

    Box_cmpd::Component component1;
    component1.component_type = 0x8002;
    component1.component_type_uri = "http://example.com/another_custom_component_uri";
    cmpd->add_component(component1);

    REQUIRE(cmpd->get_components().size() == 2);
    REQUIRE(cmpd->get_components()[0].component_type == 0x8000);
    REQUIRE(cmpd->get_components()[0].component_type_uri == "http://example.com/custom_component_uri");
    REQUIRE(cmpd->get_components()[0].get_component_type_name() == "0x8000\n");
    REQUIRE(cmpd->get_components()[1].component_type == 0x8002);
    REQUIRE(cmpd->get_components()[1].component_type_uri == "http://example.com/another_custom_component_uri");
    REQUIRE(cmpd->get_components()[1].get_component_type_name() == "0x8002\n");
    StreamWriter writer;
    Error err = cmpd->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> bytes = writer.get_data();
    std::vector<uint8_t> expected = {0x00, 0x00, 0x00, 0x68, 'c', 'm', 'p', 'd', 0x00, 0x00, 0x00, 0x02, 0x80, 0x00, 'h', 't', 't', 'p', ':',  '/', '/', 'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm', '/', 'c', 'u', 's', 't', 'o', 'm', '_', 'c', 'o', 'm', 'p', 'o', 'n', 'e', 'n', 't', '_', 'u', 'r', 'i', 0x00, 0x80, 0x02, 'h', 't', 't', 'p', ':',  '/', '/', 'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm', '/', 'a', 'n', 'o', 't', 'h', 'e', 'r', '_', 'c', 'u', 's', 't', 'o', 'm', '_', 'c', 'o', 'm', 'p', 'o', 'n', 'e', 'n', 't', '_', 'u', 'r', 'i', 0x00};
    REQUIRE(bytes == expected);

    Indent indent;
    std::string dump_output = cmpd->dump(indent);
    REQUIRE(dump_output == "Box: cmpd -----\nsize: 0   (header size: 0)\ncomponent_type: 0x8000\n| component_type_uri: http://example.com/custom_component_uri\ncomponent_type: 0x8002\n| component_type_uri: http://example.com/another_custom_component_uri\n");
}

TEST_CASE( "uncC" )
{
    std::shared_ptr<Box_uncC> uncC = std::make_shared<Box_uncC>();
    uncC->set_profile(fourcc("rgba"));
    REQUIRE(uncC->get_components().size() == 0);
    Box_uncC::Component r;
    r.component_index = 0;
    r.component_bit_depth = 8;
    r.component_format = component_format_unsigned;
    r.component_align_size = 0;
    uncC->add_component(r);
    Box_uncC::Component g;
    g.component_index = 1;
    g.component_bit_depth = 8;
    g.component_format = component_format_unsigned;
    g.component_align_size = 0;
    uncC->add_component(g);
    Box_uncC::Component b;
    b.component_index = 2;
    b.component_bit_depth = 8;
    b.component_format = component_format_unsigned;
    b.component_align_size = 0;
    uncC->add_component(b);
    Box_uncC::Component a;
    a.component_index = 3;
    a.component_bit_depth = 8;
    a.component_format = component_format_unsigned;
    a.component_align_size = 0;
    uncC->add_component(a);
    uncC->set_sampling_type(sampling_mode_no_subsampling);
    uncC->set_interleave_type(interleave_mode_pixel);

    REQUIRE(uncC->get_components().size() == 4);
    Box_uncC::Component component0 = uncC->get_components()[0];
    REQUIRE(component0.component_index == 0);
    REQUIRE(component0.component_bit_depth == 8);
    REQUIRE(component0.component_format == 0);
    REQUIRE(component0.component_align_size == 0);
    Box_uncC::Component component1 = uncC->get_components()[1];
    REQUIRE(component1.component_index == 1);
    REQUIRE(component1.component_bit_depth == 8);
    REQUIRE(component1.component_format == 0);
    REQUIRE(component1.component_align_size == 0);
    Box_uncC::Component component2 = uncC->get_components()[2];
    REQUIRE(component2.component_index == 2);
    REQUIRE(component2.component_bit_depth == 8);
    REQUIRE(component2.component_format == 0);
    REQUIRE(component2.component_align_size == 0);
    Box_uncC::Component component3 = uncC->get_components()[3];
    REQUIRE(component3.component_index == 3);
    REQUIRE(component3.component_bit_depth == 8);
    REQUIRE(component3.component_format == 0);
    REQUIRE(component3.component_align_size == 0);
    REQUIRE(uncC->get_sampling_type() == 0);
    REQUIRE(uncC->get_interleave_type() == 1);
    REQUIRE(uncC->get_block_size() == 0);
    REQUIRE(uncC->is_components_little_endian() == false);
    REQUIRE(uncC->is_block_pad_lsb() == false);
    REQUIRE(uncC->is_block_little_endian() == false);
    REQUIRE(uncC->is_pad_unknown() == false);
    REQUIRE(uncC->get_pixel_size() == 0);
    REQUIRE(uncC->get_row_align_size() == 0);
    REQUIRE(uncC->get_tile_align_size() == 0);
    REQUIRE(uncC->get_number_of_tile_columns() == 1);
    REQUIRE(uncC->get_number_of_tile_rows() == 1);

    StreamWriter writer;
    Error err = uncC->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> bytes = writer.get_data();
    std::vector<uint8_t> expected = {
    0x00, 0x00, 0x00, 0x40, 'u', 'n', 'c', 'C',
    0x00, 0x00, 0x00, 0x00, 'r', 'g', 'b', 'a',
    0x00, 0x00, 0x00, 0x04, 0, 0, 7, 0x00,
    0x00, 0x00, 0x01, 0x07, 0x00, 0x00, 0x00, 0x02,
    0x07, 0x00, 0x00, 0x00, 0x03, 0x07, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    REQUIRE(bytes == expected);

    Indent indent;
    std::string dump_output = uncC->dump(indent);
    REQUIRE(dump_output == "Box: uncC -----\nsize: 0   (header size: 0)\nprofile: 1919378017 (rgba)\ncomponent_index: 0\n| component_bit_depth: 8\n| component_format: unsigned\n| component_align_size: 0\ncomponent_index: 1\n| component_bit_depth: 8\n| component_format: unsigned\n| component_align_size: 0\ncomponent_index: 2\n| component_bit_depth: 8\n| component_format: unsigned\n| component_align_size: 0\ncomponent_index: 3\n| component_bit_depth: 8\n| component_format: unsigned\n| component_align_size: 0\nsampling_type: no subsampling\ninterleave_type: pixel\nblock_size: 0\ncomponents_little_endian: 0\nblock_pad_lsb: 0\nblock_little_endian: 0\nblock_reversed: 0\npad_unknown: 0\npixel_size: 0\nrow_align_size: 0\ntile_align_size: 0\nnum_tile_cols: 1\nnum_tile_rows: 1\n");
}

TEST_CASE("uncC_parse") {
  std::vector<uint8_t> byteArray{
    0x00, 0x00, 0x00, 0x40, 'u', 'n', 'c', 'C',
    0x00, 0x00, 0x00, 0x00, 'r', 'g', 'b', 'a',
    0x00, 0x00, 0x00, 0x04, 0, 0, 7, 0x00,
    0x00, 0x00, 0x01, 0x07, 0x00, 0x00, 0x00, 0x02,
    0x07, 0x00, 0x00, 0x00, 0x03, 0x07, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02
    };

  auto reader = std::make_shared<StreamReader_memory>(byteArray.data(),
                                                      byteArray.size(), false);

  BitstreamRange range(reader, byteArray.size());
  std::shared_ptr<Box> box;
  Error error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error == Error::Ok);
  REQUIRE(range.error() == 0);

  REQUIRE(box->get_short_type() == fourcc("uncC"));
  REQUIRE(box->get_type_string() == "uncC");
  std::shared_ptr<Box_uncC> uncC = std::dynamic_pointer_cast<Box_uncC>(box);
  REQUIRE(uncC->get_number_of_tile_columns() == 2);
  REQUIRE(uncC->get_number_of_tile_rows() == 3);
  Indent indent;
  std::string dumpResult = box->dump(indent);
  REQUIRE(dumpResult == "Box: uncC -----\n"
                        "size: 64   (header size: 12)\n"
                        "profile: 1919378017 (rgba)\n"
                        "component_index: 0\n"
                        "| component_bit_depth: 8\n"
                        "| component_format: unsigned\n"
                        "| component_align_size: 0\n"
                        "component_index: 1\n"
                        "| component_bit_depth: 8\n"
                        "| component_format: unsigned\n"
                        "| component_align_size: 0\n"
                        "component_index: 2\n"
                        "| component_bit_depth: 8\n"
                        "| component_format: unsigned\n"
                        "| component_align_size: 0\n"
                        "component_index: 3\n"
                        "| component_bit_depth: 8\n"
                        "| component_format: unsigned\n"
                        "| component_align_size: 0\n"
                        "sampling_type: no subsampling\n"
                        "interleave_type: pixel\n"
                        "block_size: 0\n"
                        "components_little_endian: 0\n"
                        "block_pad_lsb: 0\n"
                        "block_little_endian: 0\n"
                        "block_reversed: 0\n"
                        "pad_unknown: 0\n"
                        "pixel_size: 0\n"
                        "row_align_size: 0\n"
                        "tile_align_size: 0\n"
                        "num_tile_cols: 2\n"
                        "num_tile_rows: 3\n");
}

TEST_CASE("uncC_parse_no_overflow") {
  std::vector<uint8_t> byteArray{
    0x00, 0x00, 0x00, 0x40, 'u', 'n', 'c', 'C',
    0x00, 0x00, 0x00, 0x00, 'r', 'g', 'b', 'a',
    0x00, 0x00, 0x00, 0x04, 0, 0, 7, 0x00,
    0x00, 0x00, 0x01, 0x07, 0x00, 0x00, 0x00, 0x02,
    0x07, 0x00, 0x00, 0x00, 0x03, 0x07, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xfe
    };

  auto reader = std::make_shared<StreamReader_memory>(byteArray.data(),
                                                      byteArray.size(), false);

  BitstreamRange range(reader, byteArray.size());
  std::shared_ptr<Box> box;
  Error error = Box::read(range, &box, heif_get_disabled_security_limits());
  REQUIRE(error == Error::Ok);
  REQUIRE(range.error() == 0);

  REQUIRE(box->get_short_type() == fourcc("uncC"));
  REQUIRE(box->get_type_string() == "uncC");
  std::shared_ptr<Box_uncC> uncC = std::dynamic_pointer_cast<Box_uncC>(box);
  REQUIRE(uncC->get_number_of_tile_columns() == 4294967295);
  REQUIRE(uncC->get_number_of_tile_rows() == 4294967295);
}

TEST_CASE("uncC_parse_excess_tile_cols") {
  std::vector<uint8_t> byteArray{
    0x00, 0x00, 0x00, 0x40, 'u', 'n', 'c', 'C',
    0x00, 0x00, 0x00, 0x00, 'r', 'g', 'b', 'a',
    0x00, 0x00, 0x00, 0x04, 0, 0, 7, 0x00,
    0x00, 0x00, 0x01, 0x07, 0x00, 0x00, 0x00, 0x02,
    0x07, 0x00, 0x00, 0x00, 0x03, 0x07, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x7f, 0xff
    };

  auto reader = std::make_shared<StreamReader_memory>(byteArray.data(),
                                                      byteArray.size(), false);
  BitstreamRange range(reader, byteArray.size());
  std::shared_ptr<Box> box;
  Error error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(range.error() == 0);
  REQUIRE(error.error_code == 6);
  REQUIRE(error.sub_error_code == 1000);
}

TEST_CASE("uncC_parse_excess_tile_rows") {
  std::vector<uint8_t> byteArray{
    0x00, 0x00, 0x00, 0x40, 'u', 'n', 'c', 'C',
    0x00, 0x00, 0x00, 0x00, 'r', 'g', 'b', 'a',
    0x00, 0x00, 0x00, 0x04, 0, 0, 7, 0x00,
    0x00, 0x00, 0x01, 0x07, 0x00, 0x00, 0x00, 0x02,
    0x07, 0x00, 0x00, 0x00, 0x03, 0x07, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff
    };

  auto reader = std::make_shared<StreamReader_memory>(byteArray.data(),
                                                      byteArray.size(), false);
  BitstreamRange range(reader, byteArray.size());
  std::shared_ptr<Box> box;
  Error error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(range.error() == 0);
  REQUIRE(error.error_code == 6);
  REQUIRE(error.sub_error_code == 1000);
}

TEST_CASE("cmpC_defl") {
    std::vector<uint8_t> byteArray{
      0x00, 0x00, 0x00, 0x11, 'c', 'm', 'p', 'C',
      0x00, 0x00, 0x00, 0x00, 'd', 'e', 'f', 'l',
      0x00
      };

    auto reader = std::make_shared<StreamReader_memory>(byteArray.data(),
                                                        byteArray.size(), false);

    BitstreamRange range(reader, byteArray.size());
    std::shared_ptr<Box> box;
    Error error = Box::read(range, &box, heif_get_global_security_limits());
    REQUIRE(error == Error::Ok);
    REQUIRE(range.error() == 0);

    REQUIRE(box->get_short_type() == fourcc("cmpC"));
    REQUIRE(box->get_type_string() == "cmpC");
    std::shared_ptr<Box_cmpC> cmpC = std::dynamic_pointer_cast<Box_cmpC>(box);
    REQUIRE(cmpC != nullptr);
    REQUIRE(cmpC->get_compression_type() == fourcc("defl"));
    REQUIRE(cmpC->get_compressed_unit_type() == 0);

    StreamWriter writer;
    Error err = cmpC->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> written = writer.get_data();
    REQUIRE(written == byteArray);

    Indent indent;
    std::string dump_output = cmpC->dump(indent);
    REQUIRE(dump_output == "Box: cmpC -----\nsize: 17   (header size: 12)\ncompression_type: defl\ncompressed_entity_type: 0\n");

}


TEST_CASE("cmpC_zlib") {
    std::vector<uint8_t> byteArray{
      0x00, 0x00, 0x00, 0x11, 'c', 'm', 'p', 'C',
      0x00, 0x00, 0x00, 0x00, 'z', 'l', 'i', 'b',
      0x02
      };

    auto reader = std::make_shared<StreamReader_memory>(byteArray.data(),
                                                        byteArray.size(), false);

    BitstreamRange range(reader, byteArray.size());
    std::shared_ptr<Box> box;
    Error error = Box::read(range, &box, heif_get_global_security_limits());
    REQUIRE(error == Error::Ok);
    REQUIRE(range.error() == 0);

    REQUIRE(box->get_short_type() == fourcc("cmpC"));
    REQUIRE(box->get_type_string() == "cmpC");
    std::shared_ptr<Box_cmpC> cmpC = std::dynamic_pointer_cast<Box_cmpC>(box);
    REQUIRE(cmpC != nullptr);
    REQUIRE(cmpC->get_compression_type() == fourcc("zlib"));
    REQUIRE(cmpC->get_compressed_unit_type() == 2);

    StreamWriter writer;
    Error err = cmpC->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> written = writer.get_data();
    REQUIRE(written == byteArray);

    Indent indent;
    std::string dump_output = cmpC->dump(indent);
    REQUIRE(dump_output == "Box: cmpC -----\nsize: 17   (header size: 12)\ncompression_type: zlib\ncompressed_entity_type: 2\n");

}

TEST_CASE("cmpC_brot") {
    std::vector<uint8_t> byteArray{
      0x00, 0x00, 0x00, 0x11, 'c', 'm', 'p', 'C',
      0x00, 0x00, 0x00, 0x00, 'b', 'r', 'o', 't',
      0x01
      };

    auto reader = std::make_shared<StreamReader_memory>(byteArray.data(),
                                                        byteArray.size(), false);

    BitstreamRange range(reader, byteArray.size());
    std::shared_ptr<Box> box;
    Error error = Box::read(range, &box, heif_get_global_security_limits());
    REQUIRE(error == Error::Ok);
    REQUIRE(range.error() == 0);

    REQUIRE(box->get_short_type() == fourcc("cmpC"));
    REQUIRE(box->get_type_string() == "cmpC");
    std::shared_ptr<Box_cmpC> cmpC = std::dynamic_pointer_cast<Box_cmpC>(box);
    REQUIRE(cmpC != nullptr);
    REQUIRE(cmpC->get_compression_type() == fourcc("brot"));
    REQUIRE(cmpC->get_compressed_unit_type() == 1);

    StreamWriter writer;
    Error err = cmpC->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> written = writer.get_data();
    REQUIRE(written == byteArray);

    Indent indent;
    std::string dump_output = cmpC->dump(indent);
    REQUIRE(dump_output == "Box: cmpC -----\nsize: 17   (header size: 12)\ncompression_type: brot\ncompressed_entity_type: 1\n");

  }


TEST_CASE("icef_24_8_bit") {
    std::vector<uint8_t> byteArray{
      0x00, 0x00, 0x00, 0x19, 'i', 'c', 'e', 'f',
      0x00, 0x00, 0x00, 0x00,
      0b01000000,
      0x00, 0x00, 0x00, 0x02,
      0x00, 0x0a, 0x03, 0x03,
      0x02, 0x03, 0x0a, 0x07
      };

    auto reader = std::make_shared<StreamReader_memory>(byteArray.data(),
                                                        byteArray.size(), false);

    BitstreamRange range(reader, byteArray.size());
    std::shared_ptr<Box> box;
    Error error = Box::read(range, &box, heif_get_global_security_limits());
    REQUIRE(error == Error::Ok);
    REQUIRE(range.error() == 0);

    REQUIRE(box->get_short_type() == fourcc("icef"));
    REQUIRE(box->get_type_string() == "icef");
    std::shared_ptr<Box_icef> icef = std::dynamic_pointer_cast<Box_icef>(box);
    REQUIRE(icef != nullptr);
    REQUIRE(icef->get_units().size() == 2);
    REQUIRE(icef->get_version() == 0);

    StreamWriter writer;
    Error err = icef->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> written = writer.get_data();
    REQUIRE(written == byteArray);

    Indent indent;
    std::string dump_output = icef->dump(indent);
    REQUIRE(dump_output == "Box: icef -----\nsize: 25   (header size: 12)\nnum_compressed_units: 2\nunit_offset: 2563, unit_size: 3\nunit_offset: 131850, unit_size: 7\n");
}


TEST_CASE("icef_0_16_bit") {
    std::vector<uint8_t> byteArray{
      0x00, 0x00, 0x00, 0x15, 'i', 'c', 'e', 'f',
      0x00, 0x00, 0x00, 0x00,
      0b00000100,
      0x00, 0x00, 0x00, 0x02,
      0x40, 0x03,
      0x0a, 0x07
      };

    auto reader = std::make_shared<StreamReader_memory>(byteArray.data(),
                                                        byteArray.size(), false);

    BitstreamRange range(reader, byteArray.size());
    std::shared_ptr<Box> box;
    Error error = Box::read(range, &box, heif_get_global_security_limits());
    REQUIRE(error == Error::Ok);
    REQUIRE(range.error() == 0);

    REQUIRE(box->get_short_type() == fourcc("icef"));
    REQUIRE(box->get_type_string() == "icef");
    std::shared_ptr<Box_icef> icef = std::dynamic_pointer_cast<Box_icef>(box);
    REQUIRE(icef != nullptr);
    REQUIRE(icef->get_units().size() == 2);
    REQUIRE(icef->get_units()[0].unit_offset == 0);
    REQUIRE(icef->get_units()[0].unit_size == 16387);
    REQUIRE(icef->get_units()[1].unit_offset == 16387);
    REQUIRE(icef->get_units()[1].unit_size == 2567);
    REQUIRE(icef->get_version() == 0);

    StreamWriter writer;
    Error err = icef->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> written = writer.get_data();
    REQUIRE(written == byteArray);

    Indent indent;
    std::string dump_output = icef->dump(indent);
    REQUIRE(dump_output == "Box: icef -----\nsize: 21   (header size: 12)\nnum_compressed_units: 2\nunit_offset: 0, unit_size: 16387\nunit_offset: 16387, unit_size: 2567\n");
}


TEST_CASE("icef_32bit") {
    std::vector<uint8_t> byteArray{
      0x00, 0x00, 0x00, 0x21, 'i', 'c', 'e', 'f',
      0x00, 0x00, 0x00, 0x00,
      0b01101100,
      0x00, 0x00, 0x00, 0x02,
      0x00, 0x00, 0x03, 0x04, 0x01, 0x01, 0x02, 0x03,
      0x01, 0x02, 0x03, 0x0a, 0x00, 0x04, 0x05, 0x07
      };

    auto reader = std::make_shared<StreamReader_memory>(byteArray.data(),
                                                        byteArray.size(), false);

    BitstreamRange range(reader, byteArray.size());
    std::shared_ptr<Box> box;
    Error error = Box::read(range, &box, heif_get_global_security_limits());
    REQUIRE(error == Error::Ok);
    REQUIRE(range.error() == 0);

    REQUIRE(box->get_short_type() == fourcc("icef"));
    REQUIRE(box->get_type_string() == "icef");
    std::shared_ptr<Box_icef> icef = std::dynamic_pointer_cast<Box_icef>(box);
    REQUIRE(icef != nullptr);
    REQUIRE(icef->get_units().size() == 2);
    REQUIRE(icef->get_units()[0].unit_offset == 772);
    REQUIRE(icef->get_units()[0].unit_size == 16843267);
    REQUIRE(icef->get_units()[1].unit_offset == 16909066);
    REQUIRE(icef->get_units()[1].unit_size == 263431);
    REQUIRE(icef->get_version() == 0);

    StreamWriter writer;
    Error err = icef->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> written = writer.get_data();
    REQUIRE(written == byteArray);

    Indent indent;
    std::string dump_output = icef->dump(indent);
    REQUIRE(dump_output == "Box: icef -----\nsize: 33   (header size: 12)\nnum_compressed_units: 2\nunit_offset: 772, unit_size: 16843267\nunit_offset: 16909066, unit_size: 263431\n");
}


TEST_CASE("icef_uint64") {
    std::vector<uint8_t> byteArray{
      0x00, 0x00, 0x00, 0x31, 'i', 'c', 'e', 'f',
      0x00, 0x00, 0x00, 0x00,
      0b10010000,
      0x00, 0x00, 0x00, 0x02,
      0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x0a, 0x03,
      0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x02, 0x03,
      0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x03, 0x0a,
      0x00, 0x00, 0x00, 0x03, 0x00, 0x04, 0x05, 0x07
      };

    auto reader = std::make_shared<StreamReader_memory>(byteArray.data(),
                                                        byteArray.size(), false);

    BitstreamRange range(reader, byteArray.size());
    std::shared_ptr<Box> box;
    Error error = Box::read(range, &box, heif_get_global_security_limits());
    REQUIRE(error == Error::Ok);
    REQUIRE(range.error() == 0);

    REQUIRE(box->get_short_type() == fourcc("icef"));
    REQUIRE(box->get_type_string() == "icef");
    std::shared_ptr<Box_icef> icef = std::dynamic_pointer_cast<Box_icef>(box);
    REQUIRE(icef != nullptr);
    REQUIRE(icef->get_units().size() == 2);
    REQUIRE(icef->get_units()[0].unit_offset == 4294969859L);
    REQUIRE(icef->get_units()[0].unit_size ==  8590000643L);
    REQUIRE(icef->get_units()[1].unit_offset == 8590066442L);
    REQUIRE(icef->get_units()[1].unit_size ==  12885165319L);
    REQUIRE(icef->get_version() == 0);

    StreamWriter writer;
    Error err = icef->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> written = writer.get_data();
    REQUIRE(written == byteArray);

    Indent indent;
    std::string dump_output = icef->dump(indent);
    REQUIRE(dump_output == "Box: icef -----\nsize: 49   (header size: 12)\nnum_compressed_units: 2\nunit_offset: 4294969859, unit_size: 8590000643\nunit_offset: 8590066442, unit_size: 12885165319\n");
}


TEST_CASE("icef_bad_version") {
    std::vector<uint8_t> byteArray{
      0x00, 0x00, 0x00, 0x19, 'i', 'c', 'e', 'f',
      0x01, 0x00, 0x00, 0x00,
      0b01000000,
      0x00, 0x00, 0x00, 0x02,
      0x00, 0x0a, 0x03, 0x03,
      0x02, 0x03, 0x0a, 0x07
      };

    auto reader = std::make_shared<StreamReader_memory>(byteArray.data(),
                                                        byteArray.size(), false);

    BitstreamRange range(reader, byteArray.size());
    std::shared_ptr<Box> box;
    Error error = Box::read(range, &box, heif_get_global_security_limits());
    REQUIRE(error.error_code == heif_error_Unsupported_feature);
    REQUIRE(error.sub_error_code == heif_suberror_Unsupported_data_version);
    REQUIRE(error.message == std::string("icef box data version 1 is not implemented yet"));
}


TEST_CASE("cloc")
{
  // Construct and set field
  auto cloc = std::make_shared<Box_cloc>();
  cloc->set_chroma_location(2);
  REQUIRE(cloc->get_chroma_location() == 2);

  // Write
  StreamWriter writer;
  Error err = cloc->write(writer);
  REQUIRE(err.error_code == heif_error_Ok);
  const std::vector<uint8_t> bytes = writer.get_data();

  // FullBox header (12 bytes) + 1 byte payload = 13 bytes
  std::vector<uint8_t> expected = {
    0x00, 0x00, 0x00, 0x0D, 'c', 'l', 'o', 'c',
    0x00, 0x00, 0x00, 0x00,
    0x02
  };
  REQUIRE(bytes == expected);

  // Parse back
  auto reader = std::make_shared<StreamReader_memory>(bytes.data(), bytes.size(), false);
  BitstreamRange range(reader, bytes.size());
  std::shared_ptr<Box> box;
  Error error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error == Error::Ok);
  REQUIRE(range.error() == 0);

  REQUIRE(box->get_short_type() == fourcc("cloc"));
  auto parsed = std::dynamic_pointer_cast<Box_cloc>(box);
  REQUIRE(parsed != nullptr);
  REQUIRE(parsed->get_chroma_location() == 2);

  // Dump
  Indent indent;
  std::string dump_output = parsed->dump(indent);
  REQUIRE(dump_output == "Box: cloc -----\nsize: 13   (header size: 12)\nversion: 0\nflags: 0\nchroma_location: 2 (h=0,   v=0)\n");
}


TEST_CASE("cloc_bad_version")
{
  std::vector<uint8_t> byteArray = {
    0x00, 0x00, 0x00, 0x0D, 'c', 'l', 'o', 'c',
    0x01, 0x00, 0x00, 0x00,
    0x02
  };

  auto reader = std::make_shared<StreamReader_memory>(byteArray.data(), byteArray.size(), false);
  BitstreamRange range(reader, byteArray.size());
  std::shared_ptr<Box> box;
  Error error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error.error_code == heif_error_Unsupported_feature);
  REQUIRE(error.sub_error_code == heif_suberror_Unsupported_data_version);
  REQUIRE(error.message == std::string("cloc box data version 1 is not implemented yet"));
}


TEST_CASE("cloc_out_of_range")
{
  std::vector<uint8_t> byteArray = {
    0x00, 0x00, 0x00, 0x0D, 'c', 'l', 'o', 'c',
    0x00, 0x00, 0x00, 0x00,
    0x07
  };

  auto reader = std::make_shared<StreamReader_memory>(byteArray.data(), byteArray.size(), false);
  BitstreamRange range(reader, byteArray.size());
  std::shared_ptr<Box> box;
  Error error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error.error_code == heif_error_Invalid_input);
  REQUIRE(error.sub_error_code == heif_suberror_Invalid_parameter_value);
}


TEST_CASE("splz")
{
  // Construct: 2 component indices, 2x1 pattern, angles 45.0 and 90.0
  auto splz = std::make_shared<Box_splz>();
  PolarizationPattern pattern;
  pattern.component_indices = {0, 1};
  pattern.pattern_width = 2;
  pattern.pattern_height = 1;
  pattern.polarization_angles = {45.0f, 90.0f};
  splz->set_pattern(pattern);

  // Write
  StreamWriter writer;
  Error err = splz->write(writer);
  REQUIRE(err.error_code == heif_error_Ok);
  const std::vector<uint8_t> bytes = writer.get_data();

  // FullBox header (12) + 4 (count) + 8 (2×index) + 4 (w+h) + 8 (2×float) = 36
  std::vector<uint8_t> expected = {
    0x00, 0x00, 0x00, 0x24, 's', 'p', 'l', 'z',
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x02,  // component_count = 2
    0x00, 0x00, 0x00, 0x00,  // index[0] = 0
    0x00, 0x00, 0x00, 0x01,  // index[1] = 1
    0x00, 0x02,              // pattern_width = 2
    0x00, 0x01,              // pattern_height = 1
    0x42, 0x34, 0x00, 0x00,  // 45.0f
    0x42, 0xB4, 0x00, 0x00   // 90.0f
  };
  REQUIRE(bytes == expected);

  // Parse back
  auto reader = std::make_shared<StreamReader_memory>(bytes.data(), bytes.size(), false);
  BitstreamRange range(reader, bytes.size());
  std::shared_ptr<Box> box;
  Error error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error == Error::Ok);
  REQUIRE(range.error() == 0);

  REQUIRE(box->get_short_type() == fourcc("splz"));
  auto parsed = std::dynamic_pointer_cast<Box_splz>(box);
  REQUIRE(parsed != nullptr);

  const auto& p = parsed->get_pattern();
  REQUIRE(p.component_indices.size() == 2);
  REQUIRE(p.component_indices[0] == 0);
  REQUIRE(p.component_indices[1] == 1);
  REQUIRE(p.pattern_width == 2);
  REQUIRE(p.pattern_height == 1);
  REQUIRE(p.polarization_angles.size() == 2);
  REQUIRE(p.polarization_angles[0] == 45.0f);
  REQUIRE(p.polarization_angles[1] == 90.0f);

  // Dump
  Indent indent;
  std::string dump_output = parsed->dump(indent);
  REQUIRE(dump_output == "Box: splz -----\n"
                         "size: 36   (header size: 12)\n"
                         "version: 0\n"
                         "flags: 0\n"
                         "component_count: 2\n"
                         "  component_index[0]: 0\n"
                         "  component_index[1]: 1\n"
                         "pattern_width: 2\n"
                         "pattern_height: 1\n"
                         "  [0,0]: 45 degrees\n"
                         "  [1,0]: 90 degrees\n");
}


TEST_CASE("splz_bad_version")
{
  std::vector<uint8_t> byteArray = {
    0x00, 0x00, 0x00, 0x24, 's', 'p', 'l', 'z',
    0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01,
    0x00, 0x02,
    0x00, 0x01,
    0x42, 0x34, 0x00, 0x00,
    0x42, 0xB4, 0x00, 0x00
  };

  auto reader = std::make_shared<StreamReader_memory>(byteArray.data(), byteArray.size(), false);
  BitstreamRange range(reader, byteArray.size());
  std::shared_ptr<Box> box;
  Error error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error.error_code == heif_error_Unsupported_feature);
  REQUIRE(error.sub_error_code == heif_suberror_Unsupported_data_version);
  REQUIRE(error.message == std::string("splz box data version 1 is not implemented yet"));
}


TEST_CASE("snuc")
{
  // Construct: 1 component index, nuc_is_applied=true, 2x1 image, 2 gains + 2 offsets
  auto snuc = std::make_shared<Box_snuc>();
  SensorNonUniformityCorrection nuc;
  nuc.component_indices = {0};
  nuc.nuc_is_applied = true;
  nuc.image_width = 2;
  nuc.image_height = 1;
  nuc.nuc_gains = {1.0f, 2.0f};
  nuc.nuc_offsets = {0.0f, 3.0f};
  snuc->set_nuc(nuc);

  // Write
  StreamWriter writer;
  Error err = snuc->write(writer);
  REQUIRE(err.error_code == heif_error_Ok);
  const std::vector<uint8_t> bytes = writer.get_data();

  // FullBox header (12) + 4 (count) + 4 (index) + 1 (flags) + 4 (width) + 4 (height)
  // + 8 (2×gain) + 8 (2×offset) = 45
  std::vector<uint8_t> expected = {
    0x00, 0x00, 0x00, 0x2D, 's', 'n', 'u', 'c',
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01,  // component_count = 1
    0x00, 0x00, 0x00, 0x00,  // index[0] = 0
    0x80,                    // flags: nuc_is_applied = true
    0x00, 0x00, 0x00, 0x02,  // image_width = 2
    0x00, 0x00, 0x00, 0x01,  // image_height = 1
    0x3F, 0x80, 0x00, 0x00,  // gain[0] = 1.0f
    0x40, 0x00, 0x00, 0x00,  // gain[1] = 2.0f
    0x00, 0x00, 0x00, 0x00,  // offset[0] = 0.0f
    0x40, 0x40, 0x00, 0x00   // offset[1] = 3.0f
  };
  REQUIRE(bytes == expected);

  // Parse back
  auto reader = std::make_shared<StreamReader_memory>(bytes.data(), bytes.size(), false);
  BitstreamRange range(reader, bytes.size());
  std::shared_ptr<Box> box;
  Error error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error == Error::Ok);
  REQUIRE(range.error() == 0);

  REQUIRE(box->get_short_type() == fourcc("snuc"));
  auto parsed = std::dynamic_pointer_cast<Box_snuc>(box);
  REQUIRE(parsed != nullptr);

  const auto& n = parsed->get_nuc();
  REQUIRE(n.component_indices.size() == 1);
  REQUIRE(n.component_indices[0] == 0);
  REQUIRE(n.nuc_is_applied == true);
  REQUIRE(n.image_width == 2);
  REQUIRE(n.image_height == 1);
  REQUIRE(n.nuc_gains.size() == 2);
  REQUIRE(n.nuc_gains[0] == 1.0f);
  REQUIRE(n.nuc_gains[1] == 2.0f);
  REQUIRE(n.nuc_offsets.size() == 2);
  REQUIRE(n.nuc_offsets[0] == 0.0f);
  REQUIRE(n.nuc_offsets[1] == 3.0f);

  // Dump
  Indent indent;
  std::string dump_output = parsed->dump(indent);
  REQUIRE(dump_output == "Box: snuc -----\n"
                         "size: 45   (header size: 12)\n"
                         "version: 0\n"
                         "flags: 0\n"
                         "component_count: 1\n"
                         "  component_index[0]: 0\n"
                         "nuc_is_applied: 1\n"
                         "image_width: 2\n"
                         "image_height: 1\n"
                         "nuc_gains: 2 values\n"
                         "nuc_offsets: 2 values\n");
}


TEST_CASE("snuc_bad_version")
{
  std::vector<uint8_t> byteArray = {
    0x00, 0x00, 0x00, 0x2D, 's', 'n', 'u', 'c',
    0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00,
    0x80,
    0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x01,
    0x3F, 0x80, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x40, 0x40, 0x00, 0x00
  };

  auto reader = std::make_shared<StreamReader_memory>(byteArray.data(), byteArray.size(), false);
  BitstreamRange range(reader, byteArray.size());
  std::shared_ptr<Box> box;
  Error error = Box::read(range, &box, heif_get_global_security_limits());
  REQUIRE(error.error_code == heif_error_Unsupported_feature);
  REQUIRE(error.sub_error_code == heif_suberror_Unsupported_data_version);
  REQUIRE(error.message == std::string("snuc box data version 1 is not implemented yet"));
}
