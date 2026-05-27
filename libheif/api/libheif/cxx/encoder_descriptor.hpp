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

#ifndef LIBHEIF_CXX_ENCODER_DESCRIPTOR_HPP
#define LIBHEIF_CXX_ENCODER_DESCRIPTOR_HPP

#include <libheif/cxx/version.hpp>
#include <libheif/cxx/error.hpp>
#include <libheif/cxx/encoder.hpp>

#include <string>
#include <vector>

#include <libheif/heif.h>

HEIF_CXX_NAMESPACE_BEGIN

/// Non-owning view of an available encoder, used to enumerate and instantiate
/// encoders.
class EncoderDescriptor
{
public:
  [[nodiscard]] static std::vector<EncoderDescriptor>
  list(heif_compression_format format_filter, const char* name_filter = nullptr)
  {
    std::vector<EncoderDescriptor> result;
    int capacity = 16;
    for (;;) {
      std::vector<const heif_encoder_descriptor*> buf(capacity);
      int n = heif_context_get_encoder_descriptors(nullptr, format_filter, name_filter,
                                                   buf.data(), capacity);
      if (n < capacity) {
        result.reserve(n);
        for (int i = 0; i < n; i++) {
          result.push_back(EncoderDescriptor(buf[i]));
        }
        return result;
      }
      capacity *= 2;
    }
  }

  [[nodiscard]] std::string name() const noexcept
  { return heif_encoder_descriptor_get_name(m_descriptor); }

  [[nodiscard]] std::string id_name() const noexcept
  { return heif_encoder_descriptor_get_id_name(m_descriptor); }

  [[nodiscard]] heif_compression_format compression_format() const noexcept
  { return heif_encoder_descriptor_get_compression_format(m_descriptor); }

  [[nodiscard]] bool supports_lossy_compression() const noexcept
  { return heif_encoder_descriptor_supports_lossy_compression(m_descriptor); }

  [[nodiscard]] bool supports_lossless_compression() const noexcept
  { return heif_encoder_descriptor_supports_lossless_compression(m_descriptor); }

  [[nodiscard]] Result<Encoder> get_encoder() const
  {
    heif_encoder* encoder = nullptr;
    if (auto r = detail::check(heif_context_get_encoder(nullptr, m_descriptor, &encoder)); !r) {
      return std::unexpected(r.error());
    }
    return Encoder(encoder);
  }

private:
  explicit EncoderDescriptor(const heif_encoder_descriptor* descr) : m_descriptor(descr) {}

  const heif_encoder_descriptor* m_descriptor = nullptr;
};

HEIF_CXX_NAMESPACE_END

#endif // LIBHEIF_CXX_ENCODER_DESCRIPTOR_HPP
