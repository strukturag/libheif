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
#ifndef EXAMPLE_ENCODER_JPEG_H
#define EXAMPLE_ENCODER_JPEG_H

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>

#include <jpeglib.h>

#include "encoder.h"

class JpegEncoder : public Encoder {
 public:
  JpegEncoder(int quality);

  bool Encode(const std::shared_ptr<heif::HeifPixelImage>& image,
      const std::string& filename) override;

 private:
  static const int kDefaultQuality = 90;

  struct ErrorHandler {
    struct jpeg_error_mgr pub;  /* "public" fields */
    jmp_buf setjmp_buffer;  /* for return to caller */
  };

  static void OnJpegError(j_common_ptr cinfo);

  int quality_;
};

#endif  // EXAMPLE_ENCODER_JPEG_H
