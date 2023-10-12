/*
 * HEIF VVC codec.
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

#include "vvc.h"
#include <string>

Error Box_vvcC::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  uint8_t byte;

  auto& c = m_configuration; // abbreviation

  c.configurationVersion = range.read8();
  c.avgFrameRate_times_256 = range.read16();

  //printf("version: %d\n", c.configurationVersion);

  byte = range.read8();
  c.constantFrameRate = (byte & 0xc0) >> 6;
  c.numTemporalLayers = (byte & 0x38) >> 3;
  c.lengthSize = uint8_t((byte & 0x06) + 1);
  c.ptl_present_flag = (byte & 0x01);
  // assert(c.ptl_present_flag == false); // TODO   (removed the assert since it will trigger the fuzzers)

  byte = range.read8();
  c.chroma_format_present_flag = (byte & 0x80);
  c.chroma_format_idc = (byte & 0x60) >> 5;

  c.bit_depth_present_flag = (byte & 0x10);
  c.bit_depth = uint8_t(((byte & 0x0e) >> 1) + 8);

  c.numOfArrays = range.read8();

#if 0
  const int64_t configOBUs_bytes = range.get_remaining_bytes();
  m_config_OBUs.resize(configOBUs_bytes);

  if (!range.read(m_config_OBUs.data(), configOBUs_bytes)) {
    // error
  }
#endif

  return range.get_error();
}


Error Box_vvcC::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

#if 0
  const auto& c = m_configuration; // abbreviation

  writer.write8(c.version | 0x80);

  writer.write8((uint8_t) (((c.seq_profile & 0x7) << 5) |
                           (c.seq_level_idx_0 & 0x1f)));
#endif

  prepend_header(writer, box_start);

  return Error::Ok;
}


static const char* vvc_chroma_names[4] = {"mono", "4:2:0", "4:2:2", "4:4:4"};

std::string Box_vvcC::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  const auto& c = m_configuration; // abbreviation

  sstr << indent << "version: " << ((int) c.configurationVersion) << "\n"
       << indent << "frame-rate: " << (c.avgFrameRate_times_256 / 256.0f) << "\n"
       << indent << "constant frame rate: " << (c.constantFrameRate == 1 ? "constant" : (c.constantFrameRate == 2 ? "multi-layer" : "unknown")) << "\n"
       << indent << "num temporal layers: " << ((int) c.numTemporalLayers) << "\n"
       << indent << "length size: " << ((int) c.lengthSize) << "\n"
       << indent << "chroma-format: ";
  if (c.chroma_format_present_flag) {
    sstr << vvc_chroma_names[c.chroma_format_idc] << "\n";
  }
  else {
    sstr << "---\n";
  }

  sstr << indent << "bit-depth: ";
  if (c.bit_depth_present_flag) {
    sstr << ((int) c.bit_depth) << "\n";
  }
  else {
    sstr << "---\n";
  }

  sstr << "num of arrays: " << ((int) c.numOfArrays) << "\n";

#if 0
  sstr << indent << "config OBUs:";
  for (size_t i = 0; i < m_config_OBUs.size(); i++) {
    sstr << " " << std::hex << std::setfill('0') << std::setw(2)
         << ((int) m_config_OBUs[i]);
  }
  sstr << std::dec << "\n";
#endif

  return sstr.str();
}

