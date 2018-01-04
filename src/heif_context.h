/*
 * HEIF codec.
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of libheif.
 *
 * libheif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libheif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libheif.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBHEIF_HEIF_CONTEXT_H
#define LIBHEIF_HEIF_CONTEXT_H

#include "heif_file.h"

#include <map>
#include <set>


namespace heif {

  // This is a higher-level view than HeifFile.
  // Images are grouped logically into main images and their thumbnails.
  // The class also handles automatic color-space conversion.
  class HeifContext : public ErrorBuffer {
  public:
    HeifContext();
    ~HeifContext();

    Error read_from_file(const char* input_filename);
    Error read_from_memory(const void* data, size_t size);


    class Image {
    public:
      Image(std::shared_ptr<HeifFile> file, uint32_t id);
      ~Image();

      void set_resolution(int w,int h) { m_width=w; m_height=h; }

      void set_primary(bool flag=true) { m_is_primary=flag; }

      void set_is_thumbnail_of(uint32_t id) { m_is_thumbnail=true; m_thumbnail_ref_id=id; }
      void add_thumbnail(std::shared_ptr<Image> img) { m_thumbnails.push_back(img); }

      bool is_thumbnail() const { return m_is_thumbnail; }

      uint32_t get_id() const { return m_id; }

      int get_width() const { return m_width; }
      int get_height() const { return m_height; }

      bool is_primary() const { return m_is_primary; }

      std::vector<std::shared_ptr<Image>> get_thumbnails() const { return m_thumbnails; }

      Error decode_image(std::shared_ptr<HeifPixelImage>& img,
                         heif_colorspace colorspace = heif_colorspace_undefined,
                         heif_chroma chroma = heif_chroma_undefined,
                         class HeifColorConversionParams* config = nullptr) const;

    private:
      std::shared_ptr<HeifFile> m_heif_file;

      uint32_t m_id;
      uint32_t m_width=0, m_height=0;
      bool     m_is_primary = false;

      bool     m_is_thumbnail = false;
      uint32_t m_thumbnail_ref_id;

      std::vector<std::shared_ptr<Image>> m_thumbnails;
    };


    std::vector<std::shared_ptr<Image>> get_top_level_images() { return m_top_level_images; }

    std::shared_ptr<Image> get_primary_image() { return m_primary_image; }

    void register_decoder(uint32_t type, const heif_decoder_plugin* decoder_plugin) {
      // TODO: move plugin registry from HeifFile to HeifContext and decode image in this class
    }

    std::string debug_dump_boxes() const { return m_heif_file->debug_dump_boxes(); }

  private:
    std::map<uint32_t, std::shared_ptr<Image>> m_all_images;

    // We store this in a vector because we need stable indices for the C API.
    std::vector<std::shared_ptr<Image>> m_top_level_images;

    std::shared_ptr<Image> m_primary_image; // shortcut to primary image

    std::shared_ptr<HeifFile> m_heif_file;

    Error interpret_heif_file();
  };
}

#endif
