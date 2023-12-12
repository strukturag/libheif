/*
  libheif uncompressed box unit tests

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

#include "catch.hpp"
#include "libheif/heif.h"
#include "libheif/uncompressed_box.h"
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

