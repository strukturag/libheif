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

#ifndef LIBHEIF_CXX_ENCODER_PARAMETER_HPP
#define LIBHEIF_CXX_ENCODER_PARAMETER_HPP

#include <libheif/cxx/version.hpp>
#include <libheif/cxx/error.hpp>

#include <string>
#include <vector>

#include <libheif/heif.h>

HEIF_CXX_NAMESPACE_BEGIN

class Encoder; // friend

/// Non-owning view of a single encoder parameter descriptor.
class EncoderParameter
{
public:
  [[nodiscard]] std::string name() const noexcept
  { return heif_encoder_parameter_get_name(m_parameter); }

  [[nodiscard]] heif_encoder_parameter_type type() const noexcept
  { return heif_encoder_parameter_get_type(m_parameter); }

  [[nodiscard]] bool is_integer() const noexcept
  { return type() == heif_encoder_parameter_type_integer; }

  [[nodiscard]] bool is_boolean() const noexcept
  { return type() == heif_encoder_parameter_type_boolean; }

  [[nodiscard]] bool is_string() const noexcept
  { return type() == heif_encoder_parameter_type_string; }

  struct IntegerRange { bool limited; int minimum; int maximum; };

  [[nodiscard]] Result<IntegerRange> valid_integer_range() const
  {
    IntegerRange range{};
    int have_minmax = 0;
    if (auto r = detail::check(heif_encoder_parameter_get_valid_integer_range(
            m_parameter, &have_minmax, &range.minimum, &range.maximum)); !r) {
      return std::unexpected(r.error());
    }
    range.limited = (have_minmax != 0);
    return range;
  }

  [[nodiscard]] Result<std::vector<std::string>> valid_string_values() const
  {
    const char* const* arr = nullptr;
    if (auto r = detail::check(heif_encoder_parameter_get_valid_string_values(m_parameter, &arr)); !r) {
      return std::unexpected(r.error());
    }
    std::vector<std::string> values;
    for (int i = 0; arr && arr[i]; i++) {
      values.emplace_back(arr[i]);
    }
    return values;
  }

private:
  explicit EncoderParameter(const heif_encoder_parameter* param) : m_parameter(param) {}

  const heif_encoder_parameter* m_parameter = nullptr;

  friend class Encoder;
};

HEIF_CXX_NAMESPACE_END

#endif // LIBHEIF_CXX_ENCODER_PARAMETER_HPP
