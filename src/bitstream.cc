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
#include "heif_context.h"

#include <string.h>
#include <assert.h>

#define MAX_UVLC_LEADING_ZEROS 20

using namespace heif;

BitstreamRange::BitstreamRange(HeifReader* reader, BitstreamRange* parent) {
  construct(reader, reader->length(), parent);
}

BitstreamRange::BitstreamRange(HeifReader* reader, uint64_t length, BitstreamRange* parent) {
  construct(reader, length, parent);
}

uint8_t BitstreamRange::read8()
{
  uint8_t buf;
  if (!read_data(&buf, 1)) {
    return 0;
  }

  return buf;
}


uint16_t BitstreamRange::read16()
{
  uint8_t buf[2];
  if (!read_data(&buf, 2)) {
    return 0;
  }

  return static_cast<uint16_t>((buf[0]<<8) | (buf[1]));
}


uint32_t BitstreamRange::read32()
{
  uint8_t buf[4];
  if (!read_data(&buf, 4)) {
    return 0;
  }

  return ((buf[0]<<24) |
          (buf[1]<<16) |
          (buf[2]<< 8) |
          (buf[3]));
}


std::string BitstreamRange::read_string()
{
  if (eof()) {
    return "";
  }

  std::string str;
  for (;;) {
    char ch;
    if (!read_data(&ch, 1)) {
      return std::string();
    }

    if (ch == 0) {
      break;
    } else {
      str += ch;
    }
  }

  return str;
}


bool BitstreamRange::read_data(void* data, uint64_t size) {
  if (!m_remaining) {
    m_error = true;
    return false;
  }

  if (m_remaining < size) {
    if (m_parent_range) {
      m_parent_range->skip(m_remaining);
    } else {
      m_reader->seek(m_remaining, heif_seek_current);
    }

    m_remaining = 0;
    m_end_reached = true;
    m_error = true;
    return false;
  }

  if (m_parent_range) {
    if (!m_parent_range->read_data(data, size)) {
      m_error = true;
      return false;
    }
    m_end_reached = m_parent_range->m_end_reached;
  } else {
    m_reader->read(data, size);
  }

  m_remaining -= size;
  m_end_reached = (m_remaining == 0);
  return true;
}


bool BitstreamRange::read_vector(std::vector<uint8_t>* data, uint64_t size) {
  if (!m_remaining) {
    m_error = true;
    return false;
  }

  if (m_remaining < size) {
    if (m_parent_range) {
      m_parent_range->skip(m_remaining);
    } else {
      m_reader->seek(m_remaining, heif_seek_current);
    }

    m_remaining = 0;
    m_end_reached = true;
    m_error = true;
    return false;
  }

  if (m_parent_range) {
    if (!m_parent_range->read_vector(data, size)) {
      m_error = true;
      return false;
    }
    m_end_reached = m_parent_range->m_end_reached;
  } else {
    data->resize(size);
    m_reader->read(data->data(), size);
  }

  m_remaining -= size;
  m_end_reached = (m_remaining == 0);
  return true;
}


void BitstreamRange::skip(uint64_t size) {
  if (m_parent_range) {
    m_parent_range->skip(size);
  } else {
    m_reader->seek(size, heif_seek_current);
  }
  if (m_remaining >= size) {
    m_remaining -= size;
    m_end_reached = (m_remaining == 0);
  } else {
    m_remaining = 0;
    m_end_reached = true;
  }
}


void BitstreamRange::skip_to_end_of_file() {
  if (m_parent_range) {
    m_parent_range->skip_to_end_of_file();
  } else {
    m_reader->seek(0, heif_seek_end);
  }
  m_remaining = 0;
  m_end_reached = true;
}


void BitstreamRange::skip_to_end_of_box() {
  if (m_remaining) {
    if (m_parent_range) {
      m_parent_range->skip(m_remaining);
    } else {
      m_reader->seek(m_remaining, heif_seek_current);
    }
    m_remaining = 0;
  }

  m_end_reached = true;
}



BitReader::BitReader(const uint8_t* buffer, int len)
{
  data = buffer;
  data_length = len;
  bytes_remaining = len;

  nextbits=0;
  nextbits_cnt=0;

  refill();
}

int BitReader::get_bits(int n)
{
  if (nextbits_cnt < n) {
    refill();
  }

  uint64_t val = nextbits;
  val >>= 64-n;

  nextbits <<= n;
  nextbits_cnt -= n;

  return (int)val;
}

int  BitReader::get_bits_fast(int n)
{
  assert(nextbits_cnt >= n);

  uint64_t val = nextbits;
  val >>= 64-n;

  nextbits <<= n;
  nextbits_cnt -= n;

  return (int)val;
}

int  BitReader::peek_bits(int n)
{
  if (nextbits_cnt < n) {
    refill();
  }

  uint64_t val = nextbits;
  val >>= 64-n;

  return (int)val;
}

void BitReader::skip_bits(int n)
{
  if (nextbits_cnt < n) {
    refill();
  }

  nextbits <<= n;
  nextbits_cnt -= n;
}

void BitReader::skip_bits_fast(int n)
{
  nextbits <<= n;
  nextbits_cnt -= n;
}

void BitReader::skip_to_byte_boundary()
{
  int nskip = (nextbits_cnt & 7);

  nextbits <<= nskip;
  nextbits_cnt -= nskip;
}

bool BitReader::get_uvlc(int* value)
{
  int num_zeros=0;

  while (get_bits(1)==0) {
    num_zeros++;

    if (num_zeros > MAX_UVLC_LEADING_ZEROS) { return false; }
  }

  int offset = 0;
  if (num_zeros != 0) {
    offset = (int)get_bits(num_zeros);
    *value = offset + (1<<num_zeros)-1;
    assert(*value>0);
    return true;
  } else {
    *value = 0;
    return true;
  }
}

bool BitReader::get_svlc(int* value)
{
  int v;
  if (!get_uvlc(&v)) {
    return false;
  } else if (v == 0) {
    *value = v;
    return true;
  }

  bool negative = ((v&1)==0);
  *value = negative ? -v/2 : (v+1)/2;
  return true;
}

void BitReader::refill()
{
#if 0
  // TODO: activate me one I'm sure this works
  while (nextbits_cnt <= 64-8 && bytes_remaining) {
    uint64_t newval = *data++;
    bytes_remaining--;

    nextbits_cnt += 8;
    newval <<= 64-nextbits_cnt;
    nextbits |= newval;
  }
#else
  int shift = 64 - nextbits_cnt;

  while (shift >= 8 && bytes_remaining) {
    uint64_t newval = *data++;
    bytes_remaining--;

    shift -= 8;
    newval <<= shift;
    nextbits |= newval;
  }

  nextbits_cnt = 64-shift;
#endif
}


void StreamWriter::write8(uint8_t v)
{
  if (m_position == m_data.size()) {
    m_data.push_back(v);
    m_position++;
  }
  else {
    m_data[m_position++] = v;
  }
}


void StreamWriter::write16(uint16_t v)
{
  size_t required_size = m_position+2;

  if (required_size > m_data.size()) {
    m_data.resize(required_size);
  }

  m_data[m_position++] = uint8_t((v>>8) & 0xFF);
  m_data[m_position++] = uint8_t(v & 0xFF);
}


void StreamWriter::write32(uint32_t v)
{
  size_t required_size = m_position+4;

  if (required_size > m_data.size()) {
    m_data.resize(required_size);
  }

  m_data[m_position++] = uint8_t((v>>24) & 0xFF);
  m_data[m_position++] = uint8_t((v>>16) & 0xFF);
  m_data[m_position++] = uint8_t((v>>8) & 0xFF);
  m_data[m_position++] = uint8_t(v & 0xFF);
}


void StreamWriter::write64(uint64_t v)
{
  size_t required_size = m_position+8;

  if (required_size > m_data.size()) {
    m_data.resize(required_size);
  }

  m_data[m_position++] = uint8_t((v>>56) & 0xFF);
  m_data[m_position++] = uint8_t((v>>48) & 0xFF);
  m_data[m_position++] = uint8_t((v>>40) & 0xFF);
  m_data[m_position++] = uint8_t((v>>32) & 0xFF);
  m_data[m_position++] = uint8_t((v>>24) & 0xFF);
  m_data[m_position++] = uint8_t((v>>16) & 0xFF);
  m_data[m_position++] = uint8_t((v>>8) & 0xFF);
  m_data[m_position++] = uint8_t(v & 0xFF);
}


void StreamWriter::write(int size, uint64_t value)
{
  if (size==1) {
    assert(value <= 0xFF);
    write8((uint8_t)value);
  }
  else if (size==2) {
    assert(value <= 0xFFFF);
    write16((uint16_t)value);
  }
  else if (size==4) {
    assert(value <= 0xFFFFFFFF);
    write32((uint32_t)value);
  }
  else if (size==8) {
    write64((uint64_t)value);
  }
  else {
    assert(false); // unimplemented size
  }
}


void StreamWriter::write(const std::string& str)
{
  size_t required_size = m_position + str.size() +1;

  if (required_size > m_data.size()) {
    m_data.resize(required_size);
  }

  for (size_t i=0;i<str.size();i++) {
    m_data[m_position++] = str[i];
  }

  m_data[m_position++] = 0;
}


void StreamWriter::write(const std::vector<uint8_t>& vec)
{
  size_t required_size = m_position + vec.size();

  if (required_size > m_data.size()) {
    m_data.resize(required_size);
  }

  memcpy(m_data.data() + m_position, vec.data(), vec.size());
  m_position += vec.size();
}


void StreamWriter::write(const StreamWriter& writer)
{
  size_t required_size = m_position + writer.get_data().size();

  if (required_size > m_data.size()) {
    m_data.resize(required_size);
  }

  const auto& data = writer.get_data();

  memcpy(m_data.data() + m_position, data.data(), data.size());

  m_position += data.size();
}


void StreamWriter::skip(int n)
{
  assert(m_position == m_data.size());
  m_data.resize( m_data.size() + n );
  m_position += n;
}


void StreamWriter::insert(int nBytes)
{
  assert(nBytes>=0);

  if (nBytes==0) {
    return;
  }

  m_data.resize( m_data.size() + nBytes );

  if (m_position < m_data.size() - nBytes) {
    memmove(m_data.data() + m_position + nBytes,
            m_data.data() + m_position,
            m_data.size() - nBytes - m_position);
  }
}
