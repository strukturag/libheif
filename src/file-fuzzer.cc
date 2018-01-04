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
#include <sstream>

#include "libde265/de265.h"

#include "box.h"
#include "heif_file.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  heif::HeifFile file;
  heif::Error error = file.read_from_memory(data, size);
  if (error != heif::Error::OK) {
    // Not a valid HEIF file passed (which is most likely while fuzzing).
    return 0;
  }

  file.get_primary_image_ID();
  int images_count = file.get_num_images();
  std::vector<uint32_t> ids = file.get_image_IDs();
  assert(ids.size() == images_count);
  if (ids.empty()) {
    // File doesn't contain any images.
    return 0;
  }

  std::string s(size ? reinterpret_cast<const char*>(data) : nullptr, size);
  for (int i = 0; i < images_count; ++i) {
    std::shared_ptr<heif::HeifPixelImage> img;
    error = file.decode_image(ids[i], img);
    if (error != heif::Error::OK) {
      // Ignore, we are only interested in crashes here.
    }
  }
  return 0;
}
