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


Error Box_container::parse(BitstreamRange& range, const heif_security_limits* limits)
{
  return read_children(range, READ_CHILDREN_ALL, limits);
}


std::string Box_container::dump(Indent& indent) const
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
    return unsupported_version_error("mvhd");
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


double Box_tkhd::get_matrix_element(int idx) const
{
  if (idx == 8) {
    return 1.0;
  }

  return m_matrix[idx] / double(0x10000);
}


Error Box_tkhd::parse(BitstreamRange& range, const heif_security_limits* limits)
{
  parse_full_box_header(range);

  if (get_version() > 1) {
    return unsupported_version_error("tkhd");
  }

  if (get_version() == 1) {
    m_creation_time = range.read64();
    m_modification_time = range.read64();
    m_track_id = range.read32();
    range.skip(4);
    m_duration = range.read64();
  }
  else {
    // version==0
    m_creation_time = range.read32();
    m_modification_time = range.read32();
    m_track_id = range.read32();
    range.skip(4);
    m_duration = range.read32();
  }

  range.skip(8);
  m_layer = range.read16();
  m_alternate_group = range.read16();
  m_volume = range.read16();
  range.skip(2);
  for (int i = 0; i < 9; i++) {
    m_matrix[i] = range.read32();
  }

  m_width = range.read32();
  m_height = range.read32();

  return range.get_error();
}


std::string Box_tkhd::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << indent << "creation time:     " << m_creation_time << "\n"
      << indent << "modification time: " << m_modification_time << "\n"
      << indent << "track ID: " << m_track_id << "\n"
      << indent << "duration: " << m_duration << "\n";
  sstr << indent << "layer: " << m_layer << "\n"
      << indent << "alternate_group: " << m_alternate_group << "\n"
      << indent << "volume: " << get_volume() << "\n"
      << indent << "matrix:\n";
  for (int i = 0; i < 9; i++) {
    sstr << indent << "  [" << i << "] = " << get_matrix_element(i) << "\n";
  }

  sstr << indent << "width: " << m_width << "\n"
      << indent << "height: " << m_height << "\n";

  return sstr.str();
}


void Box_tkhd::derive_box_version()
{
  if (m_creation_time > 0xFFFFFFFF ||
      m_modification_time > 0xFFFFFFFF ||
      m_duration > 0xFFFFFFFF) {
    set_version(1);
  }
  else {
    set_version(0);
  }
}


Error Box_tkhd::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  if (get_version() == 1) {
    writer.write64(m_creation_time);
    writer.write64(m_modification_time);
    writer.write32(m_track_id);
    writer.write32(0);
    writer.write64(m_duration);
  }
  else {
    // version==0
    writer.write32(static_cast<uint32_t>(m_creation_time));
    writer.write32(static_cast<uint32_t>(m_modification_time));
    writer.write32(m_track_id);
    writer.write32(0);
    writer.write32(static_cast<uint32_t>(m_duration));
  }

  writer.write64(0);
  writer.write16(m_layer);
  writer.write16(m_alternate_group);
  writer.write16(m_volume);
  writer.write16(0);
  for (int i = 0; i < 9; i++) {
    writer.write32(m_matrix[i]);
  }

  writer.write32(m_width);
  writer.write32(m_height);

  prepend_header(writer, box_start);

  return Error::Ok;
}


Error Box_mdhd::parse(BitstreamRange& range, const heif_security_limits* limits)
{
  parse_full_box_header(range);

  if (get_version() > 1) {
    return unsupported_version_error("mdhd");
  }

  if (get_version() == 1) {
    m_creation_time = range.read64();
    m_modification_time = range.read64();
    m_timescale = range.read32();
    m_duration = range.read64();
  }
  else {
    // version==0
    m_creation_time = range.read32();
    m_modification_time = range.read32();
    m_timescale = range.read32();
    m_duration = range.read32();
  }

  uint16_t language_packed = range.read16();
  m_language[0] = ((language_packed >> 10) & 0x1F) + 0x60;
  m_language[1] = ((language_packed >> 5) & 0x1F) + 0x60;
  m_language[2] = ((language_packed >> 0) & 0x1F) + 0x60;
  m_language[3] = 0;

  range.skip(2);

  return range.get_error();
}


std::string Box_mdhd::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << indent << "creation time:     " << m_creation_time << "\n"
      << indent << "modification time: " << m_modification_time << "\n"
      << indent << "timescale: " << m_timescale << "\n"
      << indent << "duration: " << m_duration << "\n";
  sstr << indent << "language: " << m_language << "\n";

  return sstr.str();
}


void Box_mdhd::derive_box_version()
{
  if (m_creation_time > 0xFFFFFFFF ||
      m_modification_time > 0xFFFFFFFF ||
      m_duration > 0xFFFFFFFF) {
    set_version(1);
  }
  else {
    set_version(0);
  }
}


Error Box_mdhd::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  if (get_version() == 1) {
    writer.write64(m_creation_time);
    writer.write64(m_modification_time);
    writer.write32(m_timescale);
    writer.write64(m_duration);
  }
  else {
    // version==0
    writer.write32(static_cast<uint32_t>(m_creation_time));
    writer.write32(static_cast<uint32_t>(m_modification_time));
    writer.write32(m_timescale);
    writer.write32(static_cast<uint32_t>(m_duration));
  }

  uint16_t language_packed = ((((m_language[0] - 0x60) & 0x1F) << 10) |
                              (((m_language[1] - 0x60) & 0x1F) << 5) |
                              (((m_language[2] - 0x60) & 0x1F) << 0));
  writer.write16(language_packed);
  writer.write16(0);

  prepend_header(writer, box_start);

  return Error::Ok;
}



Error Box_vmhd::parse(BitstreamRange& range, const heif_security_limits* limits)
{
  parse_full_box_header(range);

  if (get_version() > 0) {
    return unsupported_version_error("vmhd");
  }

  m_graphics_mode = range.read16();
  for (int i = 0; i < 3; i++) {
    m_op_color[i] = range.read16();
  }

  return range.get_error();
}


std::string Box_vmhd::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << indent << "graphics mode: " << m_graphics_mode;
  if (m_graphics_mode == 0) {
    sstr << " (copy)";
  }
  sstr << "\n"
       << indent << "op color: " << m_op_color[0] << "; " << m_op_color[1] << "; " << m_op_color[2] << "\n";

  return sstr.str();
}


Error Box_vmhd::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write16(m_graphics_mode);
  for (int i = 0; i < 3; i++) {
    writer.write16(m_op_color[i]);
  }

  prepend_header(writer, box_start);

  return Error::Ok;
}
