/*
 * HEIF EVC codec.
 * Copyright (c) 2024 Brad Hards <bradh@frogmouth.net>
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

#include "evc_boxes.h"
#include "bitstream.h"
#include "error.h"
#include "file.h"

#include <iomanip>
#include <string>
#include <utility>
#include <vector>
#include <libheif/api_structs.h>



Error Box_evcC::parse(BitstreamRange& range, const heif_security_limits* limits)
{
  m_configuration.configurationVersion = range.read8();
  m_configuration.profile_idc = range.read8();
  m_configuration.level_idc = range.read8();
  m_configuration.toolset_idc_h = range.read32();
  m_configuration.toolset_idc_l = range.read32();
  uint8_t b = range.read8();
  m_configuration.chroma_format_idc = (b >> 6) & 0b11;
  uint8_t bit_depth_luma_minus8 = (b >> 3) & 0b111;
  m_configuration.bit_depth_luma = bit_depth_luma_minus8 + 8;
  uint8_t bit_depth_chroma_minus8 = b & 0b111;
  m_configuration.bit_depth_chroma = bit_depth_chroma_minus8 + 8;
  m_configuration.pic_width_in_luma_samples = range.read16();
  m_configuration.pic_height_in_luma_samples = range.read16();
  b = range.read8();
  uint8_t lengthSizeMinus1 = b & 0b11;
  m_configuration.lengthSize = lengthSizeMinus1 + 1;
  uint8_t num_of_arrays = range.read8();
  for (uint8_t j = 0; j < num_of_arrays && !range.error(); j++) {
    NalArray array;
    b = range.read8();
    array.array_completeness = ((b & 0x80) == 0x80);
    array.NAL_unit_type = (b & 0b00111111);
    uint16_t num_nalus = range.read16();
    for (int i = 0; i < num_nalus && !range.error(); i++) {
      uint16_t nal_unit_length = range.read16();
      if (nal_unit_length == 0) {
        // Ignore empty NAL units.
        continue;
      }
      std::vector<uint8_t> nal_unit;
      if (range.prepare_read(nal_unit_length)) {
        nal_unit.resize(nal_unit_length);
        bool success = range.get_istream()->read((char*) nal_unit.data(), nal_unit_length);
        if (!success) {
          return Error{heif_error_Invalid_input, heif_suberror_End_of_data, "error while reading evcC box"};
        }
      }

      array.nal_units.push_back(std::move(nal_unit));
    }
    m_nal_array.push_back(array);
  } 

  return range.get_error();
}


std::string Box_evcC::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  // TODO: decode more of this
  sstr << indent << "configurationVersion: " << ((int)m_configuration.configurationVersion) << "\n";
  sstr << indent << "profile_idc: " << ((int)m_configuration.profile_idc)
       << " (" << get_profile_as_text() << ")" << "\n";
  sstr << indent << "level_idc: " << ((int)m_configuration.level_idc) << "\n";
  sstr << indent << "toolset_idc_h: " << m_configuration.toolset_idc_h << "\n";
  sstr << indent << "toolset_idc_l: " << m_configuration.toolset_idc_l << "\n";
  sstr << indent << "chroma_format_idc: " << ((int)m_configuration.chroma_format_idc)
       << " (" << get_chroma_format_as_text() << ")\n";
  sstr << indent << "bit_depth_luma: " << ((int)m_configuration.bit_depth_luma) << "\n";
  sstr << indent << "bit_depth_chroma: " << ((int)m_configuration.bit_depth_chroma) << "\n";
  sstr << indent << "pic_width_in_luma_samples: " << m_configuration.pic_width_in_luma_samples << "\n";
  sstr << indent << "pic_height_in_luma_samples: " << m_configuration.pic_height_in_luma_samples << "\n";
  sstr << indent << "length_size: " << ((int)m_configuration.lengthSize) << "\n";
  for (const auto &array : m_nal_array)
  {
    sstr << indent << "<array>\n";

    indent++;
    sstr << indent << "array_completeness: " << (array.array_completeness  ? "true" : "false") << "\n"
         << indent << "NAL_unit_type: " << ((int) array.NAL_unit_type) << " ("
         << get_NAL_unit_type_as_text(array.NAL_unit_type) << ")" << "\n";

    for (const auto& unit : array.nal_units) {
      sstr << indent;
      for (uint8_t b : unit) {
        sstr << std::setfill('0') << std::setw(2) << std::hex << ((int) b) << " ";
      }
      sstr << "\n";
      sstr << std::dec;
    }

    indent--;
  }
  return sstr.str();
}

std::string Box_evcC::get_profile_as_text() const
{
  switch (m_configuration.profile_idc)
  {
  case 0:
    return "Baseline";
  case 1:
    return "Main";
  case 2:
    return "Baseline Still";
  case 3:
    return "Main Still";
  default:
    return std::string("Unknown");
  }
}

std::string Box_evcC::get_chroma_format_as_text() const
{
  switch (m_configuration.chroma_format_idc)
  {
  case CHROMA_FORMAT_MONOCHROME:
    return std::string("Monochrome");
  case CHROMA_FORMAT_420:
    return std::string("4:2:0");
  case CHROMA_FORMAT_422:
    return std::string("4:2:2");
  case CHROMA_FORMAT_444:
    return std::string("4:4:4");
  default:
    return std::string("Invalid");
  }
}

std::string Box_evcC::get_NAL_unit_type_as_text(uint8_t nal_unit_type) const
{
  switch (nal_unit_type)
  {
  case 0:
    return "NONIDR_NUT";
  case 1:
    return "IDR_NUT";
  case 24:
    return "SPS_NUT";
  case 25:
    return "PPS_NUT";
  case 26:
    return "APS_NUT";
  case 27:
    return "FD_NUT";
  case 28:
    return "SEI_NUT";
  default:
    return std::string("Unknown");
  }
}

Error Box_evcC::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write8(m_configuration.configurationVersion);
  writer.write8(m_configuration.profile_idc);
  writer.write8(m_configuration.level_idc);
  writer.write32(m_configuration.toolset_idc_h);
  writer.write32(m_configuration.toolset_idc_l);
  uint8_t chroma_format_idc_bits = (uint8_t)((m_configuration.chroma_format_idc & 0b11) << 6);
  uint8_t bit_depth_luma_minus8_bits = (uint8_t)(((m_configuration.bit_depth_luma - 8) & 0b111) << 3);
  uint8_t bit_depth_chroma_minus8_bits = (uint8_t)((m_configuration.bit_depth_chroma - 8) & 0b111);
  writer.write8(chroma_format_idc_bits | bit_depth_luma_minus8_bits | bit_depth_chroma_minus8_bits);
  writer.write16(m_configuration.pic_width_in_luma_samples);
  writer.write16(m_configuration.pic_height_in_luma_samples);
  writer.write8(m_configuration.lengthSize - 1);
  writer.write8((uint8_t)m_nal_array.size());
  for (const NalArray& array : m_nal_array) {

    writer.write8((uint8_t) ((array.array_completeness? 0x80: 0x00) |
                             (array.NAL_unit_type & 0b00111111)));

    writer.write16((uint16_t)array.nal_units.size());

    for (const std::vector<uint8_t>& nal_unit : array.nal_units) {
      writer.write16((uint16_t) nal_unit.size());
      writer.write(nal_unit);
    }
  }
  prepend_header(writer, box_start);

  return Error::Ok;
}

void Box_evcC::get_header_nals(std::vector<uint8_t>& data) const
{
  for (const auto& array : m_nal_array) {
    for (const auto& nalu : array.nal_units) {
      data.push_back((nalu.size() >> 24) & 0xFF);
      data.push_back((nalu.size() >> 16) & 0xFF);
      data.push_back((nalu.size() >> 8) & 0xFF);
      data.push_back((nalu.size() >> 0) & 0xFF);
      data.insert(data.end(), nalu.begin(), nalu.end());
    }
  }
}