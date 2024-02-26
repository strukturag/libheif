/*
 * HEIF codec.
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
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


#include <cstdint>
#include <cstring>
#include <algorithm>
#include <map>
#include <iostream>
#include <cassert>

#include "libheif/heif.h"
#include "uncompressed.h"
#include "uncompressed_box.h"


/**
 * Check for valid component format.
 *
 * @param format the format value to check
 * @return true if the format is a valid value, or false otherwise
 */
bool is_valid_component_format(uint8_t format)
{
  return format <= component_format_max_valid;
}

static std::map<heif_uncompressed_component_format, const char*> sNames_uncompressed_component_format{
    {component_format_unsigned, "unsigned"},
    {component_format_float,    "float"},
    {component_format_complex,  "complex"}
};


/**
 * Check for valid interleave mode.
 *
 * @param interleave the interleave value to check
 * @return true if the interleave mode is valid, or false otherwise
 */
bool is_valid_interleave_mode(uint8_t interleave)
{
  return interleave <= interleave_mode_max_valid;
}

static std::map<heif_uncompressed_interleave_mode, const char*> sNames_uncompressed_interleave_mode{
    {interleave_mode_component,      "component"},
    {interleave_mode_pixel,          "pixel"},
    {interleave_mode_mixed,          "mixed"},
    {interleave_mode_row,            "row"},
    {interleave_mode_tile_component, "tile-component"},
    {interleave_mode_multi_y,        "multi-y"}
};


/**
 * Check for valid sampling mode.
 *
 * @param sampling the sampling value to check
 * @return true if the sampling mode is valid, or false otherwise
 */
bool is_valid_sampling_mode(uint8_t sampling)
{
  return sampling <= sampling_mode_max_valid;
}

static std::map<heif_uncompressed_sampling_mode, const char*> sNames_uncompressed_sampling_mode{
    {sampling_mode_no_subsampling, "no subsampling"},
    {sampling_mode_422,            "4:2:2"},
    {sampling_mode_420,            "4:2:0"},
    {sampling_mode_411,            "4:1:1"}
};


bool is_predefined_component_type(uint16_t type)
{
  // check whether the component type can be mapped to heif_uncompressed_component_type and we have a name defined for
  // it in sNames_uncompressed_component_type.
  return (type >= 0 && type <= component_type_max_valid);
}

static std::map<heif_uncompressed_component_type, const char*> sNames_uncompressed_component_type{
    {component_type_monochrome,   "monochrome"},
    {component_type_Y,            "Y"},
    {component_type_Cb,           "Cb"},
    {component_type_Cr,           "Cr"},
    {component_type_red,          "red"},
    {component_type_green,        "green"},
    {component_type_blue,         "blue"},
    {component_type_alpha,        "alpha"},
    {component_type_depth,        "depth"},
    {component_type_disparity,    "disparity"},
    {component_type_palette,      "palette"},
    {component_type_filter_array, "filter-array"},
    {component_type_padded,       "padded"},
    {component_type_cyan,         "cyan"},
    {component_type_magenta,      "magenta"},
    {component_type_yellow,       "yellow"},
    {component_type_key_black,    "key (black)"}
};

template <typename T> const char* get_name(T val, const std::map<T, const char*>& table)
{
  auto iter = table.find(val);
  if (iter == table.end()) {
    return "unknown";
  }
  else {
    return iter->second;
  }
}

Error Box_cmpd::parse(BitstreamRange& range)
{
  unsigned int component_count = range.read32();

  for (unsigned int i = 0; i < component_count && !range.error() && !range.eof(); i++) {
    Component component;
    component.component_type = range.read16();
    if (component.component_type >= 0x8000) {
      component.component_type_uri = range.read_string();
    }
    else {
      component.component_type_uri = std::string();
    }
    m_components.push_back(component);
  }

  return range.get_error();
}

std::string Box_cmpd::Component::get_component_type_name(uint16_t component_type)
{
  std::stringstream sstr;

  if (is_predefined_component_type(component_type)) {
    sstr << get_name(heif_uncompressed_component_type(component_type), sNames_uncompressed_component_type) << "\n";
  }
  else {
    sstr << "0x" << std::hex << component_type << std::dec << "\n";
  }

  return sstr.str();
}


std::string Box_cmpd::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  for (const auto& component : m_components) {
    sstr << indent << "component_type: " << component.get_component_type_name();

    if (component.component_type >= 0x8000) {
      sstr << indent << "| component_type_uri: " << component.component_type_uri << "\n";
    }
  }

  return sstr.str();
}

Error Box_cmpd::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write32((uint32_t) m_components.size());
  for (const auto& component : m_components) {
    writer.write16(component.component_type);
    if (component.component_type >= 0x8000) {
      writer.write(component.component_type_uri);
    }
  }

  prepend_header(writer, box_start);

  return Error::Ok;
}

Error Box_uncC::parse(BitstreamRange& range)
{
  parse_full_box_header(range);
  m_profile = range.read32();
  if (get_version() == 1) {
    if (m_profile == fourcc_to_uint32("rgb3")) {
      Box_uncC::Component component0 = {0, 8, component_format_unsigned, 0};
      add_component(component0);
      Box_uncC::Component component1 = {1, 8, component_format_unsigned, 0};
      add_component(component1);
      Box_uncC::Component component2 = {2, 8, component_format_unsigned, 0};
      add_component(component2);
    } else if ((m_profile == fourcc_to_uint32("rgba")) || (m_profile == fourcc_to_uint32("abgr"))) {
      Box_uncC::Component component0 = {0, 8, component_format_unsigned, 0};
      add_component(component0);
      Box_uncC::Component component1 = {1, 8, component_format_unsigned, 0};
      add_component(component1);
      Box_uncC::Component component2 = {2, 8, component_format_unsigned, 0};
      add_component(component2);
      Box_uncC::Component component3 = {3, 8, component_format_unsigned, 0};
      add_component(component3);
    } else {
        return Error{heif_error_Invalid_input, heif_suberror_Invalid_parameter_value, "Invalid component format"};
    }
  } else if (get_version() == 0) {

    unsigned int component_count = range.read32();

    for (unsigned int i = 0; i < component_count && !range.error() && !range.eof(); i++) {
      Component component;
      component.component_index = range.read16();
      component.component_bit_depth = uint16_t(range.read8() + 1);
      component.component_format = range.read8();
      component.component_align_size = range.read8();
      m_components.push_back(component);

      if (!is_valid_component_format(component.component_format)) {
        return Error{heif_error_Invalid_input, heif_suberror_Invalid_parameter_value, "Invalid component format"};
      }
    }

    m_sampling_type = range.read8();
    if (!is_valid_sampling_mode(m_sampling_type)) {
      return Error{heif_error_Invalid_input, heif_suberror_Invalid_parameter_value, "Invalid sampling mode"};
    }

    m_interleave_type = range.read8();
    if (!is_valid_interleave_mode(m_interleave_type)) {
      return Error{heif_error_Invalid_input, heif_suberror_Invalid_parameter_value, "Invalid interleave mode"};
    }

    m_block_size = range.read8();

    uint8_t flags = range.read8();
    m_components_little_endian = !!(flags & 0x80);
    m_block_pad_lsb = !!(flags & 0x40);
    m_block_little_endian = !!(flags & 0x20);
    m_block_reversed = !!(flags & 0x10);
    m_pad_unknown = !!(flags & 0x08);

    m_pixel_size = range.read32();

    m_row_align_size = range.read32();

    m_tile_align_size = range.read32();

    m_num_tile_cols = range.read32() + 1;

    m_num_tile_rows = range.read32() + 1;
  }
  return range.get_error();
}

std::shared_ptr<std::vector<std::shared_ptr<Box>>> Box_uncC::get_implied_boxes()
{
  std::shared_ptr<std::vector<std::shared_ptr<Box>>> extra_boxes = std::make_shared<std::vector<std::shared_ptr<Box>>>();
  if (get_version() == 1) {
    std::shared_ptr<Box_cmpd> cmpd = std::make_shared<Box_cmpd>();
    if (m_profile == fourcc_to_uint32("rgb3")) {
      Box_cmpd::Component rComponent = {component_type_red};
      cmpd->add_component(rComponent);
      Box_cmpd::Component gComponent = {component_type_green};
      cmpd->add_component(gComponent);
      Box_cmpd::Component bComponent = {component_type_blue};
      cmpd->add_component(bComponent);
    } else if (m_profile == fourcc_to_uint32("rgba")) {
      Box_cmpd::Component rComponent = {component_type_red};
      cmpd->add_component(rComponent);
      Box_cmpd::Component gComponent = {component_type_green};
      cmpd->add_component(gComponent);
      Box_cmpd::Component bComponent = {component_type_blue};
      cmpd->add_component(bComponent);
      Box_cmpd::Component aComponent = {component_type_alpha};
      cmpd->add_component(aComponent);
    } else if (m_profile == fourcc_to_uint32("abgr")) {
      Box_cmpd::Component aComponent = {component_type_alpha};
      cmpd->add_component(aComponent);
      Box_cmpd::Component bComponent = {component_type_blue};
      cmpd->add_component(bComponent);
      Box_cmpd::Component gComponent = {component_type_green};
      cmpd->add_component(gComponent);
      Box_cmpd::Component rComponent = {component_type_red};
      cmpd->add_component(rComponent);
    }

    extra_boxes->push_back(std::move(cmpd));
  }
  return extra_boxes;
}


std::string Box_uncC::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "profile: " << m_profile;
  if (m_profile != 0) {
    sstr << " (" << to_fourcc(m_profile) << ")";
    sstr << "\n";
  }
  if (get_version() == 0) {
    for (const auto& component : m_components) {
      sstr << indent << "component_index: " << component.component_index << "\n";
      sstr << indent << "component_bit_depth: " << (int) component.component_bit_depth << "\n";
      sstr << indent << "component_format: " << get_name(heif_uncompressed_component_format(component.component_format), sNames_uncompressed_component_format) << "\n";
      sstr << indent << "component_align_size: " << (int) component.component_align_size << "\n";
    }

    sstr << indent << "sampling_type: " << get_name(heif_uncompressed_sampling_mode(m_sampling_type), sNames_uncompressed_sampling_mode) << "\n";

    sstr << indent << "interleave_type: " << get_name(heif_uncompressed_interleave_mode(m_interleave_type), sNames_uncompressed_interleave_mode) << "\n";

    sstr << indent << "block_size: " << (int) m_block_size << "\n";

    sstr << indent << "components_little_endian: " << m_components_little_endian << "\n";
    sstr << indent << "block_pad_lsb: " << m_block_pad_lsb << "\n";
    sstr << indent << "block_little_endian: " << m_block_little_endian << "\n";
    sstr << indent << "block_reversed: " << m_block_reversed << "\n";
    sstr << indent << "pad_unknown: " << m_pad_unknown << "\n";

    sstr << indent << "pixel_size: " << m_pixel_size << "\n";

    sstr << indent << "row_align_size: " << m_row_align_size << "\n";

    sstr << indent << "tile_align_size: " << m_tile_align_size << "\n";

    sstr << indent << "num_tile_cols: " << m_num_tile_cols << "\n";

    sstr << indent << "num_tile_rows: " << m_num_tile_rows << "\n";
  }
  return sstr.str();
}

Error Box_uncC::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);
  writer.write32(m_profile);
  if (get_version() == 1) {
  }
  else if (get_version() == 0) {
    writer.write32((uint32_t)m_components.size());
    for (const auto &component : m_components) {
      if (component.component_bit_depth < 1 || component.component_bit_depth > 256) {
        return {heif_error_Invalid_input, heif_suberror_Invalid_parameter_value, "component bit-depth out of range [1..256]"};
      }

      writer.write16(component.component_index);
      writer.write8(uint8_t(component.component_bit_depth - 1));
      writer.write8(component.component_format);
      writer.write8(component.component_align_size);
    }
    writer.write8(m_sampling_type);
    writer.write8(m_interleave_type);
    writer.write8(m_block_size);
    uint8_t flags = 0;
    flags |= (m_components_little_endian ? 0x80 : 0);
    flags |= (m_block_pad_lsb ? 0x40 : 0);
    flags |= (m_block_little_endian ? 0x20 : 0);
    flags |= (m_block_reversed ? 0x10 : 0);
    flags |= (m_pad_unknown ? 0x08 : 0);
    writer.write8(flags);
    writer.write32(m_pixel_size);
    writer.write32(m_row_align_size);
    writer.write32(m_tile_align_size);
    writer.write32(m_num_tile_cols - 1);
    writer.write32(m_num_tile_rows - 1);
  }
  prepend_header(writer, box_start);

  return Error::Ok;
}
