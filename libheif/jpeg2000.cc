/*
 * HEIF JPEG 2000 codec.
 * Copyright (c) 2023 Brad Hards <bradh@frogmouth.net>
 *
 * This file is part of libheif.
 *
 * libheif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libheif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libheif.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "jpeg2000.h"
#include <cstdint>
#include <stdio.h>

Error Box_cdef::parse(BitstreamRange& range)
{
    int channel_count = range.read16();

    for (int i = 0; i < channel_count && !range.error() && !range.eof(); i++) {
        Channel channel;
        channel.channel_index = range.read16();
        channel.channel_type = range.read16();
        channel.channel_association = range.read16();
        m_channels.push_back(channel);
    }

    return range.get_error();
}

std::string Box_cdef::dump(Indent& indent) const
{
    std::ostringstream sstr;
    sstr << Box::dump(indent);

    for (const auto &channel : m_channels)
    {
        sstr << indent << "channel_index: " << channel.channel_index
            << ", channel_type: " << channel.channel_type
            << ", channel_association: " << channel.channel_association << "\n";
    }

    return sstr.str();
}


Error Box_cdef::write(StreamWriter& writer) const
{
    size_t box_start = reserve_box_header_space(writer);

    writer.write16((uint16_t) m_channels.size());
    for (const auto& channel : m_channels) {
        writer.write16(channel.channel_index);
        writer.write16(channel.channel_type);
        writer.write16(channel.channel_association);
    }

    prepend_header(writer, box_start);

    return Error::Ok;
}


Error Box_cmap::parse(BitstreamRange& range)
{
    while (!range.eof() && !range.error()) {
        Component component;
        component.component_index = range.read16();
        component.mapping_type = range.read8();
        component.palette_colour = range.read8();
        m_components.push_back(component);
    }

    return range.get_error();
}


std::string Box_cmap::dump(Indent& indent) const
{
    std::ostringstream sstr;
    sstr << Box::dump(indent);

    for (const auto &component : m_components)
    {
        sstr << indent << "component_index: " << component.component_index
            << ", mapping_type: " << (int)(component.mapping_type)
            << ", palette_colour: " << (int)(component.palette_colour) << "\n";
    }

    return sstr.str();
}


Error Box_cmap::write(StreamWriter& writer) const
{
    size_t box_start = reserve_box_header_space(writer);

    for (const auto& component : m_components) {
        writer.write16(component.component_index);
        writer.write8(component.mapping_type);
        writer.write8(component.palette_colour);
    }

    prepend_header(writer, box_start);

    return Error::Ok;
}


Error Box_pclr::parse(BitstreamRange& range)
{
    uint16_t num_entries = range.read16();
    uint8_t num_palette_columns = range.read8();
    for (uint8_t i = 0; i < num_palette_columns; i++)
    {
        uint8_t bit_depth = range.read8();
        if (bit_depth & 0x80)
        {
            return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   "pclr with signed data is not supported");
        }
        if (bit_depth > 16)
        {
            return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   "pclr more than 16 bits per channel is not supported");
        }
        m_bitDepths.push_back(bit_depth);
    }
    for (uint16_t j = 0; j < num_entries; j++)
    {
        PaletteEntry entry;
        for (unsigned long int i = 0; i < entry.columns.size(); i++)
        {
            if (m_bitDepths[i] <=8)
            {
                entry.columns.push_back(range.read8());
            }
            else
            {
                entry.columns.push_back(range.read16());
            }
        }
        m_entries.push_back(entry);
    }

    return range.get_error();
}


std::string Box_pclr::dump(Indent& indent) const
{
    std::ostringstream sstr;
    sstr << Box::dump(indent);

    sstr << indent << "NE: " << m_entries.size();
    sstr << ", NPC: " << (int)get_num_columns();
    sstr << ", B: ";
    for (uint8_t b: m_bitDepths)
    {
        sstr << (int)b << ", ";
    }
    // TODO: maybe dump entries too?
    sstr << "\n";

    return sstr.str();
}


Error Box_pclr::write(StreamWriter& writer) const
{
    if (get_num_columns() == 0)
    {
        // skip
        return Error::Ok;
    }

    size_t box_start = reserve_box_header_space(writer);

    writer.write16(get_num_entries());
    writer.write8(get_num_columns());
    for (uint8_t b: m_bitDepths)
    {
        writer.write8(b);
    }
    for (PaletteEntry entry: m_entries)
    {
        for (unsigned long int i = 0; i < entry.columns.size(); i++)
        {
            if (m_bitDepths[i] <= 8)
            {
                writer.write8((uint8_t)(entry.columns[i]));
            }
            else
            {
                writer.write16(entry.columns[i]);
            }
        }
    }

    prepend_header(writer, box_start);

    return Error::Ok;
}

void Box_pclr::set_columns(uint8_t num_columns, uint8_t bit_depth)
{
    m_bitDepths.clear();
    m_entries.clear();
    for (int i = 0; i < num_columns; i++)
    {
        m_bitDepths.push_back(bit_depth);
    }
}

Error Box_j2kL::parse(BitstreamRange& range)
{
    int layer_count = range.read16();

    for (int i = 0; i < layer_count && !range.error() && !range.eof(); i++) {
        Layer layer;
        layer.layer_id = range.read16();
        layer.discard_levels = range.read8();
        layer.decode_layers = range.read16();
        m_layers.push_back(layer);
    }

    return range.get_error();
}

std::string Box_j2kL::dump(Indent& indent) const
{
    std::ostringstream sstr;
    sstr << Box::dump(indent);

    for (const auto &layer : m_layers)
    {
        sstr << indent << "layer_id: " << layer.layer_id
            << ", discard_levels: " << (int)(layer.discard_levels)
            << ", decode_layers: " << layer.decode_layers << "\n";
    }

    return sstr.str();
}


Error Box_j2kL::write(StreamWriter& writer) const
{
    size_t box_start = reserve_box_header_space(writer);

    writer.write16((uint16_t) m_layers.size());
    for (const auto& layer : m_layers) {
        writer.write16(layer.layer_id);
        writer.write8(layer.discard_levels);
        writer.write16(layer.decode_layers);
    }

    prepend_header(writer, box_start);

    return Error::Ok;
}


Error Box_j2kH::parse(BitstreamRange& range)
{
    return read_children(range);
}

std::string Box_j2kH::dump(Indent& indent) const
{
    std::ostringstream sstr;
    sstr << Box::dump(indent);

    sstr << dump_children(indent);

    return sstr.str();
}

