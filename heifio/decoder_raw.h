/*
  libheif example application "heif".

  MIT License

  Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>

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

#ifndef LIBHEIF_DECODER_RAW_H
#define LIBHEIF_DECODER_RAW_H

#include "decoder.h"
#include <libheif/heif_uncompressed_types.h>
#include <string>

struct RawImageParameters {
  int width = 0;
  int height = 0;
  heif_channel_datatype datatype = heif_channel_datatype_undefined;
  int bit_depth = 0;
  bool big_endian = false;
};

// Maps a CLI string like "uint16" or "float32" to datatype + bit_depth.
// Returns false if the string is not recognized.
bool parse_raw_pixel_type(const std::string& type_string,
                          heif_channel_datatype* out_datatype,
                          int* out_bit_depth);

LIBHEIF_API
heif_error loadRAW(const char* filename, const RawImageParameters& params, InputImage* input_image);

#endif //LIBHEIF_DECODER_RAW_H
