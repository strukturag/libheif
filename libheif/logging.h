/*
 * HEIF codec.
 * Copyright (c) 2017 Dirk Farin <dirk.farin@gmail.com>
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

#include <cinttypes>
#include <cstddef>

#include <vector>
#include <string>
#include <memory>
#include <limits>
#include <istream>
#include <ostream>


// dump() is a debugging aid. Boxes whose content scales with the file size (the
// sample tables, reference lists, ...) can otherwise produce gigabytes of text
// from a small input, which nested containers then copy at every level. Unless
// the caller passes full_log=true, such boxes print at most this many entries
// and then a short note about how many were omitted.
static const size_t MAX_DUMP_ENTRIES = 100;


class Indent
{
public:
  Indent() = default;

  int get_indent() const { return m_indent; }

  void operator++(int) { m_indent++; }

  void operator--(int)
  {
    m_indent--;
    if (m_indent < 0) m_indent = 0;
  }

  std::string get_string() const;

private:
  int m_indent = 0;
};


inline std::ostream& operator<<(std::ostream& ostr, const Indent& indent)
{
  ostr << indent.get_string();
  return ostr;
}


// dump() writes every box into one shared stream, so a sticky modifier such as
// std::hex or std::setfill left behind by one box would leak into the next.
// Reset the stream to its defaults (decimal, blank fill) between boxes so each
// one starts formatting from a known state.
inline void reset_stream_format(std::ostream& ostr)
{
  ostr << std::dec << std::noboolalpha;
  ostr.fill(' ');
}


// Helper for dump() loops over a container whose length scales with the file
// size. Call it at the top of the loop body with the current index and the
// total count. When full_log is false and MAX_DUMP_ENTRIES items have already
// been written, it writes a short note about the remaining items and returns
// true, telling the loop to stop.
inline bool dump_reached_entry_limit(std::ostream& ostr, const Indent& indent,
                                     bool full_log, size_t index, size_t total)
{
  if (full_log || index < MAX_DUMP_ENTRIES) {
    return false;
  }

  ostr << indent << "... (" << (total - index) << " more)\n";
  return true;
}

std::string write_raw_data_as_hex(const uint8_t* data, size_t len,
                                  const std::string& firstLineIndent,
                                  const std::string& remainingLinesIndent);

#endif
