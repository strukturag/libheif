/*
  libheif JPEG 2000 unit tests

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
#include "libheif/heif.h"
#include "codecs/jpeg2000_boxes.h"
#include "security_limits.h"
#include "test-config.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>


TEST_CASE( "cdef" )
{
    std::shared_ptr<Box_cdef> cdef = std::make_shared<Box_cdef>();
    REQUIRE(cdef->get_channels().size() == 0);
    Box_cdef::Channel channel;
    channel.channel_index = 1;
    channel.channel_type = 2;
    channel.channel_association = 0;
    cdef->add_channel(channel);
    REQUIRE(cdef->get_channels().size() == 1);
    REQUIRE(cdef->get_channels()[0].channel_index == 1);
    REQUIRE(cdef->get_channels()[0].channel_type == 2);
    REQUIRE(cdef->get_channels()[0].channel_association == 0);

    StreamWriter writer;
    Error err = cdef->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> bytes = writer.get_data();
    std::vector<uint8_t> expected = {0x00, 0x00, 0x00, 0x10, 'c', 'd', 'e', 'f', 0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00};
    REQUIRE(bytes == expected);

    Indent indent;
    std::string dump_output = cdef->dump(indent);
    REQUIRE(dump_output == "Box: cdef -----\nsize: 0   (header size: 0)\nchannel_index: 1, channel_type: 2, channel_association: 0\n");
}

TEST_CASE( "cmap" )
{
    std::shared_ptr<Box_cmap> cmap = std::make_shared<Box_cmap>();
    REQUIRE(cmap->get_components().size() == 0);
    Box_cmap::Component component;
    component.component_index = 2;
    component.mapping_type = 1;
    component.palette_colour = 3;
    cmap->add_component(component);
    Box_cmap::Component component2;
    component2.component_index = 4;
    component2.mapping_type = 0;
    component2.palette_colour = 0;
    cmap->add_component(component2);

    REQUIRE(cmap->get_components().size() == 2);
    REQUIRE(cmap->get_components()[0].component_index == 2);
    REQUIRE(cmap->get_components()[0].mapping_type == 1);
    REQUIRE(cmap->get_components()[0].palette_colour == 3);
    REQUIRE(cmap->get_components()[1].component_index == 4);
    REQUIRE(cmap->get_components()[1].mapping_type == 0);
    REQUIRE(cmap->get_components()[1].palette_colour == 0);
    StreamWriter writer;
    Error err = cmap->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> bytes = writer.get_data();
    std::vector<uint8_t> expected = {0x00, 0x00, 0x00, 0x10, 'c', 'm', 'a', 'p', 0x00, 0x02, 0x01, 0x03, 0x00, 0x04, 0x00, 0x00};
    REQUIRE(bytes == expected);

    Indent indent;
    std::string dump_output = cmap->dump(indent);
    REQUIRE(dump_output == "Box: cmap -----\nsize: 0   (header size: 0)\ncomponent_index: 2, mapping_type: 1, palette_colour: 3\ncomponent_index: 4, mapping_type: 0, palette_colour: 0\n");
}

TEST_CASE( "pclr empty" )
{
    std::shared_ptr<Box_pclr> pclr = std::make_shared<Box_pclr>();
    REQUIRE(pclr->get_entries().size() == 0);
    REQUIRE(pclr->get_num_entries() == 0);
    REQUIRE(pclr->get_num_columns() == 0);
    REQUIRE(pclr->get_bit_depths().size() == 0);

    Indent indent;
    std::string dump_output = pclr->dump(indent);
    REQUIRE(dump_output == "Box: pclr -----\nsize: 0   (header size: 0)\nNE: 0, NPC: 0, B: \n");

    StreamWriter writer;
    Error err = pclr->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> bytes = writer.get_data();
    REQUIRE(bytes.size() == 0);
}

TEST_CASE( "pclr" )
{
    std::shared_ptr<Box_pclr> pclr = std::make_shared<Box_pclr>();
    pclr->set_columns(3, 8);
    Box_pclr::PaletteEntry entry0;
    entry0.columns = std::vector<uint16_t> { 1, 2, 3};
    pclr->add_entry(entry0);
    Box_pclr::PaletteEntry entry1;
    entry1.columns = std::vector<uint16_t> { 255, 254, 253};
    pclr->add_entry(entry1);
    REQUIRE(pclr->get_entries().size() == 2);
    REQUIRE(pclr->get_num_entries() == 2);
    REQUIRE(pclr->get_num_columns() == 3);
    REQUIRE(pclr->get_bit_depths().size() == 3);
    REQUIRE(pclr->get_bit_depths()[0] == 8);
    REQUIRE(pclr->get_bit_depths()[1] == 8);
    REQUIRE(pclr->get_bit_depths()[2] == 8);
    StreamWriter writer;
    Error err = pclr->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> bytes = writer.get_data();
    std::vector<uint8_t> expected = {0x00, 0x00, 0x00, 0x14, 'p', 'c', 'l', 'r', 0x00, 0x02, 0x03, 0x07, 0x07, 0x07, 0x01, 0x02, 0x03, 0xFF, 0xFE, 0xFD};
    REQUIRE(bytes == expected);
    Indent indent;
    std::string dump_output = pclr->dump(indent);
    REQUIRE(dump_output == "Box: pclr -----\nsize: 0   (header size: 0)\nNE: 2, NPC: 3, B: 8, 8, 8, \n");
}

TEST_CASE( "pclr 12 bit" )
{
    std::shared_ptr<Box_pclr> pclr = std::make_shared<Box_pclr>();
    pclr->set_columns(3, 12);
    Box_pclr::PaletteEntry entry0;
    entry0.columns = std::vector<uint16_t> { 1, 2, 3};
    pclr->add_entry(entry0);
    Box_pclr::PaletteEntry entry1;
    entry1.columns = std::vector<uint16_t> { 4095, 4094, 4093};
    pclr->add_entry(entry1);
    REQUIRE(pclr->get_entries().size() == 2);
    REQUIRE(pclr->get_num_entries() == 2);
    REQUIRE(pclr->get_num_columns() == 3);
    REQUIRE(pclr->get_bit_depths().size() == 3);
    REQUIRE(pclr->get_bit_depths()[0] == 12);
    REQUIRE(pclr->get_bit_depths()[1] == 12);
    REQUIRE(pclr->get_bit_depths()[2] == 12);
    StreamWriter writer;
    Error err = pclr->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> bytes = writer.get_data();
    std::vector<uint8_t> expected = {0x00, 0x00, 0x00, 0x1A, 'p', 'c', 'l', 'r', 0x00, 0x02, 0x03, 0x0B, 0x0B, 0x0B, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x0F, 0xFF, 0x0F, 0xFE, 0x0F, 0xFD};
    REQUIRE(bytes == expected);
    Indent indent;
    std::string dump_output = pclr->dump(indent);
    REQUIRE(dump_output == "Box: pclr -----\nsize: 0   (header size: 0)\nNE: 2, NPC: 3, B: 12, 12, 12, \n");
}

TEST_CASE( "j2kL" )
{
    std::shared_ptr<Box_j2kL> j2kL = std::make_shared<Box_j2kL>();
    REQUIRE(j2kL->get_layers().size() == 0);
    Box_j2kL::Layer layer;
    layer.layer_id = 1;
    layer.discard_levels = 2;
    layer.decode_layers = 3;
    j2kL->add_layer(layer);
    REQUIRE(j2kL->get_layers().size() == 1);
    REQUIRE(j2kL->get_layers()[0].layer_id == 1);
    REQUIRE(j2kL->get_layers()[0].discard_levels == 2);
    REQUIRE(j2kL->get_layers()[0].decode_layers == 3);

    StreamWriter writer;
    Error err = j2kL->write(writer);
    REQUIRE(err.error_code == heif_error_Ok);
    const std::vector<uint8_t> bytes = writer.get_data();
    std::vector<uint8_t> expected = {0x00, 0x00, 0x00, 0x13, 'j', '2', 'k', 'L', 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x02, 0x00, 0x03};
    REQUIRE(bytes == expected);

    Indent indent;
    std::string dump_output = j2kL->dump(indent);
    REQUIRE(dump_output == "Box: j2kL -----\nsize: 0   (header size: 0)\nlayer_id: 1, discard_levels: 2, decode_layers: 3\n");
}


TEST_CASE( "codestream too short for SOC" )
{
    std::vector<uint8_t> data = {0xFF};
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Invalid_input);
    REQUIRE(err.sub_error_code == heif_suberror_Invalid_J2K_codestream);
}


TEST_CASE( "codestream missing SOC" )
{
    std::vector<uint8_t> data = {0xFF, 0x4E};
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Invalid_input);
    REQUIRE(err.sub_error_code == heif_suberror_Invalid_J2K_codestream);
}

TEST_CASE( "codestream too short for SIZ body" )
{
    std::vector<uint8_t> data = {0xFF, 0x4F, 0xFF, 0x51};
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Invalid_input);
    REQUIRE(err.sub_error_code == heif_suberror_Invalid_J2K_codestream);
}

TEST_CASE( "codestream - COD + SIZ" )
{
    // This data is a subset of the example in ISO/IEC 15444-1:2019 Section J.10.1 "Main header"
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x29, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x01, 0x07, 0x01, 0x01, 0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Ok);
    REQUIRE(uut.get_SIZ().reference_grid_width == 1);
    REQUIRE(uut.get_SIZ().reference_grid_height == 9);
    REQUIRE(uut.get_SIZ().image_horizontal_offset == 0);
    REQUIRE(uut.get_SIZ().image_vertical_offset == 0);
    REQUIRE(uut.get_SIZ().tile_width == 1);
    REQUIRE(uut.get_SIZ().tile_height == 9);
    REQUIRE(uut.get_SIZ().tile_offset_x == 0);
    REQUIRE(uut.get_SIZ().tile_offset_y == 0);
    REQUIRE(uut.get_SIZ().components.size() == 1);
    REQUIRE(uut.get_SIZ().components[0].precision == 8);
    REQUIRE(uut.get_SIZ().components[0].is_signed == false);
    REQUIRE(uut.get_SIZ().components[0].h_separation == 1);
    REQUIRE(uut.get_SIZ().components[0].v_separation == 1);
    REQUIRE(uut.get_chroma_format() == heif_chroma_monochrome);
    REQUIRE(uut.get_precision(0) == 8);
    REQUIRE(uut.hasHighThroughputExtension() == false);
}

TEST_CASE( "codestream - COD + SIZ first plane subsampled" )
{
    // This data is a subset of the example in ISO/IEC 15444-1:2019 Section J.10.1 "Main header"
    // with modifications
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x29, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x01, 0x07, 0x01, 0x02, 0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Ok);
    REQUIRE(uut.get_SIZ().reference_grid_width == 1);
    REQUIRE(uut.get_SIZ().reference_grid_height == 9);
    REQUIRE(uut.get_SIZ().image_horizontal_offset == 0);
    REQUIRE(uut.get_SIZ().image_vertical_offset == 0);
    REQUIRE(uut.get_SIZ().tile_width == 1);
    REQUIRE(uut.get_SIZ().tile_height == 9);
    REQUIRE(uut.get_SIZ().tile_offset_x == 0);
    REQUIRE(uut.get_SIZ().tile_offset_y == 0);
    REQUIRE(uut.get_SIZ().components.size() == 1);
    REQUIRE(uut.get_SIZ().components[0].precision == 8);
    REQUIRE(uut.get_SIZ().components[0].is_signed == false);
    REQUIRE(uut.get_SIZ().components[0].h_separation == 1);
    REQUIRE(uut.get_SIZ().components[0].v_separation == 2);
    REQUIRE(uut.get_chroma_format() == heif_chroma_undefined);
    REQUIRE(uut.get_precision(0) == 8);
    REQUIRE(uut.hasHighThroughputExtension() == false);
}

TEST_CASE( "codestream - COD + SIZ 4:4:4" )
{
    // This data is a subset of the example in ISO/IEC 15444-1:2019 Section J.10.1 "Main header"
    // with modifications
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x2F, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x03, 0x07, 0x01, 0x01, 0x06, 0x01, 0x01,
    0x05, 0x01, 0x01, 0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Ok);
    REQUIRE(uut.get_SIZ().reference_grid_width == 1);
    REQUIRE(uut.get_SIZ().reference_grid_height == 9);
    REQUIRE(uut.get_SIZ().image_horizontal_offset == 0);
    REQUIRE(uut.get_SIZ().image_vertical_offset == 0);
    REQUIRE(uut.get_SIZ().tile_width == 1);
    REQUIRE(uut.get_SIZ().tile_height == 9);
    REQUIRE(uut.get_SIZ().tile_offset_x == 0);
    REQUIRE(uut.get_SIZ().tile_offset_y == 0);
    REQUIRE(uut.get_SIZ().components.size() == 3);
    REQUIRE(uut.get_SIZ().components[0].precision == 8);
    REQUIRE(uut.get_SIZ().components[0].is_signed == false);
    REQUIRE(uut.get_SIZ().components[0].h_separation == 1);
    REQUIRE(uut.get_SIZ().components[0].v_separation == 1);
    REQUIRE(uut.get_SIZ().components[1].precision == 7);
    REQUIRE(uut.get_SIZ().components[1].is_signed == false);
    REQUIRE(uut.get_SIZ().components[1].h_separation == 1);
    REQUIRE(uut.get_SIZ().components[1].v_separation == 1);
    REQUIRE(uut.get_SIZ().components[2].precision == 6);
    REQUIRE(uut.get_SIZ().components[2].is_signed == false);
    REQUIRE(uut.get_SIZ().components[2].h_separation == 1);
    REQUIRE(uut.get_SIZ().components[2].v_separation == 1);
    REQUIRE(uut.get_chroma_format() == heif_chroma_444);
    REQUIRE(uut.get_precision(0) == 8);
    REQUIRE(uut.get_precision(1) == 7);
    REQUIRE(uut.get_precision(2) == 6);
    REQUIRE(uut.hasHighThroughputExtension() == false);
}

TEST_CASE( "codestream - COD + SIZ 4:2:2" )
{
    // This data is a subset of the example in ISO/IEC 15444-1:2019 Section J.10.1 "Main header"
    // with modifications
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x2F, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x03, 0x07, 0x01, 0x01, 0x07, 0x02, 0x01,
    0x07, 0x02, 0x01, 0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Ok);
    REQUIRE(uut.get_SIZ().reference_grid_width == 1);
    REQUIRE(uut.get_SIZ().reference_grid_height == 9);
    REQUIRE(uut.get_SIZ().image_horizontal_offset == 0);
    REQUIRE(uut.get_SIZ().image_vertical_offset == 0);
    REQUIRE(uut.get_SIZ().tile_width == 1);
    REQUIRE(uut.get_SIZ().tile_height == 9);
    REQUIRE(uut.get_SIZ().tile_offset_x == 0);
    REQUIRE(uut.get_SIZ().tile_offset_y == 0);
    REQUIRE(uut.get_SIZ().components.size() == 3);
    REQUIRE(uut.get_SIZ().components[0].precision == 8);
    REQUIRE(uut.get_SIZ().components[0].is_signed == false);
    REQUIRE(uut.get_SIZ().components[0].h_separation == 1);
    REQUIRE(uut.get_SIZ().components[0].v_separation == 1);
    REQUIRE(uut.get_SIZ().components[1].precision == 8);
    REQUIRE(uut.get_SIZ().components[1].is_signed == false);
    REQUIRE(uut.get_SIZ().components[1].h_separation == 2);
    REQUIRE(uut.get_SIZ().components[1].v_separation == 1);
    REQUIRE(uut.get_SIZ().components[2].precision == 8);
    REQUIRE(uut.get_SIZ().components[2].is_signed == false);
    REQUIRE(uut.get_SIZ().components[2].h_separation == 2);
    REQUIRE(uut.get_SIZ().components[2].v_separation == 1);
    REQUIRE(uut.get_chroma_format() == heif_chroma_422);
    REQUIRE(uut.get_precision(0) == 8);
    REQUIRE(uut.get_precision(1) == 8);
    REQUIRE(uut.get_precision(2) == 8);
    REQUIRE(uut.hasHighThroughputExtension() == false);
}

TEST_CASE( "codestream - COD + SIZ 4:2:0" )
{
    // This data is a subset of the example in ISO/IEC 15444-1:2019 Section J.10.1 "Main header"
    // with modifications
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x2F, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x03, 0x07, 0x01, 0x01, 0x07, 0x02, 0x02,
    0x07, 0x02, 0x02, 0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Ok);
    REQUIRE(uut.get_SIZ().reference_grid_width == 1);
    REQUIRE(uut.get_SIZ().reference_grid_height == 9);
    REQUIRE(uut.get_SIZ().image_horizontal_offset == 0);
    REQUIRE(uut.get_SIZ().image_vertical_offset == 0);
    REQUIRE(uut.get_SIZ().tile_width == 1);
    REQUIRE(uut.get_SIZ().tile_height == 9);
    REQUIRE(uut.get_SIZ().tile_offset_x == 0);
    REQUIRE(uut.get_SIZ().tile_offset_y == 0);
    REQUIRE(uut.get_SIZ().components.size() == 3);
    REQUIRE(uut.get_SIZ().components[0].precision == 8);
    REQUIRE(uut.get_SIZ().components[0].is_signed == false);
    REQUIRE(uut.get_SIZ().components[0].h_separation == 1);
    REQUIRE(uut.get_SIZ().components[0].v_separation == 1);
    REQUIRE(uut.get_SIZ().components[1].precision == 8);
    REQUIRE(uut.get_SIZ().components[1].is_signed == false);
    REQUIRE(uut.get_SIZ().components[1].h_separation == 2);
    REQUIRE(uut.get_SIZ().components[1].v_separation == 2);
    REQUIRE(uut.get_SIZ().components[2].precision == 8);
    REQUIRE(uut.get_SIZ().components[2].is_signed == false);
    REQUIRE(uut.get_SIZ().components[2].h_separation == 2);
    REQUIRE(uut.get_SIZ().components[2].v_separation == 2);
    REQUIRE(uut.get_chroma_format() == heif_chroma_420);
    REQUIRE(uut.get_precision(0) == 8);
    REQUIRE(uut.get_precision(1) == 8);
    REQUIRE(uut.get_precision(2) == 8);
    REQUIRE(uut.hasHighThroughputExtension() == false);
}


TEST_CASE( "codestream - COD + SIZ mismatched v subsampling" )
{
    // This data is a subset of the example in ISO/IEC 15444-1:2019 Section J.10.1 "Main header"
    // with modifications
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x2F, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x03, 0x07, 0x01, 0x01, 0x07, 0x02, 0x02,
    0x07, 0x02, 0x01, 0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Ok);
    REQUIRE(uut.get_SIZ().components.size() == 3);
    REQUIRE(uut.get_SIZ().components[1].h_separation == 2);
    REQUIRE(uut.get_SIZ().components[1].v_separation == 2);
    REQUIRE(uut.get_SIZ().components[2].h_separation == 2);
    REQUIRE(uut.get_SIZ().components[2].v_separation == 1);
    REQUIRE(uut.get_chroma_format() == heif_chroma_undefined);
}

TEST_CASE( "codestream - COD + SIZ mismatched h subsampling" )
{
    // This data is a subset of the example in ISO/IEC 15444-1:2019 Section J.10.1 "Main header"
    // with modifications
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x2F, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x03, 0x07, 0x01, 0x01, 0x07, 0x01, 0x02,
    0x07, 0x02, 0x02, 0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Ok);
    REQUIRE(uut.get_SIZ().components.size() == 3);
    REQUIRE(uut.get_SIZ().components[1].h_separation == 1);
    REQUIRE(uut.get_SIZ().components[1].v_separation == 2);
    REQUIRE(uut.get_SIZ().components[2].h_separation == 2);
    REQUIRE(uut.get_SIZ().components[2].v_separation == 2);
    REQUIRE(uut.get_chroma_format() == heif_chroma_undefined);
}

TEST_CASE( "codestream - COD + SIZ unsupported subsampling" )
{
    // This data is a subset of the example in ISO/IEC 15444-1:2019 Section J.10.1 "Main header"
    // with modifications
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x2F, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x03, 0x07, 0x01, 0x01, 0x07, 0x04, 0x01,
    0x07, 0x04, 0x01, 0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Ok);
    REQUIRE(uut.get_SIZ().components.size() == 3);
    REQUIRE(uut.get_SIZ().components[1].h_separation == 4);
    REQUIRE(uut.get_SIZ().components[1].v_separation == 1);
    REQUIRE(uut.get_SIZ().components[2].h_separation == 4);
    REQUIRE(uut.get_SIZ().components[2].v_separation == 1);
    REQUIRE(uut.get_chroma_format() == heif_chroma_undefined);
}


TEST_CASE( "codestream wrong marker SIZ" )
{
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0xEF, 0x00, 0x29, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x01, 0x07, 0x01, 0x01, 0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Invalid_input);
    REQUIRE(err.sub_error_code == heif_suberror_Invalid_J2K_codestream);
}

TEST_CASE( "codestream Lsiz too small" )
{
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x28, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x01, 0x07, 0x01, 0x01, 0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Invalid_input);
    REQUIRE(err.sub_error_code == heif_suberror_Invalid_J2K_codestream);
}

TEST_CASE( "codestream Lsiz too large" )
{
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0xC0, 0x27, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x01, 0x07, 0x01, 0x01, 0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Invalid_input);
    REQUIRE(err.sub_error_code == heif_suberror_Invalid_J2K_codestream);
}

TEST_CASE( "codestream Csiz too small" )
{
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x29, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x07, 0x01, 0x01, 0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Invalid_input);
    REQUIRE(err.sub_error_code == heif_suberror_Invalid_J2K_codestream);
}

TEST_CASE( "codestream Csiz too large" )
{
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x29, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x40, 0x01, 0x07, 0x01, 0x01, 0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Invalid_input);
    REQUIRE(err.sub_error_code == heif_suberror_Invalid_J2K_codestream);
}


TEST_CASE( "codestream bad Csiz" )
{
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x29, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x04, 0x07, 0x01, 0x01, 0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Invalid_input);
    REQUIRE(err.sub_error_code == heif_suberror_Invalid_J2K_codestream);
}


TEST_CASE( "codestream missing segments" )
{
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x29, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x01, 0x07, 0x01, 0x01
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Invalid_input);
    REQUIRE(err.sub_error_code == heif_suberror_Invalid_J2K_codestream);
}

TEST_CASE( "codestream - COD + SIZ + CAP" )
{
    // This data is a modified version of subset of the example in ISO/IEC 15444-1:2019 Section
    // J.10.1 "Main header"
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x29, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x01, 0x07, 0x01, 0x01, 
    0xFF, 0x50, 0x00, 0x08, 0x00, 0x02, 0x00, 0x00,   0x00, 0x22,
    0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Ok);
    REQUIRE(uut.get_SIZ().reference_grid_width == 1);
    REQUIRE(uut.get_SIZ().reference_grid_height == 9);
    REQUIRE(uut.get_SIZ().image_horizontal_offset == 0);
    REQUIRE(uut.get_SIZ().image_vertical_offset == 0);
    REQUIRE(uut.get_SIZ().tile_width == 1);
    REQUIRE(uut.get_SIZ().tile_height == 9);
    REQUIRE(uut.get_SIZ().tile_offset_x == 0);
    REQUIRE(uut.get_SIZ().tile_offset_y == 0);
    REQUIRE(uut.get_SIZ().components.size() == 1);
    REQUIRE(uut.get_SIZ().components[0].precision == 8);
    REQUIRE(uut.get_SIZ().components[0].is_signed == false);
    REQUIRE(uut.get_SIZ().components[0].h_separation == 1);
    REQUIRE(uut.get_SIZ().components[0].v_separation == 1);
    REQUIRE(uut.get_chroma_format() == heif_chroma_monochrome);
    REQUIRE(uut.get_precision(0) == 8);
    REQUIRE(uut.hasHighThroughputExtension() == true);
}

TEST_CASE( "codestream CAP short" )
{
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x29, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x01, 0x07, 0x01, 0x01, 
    0xFF, 0x50, 0x00, 0x08, 0x00, 0x02, 0x00, 0x00,   0x00
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Invalid_input);
    REQUIRE(err.sub_error_code == heif_suberror_Invalid_J2K_codestream);
}

TEST_CASE( "codestream Lcap short" )
{
std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x29, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x01, 0x07, 0x01, 0x01, 
    0xFF, 0x50, 0x00, 0x07, 0x00, 0x02, 0x00, 0x00,   0x00, 0x22,
    0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Invalid_input);
    REQUIRE(err.sub_error_code == heif_suberror_Invalid_J2K_codestream);
}

TEST_CASE( "codestream Lcap long" )
{
std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x29, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x01, 0x07, 0x01, 0x01, 
    0xFF, 0x50, 0x00, 0x47, 0x00, 0x02, 0x00, 0x00,   0x00, 0x22,
    0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Invalid_input);
    REQUIRE(err.sub_error_code == heif_suberror_Invalid_J2K_codestream);
}


TEST_CASE( "codestream - COD + SIZ + CAP multiple" )
{
    // This data is a modified version of subset of the example in ISO/IEC 15444-1:2019 Section
    // J.10.1 "Main header"
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x29, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x01, 0x07, 0x01, 0x01, 
    0xFF, 0x50, 0x00, 0x0A, 0x00, 0x12, 0x00, 0x00,   0xFF, 0xFF, 0x00, 0x22,
    0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Ok);
    REQUIRE(uut.get_SIZ().reference_grid_width == 1);
    REQUIRE(uut.get_SIZ().reference_grid_height == 9);
    REQUIRE(uut.get_SIZ().image_horizontal_offset == 0);
    REQUIRE(uut.get_SIZ().image_vertical_offset == 0);
    REQUIRE(uut.get_SIZ().tile_width == 1);
    REQUIRE(uut.get_SIZ().tile_height == 9);
    REQUIRE(uut.get_SIZ().tile_offset_x == 0);
    REQUIRE(uut.get_SIZ().tile_offset_y == 0);
    REQUIRE(uut.get_SIZ().components.size() == 1);
    REQUIRE(uut.get_SIZ().components[0].precision == 8);
    REQUIRE(uut.get_SIZ().components[0].is_signed == false);
    REQUIRE(uut.get_SIZ().components[0].h_separation == 1);
    REQUIRE(uut.get_SIZ().components[0].v_separation == 1);
    REQUIRE(uut.get_chroma_format() == heif_chroma_monochrome);
    REQUIRE(uut.get_precision(0) == 8);
    REQUIRE(uut.hasHighThroughputExtension() == true);
}


TEST_CASE( "codestream - COD + SIZ + CAP other" )
{
    // This data is a modified version of subset of the example in ISO/IEC 15444-1:2019 Section
    // J.10.1 "Main header"
    // Note that the ccap2 value may not be valid
    std::vector<uint8_t> data = {
    0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x29, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0x01, 0x07, 0x01, 0x01, 
    0xFF, 0x50, 0x00, 0x08, 0x00, 0x40, 0x00, 0x00,   0x00, 0x22,
    0xFF, 0x5C, 0x00,
    };
    JPEG2000MainHeader uut;
    uut.setHeaderData(data);
    Error err = uut.doParse();
    REQUIRE(err.error_code == heif_error_Ok);
    REQUIRE(uut.get_SIZ().reference_grid_width == 1);
    REQUIRE(uut.get_SIZ().reference_grid_height == 9);
    REQUIRE(uut.get_SIZ().image_horizontal_offset == 0);
    REQUIRE(uut.get_SIZ().image_vertical_offset == 0);
    REQUIRE(uut.get_SIZ().tile_width == 1);
    REQUIRE(uut.get_SIZ().tile_height == 9);
    REQUIRE(uut.get_SIZ().tile_offset_x == 0);
    REQUIRE(uut.get_SIZ().tile_offset_y == 0);
    REQUIRE(uut.get_SIZ().components.size() == 1);
    REQUIRE(uut.get_SIZ().components[0].precision == 8);
    REQUIRE(uut.get_SIZ().components[0].is_signed == false);
    REQUIRE(uut.get_SIZ().components[0].h_separation == 1);
    REQUIRE(uut.get_SIZ().components[0].v_separation == 1);
    REQUIRE(uut.get_chroma_format() == heif_chroma_monochrome);
    REQUIRE(uut.get_precision(0) == 8);
    REQUIRE(uut.hasHighThroughputExtension() == false);
}


// --- 'pclr' parsing (GHSA-9c75-9g8r-4728) ----------------------------------
//
// A 'pclr' box with zero palette columns declares no entry data, so the entry
// count was not bounded by the box size and each declared entry allocated an
// empty vector. Nested 'j2kH' containers repeated this hundreds of times from
// a few KB of input. The parser now rejects zero columns, bounds the entry
// count by the box payload, and charges the palette to the memory limits.

// Parses a single box from raw bytes, as it would be encountered inside a
// 'j2kH' container.
static Error parse_box(const std::vector<uint8_t>& bytes,
                       const heif_security_limits* limits,
                       std::shared_ptr<Box>* box)
{
  auto reader = std::make_shared<StreamReader_memory>(bytes.data(), bytes.size(), false);
  BitstreamRange range(reader, bytes.size());
  return Box::read(range, box, limits);
}

// Box header (size, 'pclr') followed by the payload.
static std::vector<uint8_t> pclr_box(std::vector<uint8_t> payload)
{
  uint32_t size = static_cast<uint32_t>(8 + payload.size());
  std::vector<uint8_t> bytes{static_cast<uint8_t>(size >> 24), static_cast<uint8_t>(size >> 16),
                             static_cast<uint8_t>(size >> 8), static_cast<uint8_t>(size),
                             'p', 'c', 'l', 'r'};
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}


TEST_CASE("pclr parse")
{
  // NE=2, NPC=2, B={7,11} (8 and 12 bits), entries (0x10, 0x0123) and (0x20, 0x0456)
  auto bytes = pclr_box({0x00, 0x02, 0x02, 0x07, 0x0b,
                         0x10, 0x01, 0x23,
                         0x20, 0x04, 0x56});
  std::shared_ptr<Box> box;
  Error err = parse_box(bytes, heif_get_global_security_limits(), &box);
  REQUIRE(err == Error::Ok);

  auto pclr = std::dynamic_pointer_cast<Box_pclr>(box);
  REQUIRE(pclr);
  REQUIRE(pclr->get_bit_depths() == std::vector<uint8_t>{8, 12});
  REQUIRE(pclr->get_entries().size() == 2);
  REQUIRE(pclr->get_entries()[0].columns == std::vector<uint16_t>{0x10, 0x0123});
  REQUIRE(pclr->get_entries()[1].columns == std::vector<uint16_t>{0x20, 0x0456});
}


TEST_CASE("pclr bit depth field is precision minus one")
{
  // Table I.13: B_i = 0 is a 1-bit column stored in one byte, B_i = 15 is a
  // 16-bit column stored in two bytes. NE=1, NPC=2, entry (1, 0xFFFF).
  auto bytes = pclr_box({0x00, 0x01, 0x02, 0x00, 0x0f,
                         0x01, 0xff, 0xff});
  std::shared_ptr<Box> box;
  Error err = parse_box(bytes, heif_get_global_security_limits(), &box);
  REQUIRE(err == Error::Ok);

  auto pclr = std::dynamic_pointer_cast<Box_pclr>(box);
  REQUIRE(pclr);
  REQUIRE(pclr->get_bit_depths() == std::vector<uint8_t>{1, 16});
  REQUIRE(pclr->get_entries().size() == 1);
  REQUIRE(pclr->get_entries()[0].columns == std::vector<uint16_t>{1, 0xffff});
}


TEST_CASE("pclr rejects unsupported bit depths")
{
  SECTION("17 bits does not fit the 16-bit entry storage") {
    // NE=1, NPC=1, B=16 (17 bits), entry 0x000001
    auto bytes = pclr_box({0x00, 0x01, 0x01, 0x10, 0x00, 0x00, 0x01});
    std::shared_ptr<Box> box;
    Error err = parse_box(bytes, heif_get_global_security_limits(), &box);
    REQUIRE(err.error_code == heif_error_Unsupported_feature);
  }

  SECTION("signed columns") {
    // NE=1, NPC=1, B=0x87 (signed 8 bits), entry 0x01
    auto bytes = pclr_box({0x00, 0x01, 0x01, 0x87, 0x01});
    std::shared_ptr<Box> box;
    Error err = parse_box(bytes, heif_get_global_security_limits(), &box);
    REQUIRE(err.error_code == heif_error_Unsupported_feature);
  }
}


TEST_CASE("pclr one-bit columns still bound the entry count")
{
  // The smallest possible column (B_i = 0, 1 bit) still occupies one byte per
  // entry, so the byte bound must reject NE=65535 with no entry data. This is
  // the case a naive ceil(B_i / 8) would let through.
  auto bytes = pclr_box({0xff, 0xff, 0x01, 0x00});
  std::shared_ptr<Box> box;
  Error err = parse_box(bytes, heif_get_global_security_limits(), &box);
  REQUIRE(err.error_code == heif_error_Invalid_input);
  REQUIRE(err.sub_error_code == heif_suberror_End_of_data);
}


TEST_CASE("pclr write and parse round trip")
{
  auto pclr = std::make_shared<Box_pclr>();
  pclr->set_columns(3, 12);
  Box_pclr::PaletteEntry entry0;
  entry0.columns = {0x001, 0x002, 0xfff};
  pclr->add_entry(entry0);
  Box_pclr::PaletteEntry entry1;
  entry1.columns = {0x800, 0x000, 0x7ff};
  pclr->add_entry(entry1);

  StreamWriter writer;
  REQUIRE(pclr->write(writer) == Error::Ok);
  const std::vector<uint8_t> bytes = writer.get_data();

  std::shared_ptr<Box> box;
  Error err = parse_box(bytes, heif_get_global_security_limits(), &box);
  REQUIRE(err == Error::Ok);

  auto parsed = std::dynamic_pointer_cast<Box_pclr>(box);
  REQUIRE(parsed);
  REQUIRE(parsed->get_bit_depths() == pclr->get_bit_depths());
  REQUIRE(parsed->get_entries().size() == 2);
  REQUIRE(parsed->get_entries()[0].columns == entry0.columns);
  REQUIRE(parsed->get_entries()[1].columns == entry1.columns);
}


TEST_CASE("pclr zero columns rejected")
{
  // NE=65535, NPC=0: the 11-byte box from the advisory.
  auto bytes = pclr_box({0xff, 0xff, 0x00});
  std::shared_ptr<Box> box;
  Error err = parse_box(bytes, heif_get_global_security_limits(), &box);
  REQUIRE(err.error_code == heif_error_Invalid_input);
  REQUIRE(std::dynamic_pointer_cast<Box_pclr>(box) == nullptr);
}


TEST_CASE("pclr entry count bounded by box payload")
{
  // NE=65535, NPC=1, B=7 (8 bits), but no entry data at all.
  auto bytes = pclr_box({0xff, 0xff, 0x01, 0x07});
  std::shared_ptr<Box> box;
  Error err = parse_box(bytes, heif_get_global_security_limits(), &box);
  REQUIRE(err.error_code == heif_error_Invalid_input);
  REQUIRE(err.sub_error_code == heif_suberror_End_of_data);
}


TEST_CASE("pclr palette charged to memory limits")
{
  // NE=256, NPC=1, B=7 (8 bits), with all 256 entry bytes present. This is a
  // valid palette, but it costs 256 PaletteEntry vectors in memory.
  std::vector<uint8_t> payload{0x01, 0x00, 0x01, 0x07};
  for (int i = 0; i < 256; i++) {
    payload.push_back(static_cast<uint8_t>(i));
  }
  auto bytes = pclr_box(payload);

  heif_security_limits limits = *heif_get_global_security_limits();
  TotalMemoryTracker tracker(&limits);

  SECTION("accepted within the budget") {
    std::shared_ptr<Box> box;
    Error err = parse_box(bytes, &limits, &box);
    REQUIRE(err == Error::Ok);
    auto pclr = std::dynamic_pointer_cast<Box_pclr>(box);
    REQUIRE(pclr);
    REQUIRE(pclr->get_entries().size() == 256);
  }

  SECTION("rejected when it exceeds max_total_memory") {
    limits.max_total_memory = 1024;
    std::shared_ptr<Box> box;
    Error err = parse_box(bytes, &limits, &box);
    REQUIRE(err.error_code == heif_error_Memory_allocation_error);
    REQUIRE(err.sub_error_code == heif_suberror_Security_limit_exceeded);
  }
}


TEST_CASE("pclr zero columns in nested j2kH rejects the file")
{
  // The advisory's reproducer: 200 zero-column 'pclr' boxes spread over
  // nested 'j2kH' properties. It used to parse successfully at ~330 MB RSS.
  heif_context* ctx = heif_context_alloc();
  std::string path = tests_data_directory + "/pclr_zero_columns.heic";
  heif_error err = heif_context_read_from_file(ctx, path.c_str(), nullptr);
  REQUIRE(err.code == heif_error_Invalid_input);
  heif_context_free(ctx);
}
