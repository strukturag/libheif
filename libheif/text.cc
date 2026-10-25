/*
 * HEIF codec.
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
 * Copyright (c) 2025 Brad Hards <bradh@frogmouth.net>
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

#include "text.h"
#include "error.h"
#include "file.h"
#include <utility>


Result<std::vector<uint8_t>> TextItem::encode() const
{
  return m_data;
}

Error TextItem::parse(std::vector<uint8_t>&& data, const heif_security_limits* limits)
{
  // The decompressor's temporary MemoryHandle is released when it returns.
  // Keep the charge with the object that retains its output vector.
  MemoryHandle new_memory_handle;
  if (Error err = new_memory_handle.alloc(data.capacity(), limits, "decompressed text item")) {
    return err;
  }

  m_data = std::move(data);
  m_memory_handle = std::move(new_memory_handle);
  m_source.reset();
  return Error::Ok;
}


Result<std::string_view> TextItem::get_item_text(size_t max_output_size)
{
  std::lock_guard<std::mutex> lock(m_load_mutex);

  if (m_source) {
    auto data_result = m_source->get_uncompressed_item_data(m_item_id, max_output_size);
    if (!data_result) {
      return data_result.error();
    }

    if (Error err = parse(std::move(*data_result), m_source->get_security_limits())) {
      return err;
    }
  }

  if (m_data.size() > max_output_size) {
    return Error(heif_error_Memory_allocation_error, heif_suberror_Security_limit_exceeded,
                 "Text item exceeds the requested maximum output size");
  }

  if (m_data.empty()) {
    return std::string_view{};
  }
  return std::string_view{reinterpret_cast<const char*>(m_data.data()), m_data.size()};
}
