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

#ifndef LIBHEIF_CXX_IMAGE_HPP
#define LIBHEIF_CXX_IMAGE_HPP

#include <libheif/cxx/version.hpp>
#include <libheif/cxx/error.hpp>
#include <libheif/cxx/color_profile.hpp>
#include <libheif/cxx/image_base.hpp>

#include <cstdint>
#include <memory>
#include <vector>

#include <libheif/heif.h>

HEIF_CXX_NAMESPACE_BEGIN

class Context; // friend

/// A pointer + row stride view onto one pixel plane.
template <typename Byte>
struct PlaneView
{
  Byte* data = nullptr;
  size_t stride = 0;
};

/// RAII wrapper around heif_image -- a decoded / encodable pixel buffer.
/// Shared metadata accessors (color profile, HDR, pixel aspect, ...) are
/// inherited from ImageBase.
class Image : public ImageBase
{
public:
  Image() = default;

  /// Adopt an owning heif_image* (e.g. produced by the C API).
  explicit Image(heif_image* image)
      : ImageBase(std::shared_ptr<heif_image>(image, &heif_image_release))
  {
  }

  // Un-hide the parameterless overall-dimension accessors from the base, which
  // would otherwise be shadowed by the per-channel width()/height() below.
  using ImageBase::width;
  using ImageBase::height;

  /// Create an empty image of the given geometry.
  [[nodiscard]] static Result<Image> create(int width, int height,
                                            heif_colorspace colorspace,
                                            heif_chroma chroma)
  {
    heif_image* image = nullptr;
    if (auto r = detail::check(heif_image_create(width, height, colorspace, chroma, &image)); !r) {
      return std::unexpected(r.error());
    }
    return Image(image);
  }

  [[nodiscard]] Result<void> add_plane(heif_channel channel,
                                       int width, int height, int bit_depth)
  {
    return detail::check(heif_image_add_plane(raw_image(), channel, width, height, bit_depth));
  }

  [[nodiscard]] heif_colorspace colorspace() const noexcept
  { return heif_image_get_colorspace(raw_image()); }

  [[nodiscard]] heif_chroma chroma_format() const noexcept
  { return heif_image_get_chroma_format(raw_image()); }

  [[nodiscard]] int width(heif_channel channel) const noexcept
  { return heif_image_get_width(raw_image(), channel); }

  [[nodiscard]] int height(heif_channel channel) const noexcept
  { return heif_image_get_height(raw_image(), channel); }

  [[nodiscard]] int bits_per_pixel(heif_channel channel) const noexcept
  { return heif_image_get_bits_per_pixel(raw_image(), channel); }

  [[nodiscard]] int bits_per_pixel_range(heif_channel channel) const noexcept
  { return heif_image_get_bits_per_pixel_range(raw_image(), channel); }

  [[nodiscard]] bool has_channel(heif_channel channel) const noexcept
  { return heif_image_has_channel(raw_image(), channel); }

  // Plane access uses the size_t-stride C entry points; the legacy int-stride
  // overloads are intentionally dropped.
  [[nodiscard]] PlaneView<const uint8_t> plane(heif_channel channel) const noexcept
  {
    PlaneView<const uint8_t> v;
    v.data = heif_image_get_plane_readonly2(raw_image(), channel, &v.stride);
    return v;
  }

  [[nodiscard]] PlaneView<uint8_t> plane(heif_channel channel) noexcept
  {
    PlaneView<uint8_t> v;
    v.data = heif_image_get_plane2(raw_image(), channel, &v.stride);
    return v;
  }

  // nclx_color_profile(), color_profile_type(), raw_color_profile() and
  // is_premultiplied_alpha() live in ImageBase. The SETTERS below are
  // image-only (no heif_image_handle_set_* equivalents exist).

  [[nodiscard]] Result<void> set_nclx_color_profile(const ColorProfile_nclx& nclx)
  {
    return detail::check(heif_image_set_nclx_color_profile(raw_image(), nclx.m_profile.get()));
  }

  [[nodiscard]] Result<void> set_raw_color_profile(heif_color_profile_type type,
                                                   const std::vector<uint8_t>& data)
  {
    const char* profile_type = nullptr;
    switch (type) {
      case heif_color_profile_type_prof: profile_type = "prof"; break;
      case heif_color_profile_type_rICC: profile_type = "rICC"; break;
      default:
        return std::unexpected(Error(heif_error_Usage_error, heif_suberror_Unspecified,
                                     "invalid raw color profile type"));
    }
    return detail::check(heif_image_set_raw_color_profile(raw_image(), profile_type,
                                                          data.data(), data.size()));
  }

  void set_premultiplied_alpha(bool premultiplied) noexcept
  { heif_image_set_premultiplied_alpha(raw_image(), premultiplied); }

  [[nodiscard]] Result<Image> scale(int width, int height) const
  {
    heif_image* img = nullptr;
    if (auto r = detail::check(heif_image_scale_image(raw_image(), &img, width, height, nullptr)); !r) {
      return std::unexpected(r.error());
    }
    return Image(img);
  }

  friend class Context;
};

HEIF_CXX_NAMESPACE_END

#endif // LIBHEIF_CXX_IMAGE_HPP
