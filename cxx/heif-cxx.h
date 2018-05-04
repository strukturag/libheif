/*
 * C++ interface to libheif
 * Copyright (c) 2018 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of heif, an example application using libheif.
 *
 * heif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * heif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with heif.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBHEIF_HEIF_CXX_H
#define LIBHEIF_HEIF_CXX_H

#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <libheif/heif.h>
}


namespace heif {

  class Error
  {
  public:
    Error() {
      m_code = heif_error_Ok;
      m_subcode = heif_suberror_Unspecified;
      m_message = "Ok";
    }

    Error(const heif_error& err) {
      m_code = err.code;
      m_subcode = err.subcode;
      m_message = err.message;
    }

    std::string get_message() const { return m_message; }

    heif_error_code get_code() const { return m_code; }

    heif_suberror_code get_subcode() const { return m_subcode; }

    operator bool() const { return m_code != heif_error_Ok; }

  private:
    heif_error_code m_code;
    heif_suberror_code m_subcode;
    std::string m_message;
  };


  class ImageHandle;
  class Image;


  class Context
  {
  public:
    Context();

    class ReadingOptions { };

    Error read_from_file(std::string filename, const ReadingOptions& opts = ReadingOptions());

    int get_number_of_top_level_images() const;

    bool is_top_level_image_ID(heif_item_id id) const;

    std::vector<heif_item_id> get_list_of_top_level_image_IDs() const;

    Error get_primary_image_ID(heif_item_id* id);

    Error get_primary_image_handle(ImageHandle* out_handle);

  private:
    std::shared_ptr<heif_context> m_context;
  };


  class ImageHandle
  {
  public:
    ImageHandle() { }

    ImageHandle(heif_image_handle* handle);

    bool is_primary_image() const;

    int get_width() const;

    int get_height() const;

    bool has_alpha_channel() const;

    // ------------------------- depth images -------------------------

    // TODO

    // ------------------------- thumbnails -------------------------

    int get_number_of_thumbnails() const;

    std::vector<heif_item_id> get_list_of_thumbnail_IDs() const;

    Error get_thumbnail(heif_item_id id, ImageHandle* out_handle);

    // ------------------------- metadata (Exif / XMP) -------------------------

    // TODO


    class DecodingOptions { };

    Error decode_image(Image* output, heif_colorspace colorspace, heif_chroma chroma,
                       const DecodingOptions& options = DecodingOptions());

  private:
    std::shared_ptr<heif_image_handle> m_image_handle;
  };


  class Image
  {
  public:
    Image() { }
    Image(heif_image* image);


    Error create(int width, int height,
                 enum heif_colorspace colorspace,
                 enum heif_chroma chroma);

    Error add_plane(enum heif_channel channel,
                    int width, int height, int bit_depth);

    heif_colorspace get_colorspace() const;

    heif_chroma get_chroma_format() const;

    int get_width(enum heif_channel channel) const;

    int get_height(enum heif_channel channel) const;

    int get_bits_per_pixel(enum heif_channel channel) const;

    const uint8_t* get_plane(enum heif_channel channel, int* out_stride) const;

    uint8_t* get_plane(enum heif_channel channel, int* out_stride);

    class ScalingOptions { };

    Error scale_image(Image* output,
                      int width, int height,
                      const ScalingOptions& options = ScalingOptions()) const;

  private:
    std::shared_ptr<heif_image> m_image;
  };


  // ==========================================================================================
  //                                     IMPLEMENTATION
  // ==========================================================================================

  inline Context::Context() {
    heif_context* ctx = heif_context_alloc();
    m_context = std::shared_ptr<heif_context>(ctx,
                                              [] (heif_context* c) { heif_context_free(c); });
  }

  inline Error Context::read_from_file(std::string filename, const ReadingOptions& opts) {
    return Error(heif_context_read_from_file(m_context.get(), filename.c_str(), NULL));
  }


  inline int Context::get_number_of_top_level_images() const {
    return heif_context_get_number_of_top_level_images(m_context.get());
  }

  inline bool Context::is_top_level_image_ID(heif_item_id id) const {
    return heif_context_is_top_level_image_ID(m_context.get(), id);
  }

  inline std::vector<heif_item_id> Context::get_list_of_top_level_image_IDs() const {
    int num = get_number_of_top_level_images();
    std::vector<heif_item_id> IDs(num);
    heif_context_get_list_of_top_level_image_IDs(m_context.get(), IDs.data(), num);
    return IDs;
  }

  inline Error Context::get_primary_image_ID(heif_item_id* id) {
    return Error(heif_context_get_primary_image_ID(m_context.get(), id));
  }

  inline Error Context::get_primary_image_handle(ImageHandle* out_handle) {
    if (out_handle==nullptr) {
      return Error(heif_context_get_primary_image_handle(m_context.get(), nullptr));
    }
    else {
      heif_image_handle* handle;
      heif_error err = heif_context_get_primary_image_handle(m_context.get(), &handle);
      if (err.code == heif_error_Ok) {
        *out_handle = ImageHandle(handle);
      }

      return Error(err);
    }
  }




  inline ImageHandle::ImageHandle(heif_image_handle* handle) {
    m_image_handle = std::shared_ptr<heif_image_handle>(handle,
                                                        [] (heif_image_handle* h) { heif_image_handle_release(h); });
  }

  inline bool ImageHandle::is_primary_image() const {
    return heif_image_handle_is_primary_image(m_image_handle.get()) != 0;
  }

  inline int ImageHandle::get_width() const {
    return heif_image_handle_get_width(m_image_handle.get());
  }

  inline int ImageHandle::get_height() const {
    return heif_image_handle_get_height(m_image_handle.get());
  }

  inline bool ImageHandle::has_alpha_channel() const {
    return heif_image_handle_has_alpha_channel(m_image_handle.get()) != 0;
  }

  // ------------------------- depth images -------------------------

  // TODO

  // ------------------------- thumbnails -------------------------

  inline int ImageHandle::get_number_of_thumbnails() const {
    return heif_image_handle_get_number_of_thumbnails(m_image_handle.get());
  }

  inline std::vector<heif_item_id> ImageHandle::get_list_of_thumbnail_IDs() const {
    int num = get_number_of_thumbnails();
    std::vector<heif_item_id> IDs(num);
    heif_image_handle_get_list_of_thumbnail_IDs(m_image_handle.get(), IDs.data(), num);
    return IDs;
  }

  inline Error ImageHandle::get_thumbnail(heif_item_id id, ImageHandle* out_handle) {
    if (out_handle==nullptr) {
      return Error(heif_image_handle_get_thumbnail(m_image_handle.get(), id, nullptr));
    }
    else {
      heif_image_handle* handle;
      heif_error err = heif_image_handle_get_thumbnail(m_image_handle.get(), id, &handle);
      if (err.code == heif_error_Ok) {
        *out_handle = ImageHandle(handle);
      }

      return Error(err);
    }
  }

  inline Error ImageHandle::decode_image(Image* output, heif_colorspace colorspace, heif_chroma chroma,
                                         const DecodingOptions& options) {
    if (output==nullptr) {
      return Error(heif_decode_image(m_image_handle.get(), nullptr, colorspace, chroma, nullptr));
    }
    else {
      heif_image* out_img;
      heif_error err = heif_decode_image(m_image_handle.get(),
                                         &out_img,
                                         colorspace,
                                         chroma,
                                         nullptr); //const struct heif_decoding_options* options);
      if (err.code == heif_error_Ok) {
        *output = Image(out_img);
      }

      return Error(err);
    }
  }



  inline Image::Image(heif_image* image) {
    m_image = std::shared_ptr<heif_image>(image,
                                          [] (heif_image* h) { heif_image_release(h); });
  }


  inline Error Image::create(int width, int height,
                             enum heif_colorspace colorspace,
                             enum heif_chroma chroma) {
    heif_image* image;
    heif_error err = heif_image_create(width, height, colorspace, chroma, &image);
    if (err.code == heif_error_Ok) {
      m_image = std::shared_ptr<heif_image>(image,
                                            [] (heif_image* h) { heif_image_release(h); });
    }
    else {
      m_image.reset();
    }

    return Error(err);
  }

  inline Error Image::add_plane(enum heif_channel channel,
                                int width, int height, int bit_depth) {
    return Error(heif_image_add_plane(m_image.get(), channel, width, height, bit_depth));
  }

  inline heif_colorspace Image::get_colorspace() const {
    return heif_image_get_colorspace(m_image.get());
  }

  inline heif_chroma Image::get_chroma_format() const {
    return heif_image_get_chroma_format(m_image.get());
  }

  inline int Image::get_width(enum heif_channel channel) const {
    return heif_image_get_width(m_image.get(), channel);
  }

  inline int Image::get_height(enum heif_channel channel) const {
    return heif_image_get_height(m_image.get(), channel);
  }

  inline int Image::get_bits_per_pixel(enum heif_channel channel) const {
    return heif_image_get_bits_per_pixel(m_image.get(), channel);
  }

  inline const uint8_t* Image::get_plane(enum heif_channel channel, int* out_stride) const {
    return heif_image_get_plane_readonly(m_image.get(), channel, out_stride);
  }

  inline uint8_t* Image::get_plane(enum heif_channel channel, int* out_stride) {
    return heif_image_get_plane(m_image.get(), channel, out_stride);
  }

  inline Error Image::scale_image(Image* output,
                                  int width, int height,
                                  const ScalingOptions& options) const {
    heif_image* img;
    if (output==nullptr) {
      return Error(heif_image_scale_image(m_image.get(), nullptr, width,height, nullptr));
    }
    else {
      heif_error err = heif_image_scale_image(m_image.get(), &img, width,height,
                                              nullptr); // TODO: scaling options not defined yet
      if (err.code == heif_error_Ok) {
        *output = Image(img);
      }

      return Error(err);
    }
  }

}


#endif
