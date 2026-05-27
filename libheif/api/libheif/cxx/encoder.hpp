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

#ifndef LIBHEIF_CXX_ENCODER_HPP
#define LIBHEIF_CXX_ENCODER_HPP

#include <libheif/cxx/version.hpp>
#include <libheif/cxx/error.hpp>
#include <libheif/cxx/encoder_parameter.hpp>

#include <memory>
#include <string>
#include <vector>

#include <libheif/heif.h>

HEIF_CXX_NAMESPACE_BEGIN

class EncoderDescriptor; // friend
class Context;           // friend

/// RAII wrapper around heif_encoder.
class Encoder
{
public:
  /// Obtain an encoder for the given compression format. Fails if no encoder
  /// plugin for that format is available (a domain error).
  [[nodiscard]] static Result<Encoder> create(heif_compression_format format)
  {
    heif_encoder* encoder = nullptr;
    if (auto r = detail::check(heif_context_get_encoder_for_format(nullptr, format, &encoder)); !r) {
      return std::unexpected(r.error());
    }
    return Encoder(encoder);
  }

  [[nodiscard]] Result<void> set_lossy_quality(int quality)
  { return detail::check(heif_encoder_set_lossy_quality(m_encoder.get(), quality)); }

  [[nodiscard]] Result<void> set_lossless(bool enable)
  { return detail::check(heif_encoder_set_lossless(m_encoder.get(), enable)); }

  [[nodiscard]] std::vector<EncoderParameter> parameters() const
  {
    std::vector<EncoderParameter> params;
    for (const heif_encoder_parameter* const* p = heif_encoder_list_parameters(m_encoder.get());
         p && *p; p++) {
      params.push_back(EncoderParameter(*p));
    }
    return params;
  }

  [[nodiscard]] Result<void> set_integer_parameter(const std::string& name, int value)
  { return detail::check(heif_encoder_set_parameter_integer(m_encoder.get(), name.c_str(), value)); }

  [[nodiscard]] Result<int> integer_parameter(const std::string& name) const
  {
    int value = 0;
    if (auto r = detail::check(heif_encoder_get_parameter_integer(m_encoder.get(), name.c_str(), &value)); !r) {
      return std::unexpected(r.error());
    }
    return value;
  }

  [[nodiscard]] Result<void> set_boolean_parameter(const std::string& name, bool value)
  { return detail::check(heif_encoder_set_parameter_boolean(m_encoder.get(), name.c_str(), value)); }

  [[nodiscard]] Result<bool> boolean_parameter(const std::string& name) const
  {
    int value = 0;
    if (auto r = detail::check(heif_encoder_get_parameter_boolean(m_encoder.get(), name.c_str(), &value)); !r) {
      return std::unexpected(r.error());
    }
    return value != 0;
  }

  [[nodiscard]] Result<void> set_string_parameter(const std::string& name, const std::string& value)
  { return detail::check(heif_encoder_set_parameter_string(m_encoder.get(), name.c_str(), value.c_str())); }

  [[nodiscard]] Result<std::string> string_parameter(const std::string& name) const
  {
    constexpr int max_size = 250;
    char value[max_size];
    if (auto r = detail::check(heif_encoder_get_parameter_string(m_encoder.get(), name.c_str(), value, max_size)); !r) {
      return std::unexpected(r.error());
    }
    return std::string(value);
  }

  [[nodiscard]] Result<void> set_parameter(const std::string& name, const std::string& value)
  { return detail::check(heif_encoder_set_parameter(m_encoder.get(), name.c_str(), value.c_str())); }

  [[nodiscard]] Result<std::string> parameter(const std::string& name) const
  {
    constexpr int max_size = 250;
    char value[max_size];
    if (auto r = detail::check(heif_encoder_get_parameter(m_encoder.get(), name.c_str(), value, max_size)); !r) {
      return std::unexpected(r.error());
    }
    return std::string(value);
  }

private:
  explicit Encoder(heif_encoder* encoder)
      : m_encoder(encoder, &heif_encoder_release)
  {
  }

  std::shared_ptr<heif_encoder> m_encoder;

  friend class EncoderDescriptor;
  friend class Context;
};

HEIF_CXX_NAMESPACE_END

#endif // LIBHEIF_CXX_ENCODER_HPP
