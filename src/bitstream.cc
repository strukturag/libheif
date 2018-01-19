/*
 * HEIF codec.
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
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

#include "bitstream.h"

using namespace heif;


uint8_t BitstreamRange::read8()
{
  if (!read(1)) {
    return 0;
  }

  uint8_t buf;

  std::istream* istr = get_istream();
  istr->read((char*)&buf,1);

  if (istr->fail()) {
    set_eof_reached();
    return 0;
  }

  return buf;
}


uint16_t BitstreamRange::read16()
{
  if (!read(2)) {
    return 0;
  }

  uint8_t buf[2];

  std::istream* istr = get_istream();
  istr->read((char*)buf,2);

  if (istr->fail()) {
    set_eof_reached();
    return 0;
  }

  return static_cast<uint16_t>((buf[0]<<8) | (buf[1]));
}


uint32_t BitstreamRange::read32()
{
  if (!read(4)) {
    return 0;
  }

  uint8_t buf[4];

  std::istream* istr = get_istream();
  istr->read((char*)buf,4);

  if (istr->fail()) {
    set_eof_reached();
    return 0;
  }

  return ((buf[0]<<24) |
          (buf[1]<<16) |
          (buf[2]<< 8) |
          (buf[3]));
}


std::string BitstreamRange::read_string()
{
  std::string str;

  if (eof()) {
    return "";
  }

  for (;;) {
    if (!read(1)) {
      return std::string();
    }

    std::istream* istr = get_istream();
    int c = istr->get();

    if (istr->fail()) {
      set_eof_reached();
      return std::string();
    }

    if (c==0) {
      break;
    }
    else {
      str += (char)c;
    }
  }

  return str;
}
