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

#ifndef LIBHEIF_CXX_IMAGE_BASE_HPP
#define LIBHEIF_CXX_IMAGE_BASE_HPP

#include <libheif/cxx/version.hpp>
#include <libheif/cxx/error.hpp>
#include <libheif/cxx/color_profile.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <libheif/heif.h>

HEIF_CXX_NAMESPACE_BEGIN

/// Display pixel aspect ratio (horizontal:vertical).
struct PixelAspectRatio
{
  uint32_t horizontal;
  uint32_t vertical;
};

/// Common base of Image and ImageHandle, hosting the metadata accessors that
/// both the heif_image (decoded buffer) and heif_image_handle (file item) C
/// types support. The two map internally to the same ImageDescription; the C
/// API just splits the surface across two object types.
///
/// Exactly one of the two underlying handles is held at a time, in a
/// std::variant. The class is intentionally non-polymorphic (no vtable): the
/// derived classes add no data members, so passing or copying as ImageBase is
/// lossless.
class ImageBase
{
public:
  [[nodiscard]] bool empty() const noexcept { return !raw_image() && !raw_handle(); }

  // ------------------------- dimensions -------------------------
  // Overall (primary) image dimensions. Note: heif_image_get_width()/height()
  // take a channel; the channel-less "whole image" size is get_primary_*.

  [[nodiscard]] int width() const noexcept
  {
    if (auto* i = raw_image()) return heif_image_get_primary_width(i);
    return heif_image_handle_get_width(raw_handle());
  }

  [[nodiscard]] int height() const noexcept
  {
    if (auto* i = raw_image()) return heif_image_get_primary_height(i);
    return heif_image_handle_get_height(raw_handle());
  }

  [[nodiscard]] bool is_premultiplied_alpha() const noexcept
  {
    if (auto* i = raw_image()) return heif_image_is_premultiplied_alpha(i) != 0;
    return heif_image_handle_is_premultiplied_alpha(raw_handle()) != 0;
  }

  // ------------------------- color profile -------------------------

  [[nodiscard]] heif_color_profile_type color_profile_type() const noexcept
  {
    if (auto* i = raw_image()) return heif_image_get_color_profile_type(i);
    return heif_image_handle_get_color_profile_type(raw_handle());
  }

  [[nodiscard]] Result<ColorProfile_nclx> nclx_color_profile() const
  {
    heif_color_profile_nclx* nclx = nullptr;
    heif_error err;
    if (auto* i = raw_image()) {
      err = heif_image_get_nclx_color_profile(i, &nclx);
    }
    else {
      err = heif_image_handle_get_nclx_color_profile(raw_handle(), &nclx);
    }
    if (err.code != heif_error_Ok) {
      return std::unexpected(Error(err));
    }
    return ColorProfile_nclx(nclx);
  }

  [[nodiscard]] std::vector<uint8_t> raw_color_profile() const
  {
    size_t size;
    if (auto* i = raw_image()) {
      size = heif_image_get_raw_color_profile_size(i);
    }
    else {
      size = heif_image_handle_get_raw_color_profile_size(raw_handle());
    }
    std::vector<uint8_t> data(size);
    if (auto* i = raw_image()) {
      heif_image_get_raw_color_profile(i, data.data());
    }
    else {
      heif_image_handle_get_raw_color_profile(raw_handle(), data.data());
    }
    return data;
  }

  // ------------------------- HDR metadata -------------------------
  // The C API reports presence differently for the two types (handle: int
  // return; image: a separate has_*() predicate). Both are smoothed into
  // std::optional here.

  [[nodiscard]] std::optional<heif_content_light_level> content_light_level() const noexcept
  {
    heif_content_light_level out{};
    if (auto* i = raw_image()) {
      if (!heif_image_has_content_light_level(i)) return std::nullopt;
      heif_image_get_content_light_level(i, &out);
      return out;
    }
    if (heif_image_handle_get_content_light_level(raw_handle(), &out)) return out;
    return std::nullopt;
  }

  [[nodiscard]] std::optional<heif_mastering_display_colour_volume>
  mastering_display_colour_volume() const noexcept
  {
    heif_mastering_display_colour_volume out{};
    if (auto* i = raw_image()) {
      if (!heif_image_has_mastering_display_colour_volume(i)) return std::nullopt;
      heif_image_get_mastering_display_colour_volume(i, &out);
      return out;
    }
    if (heif_image_handle_get_mastering_display_colour_volume(raw_handle(), &out)) return out;
    return std::nullopt;
  }

  [[nodiscard]] std::optional<uint32_t> nominal_diffuse_white_luminance() const noexcept
  {
    if (auto* i = raw_image()) {
      if (!heif_image_has_nominal_diffuse_white_luminance(i)) return std::nullopt;
      return heif_image_get_nominal_diffuse_white_luminance(i);
    }
    if (!heif_image_handle_has_nominal_diffuse_white_luminance(raw_handle())) return std::nullopt;
    return heif_image_handle_get_nominal_diffuse_white_luminance(raw_handle());
  }

  // ------------------------- display / projection -------------------------

  [[nodiscard]] PixelAspectRatio pixel_aspect_ratio() const noexcept
  {
    PixelAspectRatio par{1, 1};
    if (auto* i = raw_image()) {
      heif_image_get_pixel_aspect_ratio(i, &par.horizontal, &par.vertical);
    }
    else {
      heif_image_handle_get_pixel_aspect_ratio(raw_handle(), &par.horizontal, &par.vertical);
    }
    return par;
  }

  [[nodiscard]] heif_omaf_image_projection omaf_image_projection() const noexcept
  {
    if (auto* i = raw_image()) return heif_image_get_omaf_image_projection(i);
    return heif_image_handle_get_omaf_image_projection(raw_handle());
  }

protected:
  ImageBase() = default;  // empty: active alternative is a null heif_image
  explicit ImageBase(std::shared_ptr<heif_image> img) : m_payload(std::move(img)) {}
  explicit ImageBase(std::shared_ptr<heif_image_handle> h) : m_payload(std::move(h)) {}

  // Exactly one alternative is active at a time (type-enforced). Each derived
  // class constructs ITS OWN alternative (even when null), so the kind is
  // always known. No std::monostate.
  std::variant<std::shared_ptr<heif_image>,
               std::shared_ptr<heif_image_handle>> m_payload;

  // Centralized std::get_if: returns a pointer INTO the variant (no shared_ptr
  // copy, no refcount activity), or nullptr if that alternative isn't active.
  [[nodiscard]] heif_image* raw_image() const noexcept
  {
    if (auto* p = std::get_if<std::shared_ptr<heif_image>>(&m_payload)) return p->get();
    return nullptr;
  }

  [[nodiscard]] heif_image_handle* raw_handle() const noexcept
  {
    if (auto* p = std::get_if<std::shared_ptr<heif_image_handle>>(&m_payload)) return p->get();
    return nullptr;
  }

  // Context needs the raw heif_image* / heif_image_handle* for encode/decode.
  friend class Context;
};

HEIF_CXX_NAMESPACE_END

#endif // LIBHEIF_CXX_IMAGE_BASE_HPP
