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

#include "error.h"


namespace heif {

  class BitstreamRange
  {
  public:
    BitstreamRange(std::istream* istr, uint64_t length, BitstreamRange* parent = nullptr) {
      construct(istr, length, parent);
    }

    bool read(int n) {
      if (n<0) {
        return false;
      }
      if (m_remaining >= static_cast<uint64_t>(n)) {
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
}

#endif
