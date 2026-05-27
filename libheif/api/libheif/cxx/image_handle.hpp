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

#ifndef LIBHEIF_CXX_IMAGE_HANDLE_HPP
#define LIBHEIF_CXX_IMAGE_HANDLE_HPP

#include <libheif/cxx/version.hpp>
#include <libheif/cxx/error.hpp>
#include <libheif/cxx/image_base.hpp>
#include <libheif/cxx/image.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <libheif/heif.h>

HEIF_CXX_NAMESPACE_BEGIN

/// RAII wrapper around heif_image_handle -- a coded image item in a file.
/// Shared metadata accessors (color profile, HDR, dimensions, ...) are
/// inherited from ImageBase.
class ImageHandle : public ImageBase
{
public:
  /// Empty handle (kind is still "handle", so the variant alternative matches).
  ImageHandle() : ImageBase(std::shared_ptr<heif_image_handle>()) {}

  explicit ImageHandle(heif_image_handle* handle)
      : ImageBase(handle ? std::shared_ptr<heif_image_handle>(handle, &heif_image_handle_release)
                        : std::shared_ptr<heif_image_handle>())
  {
  }

  [[nodiscard]] bool is_primary_image() const noexcept
  { return heif_image_handle_is_primary_image(raw_handle()) != 0; }

  [[nodiscard]] bool has_alpha_channel() const noexcept
  { return heif_image_handle_has_alpha_channel(raw_handle()) != 0; }

  // Bit depth from the coded config (NOT the per-channel buffer depths that
  // Image exposes), so these stay here rather than in ImageBase.
  [[nodiscard]] int luma_bits_per_pixel() const noexcept
  { return heif_image_handle_get_luma_bits_per_pixel(raw_handle()); }

  [[nodiscard]] int chroma_bits_per_pixel() const noexcept
  { return heif_image_handle_get_chroma_bits_per_pixel(raw_handle()); }

  [[nodiscard]] int ispe_width() const noexcept
  { return heif_image_handle_get_ispe_width(raw_handle()); }

  [[nodiscard]] int ispe_height() const noexcept
  { return heif_image_handle_get_ispe_height(raw_handle()); }

  // ------------------------- thumbnails -------------------------

  [[nodiscard]] int number_of_thumbnails() const noexcept
  { return heif_image_handle_get_number_of_thumbnails(raw_handle()); }

  [[nodiscard]] std::vector<heif_item_id> thumbnail_IDs() const
  {
    int num = number_of_thumbnails();
    std::vector<heif_item_id> ids(num);
    heif_image_handle_get_list_of_thumbnail_IDs(raw_handle(), ids.data(), num);
    return ids;
  }

  [[nodiscard]] Result<ImageHandle> thumbnail(heif_item_id id) const
  {
    heif_image_handle* handle = nullptr;
    if (auto r = detail::check(heif_image_handle_get_thumbnail(raw_handle(), id, &handle)); !r) {
      return std::unexpected(r.error());
    }
    return ImageHandle(handle);
  }

  // ------------------------- metadata (Exif / XMP) -------------------------

  [[nodiscard]] std::vector<heif_item_id>
  metadata_block_IDs(const char* type_filter = nullptr) const
  {
    int n = heif_image_handle_get_number_of_metadata_blocks(raw_handle(), type_filter);
    std::vector<heif_item_id> ids(n);
    heif_image_handle_get_list_of_metadata_block_IDs(raw_handle(), type_filter, ids.data(), n);
    return ids;
  }

  [[nodiscard]] std::string metadata_type(heif_item_id metadata_id) const
  { return heif_image_handle_get_metadata_type(raw_handle(), metadata_id); }

  [[nodiscard]] std::string metadata_content_type(heif_item_id metadata_id) const
  { return heif_image_handle_get_metadata_content_type(raw_handle(), metadata_id); }

  [[nodiscard]] Result<std::vector<uint8_t>> metadata(heif_item_id metadata_id) const
  {
    size_t size = heif_image_handle_get_metadata_size(raw_handle(), metadata_id);
    std::vector<uint8_t> data(size);
    if (auto r = detail::check(heif_image_handle_get_metadata(raw_handle(), metadata_id, data.data())); !r) {
      return std::unexpected(r.error());
    }
    return data;
  }

  // ------------------------- decoding -------------------------

  [[nodiscard]] Result<Image> decode_image(heif_colorspace colorspace,
                                           heif_chroma chroma) const
  {
    heif_image* out = nullptr;
    if (auto r = detail::check(heif_decode_image(raw_handle(), &out, colorspace, chroma, nullptr)); !r) {
      return std::unexpected(r.error());
    }
    return Image(out);
  }

  // is_premultiplied_alpha(), width(), height() and the color / HDR accessors
  // are inherited from ImageBase.

  friend class Context;
};

HEIF_CXX_NAMESPACE_END

#endif // LIBHEIF_CXX_IMAGE_HANDLE_HPP
