/*
 * HEIF codec.
 * Copyright (c) 2017 struktur AG, Joachim Bauch <bauch@struktur.de>
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

#include <assert.h>
#include <string.h>

#include "box.h"
#include "heif.h"
#include "heif_context.h"

static uint64_t memory_get_length(struct heif_context* ctx, void* userdata) {
  heif::HeifContext::InternalReader* reader =
      static_cast<heif::HeifContext::InternalReader*>(userdata);
  return reader->length();
}

static uint64_t memory_get_position(struct heif_context* ctx, void* userdata) {
  heif::HeifContext::InternalReader* reader =
      static_cast<heif::HeifContext::InternalReader*>(userdata);
  return reader->position();
}

static int memory_read(struct heif_context* ctx, void* data,
    size_t size,  void* userdata) {
  heif::HeifContext::InternalReader* reader =
      static_cast<heif::HeifContext::InternalReader*>(userdata);
  return reader->read(data, size);
}

static int memory_seek(struct heif_context* ctx, int64_t position,
    enum heif_reader_offset offset, void* userdata) {
  heif::HeifContext::InternalReader* reader =
      static_cast<heif::HeifContext::InternalReader*>(userdata);
  return reader->seek(position, offset);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::unique_ptr<heif::HeifContext::InternalReader> mem(heif::HeifContext::CreateReader(data, size));
  struct heif_reader reader = {0};
  reader.reader_api_version = 1;
  reader.get_length = memory_get_length;
  reader.get_position = memory_get_position;
  reader.read = memory_read;
  reader.seek = memory_seek;
  heif::HeifReader r(nullptr, &reader, mem.get());
  heif::BitstreamRange range(&r);
  for (;;) {
    std::shared_ptr<heif::Box> box;
    heif::Error error = heif::Box::read(range, &box);
    if (error != heif::Error::Ok || range.error()) {
      break;
    }
  }
  return 0;
}
