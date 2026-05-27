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

#ifndef LIBHEIF_CXX_CONTEXT_HPP
#define LIBHEIF_CXX_CONTEXT_HPP

#include <libheif/cxx/version.hpp>
#include <libheif/cxx/error.hpp>
#include <libheif/cxx/image.hpp>
#include <libheif/cxx/image_handle.hpp>
#include <libheif/cxx/encoder.hpp>

#include <memory>
#include <new>
#include <string>
#include <vector>

#include <libheif/heif.h>

HEIF_CXX_NAMESPACE_BEGIN

/// RAII wrapper around heif_context -- the top-level entry point.
class Context
{
public:
  /// Allocate a new context. Throws std::bad_alloc on allocation failure.
  Context()
      : m_context(heif_context_alloc(), &heif_context_free)
  {
    if (!m_context) {
      throw std::bad_alloc();
    }
  }

  // ------------------------- reading -------------------------

  [[nodiscard]] Result<void> read_from_file(const std::string& filename)
  { return detail::check(heif_context_read_from_file(m_context.get(), filename.c_str(), nullptr)); }

  [[nodiscard]] Result<void> read_from_memory_without_copy(const void* mem, size_t size)
  { return detail::check(heif_context_read_from_memory_without_copy(m_context.get(), mem, size, nullptr)); }

  /// User-supplied source of file data. Subclass and pass to read_from_reader().
  class Reader
  {
  public:
    virtual ~Reader() = default;
    virtual int64_t get_position() const = 0;
    virtual int read(void* data, size_t size) = 0;
    virtual int seek(int64_t position) = 0;
    virtual heif_reader_grow_status wait_for_file_size(int64_t target_size) = 0;
  };

  [[nodiscard]] Result<void> read_from_reader(Reader& reader)
  {
    return detail::check(heif_context_read_from_reader(m_context.get(), &s_reader_trampoline,
                                                      &reader, nullptr));
  }

  // ------------------------- navigation -------------------------

  [[nodiscard]] int number_of_top_level_images() const noexcept
  { return heif_context_get_number_of_top_level_images(m_context.get()); }

  [[nodiscard]] bool is_top_level_image_ID(heif_item_id id) const noexcept
  { return heif_context_is_top_level_image_ID(m_context.get(), id); }

  [[nodiscard]] std::vector<heif_item_id> top_level_image_IDs() const
  {
    int num = number_of_top_level_images();
    std::vector<heif_item_id> ids(num);
    heif_context_get_list_of_top_level_image_IDs(m_context.get(), ids.data(), num);
    return ids;
  }

  [[nodiscard]] Result<heif_item_id> primary_image_ID() const
  {
    heif_item_id id = 0;
    if (auto r = detail::check(heif_context_get_primary_image_ID(m_context.get(), &id)); !r) {
      return std::unexpected(r.error());
    }
    return id;
  }

  [[nodiscard]] Result<ImageHandle> primary_image_handle() const
  {
    heif_image_handle* handle = nullptr;
    if (auto r = detail::check(heif_context_get_primary_image_handle(m_context.get(), &handle)); !r) {
      return std::unexpected(r.error());
    }
    return ImageHandle(handle);
  }

  [[nodiscard]] Result<ImageHandle> image_handle(heif_item_id id) const
  {
    heif_image_handle* handle = nullptr;
    if (auto r = detail::check(heif_context_get_image_handle(m_context.get(), id, &handle)); !r) {
      return std::unexpected(r.error());
    }
    return ImageHandle(handle);
  }

  // ------------------------- encoding -------------------------

  /// Owning wrapper for heif_encoding_options, initialized to library defaults.
  class EncodingOptions : public heif_encoding_options
  {
  public:
    EncodingOptions()
    {
      heif_encoding_options* defaults = heif_encoding_options_alloc();
      *static_cast<heif_encoding_options*>(this) = *defaults;
      heif_encoding_options_free(defaults);
    }
  };

  [[nodiscard]] Result<ImageHandle> encode_image(const Image& img, Encoder& encoder,
                                                 const EncodingOptions& options = EncodingOptions())
  {
    heif_image_handle* handle = nullptr;
    if (auto r = detail::check(heif_context_encode_image(m_context.get(), img.raw_image(),
                                                        encoder.m_encoder.get(), &options, &handle)); !r) {
      return std::unexpected(r.error());
    }
    return ImageHandle(handle);
  }

  [[nodiscard]] Result<ImageHandle> encode_thumbnail(const Image& image,
                                                     const ImageHandle& master,
                                                     Encoder& encoder,
                                                     const EncodingOptions& options,
                                                     int bbox_size)
  {
    heif_image_handle* handle = nullptr;
    if (auto r = detail::check(heif_context_encode_thumbnail(m_context.get(), image.raw_image(),
                                                            master.raw_handle(), encoder.m_encoder.get(),
                                                            &options, bbox_size, &handle)); !r) {
      return std::unexpected(r.error());
    }
    return ImageHandle(handle);
  }

  [[nodiscard]] Result<void> set_primary_image(ImageHandle& new_primary)
  { return detail::check(heif_context_set_primary_image(m_context.get(), new_primary.raw_handle())); }

  [[nodiscard]] Result<void> assign_thumbnail(const ImageHandle& thumbnail, const ImageHandle& master)
  { return detail::check(heif_context_assign_thumbnail(m_context.get(), thumbnail.raw_handle(), master.raw_handle())); }

  [[nodiscard]] Result<void> add_exif_metadata(const ImageHandle& master, const void* data, int size)
  { return detail::check(heif_context_add_exif_metadata(m_context.get(), master.raw_handle(), data, size)); }

  [[nodiscard]] Result<void> add_XMP_metadata(const ImageHandle& master, const void* data, int size)
  { return detail::check(heif_context_add_XMP_metadata(m_context.get(), master.raw_handle(), data, size)); }

  // ------------------------- writing -------------------------

  /// User-supplied sink for file data. Subclass and pass to write().
  class Writer
  {
  public:
    virtual ~Writer() = default;
    virtual heif_error write(const void* data, size_t size) = 0;
  };

  [[nodiscard]] Result<void> write(Writer& writer)
  { return detail::check(heif_context_write(m_context.get(), &s_writer_trampoline, &writer)); }

  [[nodiscard]] Result<void> write_to_file(const std::string& filename) const
  { return detail::check(heif_context_write_to_file(m_context.get(), filename.c_str())); }

private:
  std::shared_ptr<heif_context> m_context;

  // ---- C-callback trampolines forwarding into the virtual Reader/Writer ----

  static int64_t reader_get_position(void* ud)
  { return static_cast<Reader*>(ud)->get_position(); }

  static int reader_read(void* data, size_t size, void* ud)
  { return static_cast<Reader*>(ud)->read(data, size); }

  static int reader_seek(int64_t pos, void* ud)
  { return static_cast<Reader*>(ud)->seek(pos); }

  static heif_reader_grow_status reader_wait(int64_t target, void* ud)
  { return static_cast<Reader*>(ud)->wait_for_file_size(target); }

  static heif_error writer_write(heif_context*, const void* data, size_t size, void* ud)
  { return static_cast<Writer*>(ud)->write(data, size); }

  inline static heif_reader s_reader_trampoline = {
      1, reader_get_position, reader_read, reader_seek, reader_wait,
      nullptr, nullptr, nullptr, nullptr};

  inline static heif_writer s_writer_trampoline = {1, writer_write};
};

HEIF_CXX_NAMESPACE_END

#endif // LIBHEIF_CXX_CONTEXT_HPP
