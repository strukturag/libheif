/*
 * libheif example application "convert".
 * Copyright (c) 2017 struktur AG, Joachim Bauch <bauch@struktur.de>
 *
 * This file is part of convert, an example application using libheif.
 *
 * convert is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * convert is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with convert.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef EXAMPLE_ENCODER_H
#define EXAMPLE_ENCODER_H

#include <string>
#include <memory>

#include "heif.h"

class Encoder {
 public:
  virtual ~Encoder() {}

  virtual heif_colorspace colorspace(bool has_alpha) const = 0;
  virtual heif_chroma chroma(bool has_alpha) const = 0;

  virtual bool Encode(const struct heif_image_handle* handle,
      const struct heif_image* image, const std::string& filename) = 0;

 protected:
  static uint8_t* GetExifMetaData(const struct heif_image_handle* handle, size_t* size);
};

#endif  // EXAMPLE_ENCODER_H
