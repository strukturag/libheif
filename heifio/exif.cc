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
#include <cstdio>
#include <vector>
#include "exif.h"

#include "common_utils.h"

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
    return four_bytes_to_uint32(p[3], p[2], p[1], p[0]);
  }
  else {
    return four_bytes_to_uint32(p[0], p[1], p[2], p[3]);
  }
}


static uint16_t read16(const uint8_t* data, uint32_t size, uint32_t pos, bool littleEndian)
{
  assert(pos <= size - 2);

  const uint8_t* p = data + pos;

  if (littleEndian) {
    return two_bytes_to_uint16(p[1], p[0]);
  }
  else {
    return two_bytes_to_uint16(p[0], p[1]);
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
                                     uint32_t remaining_bytes)
{
  struct IFDFrame {
    uint32_t offset;
    uint16_t count = 0;
    uint16_t next_entry = 0;
    bool initialized = false;
  };

  std::vector<IFDFrame> stack;
  stack.push_back({ifd_offset});

  while (!stack.empty()) {
    IFDFrame& frame = stack.back();
    uint32_t offset = frame.offset;

    if (!frame.initialized) {
      if (offset == 0 || size < 6 || offset > size - 6) {
        stack.pop_back();
        continue;
      }

      uint16_t count = read16(exif, size, offset, littleEndian);
      uint32_t table_size = 2U + count * 12U + 4U;
      if (table_size > size - offset) {
        stack.pop_back();
        continue;
      }

      // Charge the whole IFD, including the next-IFD pointer. Repeated visits
      // consume the budget even when an IFD has no entries.
      if (table_size > remaining_bytes) {
        stack.pop_back();
        continue;
      }
      remaining_bytes -= table_size;

      frame.count = count;
      frame.initialized = true;
    }

    if (frame.next_entry < frame.count) {
      uint32_t entry_offset = offset + 2U + frame.next_entry * 12U;
      frame.next_entry++;
      uint16_t tag = read16(exif, size, entry_offset, littleEndian);
      if (tag == query_tag) {
        return entry_offset;
      }

      if (tag == EXIF_TAG_EXIF_IFD_POINTER) {
        uint32_t exif_ifd_offset = read32(exif, size, entry_offset + 8, littleEndian);
        if (exif_ifd_offset != 0) {
          stack.push_back({exif_ifd_offset});
        }
      }
    }
    else {
      uint32_t next_offset_pos = offset + 2U + frame.count * 12U;
      uint32_t next_ifd_offset = read32(exif, size, next_offset_pos, littleEndian);
      stack.pop_back();
      if (next_ifd_offset != 0) {
        stack.push_back({next_ifd_offset});
      }
    }
  }

  return 0;
}


// Returns 0 if the query_tag was not found.
static uint32_t find_exif_tag(const uint8_t* exif, uint32_t size, uint16_t query_tag, bool* out_littleEndian)
{
  // read TIFF header

  if (size < 8) {
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

  uint32_t tag_position = find_exif_tag_in_ifd(exif, size, offset, query_tag, littleEndian, size);
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


bool fix_icc_profile(const uint8_t* profile_data, size_t& profile_size)
{
  if (profile_size < 128) {
    return false;
  }

  // --- check that profile size specified in header matches the real size

  uint32_t size_in_header = four_bytes_to_uint32(profile_data[0],
                                                 profile_data[1],
                                                 profile_data[2],
                                                 profile_data[3]);

  if (size_in_header != profile_size) {

    // Size in header is smaller than actual size, but alignment indicates that it might
    // be correct. Replace real data length with size in header.
    if (size_in_header < profile_size && (size_in_header & 3) == 0) {
      fprintf(stderr, "Input ICC profile has wrong size in header (%u instead of %zu). Skipping extra bytes at the end. "
                      "Note that this may still be incorrect and the ICC profile may be broken.\n", size_in_header, profile_size);

      profile_size = size_in_header;
    }
    else {
      return false;
    }
  }

  return true;
}
