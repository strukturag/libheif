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

#ifndef LIBHEIF_TEXT_H
#define LIBHEIF_TEXT_H

#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>
#include "error.h"
#include "libheif/heif_text.h"
#include "security_limits.h"

class HeifFile;

class TextItem
{
public:
  TextItem() = default;

  TextItem(heif_item_id itemId, const char *text) : m_item_id(itemId)
  {
    m_data.assign(text, text + std::strlen(text));
  }

  TextItem(heif_item_id itemId, std::shared_ptr<HeifFile> file)
      : m_item_id(itemId), m_source(std::move(file)) {}

  Error parse(std::vector<uint8_t>&& data, const heif_security_limits* limits);

  Result<std::vector<uint8_t>> encode() const;

  heif_item_id get_item_id() const {
    return m_item_id;
  }

  void set_item_id(heif_item_id id) {
    m_item_id = id;
  }

  Result<std::string_view> get_item_text(size_t max_output_size = SIZE_MAX);

private:
  heif_item_id m_item_id = 0;
  MemoryHandle m_memory_handle;
  std::vector<uint8_t> m_data;
  std::shared_ptr<HeifFile> m_source;
  std::mutex m_load_mutex;
};

#endif // LIBHEIF_TEXT_H
