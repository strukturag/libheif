/*
 * HEIF codec.
 *
 * MIT License
 *
 * Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef LIBHEIF_CXX_FWD_HPP
#define LIBHEIF_CXX_FWD_HPP

#include <libheif/cxx/version.hpp>

// Forward declarations of all wrapper types. Because the real types live in
// `heif::vN`, hand-written forward declarations like `namespace heif { class
// Image; }` would NOT match. Always forward-declare via this header instead.

HEIF_CXX_NAMESPACE_BEGIN

class Error;
// heif::Result<T> is an alias template (see error.hpp); alias templates
// cannot be forward-declared, so it is intentionally absent here.

class Context;
class ImageBase;
class ImageHandle;
class Image;
class ColorProfile_nclx;
class Encoder;
class EncoderParameter;
class EncoderDescriptor;

HEIF_CXX_NAMESPACE_END

#endif // LIBHEIF_CXX_FWD_HPP
