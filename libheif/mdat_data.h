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

#ifndef LIBHEIF_MDAT_DATA_H
#define LIBHEIF_MDAT_DATA_H

#include <cstdint>
#include <vector>
#include <cassert>
#include <algorithm>


class MdatData
{
public:
  virtual ~MdatData() = default;

  // returns the start position of the appended data
  virtual size_t append_data(const std::vector<uint8_t>& data) = 0;

  virtual size_t get_data_size() const = 0;

  virtual void seek(size_t pos) = 0;

  virtual std::vector<uint8_t> get_data(size_t len) = 0;

  // Number of bytes that have not been extracted with get_data().
  virtual size_t get_remaining_data_size() const = 0;
};


class MdatData_Memory : public MdatData
{
public:
  size_t append_data(const std::vector<uint8_t>& data) {
    size_t startPos = m_data.size();
    m_data.insert(m_data.end(), data.begin(), data.end());
    return startPos;
  }

  size_t get_data_size() const { return m_data.size(); }

  void seek(size_t pos) { assert(pos <= m_data.size()); m_read_pos = pos; }

  std::vector<uint8_t> get_data(size_t len)  {
    if (len==0) {
      return m_data;
    }

    std::vector<uint8_t> out;
    size_t maxCopy = m_data.size() - m_read_pos;
    size_t nCopy = std::min(len, maxCopy);

    out.insert(out.begin(), m_data.data() + m_read_pos, m_data.data() + m_read_pos + nCopy);
    m_read_pos += nCopy;

    return out;
  }

  size_t get_remaining_data_size() const override {
    return m_data.size() - m_read_pos;
  }

private:
  std::vector<uint8_t> m_data;
  size_t m_read_pos = 0;
};

#endif //LIBHEIF_MDAT_DATA_H
