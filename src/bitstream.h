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

#include "error.h"


namespace heif {

  class HeifReader;

  class BitstreamRange
  {
  public:
    explicit BitstreamRange(HeifReader* reader, BitstreamRange* parent = nullptr);
    BitstreamRange(HeifReader* reader, uint64_t length, BitstreamRange* parent = nullptr);

    uint8_t read8();
    uint16_t read16();
    uint32_t read32();
    std::string read_string();

    bool read_data(void* data, uint64_t size);
    bool read_vector(std::vector<uint8_t>* data, uint64_t size);

    void skip(uint64_t size);

    void skip_to_end_of_file();

    void skip_to_end_of_box();

    void set_eof_reached() {
      m_remaining = 0;
      m_end_reached = true;

      if (m_parent_range) {
        m_parent_range->set_eof_reached();
      }
    }

    bool eof() const {
      if (m_end_reached) {
        return true;
      } else if (m_parent_range) {
        return m_parent_range->eof();
      } else {
        return false;
      }
    }

    bool error() const {
      if (m_error) {
        return true;
      } else if (m_parent_range) {
        return m_parent_range->error();
      } else {
        return false;
      }
    }

    Error get_error() const {
      if (m_error) {
        return Error(heif_error_Invalid_input,
                     heif_suberror_End_of_data);
      } else if (m_parent_range) {
        return m_parent_range->get_error();
      } else {
        return Error::Ok;
      }
    }

    HeifReader* reader() const { return m_reader; }

    int get_nesting_level() const { return m_nesting_level; }

  protected:
    void construct(HeifReader* reader,
        uint64_t length, BitstreamRange* parent) {
      m_reader = reader;
      m_remaining = length;
      m_end_reached = (length == 0);
      m_parent_range = parent;

      if (parent) {
        m_nesting_level = parent->m_nesting_level + 1;
      }
    }

  private:
    HeifReader* m_reader = nullptr;
    BitstreamRange* m_parent_range = nullptr;
    int m_nesting_level = 0;

    uint64_t m_remaining = 0;
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
