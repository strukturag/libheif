/*
 * HEIF codec.
 * Copyright (c) 2024 Dirk Farin <dirk.farin@gmail.com>
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

#include "sequences/seq_boxes.h"


Error Box_moov::parse(BitstreamRange& range, const heif_security_limits* limits)
{
  return read_children(range, READ_CHILDREN_ALL, limits);
}


std::string Box_moov::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << dump_children(indent);

  return sstr.str();
}


double Box_mvhd::get_matrix_element(int idx) const
{
  if (idx == 8) {
    return 1.0;
  }

  return m_matrix[idx] / double(0x10000);
}


Error Box_mvhd::parse(BitstreamRange& range, const heif_security_limits* limits)
{
  parse_full_box_header(range);

  if (get_version() > 1) {
    return unsupported_version_error("hdlr");
  }

  if (get_version() == 1) {
    m_creation_time = range.read64();
    m_modification_time = range.read64();
    m_timescale = range.read64();
    m_duration = range.read64();
  }
  else {
    // version==0
    m_creation_time = range.read32();
    m_modification_time = range.read32();
    m_timescale = range.read32();
    m_duration = range.read32();
  }

  m_rate = range.read32();
  m_volume = range.read16();
  range.skip(2);
  range.skip(8);
  for (int i = 0; i < 9; i++) {
    m_matrix[i] = range.read32();
  }
  for (int i = 0; i < 6; i++) {
    range.skip(4);
  }

  m_next_track_ID = range.read32();

  return range.get_error();
}


std::string Box_mvhd::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << indent << "creation time:     " << m_creation_time << "\n"
      << indent << "modification time: " << m_modification_time << "\n"
      << indent << "timescale: " << m_timescale << "\n"
      << indent << "duration: " << m_duration << "\n";
  sstr << indent << "rate: " << get_rate() << "\n"
      << indent << "volume: " << get_volume() << "\n"
      << indent << "matrix:\n";
  for (int i = 0; i < 9; i++) {
    sstr << indent << "  [" << i << "] = " << get_matrix_element(i) << "\n";
  }
  sstr << indent << "next_track_ID: " << m_next_track_ID << "\n";

  return sstr.str();
}


void Box_mvhd::derive_box_version()
{
  if (m_creation_time > 0xFFFFFFFF ||
      m_modification_time > 0xFFFFFFFF ||
      m_timescale > 0xFFFFFFFF ||
      m_duration > 0xFFFFFFFF) {
    set_version(1);
  }
  else {
    set_version(0);
  }
}


Error Box_mvhd::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  if (get_version() == 1) {
    writer.write64(m_creation_time);
    writer.write64(m_modification_time);
    writer.write64(m_timescale);
    writer.write64(m_duration);
  }
  else {
    // version==0
    writer.write32(static_cast<uint32_t>(m_creation_time));
    writer.write32(static_cast<uint32_t>(m_modification_time));
    writer.write32(static_cast<uint32_t>(m_timescale));
    writer.write32(static_cast<uint32_t>(m_duration));
  }

  writer.write32(m_rate);
  writer.write16(m_volume);
  writer.write16(0);
  writer.write64(0);
  for (int i = 0; i < 9; i++) {
    writer.write32(m_matrix[i]);
  }
  for (int i = 0; i < 6; i++) {
    writer.write32(0);
  }

  writer.write32(m_next_track_ID);

  prepend_header(writer, box_start);

  return Error::Ok;
}


Error Box_trak::parse(BitstreamRange& range, const heif_security_limits* limits)
{
  return read_children(range, READ_CHILDREN_ALL, limits);
}


std::string Box_trak::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << dump_children(indent);

  return sstr.str();
}
