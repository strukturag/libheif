/*
 * HEIF AVC codec.
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

#include "avc.h"
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

Error Box_avcC::parse(BitstreamRange &range) {
  m_configuration.configuration_version = range.read8();
  m_configuration.AVCProfileIndication = range.read8();
  m_configuration.profile_compatibility = range.read8();
  m_configuration.AVCLevelIndication = range.read8();
  uint8_t lengthSizeMinusOneWithReserved = range.read8();
  m_configuration.lengthSize =
      (lengthSizeMinusOneWithReserved & 0b00000011) + 1;

  uint8_t numOfSequenceParameterSets = (range.read8() & 0b00011111);
  for (int i = 0; i < numOfSequenceParameterSets; i++) {
    uint16_t sequenceParameterSetLength = range.read16();
    std::vector<uint8_t> sps(sequenceParameterSetLength);
    range.read(sps.data(), sps.size());
    m_sps.push_back(sps);
  }

  uint8_t numOfPictureParameterSets = range.read8();
  for (int i = 0; i < numOfPictureParameterSets; i++) {
    uint16_t pictureParameterSetLength = range.read16();
    std::vector<uint8_t> pps(pictureParameterSetLength);
    range.read(pps.data(), pps.size());
    m_pps.push_back(pps);
  }

  if ((m_configuration.AVCProfileIndication != 66) &&
      (m_configuration.AVCProfileIndication != 77) &&
      (m_configuration.AVCProfileIndication != 88)) {
    // TODO: we don't support this yet
  }

  return range.get_error();
}

Error Box_avcC::write(StreamWriter &writer) const {
  size_t box_start = reserve_box_header_space(writer);

  writer.write8(m_configuration.configuration_version);
  writer.write8(m_configuration.AVCProfileIndication);
  writer.write8(m_configuration.profile_compatibility);
  writer.write8(m_configuration.AVCLevelIndication);
  uint8_t lengthSizeMinusOneWithReserved = 0b11111100 | ((m_configuration.lengthSize - 1) & 0b11);
  writer.write8(lengthSizeMinusOneWithReserved);
  uint8_t numSpsWithReserved = 0b11100000 | (m_sps.size() & 0b00011111);
  writer.write8(numSpsWithReserved);
  for (const auto &sps: m_sps) {
    writer.write16((uint16_t) sps.size());
    writer.write(sps);
  }
  writer.write8(m_pps.size() & 0xFF);
  for (const auto &pps: m_pps) {
    writer.write16((uint16_t) pps.size());
    writer.write(pps);
  }
  prepend_header(writer, box_start);

  return Error::Ok;
}

std::string Box_avcC::dump(Indent &indent) const {
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << indent << "configuration_version: "
       << ((int)m_configuration.configuration_version) << "\n"
       << indent << "AVCProfileIndication: "
       << ((int)m_configuration.AVCProfileIndication) << " ("
       << profileIndicationAsText() << ")"
       << "\n"
       << indent << "profile_compatibility: "
       << ((int)m_configuration.profile_compatibility) << "\n"
       << indent
       << "AVCLevelIndication: " << ((int)m_configuration.AVCLevelIndication)
       << "\n";

  for (const auto &sps : m_sps) {
    sstr << indent << "SPS: ";
    for (uint8_t b : sps) {
      sstr << std::setfill('0') << std::setw(2) << std::hex << ((int)b) << " ";
    }
    sstr << "\n";
    sstr << std::dec;
  }

  for (const auto &pps : m_pps) {
    sstr << indent << "PPS: ";
    for (uint8_t b : pps) {
      sstr << std::setfill('0') << std::setw(2) << std::hex << ((int)b) << " ";
    }
    sstr << "\n";
    sstr << std::dec;
  }

  return sstr.str();
}

std::string Box_avcC::profileIndicationAsText() const {
  // See ISO/IEC 14496-10:2022 Annex A
  switch (m_configuration.AVCProfileIndication) {
  case 44:
    return "CALVC 4:4:4";
  case 66:
    return "Constrained Baseline";
  case 77:
    return "Main";
  case 88:
    return "Extended";
  case 100:
    return "High variant";
  case 110:
    return "High 10";
  case 122:
    return "High 4:2:2";
  case 244:
    return "High 4:4:4";
  default:
    return "Unknown";
  }
}
