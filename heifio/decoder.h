/*
  libheif example application "heif".

  MIT License

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

#ifndef LIBHEIF_DECODER_H
#define LIBHEIF_DECODER_H

#include "libheif/heif.h"
#include <memory>
#include <string>
#include <vector>

struct InputImage
{
  std::shared_ptr<heif_image> image;
  std::vector<uint8_t> xmp;
  std::vector<uint8_t> exif;
  heif_orientation orientation = heif_orientation_normal;
};


// libheif's heif_error.message is owned by the object whose function returned the error
// (the heif_context, heif_image_handle or heif_image) and becomes invalid once that object
// is released. A loader that releases the object and then returns the error has to copy
// the message first (GHSA-hvwh-xpj6-ph5x). This copies it into a thread-local string, so
// that it stays valid for the caller and concurrent callers on different threads do not
// race on the buffer.
// The caller is expected to log the error promptly (heif-enc calls exit() immediately).
inline heif_error stable_error(const heif_error& err)
{
  static thread_local std::string msg;
  msg = err.message ? err.message : "";
  return {err.code, err.subcode, msg.c_str()};
}

#endif //LIBHEIF_DECODER_H
