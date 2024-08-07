/*
 * HEIF codec.
 * Copyright (c) 2022 Dirk Farin <dirk.farin@gmail.com>
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

#include <cassert>
#include "exif.h"

#define EXIF_TYPE_SHORT 3
#define EXIF_TYPE_LONG 4
#define DEFAULT_EXIF_ORIENTATION 1
#define EXIF_TAG_ORIENTATION 0x112
#define EXIF_TAG_IMAGE_WIDTH ((uint16_t)0x0100)
#define EXIF_TAG_IMAGE_HEIGHT ((uint16_t)0x0101)
#define EXIF_TAG_VALID_IMAGE_WIDTH ((uint16_t)0xA002)
#define EXIF_TAG_VALID_IMAGE_HEIGHT ((uint16_t)0xA003)
#define EXIF_TAG_EXIF_IFD_POINTER ((uint16_t)0x8769)

// Note: As far as I can see, it is not defined in the EXIF standard whether the offsets and counts of the IFD is signed or unsigned.
// We assume that these are all unsigned.

static uint32_t read32(const uint8_t* data, uint32_t size, uint32_t pos, bool littleEndian)
{
  assert(pos <= size - 4);

  const uint8_t* p = data + pos;

  if (littleEndian) {
    return (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
  }
  else {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
  }
}


static uint16_t read16(const uint8_t* data, uint32_t size, uint32_t pos, bool littleEndian)
{
  assert(pos <= size - 2);

  const uint8_t* p = data + pos;

  if (littleEndian) {
    return static_cast<uint16_t>((p[1] << 8) | p[0]);
  }
  else {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
  }
}


static void write16(uint8_t* data, uint32_t size, uint32_t pos, uint16_t value, bool littleEndian)
{
  assert(pos <= size - 2);

  uint8_t* p = data + pos;

  if (littleEndian) {
    p[0] = (uint8_t) (value & 0xFF);
    p[1] = (uint8_t) (value >> 8);
  }
  else {
    p[0] = (uint8_t) (value >> 8);
    p[1] = (uint8_t) (value & 0xFF);
  }
}


static void write32(uint8_t* data, uint32_t size, uint32_t pos, uint32_t value, bool littleEndian)
{
  assert(pos <= size - 4);

  uint8_t* p = data + pos;

  if (littleEndian) {
    p[0] = (uint8_t) (value & 0xFF);
    p[1] = (uint8_t) ((value >> 8) & 0xFF);
    p[2] = (uint8_t) ((value >> 16) & 0xFF);
    p[3] = (uint8_t) ((value >> 24) & 0xFF);
  } else {
    p[0] = (uint8_t) ((value >> 24) & 0xFF);
    p[1] = (uint8_t) ((value >> 16) & 0xFF);
    p[2] = (uint8_t) ((value >> 8) & 0xFF);
    p[3] = (uint8_t) (value & 0xFF);
  }
}


// Returns 0 if the query_tag was not found.
static uint32_t find_exif_tag_in_ifd(const uint8_t* exif, uint32_t size,
                                     uint32_t ifd_offset,
                                     uint16_t query_tag,
                                     bool littleEndian,
                                     int recursion_depth)
{
  const int MAX_IFD_TABLE_RECURSION_DEPTH = 5;

  if (recursion_depth > MAX_IFD_TABLE_RECURSION_DEPTH) {
    return 0;
  }

  uint32_t offset = ifd_offset;

  // is offset valid (i.e. can we read at least the 'size' field and the pointer to the next IFD ?)
  if (offset == 0) {
    return 0;
  }

  if (size < 6 || size - 2 - 4 < offset) {
    return 0;
  }

  uint16_t cnt = read16(exif, size, offset, littleEndian);

  // Does the IFD table fit into our memory range? We need this check to prevent an underflow in the following statement.
  uint32_t IFD_table_size = 2U + cnt * 12U + 4U;
  if (IFD_table_size > size) {
    return 0;
  }

  // end of IFD table would exceed the end of the EXIF data
  // offset + IFD_table_size > size ?
  if (size - IFD_table_size < offset) {
    return 0;
  }

  for (int i = 0; i < cnt; i++) {
    int tag = read16(exif, size, offset + 2 + i * 12, littleEndian);
    if (tag == query_tag) {
      return offset + 2 + i * 12;
    }

    if (tag == EXIF_TAG_EXIF_IFD_POINTER) {
      uint32_t exifIFD_offset = read32(exif, size, offset + 2 + i * 12 + 8, littleEndian);
      uint32_t tag_position = find_exif_tag_in_ifd(exif, size, exifIFD_offset, query_tag, littleEndian,
                                                   recursion_depth + 1);
      if (tag_position) {
        return tag_position;
      }
    }
  }

  // continue with next IFD table

  uint32_t pos = offset + 2 + cnt * 12;
  uint32_t next_ifd_offset = read32(exif, size, pos, littleEndian);

  return find_exif_tag_in_ifd(exif, size, next_ifd_offset, query_tag, littleEndian, recursion_depth + 1);
}


// Returns 0 if the query_tag was not found.
static uint32_t find_exif_tag(const uint8_t* exif, uint32_t size, uint16_t query_tag, bool* out_littleEndian)
{
  // read TIFF header

  if (size < 4) {
    return 0;
  }

  if ((exif[0] != 'I' && exif[0] != 'M') ||
      (exif[1] != 'I' && exif[1] != 'M')) {
    return 0;
  }

  bool littleEndian = (exif[0] == 'I');

  assert(out_littleEndian);
  *out_littleEndian = littleEndian;


  // read main IFD table

  uint32_t offset;
  offset = read32(exif, size, 4, littleEndian);

  uint32_t tag_position = find_exif_tag_in_ifd(exif, size, offset, query_tag, littleEndian, 1);
  return tag_position;
}


void overwrite_exif_image_size_if_it_exists(uint8_t* exif, uint32_t size, uint32_t width, uint32_t height)
{
  bool little_endian;
  uint32_t pos;

  for (uint16_t tag: {EXIF_TAG_IMAGE_WIDTH, EXIF_TAG_VALID_IMAGE_WIDTH}) {
    pos = find_exif_tag(exif, size, tag, &little_endian);
    if (pos != 0) {
      write16(exif, size, pos + 2, EXIF_TYPE_LONG, little_endian);
      write32(exif, size, pos + 4, 1, little_endian);
      write32(exif, size, pos + 8, width, little_endian);
    }
  }

  for (uint16_t tag: {EXIF_TAG_IMAGE_HEIGHT, EXIF_TAG_VALID_IMAGE_HEIGHT}) {
    pos = find_exif_tag(exif, size, tag, &little_endian);
    if (pos != 0) {
      write16(exif, size, pos + 2, EXIF_TYPE_LONG, little_endian);
      write32(exif, size, pos + 4, 1, little_endian);
      write32(exif, size, pos + 8, height, little_endian);
    }
  }
}


void modify_exif_tag_if_it_exists(uint8_t* exif, uint32_t size, uint16_t modify_tag, uint16_t modify_value)
{
  bool little_endian;
  uint32_t pos = find_exif_tag(exif, size, modify_tag, &little_endian);
  if (pos == 0) {
    return;
  }

  uint16_t type = read16(exif, size, pos + 2, little_endian);
  uint32_t count = read32(exif, size, pos + 4, little_endian);

  if (type == EXIF_TYPE_SHORT && count == 1) {
    write16(exif, size, pos + 8, modify_value, little_endian);
  }
}


void modify_exif_orientation_tag_if_it_exists(uint8_t* exifData, uint32_t size, uint16_t orientation)
{
  modify_exif_tag_if_it_exists(exifData, size, EXIF_TAG_ORIENTATION, orientation);
}


int read_exif_orientation_tag(const uint8_t* exif, uint32_t size)
{
  bool little_endian;
  uint32_t pos = find_exif_tag(exif, size, EXIF_TAG_ORIENTATION, &little_endian);
  if (pos == 0) {
    return DEFAULT_EXIF_ORIENTATION;
  }

  uint16_t type = read16(exif, size, pos + 2, little_endian);
  uint32_t count = read32(exif, size, pos + 4, little_endian);

  if (type == EXIF_TYPE_SHORT && count == 1) {
    return read16(exif, size, pos + 8, little_endian);
  }

  return DEFAULT_EXIF_ORIENTATION;
}
