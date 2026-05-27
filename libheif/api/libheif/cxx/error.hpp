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

#ifndef LIBHEIF_CXX_ERROR_HPP
#define LIBHEIF_CXX_ERROR_HPP

#include <libheif/cxx/version.hpp>

#include <expected>
#include <string>
#include <utility>

#include <libheif/heif.h>

HEIF_CXX_NAMESPACE_BEGIN

/// A failure returned by a libheif operation.
///
/// Unlike the legacy heif::Error (heif_cxx.h), this type only ever represents
/// a failure -- success is expressed by the absence of an Error, via
/// heif::Result<T>. The human-readable message is copied out of the C
/// heif_error immediately, so it does not dangle once the owning context is
/// freed.
class Error
{
public:
  Error() = default;

  explicit Error(const heif_error& err)
      : m_code(err.code),
        m_subcode(err.subcode),
        m_message(err.message ? err.message : "")
  {
  }

  Error(heif_error_code code, heif_suberror_code subcode, std::string msg)
      : m_code(code), m_subcode(subcode), m_message(std::move(msg))
  {
  }

  [[nodiscard]] heif_error_code code() const noexcept { return m_code; }

  [[nodiscard]] heif_suberror_code subcode() const noexcept { return m_subcode; }

  [[nodiscard]] const std::string& message() const noexcept { return m_message; }

private:
  heif_error_code m_code = heif_error_Ok;
  heif_suberror_code m_subcode = heif_suberror_Unspecified;
  std::string m_message;
};


/// The result of a fallible operation: either a value of type T, or an Error.
/// For operations that return nothing on success, use Result<void>.
template <typename T>
using Result = std::expected<T, Error>;


namespace detail {

  /// Translate a C heif_error into a Result<void>: empty on success,
  /// std::unexpected(Error) on failure. Use as the building block for
  /// propagating errors out of wrapper methods.
  [[nodiscard]] inline Result<void> check(const heif_error& err)
  {
    if (err.code != heif_error_Ok) {
      return std::unexpected(Error(err));
    }
    return {};
  }

} // namespace detail

HEIF_CXX_NAMESPACE_END

#endif // LIBHEIF_CXX_ERROR_HPP
