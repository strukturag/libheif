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

#ifndef LIBHEIF_BITSTREAM_H
#define LIBHEIF_BITSTREAM_H

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#if defined(HAVE_INTTYPES_H)
#include <inttypes.h>
#endif
#if defined(HAVE_STDDEF_H)
#include <stddef.h>
#endif
#include <vector>
#include <string>
#include <memory>
#include <limits>
#include <istream>
#include <string>

#include "error.h"


namespace heif {

  class BitstreamRange
  {
  public:
    BitstreamRange(std::istream* istr, uint64_t length, BitstreamRange* parent = nullptr) {
      construct(istr, length, parent);
    }

    uint8_t read8();
    uint16_t read16();
    uint32_t read32();
    std::string read_string();

    bool read(int n) {
      if (n<0) {
        return false;
      }
      return read(static_cast<uint64_t>(n));
    }

    bool read(uint64_t n) {
      if (m_remaining >= n) {
        if (m_parent_range) {
          m_parent_range->read(n);
        }

        m_remaining -= n;
        m_end_reached = (m_remaining==0);

        return true;
      }
      else if (m_remaining==0) {
        m_error = true;
        return false;
      }
      else {
        if (m_parent_range) {
          m_parent_range->read(m_remaining);
        }

        m_istr->seekg(m_remaining, std::ios::cur);
        m_remaining = 0;
        m_end_reached = true;
        m_error = true;
        return false;
      }
    }

    void skip_to_end_of_file() {
      m_istr->seekg(0, std::ios_base::end);
      m_remaining = 0;
      m_end_reached = true;
    }

    void skip_to_end_of_box() {
      if (m_remaining) {
        if (m_parent_range) {
          m_parent_range->read(m_remaining);
        }

        m_istr->seekg(m_remaining, std::ios_base::cur);
        m_remaining = 0;
      }

      m_end_reached = true;
    }

    void set_eof_reached() {
      m_remaining = 0;
      m_end_reached = true;

      if (m_parent_range) {
        m_parent_range->set_eof_reached();
      }
    }

    bool eof() const {
      return m_end_reached;
    }

    bool error() const {
      return m_error;
    }

    Error get_error() const {
      if (m_error) {
        return Error(heif_error_Invalid_input,
                     heif_suberror_End_of_data);
      }
      else {
        return Error::Ok;
      }
    }

    std::istream* get_istream() { return m_istr; }

  protected:
    void construct(std::istream* istr, uint64_t length, BitstreamRange* parent) {
      m_remaining = length;
      m_end_reached = (length==0);

      m_istr = istr;
      m_parent_range = parent;
    }

  private:
    std::istream* m_istr = nullptr;
    BitstreamRange* m_parent_range = nullptr;

    uint64_t m_remaining;
    bool m_end_reached = false;
    bool m_error = false;
  };



  class BitReader
  {
  public:
    BitReader(const uint8_t* buffer, int len);

    int get_bits(int n);
    int get_bits_fast(int n);
    int peek_bits(int n);
    void skip_bits(int n);
    void skip_bits_fast(int n);
    void skip_to_byte_boundary();
    bool get_uvlc(int* value);
    bool get_svlc(int* value);

    int get_current_byte_index() const {
      return data_length - bytes_remaining - nextbits_cnt/8;
    }

  private:
    const uint8_t* data;
    int data_length;
    int bytes_remaining;

    uint64_t nextbits; // left-aligned bits
    int nextbits_cnt;

    void refill(); // refill to at least 56+1 bits
  };



  class StreamWriter
  {
  public:
    void write8(uint8_t);
    void write16(uint16_t);
    void write32(uint32_t);
    void write64(uint64_t);
    void write(int size, uint64_t value);
    void write(const std::string&);
    void write(const std::vector<uint8_t>&);
    void write(const StreamWriter&);

    void skip(int n);

    void insert(int nBytes);

    size_t data_size() const { return m_data.size(); }

    size_t get_position() const { return m_position; }
    void set_position(size_t pos) { m_position=pos; }
    void set_position_to_end() { m_position=m_data.size(); }

    const std::vector<uint8_t> get_data() const { return m_data; }

  private:
    std::vector<uint8_t> m_data;
    size_t m_position = 0;
  };
}

#endif
