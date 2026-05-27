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

/*
 * Umbrella header for the modern C++ API -- the replacement for
 * <libheif/heif_cxx.h>. It mirrors the C API but uses RAII, std::expected-based
 * error handling (heif::Result<T>), and a versioned namespace (heif::v1).
 * Include this single header to get the full C++ API, or include the individual
 * per-class headers under <libheif/cxx/>.
 */

#ifndef LIBHEIF_CXX_HEIF_HPP
#define LIBHEIF_CXX_HEIF_HPP

#include <libheif/cxx/version.hpp>
#include <libheif/cxx/error.hpp>
#include <libheif/cxx/color_profile.hpp>
#include <libheif/cxx/image_base.hpp>
#include <libheif/cxx/image.hpp>
#include <libheif/cxx/image_handle.hpp>
#include <libheif/cxx/encoder_parameter.hpp>
#include <libheif/cxx/encoder.hpp>
#include <libheif/cxx/encoder_descriptor.hpp>
#include <libheif/cxx/context.hpp>

#endif // LIBHEIF_CXX_HEIF_HPP
