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

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "error.h"

namespace heif {

  class HeifFile;
  class HeifPixelImage;


  class ImageMetadata
  {
  public:
    std::string item_type;  // e.g. "Exif"
    std::vector<uint8_t> m_data;
  };


  // This is a higher-level view than HeifFile.
  // Images are grouped logically into main images and their thumbnails.
  // The class also handles automatic color-space conversion.
  class HeifContext : public ErrorBuffer {
  public:
    HeifContext();
    ~HeifContext();

    Error read_from_file(const char* input_filename);
    Error read_from_memory(const void* data, size_t size);


    class Image : public ErrorBuffer {
    public:
      Image(HeifContext* file, heif_image_id id);
      ~Image();

      void set_resolution(int w,int h) { m_width=w; m_height=h; }

      void set_primary(bool flag=true) { m_is_primary=flag; }

      void set_is_thumbnail_of(heif_image_id id) { m_is_thumbnail=true; m_thumbnail_ref_id=id; }
      void add_thumbnail(std::shared_ptr<Image> img) { m_thumbnails.push_back(img); }

      bool is_thumbnail() const { return m_is_thumbnail; }

      void set_is_alpha_channel_of(heif_image_id id) { m_is_alpha_channel=true; m_alpha_channel_ref_id=id; }
      void set_alpha_channel(std::shared_ptr<Image> img) { m_alpha_channel=img; }

      bool is_alpha_channel() const { return m_is_alpha_channel; }
      std::shared_ptr<Image> get_alpha_channel() const { return m_alpha_channel; }

      heif_image_id get_id() const { return m_id; }

      int get_width() const { return m_width; }
      int get_height() const { return m_height; }

      bool is_primary() const { return m_is_primary; }

      std::vector<std::shared_ptr<Image>> get_thumbnails() const { return m_thumbnails; }

      Error decode_image(std::shared_ptr<HeifPixelImage>& img,
                         heif_colorspace colorspace = heif_colorspace_undefined,
                         heif_chroma chroma = heif_chroma_undefined,
                         class HeifColorConversionParams* config = nullptr) const;

      void add_metadata(std::shared_ptr<ImageMetadata> metadata) {
        m_metadata.push_back(metadata);
      }

      std::vector<std::shared_ptr<ImageMetadata>> get_metadata() const { return m_metadata; }

    private:
      HeifContext* m_heif_context;

      heif_image_id m_id;
      uint32_t m_width=0, m_height=0;
      bool     m_is_primary = false;

      bool     m_is_thumbnail = false;
      heif_image_id m_thumbnail_ref_id;

      std::vector<std::shared_ptr<Image>> m_thumbnails;

      bool m_is_alpha_channel = false;
      heif_image_id m_alpha_channel_ref_id;
      std::shared_ptr<Image> m_alpha_channel;

      std::vector<std::shared_ptr<ImageMetadata>> m_metadata;
    };


    std::vector<std::shared_ptr<Image>> get_top_level_images() { return m_top_level_images; }

    std::shared_ptr<Image> get_primary_image() { return m_primary_image; }

    void register_decoder(const heif_decoder_plugin* decoder_plugin);

    Error decode_image(heif_image_id ID, std::shared_ptr<HeifPixelImage>& img) const;

    std::string debug_dump_boxes() const;

  private:
    const struct heif_decoder_plugin* get_decoder(uint32_t type) const;

    std::set<const struct heif_decoder_plugin*> m_decoder_plugins;

    std::map<heif_image_id, std::shared_ptr<Image>> m_all_images;

    // We store this in a vector because we need stable indices for the C API.
    std::vector<std::shared_ptr<Image>> m_top_level_images;

    std::shared_ptr<Image> m_primary_image; // shortcut to primary image

    std::shared_ptr<HeifFile> m_heif_file;

    Error interpret_heif_file();

    Error decode_full_grid_image(heif_image_id ID,
                                 std::shared_ptr<HeifPixelImage>& img,
                                 const std::vector<uint8_t>& grid_data) const;

    Error decode_derived_image(heif_image_id ID,
                               std::shared_ptr<HeifPixelImage>& img) const;

    Error decode_overlay_image(heif_image_id ID,
                               std::shared_ptr<HeifPixelImage>& img,
                               const std::vector<uint8_t>& overlay_data) const;
  };
}

#endif
