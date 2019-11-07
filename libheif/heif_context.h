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

#include "heif.h"
#include "heif_plugin.h"
#include "bitstream.h"

#include "box.h" // only for color_profile, TODO: maybe move the color_profiles to its own header

namespace heif {
class HeifContext;
}


namespace heif {

  class HeifFile;
  class HeifPixelImage;
  class StreamWriter;


  class ImageMetadata
  {
  public:
    heif_item_id item_id;
    std::string item_type;  // e.g. "Exif"
    std::string content_type;
    std::vector<uint8_t> m_data;
  };


  // This is a higher-level view than HeifFile.
  // Images are grouped logically into main images and their thumbnails.
  // The class also handles automatic color-space conversion.
  class HeifContext : public ErrorBuffer {
  public:
    HeifContext();
    ~HeifContext();

    void set_max_decoding_threads(int max_threads) { m_max_decoding_threads = max_threads; }

    void set_maximum_image_size_limit(int maximum_size) {
      m_maximum_image_width_limit = maximum_size;
      m_maximum_image_height_limit = maximum_size;
    }

    Error read(std::shared_ptr<StreamReader> reader);
    Error read_from_file(const char* input_filename);
    Error read_from_memory(const void* data, size_t size, bool copy);

    class Image : public ErrorBuffer {
    public:
      Image(HeifContext* file, heif_item_id id);
      ~Image();

      void set_resolution(int w,int h) { m_width=w; m_height=h; }
      void set_ispe_resolution(int w,int h) { m_ispe_width=w; m_ispe_height=h; }

      void set_primary(bool flag=true) { m_is_primary=flag; }

      heif_item_id get_id() const { return m_id; }

      //void set_id(heif_item_id id) { m_id=id; }  (already set in constructor)

      int get_width() const { return m_width; }
      int get_height() const { return m_height; }

      int get_ispe_width() const { return m_ispe_width; }
      int get_ispe_height() const { return m_ispe_height; }

      int get_luma_bits_per_pixel() const;
      int get_chroma_bits_per_pixel() const;

      bool is_primary() const { return m_is_primary; }

      Error decode_image(std::shared_ptr<HeifPixelImage>& img,
                         heif_colorspace colorspace = heif_colorspace_undefined,
                         heif_chroma chroma = heif_chroma_undefined,
                         const struct heif_decoding_options* options = nullptr) const;


      // -- thumbnails

      void set_is_thumbnail_of(heif_item_id id) { m_is_thumbnail=true; m_thumbnail_ref_id=id; }
      void add_thumbnail(std::shared_ptr<Image> img) { m_thumbnails.push_back(img); }

      bool is_thumbnail() const { return m_is_thumbnail; }
      std::vector<std::shared_ptr<Image>> get_thumbnails() const { return m_thumbnails; }


      // --- alpha channel

      void set_is_alpha_channel_of(heif_item_id id) { m_is_alpha_channel=true; m_alpha_channel_ref_id=id; }
      void set_alpha_channel(std::shared_ptr<Image> img) { m_alpha_channel=img; }

      bool is_alpha_channel() const { return m_is_alpha_channel; }
      std::shared_ptr<Image> get_alpha_channel() const { return m_alpha_channel; }


      // --- depth channel

      void set_is_depth_channel_of(heif_item_id id) { m_is_depth_channel=true; m_depth_channel_ref_id=id; }
      void set_depth_channel(std::shared_ptr<Image> img) { m_depth_channel=img; }

      bool is_depth_channel() const { return m_is_depth_channel; }
      std::shared_ptr<Image> get_depth_channel() const { return m_depth_channel; }


      void set_depth_representation_info(struct heif_depth_representation_info& info) {
        m_has_depth_representation_info = true;
        m_depth_representation_info = info;
      }

      bool has_depth_representation_info() const {
        return m_has_depth_representation_info;
      }

      const struct heif_depth_representation_info& get_depth_representation_info() const {
        return m_depth_representation_info;
      }


      // --- metadata

      void add_metadata(std::shared_ptr<ImageMetadata> metadata) {
        m_metadata.push_back(metadata);
      }

      std::vector<std::shared_ptr<ImageMetadata>> get_metadata() const { return m_metadata; }


      // === writing ===

      void set_preencoded_hevc_image(const std::vector<uint8_t>& data);

      Error encode_image_as_hevc(std::shared_ptr<HeifPixelImage> image,
                                 struct heif_encoder* encoder,
                                 const struct heif_encoding_options* options,
                                 enum heif_image_input_class input_class);

      std::shared_ptr<const color_profile> get_color_profile() const { return m_color_profile; }

      void set_color_profile(std::shared_ptr<const color_profile> profile) { m_color_profile = profile; };

    private:
      HeifContext* m_heif_context;

      heif_item_id m_id = 0;
      uint32_t m_width=0, m_height=0;
      uint32_t m_ispe_width=0, m_ispe_height=0; // original image resolution
      bool     m_is_primary = false;

      bool     m_is_thumbnail = false;
      heif_item_id m_thumbnail_ref_id = 0;

      std::vector<std::shared_ptr<Image>> m_thumbnails;

      bool m_is_alpha_channel = false;
      heif_item_id m_alpha_channel_ref_id = 0;
      std::shared_ptr<Image> m_alpha_channel;

      bool m_is_depth_channel = false;
      heif_item_id m_depth_channel_ref_id = 0;
      std::shared_ptr<Image> m_depth_channel;

      bool m_has_depth_representation_info = false;
      struct heif_depth_representation_info m_depth_representation_info;

      std::vector<std::shared_ptr<ImageMetadata>> m_metadata;

      std::shared_ptr<const color_profile> m_color_profile;
    };

    std::vector<std::shared_ptr<Image>> get_top_level_images() { return m_top_level_images; }

    std::shared_ptr<Image> get_primary_image() { return m_primary_image; }

    void register_decoder(const heif_decoder_plugin* decoder_plugin);

    bool is_image(heif_item_id ID) const;

    Error decode_image(heif_item_id ID, std::shared_ptr<HeifPixelImage>& img,
                       const struct heif_decoding_options* options = nullptr) const;

    std::string debug_dump_boxes() const;


    // === writing ===

    // Create all boxes necessary for an empty HEIF file.
    // Note that this is no valid HEIF file, since some boxes (e.g. pitm) are generated, but
    // contain no valid data yet.
    void reset_to_empty_heif();

    Error encode_image(std::shared_ptr<HeifPixelImage> image,
                       struct heif_encoder* encoder,
                       const struct heif_encoding_options* options,
                       enum heif_image_input_class input_class,
                       std::shared_ptr<Image>& out_image);

    void set_primary_image(std::shared_ptr<Image> image);

    Error set_primary_item(heif_item_id id);

    bool  is_primary_image_set() const { return !!m_primary_image; }

    Error assign_thumbnail(std::shared_ptr<Image> master_image,
                           std::shared_ptr<Image> thumbnail_image);

    Error encode_thumbnail(std::shared_ptr<HeifPixelImage> image,
                           struct heif_encoder* encoder,
                           const struct heif_encoding_options* options,
                           int bbox_size,
                           std::shared_ptr<Image>& out_image_handle);

    Error add_exif_metadata(std::shared_ptr<Image> master_image, const void* data, int size);

    Error add_XMP_metadata(std::shared_ptr<Image> master_image, const void* data, int size);

    Error add_generic_metadata(std::shared_ptr<Image> master_image, const void* data, int size,
                               const char* item_type, const char* content_type);

    void write(StreamWriter& writer);

  private:
    const struct heif_decoder_plugin* get_decoder(enum heif_compression_format type) const;

    std::set<const struct heif_decoder_plugin*> m_decoder_plugins;

    std::map<heif_item_id, std::shared_ptr<Image>> m_all_images;

    // We store this in a vector because we need stable indices for the C API.
    // TODO: stable indices are obsolet now...
    std::vector<std::shared_ptr<Image>> m_top_level_images;

    std::shared_ptr<Image> m_primary_image; // shortcut to primary image

    std::shared_ptr<HeifFile> m_heif_file;

    int m_max_decoding_threads = 4;

    uint32_t m_maximum_image_width_limit;
    uint32_t m_maximum_image_height_limit;

    Error interpret_heif_file();

    void remove_top_level_image(std::shared_ptr<Image> image);

    Error decode_full_grid_image(heif_item_id ID,
                                 std::shared_ptr<HeifPixelImage>& img,
                                 const std::vector<uint8_t>& grid_data) const;

    Error decode_and_paste_tile_image(heif_item_id tileID,
                                      std::shared_ptr<HeifPixelImage> out_image,
                                      int x0,int y0) const;

    Error decode_derived_image(heif_item_id ID,
                               std::shared_ptr<HeifPixelImage>& img) const;

    Error decode_overlay_image(heif_item_id ID,
                               std::shared_ptr<HeifPixelImage>& img,
                               const std::vector<uint8_t>& overlay_data) const;

    Error get_id_of_non_virtual_child_image(heif_item_id in, heif_item_id& out) const;
  };
}

#endif
