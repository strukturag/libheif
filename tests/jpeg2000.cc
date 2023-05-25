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

#include "catch.hpp"
#include "libheif/heif.h"
#include "libheif/jpeg2000.h"
#include <cstdint>
#include <iostream>


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
    std::vector<uint8_t> expected = {0x00, 0x00, 0x00, 0x14, 'p', 'c', 'l', 'r', 0x00, 0x02, 0x03, 0x08, 0x08, 0x08, 0x01, 0x02, 0x03, 0xFF, 0xFE, 0xFD};
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
    std::vector<uint8_t> expected = {0x00, 0x00, 0x00, 0x1A, 'p', 'c', 'l', 'r', 0x00, 0x02, 0x03, 0x0C, 0x0C, 0x0C, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x0F, 0xFF, 0x0F, 0xFE, 0x0F, 0xFD};
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
