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
    std::vector<uint8_t> m_data;
  };

  class HeifReader {
   public:
    HeifReader(struct heif_context* ctx, struct heif_reader* reader, void* userdata);

    uint64_t length() const;
    uint64_t position() const;

    bool read(void* data, size_t size);
    bool seek(int64_t position, enum heif_reader_offset offset);

   private:
    struct heif_context* m_ctx;
    struct heif_reader* m_reader;
    void* m_userdata;
  };

  // This is a higher-level view than HeifFile.
  // Images are grouped logically into main images and their thumbnails.
  // The class also handles automatic color-space conversion.
  class HeifContext : public ErrorBuffer {
  public:
    HeifContext();
    ~HeifContext();

    Error read(struct heif_context* ctx, struct heif_reader* reader, void* userdata);
    Error read_from_file(const char* input_filename);
    Error read_from_memory(const void* data, size_t size);

    class Image : public ErrorBuffer {
    public:
      Image(HeifContext* file, heif_item_id id);
      ~Image();

      void set_resolution(int w,int h) { m_width=w; m_height=h; }

      void set_primary(bool flag=true) { m_is_primary=flag; }

      heif_item_id get_id() const { return m_id; }

      //void set_id(heif_item_id id) { m_id=id; }  (already set in constructor)

      int get_width() const { return m_width; }
      int get_height() const { return m_height; }

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
                                 enum heif_image_input_class input_class);

    private:
      HeifContext* m_heif_context;

      heif_item_id m_id;
      uint32_t m_width=0, m_height=0;
      bool     m_is_primary = false;

      bool     m_is_thumbnail = false;
      heif_item_id m_thumbnail_ref_id;

      std::vector<std::shared_ptr<Image>> m_thumbnails;

      bool m_is_alpha_channel = false;
      heif_item_id m_alpha_channel_ref_id;
      std::shared_ptr<Image> m_alpha_channel;

      bool m_is_depth_channel = false;
      heif_item_id m_depth_channel_ref_id;
      std::shared_ptr<Image> m_depth_channel;

      bool m_has_depth_representation_info = false;
      struct heif_depth_representation_info m_depth_representation_info;

      std::vector<std::shared_ptr<ImageMetadata>> m_metadata;
    };


    std::vector<std::shared_ptr<Image>> get_top_level_images() { return m_top_level_images; }

    std::shared_ptr<Image> get_primary_image() { return m_primary_image; }

    void register_decoder(const heif_decoder_plugin* decoder_plugin);

    Error decode_image(heif_item_id ID, std::shared_ptr<HeifPixelImage>& img,
                       const struct heif_decoding_options* options = nullptr) const;

    std::string debug_dump_boxes() const;


    // === writing ===

    // Create all boxes necessary for an empty HEIF file.
    // Note that this is no valid HEIF file, since some boxes (e.g. pitm) are generated, but
    // contain no valid data yet.
    void reset_to_empty_heif();

    std::shared_ptr<Image> add_new_hvc1_image();

    Error add_alpha_image(std::shared_ptr<HeifPixelImage> image,
                          heif_item_id* out_item_id,
                          struct heif_encoder* encoder);

    void set_primary_image(std::shared_ptr<Image> image);

    void write(StreamWriter& writer);

    class ReaderInterface {
     public:
      virtual ~ReaderInterface() {}

      virtual int64_t length() const = 0;
      virtual int64_t position() const = 0;

      virtual bool read(void* data, size_t size) = 0;
      virtual bool seek(int64_t position, enum heif_reader_offset offset) = 0;
    };

    static std::unique_ptr<ReaderInterface> CreateReader(const void* data, size_t size);
    static std::unique_ptr<ReaderInterface> CreateReader(const char* filename);

  private:
    const struct heif_decoder_plugin* get_decoder(enum heif_compression_format type) const;

    std::set<const struct heif_decoder_plugin*> m_decoder_plugins;

    std::map<heif_item_id, std::shared_ptr<Image>> m_all_images;

    // We store this in a vector because we need stable indices for the C API.
    // TODO: stable indices are obsolet now...
    std::vector<std::shared_ptr<Image>> m_top_level_images;

    std::shared_ptr<Image> m_primary_image; // shortcut to primary image

    std::shared_ptr<HeifFile> m_heif_file;
    std::unique_ptr<HeifReader> m_heif_reader;
    struct heif_reader m_internal_reader;

    static int64_t internal_get_length(struct heif_context* ctx,
                                       void* userdata);
    static int64_t internal_get_position(struct heif_context* ctx,
                                         void* userdata);
    static int internal_read(struct heif_context* ctx,
                             void* data,
                             size_t size,
                             void* userdata);
    static int internal_seek(struct heif_context* ctx,
                             int64_t position,
                             enum heif_reader_offset offset,
                             void* userdata);

    class FileReader;
    class MemoryReader;

    std::unique_ptr<ReaderInterface> m_temp_reader;

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
  };
}

#endif
