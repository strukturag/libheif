/*
  libheif example application.

  MIT License

  Copyright (c) 2017 struktur AG, Joachim Bauch <bauch@struktur.de>
  Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/
#ifndef EXAMPLE_ENCODER_H
#define EXAMPLE_ENCODER_H

#include <string>
#include <memory>

#include "libheif/heif.h"
#include <vector>


class Encoder
{
public:
  virtual ~Encoder() = default;

  virtual heif_colorspace colorspace(bool has_alpha) const = 0;

  virtual heif_chroma chroma(bool has_alpha, int bit_depth) const = 0;

  virtual void UpdateDecodingOptions(const struct heif_image_handle* handle,
                                     struct heif_decoding_options* options) const
  {
    // Override if necessary.
  }

  virtual bool Encode(const struct heif_image_handle* handle,
                      const struct heif_image* image, const std::string& filename) = 0;

protected:
  static bool HasExifMetaData(const struct heif_image_handle* handle);

  static uint8_t* GetExifMetaData(const struct heif_image_handle* handle, size_t* size);

  static std::vector<uint8_t> get_xmp_metadata(const struct heif_image_handle* handle);
};

#endif  // EXAMPLE_ENCODER_H
