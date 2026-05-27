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

#ifndef LIBHEIF_CXX_COLOR_PROFILE_HPP
#define LIBHEIF_CXX_COLOR_PROFILE_HPP

#include <libheif/cxx/version.hpp>
#include <libheif/cxx/error.hpp>

#include <memory>

#include <libheif/heif.h>

HEIF_CXX_NAMESPACE_BEGIN

class Image; // friend

/// RAII wrapper around heif_color_profile_nclx.
class ColorProfile_nclx
{
public:
  /// Allocate a default-initialized nclx profile. Throws std::bad_alloc on
  /// allocation failure (a non-domain error, like the standard library).
  ColorProfile_nclx()
      : m_profile(heif_nclx_color_profile_alloc(), &heif_nclx_color_profile_free)
  {
    if (!m_profile) {
      throw std::bad_alloc();
    }
  }

  [[nodiscard]] heif_color_primaries color_primaries() const noexcept
  { return m_profile->color_primaries; }

  [[nodiscard]] heif_transfer_characteristics transfer_characteristics() const noexcept
  { return m_profile->transfer_characteristics; }

  [[nodiscard]] heif_matrix_coefficients matrix_coefficients() const noexcept
  { return m_profile->matrix_coefficients; }

  [[nodiscard]] bool is_full_range() const noexcept
  { return m_profile->full_range_flag; }

  void set_color_primaries(heif_color_primaries cp) noexcept
  { m_profile->color_primaries = cp; }

  void set_transfer_characteristics(heif_transfer_characteristics tc) noexcept
  { m_profile->transfer_characteristics = tc; }

  void set_matrix_coefficients(heif_matrix_coefficients mc) noexcept
  { m_profile->matrix_coefficients = mc; }

  void set_full_range_flag(bool full_range) noexcept
  { m_profile->full_range_flag = full_range; }

private:
  explicit ColorProfile_nclx(heif_color_profile_nclx* nclx)
      : m_profile(nclx, &heif_nclx_color_profile_free)
  {
  }

  std::shared_ptr<heif_color_profile_nclx> m_profile;

  friend class Image;
  friend class ImageBase;
};

HEIF_CXX_NAMESPACE_END

#endif // LIBHEIF_CXX_COLOR_PROFILE_HPP
