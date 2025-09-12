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

#include <sstream>

#include "box.h"
#include "bitstream.h"
#include "context.h"
#include "logging.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  auto reader = std::make_shared<StreamReader_memory>(data, size, false);

  // --- OSS-Fuzz assumes a bug if the allocated memory exceeds 2560 MB.
  //     Set a lower allocation limit to prevent this.

  // Use a context for tracking the memory usage and set the reduced limit.
  HeifContext ctx;
  ctx.get_security_limits()->max_total_memory = UINT64_C(2) * 1024 * 1024 * 1024;

  BitstreamRange range(reader, size);
  for (;;) {
    std::shared_ptr<Box> box;
    Error error = Box::read(range, &box, ctx.get_security_limits());
    if (error != Error::Ok || range.error()) {
      break;
    }

    box->get_type();
    box->get_type_string();
    Indent indent;
    box->dump(indent);
  }

  return 0;
}
