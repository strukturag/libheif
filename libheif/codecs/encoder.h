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

#ifndef HEIF_ENCODER_H
#define HEIF_ENCODER_H

#include "libheif/heif.h"
#include "error.h"
#include "libheif/heif_plugin.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

class HeifPixelImage;
class Box;


class Encoder
{
public:
  virtual ~Encoder() { }

  struct CodedImageData
  {
    std::vector<std::shared_ptr<Box>> properties;
    std::vector<uint8_t> bitstream;

    // If 0, the encoded size is equal to the input size.
    uint32_t encoded_image_width = 0;
    uint32_t encoded_image_height = 0;

    void append(const uint8_t* data, size_t size);

    void append_with_4bytes_size(const uint8_t* data, size_t size);
  };


  virtual Result<CodedImageData> encode(const std::shared_ptr<HeifPixelImage>& image,
                                        struct heif_encoder* encoder,
                                        const struct heif_encoding_options& options,
                                        enum heif_image_input_class input_class) { return {}; }
};


#endif
