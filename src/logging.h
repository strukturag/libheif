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

#ifndef LIBHEIF_LOGGING_H
#define LIBHEIF_LOGGING_H

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


namespace heif {

  class Indent {
  public:
  Indent() : m_indent(0) { }

    int get_indent() const { return m_indent; }

    void operator++(int) { m_indent++; }
    void operator--(int) { m_indent--; if (m_indent<0) m_indent=0; }

  private:
    int m_indent;
  };


  inline std::ostream& operator<<(std::ostream& ostr, const Indent& indent) {
    for (int i=0;i<indent.get_indent();i++) {
      ostr << "| ";
    }

    return ostr;
  }
}

#endif
