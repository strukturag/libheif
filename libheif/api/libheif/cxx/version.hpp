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

#ifndef LIBHEIF_CXX_VERSION_HPP
#define LIBHEIF_CXX_VERSION_HPP

// ---------------------------------------------------------------------------
// C++ API versioning.
//
// The version namespace below (`heif::vN`) is part of the *mangled symbol
// name* of every type and inline function in this wrapper. That gives us two
// things:
//
//   1. Two API versions can coexist in one translation unit, so users can
//      migrate type-by-type (`heif::v1::Image` next to `heif::v2::Image`).
//   2. If two libraries built against different wrapper versions are linked
//      into one binary, their inline (weak) symbols do NOT collide, because
//      they are tagged `v1` vs `v2`. This avoids silent ODR violations.
//
// Rules of thumb:
//   - Additive changes (new method / class / overload) go into the CURRENT
//     version. Do NOT bump.
//   - Deprecations use [[deprecated]] within the current version. Do NOT bump.
//   - Only a source-INCOMPATIBLE change to an existing type opens a new
//     version namespace.
//
// To add v2 later: add a `v2` block in each header guarded the same way, flip
// which namespace is `inline` based on HEIF_CXX_API_VERSION, and leave v1
// present (non-inline) until its deprecation window closes.
// ---------------------------------------------------------------------------

#ifndef HEIF_CXX_API_VERSION
#define HEIF_CXX_API_VERSION 1
#endif

#if HEIF_CXX_API_VERSION == 1
#  define HEIF_CXX_NAMESPACE_BEGIN namespace heif { inline namespace v1 {
#  define HEIF_CXX_NAMESPACE_END   } /*vN*/ } /*heif*/
#else
#  error "Unsupported HEIF_CXX_API_VERSION"
#endif

// This wrapper relies on std::expected (C++23). Domain errors are reported via
// heif::Result<T>; only allocation failure throws (std::bad_alloc), matching
// standard-library practice. <version> provides the feature-test macro.
#include <version>
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
// ok
#else
#  error "libheif C++ API requires std::expected (compile with C++23 or later)"
#endif

#endif // LIBHEIF_CXX_VERSION_HPP
