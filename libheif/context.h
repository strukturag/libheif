/*
 * HEIF codec.
 * Copyright (c) 2017 Dirk Farin <dirk.farin@gmail.com>
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

#ifndef LIBHEIF_CONTEXT_H
#define LIBHEIF_CONTEXT_H

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <utility>

#include "error.h"

#include "libheif/heif.h"
#include "libheif/heif_plugin.h"
#include "bitstream.h"

#include "box.h" // only for color_profile, TODO: maybe move the color_profiles to its own header

#include "region.h"

class HeifContext;

class HeifFile;

class HeifPixelImage;

class StreamWriter;


class ImageMetadata
{
public:
  heif_item_id item_id;
  std::string item_type;  // e.g. "Exif"
  std::string content_type;
  std::string item_uri_type;
  std::vector<uint8_t> m_data;
};


class ImageGrid
{
public:
  Error parse(const std::vector<uint8_t>& data);

  std::vector<uint8_t> write() const;

  std::string dump() const;

  uint32_t get_width() const { return m_output_width; }

  uint32_t get_height() const { return m_output_height; }

  uint16_t get_rows() const
  {
    return m_rows;
  }

  uint16_t get_columns() const
  {
    return m_columns;
  }

  void set_num_tiles(uint16_t columns, uint16_t rows)
  {
    m_rows = rows;
    m_columns = columns;
  }

  void set_output_size(uint32_t width, uint32_t height)
  {
    m_output_width = width;
    m_output_height = height;
  }

private:
  uint16_t m_rows = 0;
  uint16_t m_columns = 0;
  uint32_t m_output_width = 0;
  uint32_t m_output_height = 0;
};




// This is a higher-level view than HeifFile.
// Images are grouped logically into main images and their thumbnails.
// The class also handles automatic color-space conversion.
class HeifContext : public ErrorBuffer
{
public:
  HeifContext();

  ~HeifContext();

  void set_max_decoding_threads(int max_threads) { m_max_decoding_threads = max_threads; }

  // Sets the maximum size of both width and height of an image. The total limit
  // of the image size (width * height) will be "maximum_size * maximum_size".
  void set_maximum_image_size_limit(int maximum_size)
  {
    m_maximum_image_size_limit = int64_t(maximum_size) * maximum_size;
  }

  Error read(const std::shared_ptr<StreamReader>& reader);

  Error read_from_file(const char* input_filename);

  Error read_from_memory(const void* data, size_t size, bool copy);

  class Image : public ErrorBuffer
  {
  public:
    Image(HeifContext* file, heif_item_id id);

    ~Image();

    void clear()
    {
      m_thumbnails.clear();
      m_alpha_channel.reset();
      m_depth_channel.reset();
      m_aux_images.clear();
    }

    Error check_resolution(uint32_t w, uint32_t h) const {
      return m_heif_context->check_resolution(w, h);
    }

    void set_resolution(int w, int h)
    {
      m_width = w;
      m_height = h;
    }

    void set_primary(bool flag = true) { m_is_primary = flag; }

    heif_item_id get_id() const { return m_id; }

    //void set_id(heif_item_id id) { m_id=id; }  (already set in constructor)

    int get_width() const { return m_width; }

    int get_height() const { return m_height; }

    int get_ispe_width() const;

    int get_ispe_height() const;

    int get_luma_bits_per_pixel() const;

    int get_chroma_bits_per_pixel() const;

    Error get_preferred_decoding_colorspace(heif_colorspace* out_colorspace, heif_chroma* out_chroma) const;

    bool is_primary() const { return m_is_primary; }

    void set_size(int w, int h)
    {
      m_width = w;
      m_height = h;
    }


    // -- thumbnails

    void set_is_thumbnail()
    {
      m_is_thumbnail = true;
    }

    void add_thumbnail(const std::shared_ptr<Image>& img) { m_thumbnails.push_back(img); }

    bool is_thumbnail() const { return m_is_thumbnail; }

    const std::vector<std::shared_ptr<Image>>& get_thumbnails() const { return m_thumbnails; }


    // --- alpha channel

    void set_is_alpha_channel()
    {
      m_is_alpha_channel = true;
    }

    void set_alpha_channel(std::shared_ptr<Image> img) { m_alpha_channel = std::move(img); }

    bool is_alpha_channel() const { return m_is_alpha_channel; }

    const std::shared_ptr<Image>& get_alpha_channel() const { return m_alpha_channel; }

    void set_is_premultiplied_alpha(bool flag) { m_premultiplied_alpha = flag; }

    bool is_premultiplied_alpha() const { return m_premultiplied_alpha; }


    // --- depth channel

    void set_is_depth_channel()
    {
      m_is_depth_channel = true;
    }

    void set_depth_channel(std::shared_ptr<Image> img) { m_depth_channel = std::move(img); }

    bool is_depth_channel() const { return m_is_depth_channel; }

    const std::shared_ptr<Image>& get_depth_channel() const { return m_depth_channel; }


    void set_depth_representation_info(struct heif_depth_representation_info& info)
    {
      m_has_depth_representation_info = true;
      m_depth_representation_info = info;
    }

    bool has_depth_representation_info() const
    {
      return m_has_depth_representation_info;
    }

    const struct heif_depth_representation_info& get_depth_representation_info() const
    {
      return m_depth_representation_info;
    }


    // --- generic aux image

    void set_is_aux_image(const std::string& aux_type)
    {
      m_is_aux_image = true;
      m_aux_image_type = aux_type;
    }

    void add_aux_image(std::shared_ptr<Image> img) { m_aux_images.push_back(std::move(img)); }

    bool is_aux_image() const { return m_is_aux_image; }

    const std::string& get_aux_type() const { return m_aux_image_type; }

    std::vector<std::shared_ptr<Image>> get_aux_images(int aux_image_filter = 0) const
    {
      if (aux_image_filter == 0) {
        return m_aux_images;
      }
      else {
        std::vector<std::shared_ptr<Image>> auxImgs;
        for (const auto& aux : m_aux_images) {
          if ((aux_image_filter & LIBHEIF_AUX_IMAGE_FILTER_OMIT_ALPHA) && aux->is_alpha_channel()) {
            continue;
          }

          if ((aux_image_filter & LIBHEIF_AUX_IMAGE_FILTER_OMIT_DEPTH) &&
              aux->is_depth_channel()) {
            continue;
          }

          auxImgs.push_back(aux);
        }

        return auxImgs;
      }
    }


    // --- metadata

    void add_metadata(std::shared_ptr<ImageMetadata> metadata)
    {
      m_metadata.push_back(std::move(metadata));
    }

    const std::vector<std::shared_ptr<ImageMetadata>>& get_metadata() const { return m_metadata; }


    // --- miaf

    void mark_not_miaf_compatible() { m_miaf_compatible = false; }

    bool is_miaf_compatible() const { return m_miaf_compatible; }


    // === writing ===

    void set_preencoded_hevc_image(const std::vector<uint8_t>& data);

    const std::shared_ptr<const color_profile_nclx>& get_color_profile_nclx() const { return m_color_profile_nclx; }

    const std::shared_ptr<const color_profile_raw>& get_color_profile_icc() const { return m_color_profile_icc; }

    void set_color_profile(const std::shared_ptr<const color_profile>& profile)
    {
      auto icc = std::dynamic_pointer_cast<const color_profile_raw>(profile);
      if (icc) {
        m_color_profile_icc = std::move(icc);
      }

      auto nclx = std::dynamic_pointer_cast<const color_profile_nclx>(profile);
      if (nclx) {
        m_color_profile_nclx = std::move(nclx);
      }
    };

    void set_intrinsic_matrix(const Box_cmin::RelativeIntrinsicMatrix& cmin) {
      m_has_intrinsic_matrix = true;
      m_intrinsic_matrix = cmin.to_absolute(get_ispe_width(), get_ispe_height());
    }

    bool has_intrinsic_matrix() const { return m_has_intrinsic_matrix; }

    Box_cmin::AbsoluteIntrinsicMatrix& get_intrinsic_matrix() { return m_intrinsic_matrix; }

    const Box_cmin::AbsoluteIntrinsicMatrix& get_intrinsic_matrix() const { return m_intrinsic_matrix; }


    void set_extrinsic_matrix(const Box_cmex::ExtrinsicMatrix& cmex) {
      m_has_extrinsic_matrix = true;
      m_extrinsic_matrix = cmex;
    }

    bool has_extrinsic_matrix() const { return m_has_extrinsic_matrix; }

    Box_cmex::ExtrinsicMatrix& get_extrinsic_matrix() { return m_extrinsic_matrix; }

    const Box_cmex::ExtrinsicMatrix& get_extrinsic_matrix() const { return m_extrinsic_matrix; }


    void add_region_item_id(heif_item_id id) { m_region_item_ids.push_back(id); }

    const std::vector<heif_item_id>& get_region_item_ids() const { return m_region_item_ids; }

    Error read_grid_spec();

    bool is_grid() const { return m_is_grid; }

    const ImageGrid& get_grid_spec() const { return m_grid_spec; }

    const std::vector<heif_item_id>& get_grid_tiles() const { return m_grid_tile_ids; }

  private:
    HeifContext* m_heif_context;

    heif_item_id m_id = 0;
    uint32_t m_width = 0, m_height = 0;  // after all transformations have been applied
    bool m_is_primary = false;

    bool m_is_thumbnail = false;

    std::vector<std::shared_ptr<Image>> m_thumbnails;

    bool m_is_alpha_channel = false;
    bool m_premultiplied_alpha = false;
    std::shared_ptr<Image> m_alpha_channel;

    bool m_is_depth_channel = false;
    std::shared_ptr<Image> m_depth_channel;

    bool m_has_depth_representation_info = false;
    struct heif_depth_representation_info m_depth_representation_info;

    bool m_is_aux_image = false;
    std::string m_aux_image_type;
    std::vector<std::shared_ptr<Image>> m_aux_images;

    std::vector<std::shared_ptr<ImageMetadata>> m_metadata;

    std::shared_ptr<const color_profile_nclx> m_color_profile_nclx;
    std::shared_ptr<const color_profile_raw> m_color_profile_icc;

    bool m_miaf_compatible = true;

    std::vector<heif_item_id> m_region_item_ids;

    bool m_has_intrinsic_matrix = false;
    Box_cmin::AbsoluteIntrinsicMatrix m_intrinsic_matrix{};

    bool m_has_extrinsic_matrix = false;
    Box_cmex::ExtrinsicMatrix m_extrinsic_matrix{};

    bool m_is_grid = false;
    ImageGrid m_grid_spec;
    std::vector<heif_item_id> m_grid_tile_ids;
  };

  Error check_resolution(uint32_t width, uint32_t height) const;

  std::shared_ptr<HeifFile> get_heif_file() const { return m_heif_file; }

  std::vector<std::shared_ptr<Image>> get_top_level_images() { return m_top_level_images; }

  std::shared_ptr<Image> get_top_level_image(heif_item_id id)
  {
    for (auto& img : m_top_level_images) {
      if (img->get_id() == id) {
        return img;
      }
    }

    return nullptr;
  }

  std::shared_ptr<const Image> get_top_level_image(heif_item_id id) const
  {
    return const_cast<HeifContext*>(this)->get_top_level_image(id);
  }

  std::shared_ptr<Image> get_image(heif_item_id id)
  {
    auto iter = m_all_images.find(id);
    if (iter == m_all_images.end()) {
      return nullptr;
    }
    else {
      return iter->second;
    }
  }

  std::shared_ptr<const Image> get_image(heif_item_id id) const
  {
    return const_cast<HeifContext*>(this)->get_image(id);
  }

  std::shared_ptr<Image> get_primary_image() { return m_primary_image; }

  bool is_image(heif_item_id ID) const;

  bool has_alpha(heif_item_id ID) const;

  Error decode_image_user(heif_item_id ID, std::shared_ptr<HeifPixelImage>& img,
                          heif_colorspace out_colorspace,
                          heif_chroma out_chroma,
                          const struct heif_decoding_options& options) const;

  Error decode_image_planar(heif_item_id ID, std::shared_ptr<HeifPixelImage>& img,
                            heif_colorspace out_colorspace,
                            const struct heif_decoding_options& options,
                            bool alphaImage) const;

  std::string debug_dump_boxes() const;


  // === writing ===

  // Create all boxes necessary for an empty HEIF file.
  // Note that this is no valid HEIF file, since some boxes (e.g. pitm) are generated, but
  // contain no valid data yet.
  void reset_to_empty_heif();

  Error encode_image(const std::shared_ptr<HeifPixelImage>& image,
                     struct heif_encoder* encoder,
                     const struct heif_encoding_options& options,
                     enum heif_image_input_class input_class,
                     std::shared_ptr<Image>& out_image);

  Error encode_grid(const std::vector<std::shared_ptr<HeifPixelImage>>& tiles,
                    uint16_t rows,
                    uint16_t columns,
                    struct heif_encoder* encoder,
                    const struct heif_encoding_options& options,
                    std::shared_ptr<Image>& out_image);

  Error add_grid_item(const std::vector<heif_item_id>& tile_ids,
                      uint32_t output_width,
                      uint32_t output_height,
                      uint16_t tile_rows,
                      uint16_t tile_columns,
                      std::shared_ptr<Image>& out_grid_image);

  Error encode_image_as_hevc(const std::shared_ptr<HeifPixelImage>& image,
                             struct heif_encoder* encoder,
                             const struct heif_encoding_options& options,
                             enum heif_image_input_class input_class,
                             std::shared_ptr<Image>& out_image);

  Error encode_image_as_vvc(const std::shared_ptr<HeifPixelImage>& image,
                             struct heif_encoder* encoder,
                             const struct heif_encoding_options& options,
                             enum heif_image_input_class input_class,
                             std::shared_ptr<Image>& out_image);

  Error encode_image_as_av1(const std::shared_ptr<HeifPixelImage>& image,
                            struct heif_encoder* encoder,
                            const struct heif_encoding_options& options,
                            enum heif_image_input_class input_class,
                            std::shared_ptr<Image>& out_image);

  Error encode_image_as_jpeg(const std::shared_ptr<HeifPixelImage>& image,
                             struct heif_encoder* encoder,
                             const struct heif_encoding_options& options,
                             enum heif_image_input_class input_class,
                             std::shared_ptr<Image>& out_image);

  Error encode_image_as_jpeg2000(const std::shared_ptr<HeifPixelImage>& image,
                                 struct heif_encoder* encoder,
                                 const struct heif_encoding_options& options,
                                 enum heif_image_input_class input_class,
                                 std::shared_ptr<Image>& out_image);

  Error encode_image_as_uncompressed(const std::shared_ptr<HeifPixelImage>& src_image,
                                     struct heif_encoder* encoder,
                                     const struct heif_encoding_options& options,
                                     enum heif_image_input_class input_class,
                                     std::shared_ptr<Image>& out_image);

  Error encode_image_as_mask(const std::shared_ptr<HeifPixelImage>& src_image,
                             struct heif_encoder* encoder,
                             const struct heif_encoding_options& options,
                             enum heif_image_input_class input_class,
                             std::shared_ptr<Image>& out_image);

  // write PIXI, CLLI, MDVC
  void write_image_metadata(std::shared_ptr<HeifPixelImage> src_image, int image_id);

  void set_primary_image(const std::shared_ptr<Image>& image);

  Error set_primary_item(heif_item_id id);

  bool is_primary_image_set() const { return m_primary_image != nullptr; }

  Error assign_thumbnail(const std::shared_ptr<Image>& master_image,
                         const std::shared_ptr<Image>& thumbnail_image);

  Error encode_thumbnail(const std::shared_ptr<HeifPixelImage>& image,
                         struct heif_encoder* encoder,
                         const struct heif_encoding_options& options,
                         int bbox_size,
                         std::shared_ptr<Image>& out_image_handle);

  Error add_exif_metadata(const std::shared_ptr<Image>& master_image, const void* data, int size);

  Error add_XMP_metadata(const std::shared_ptr<Image>& master_image, const void* data, int size, heif_metadata_compression compression);

  Error add_generic_metadata(const std::shared_ptr<Image>& master_image, const void* data, int size,
                             const char* item_type, const char* content_type, const char* item_uri_type,
                             heif_metadata_compression compression, heif_item_id* out_item_id);

  heif_property_id add_property(heif_item_id targetItem, std::shared_ptr<Box> property, bool essential);

  Result<heif_item_id> add_pyramid_group(uint16_t tile_size_x, uint16_t tile_size_y,
                                         std::vector<heif_pyramid_layer_info> layers);

  // --- region items

  void add_region_item(std::shared_ptr<RegionItem> region_item)
  {
    m_region_items.push_back(std::move(region_item));
  }

  std::shared_ptr<RegionItem> add_region_item(uint32_t reference_width, uint32_t reference_height);

  std::shared_ptr<RegionItem> get_region_item(heif_item_id id) const
  {
    for (auto& item : m_region_items) {
      if (item->item_id == id)
        return item;
    }

    return nullptr;
  }

  void add_region_referenced_mask_ref(heif_item_id region_item_id, heif_item_id mask_item_id);

  void write(StreamWriter& writer);

private:
  std::map<heif_item_id, std::shared_ptr<Image>> m_all_images;

  // We store this in a vector because we need stable indices for the C API.
  // TODO: stable indices are obsolet now...
  std::vector<std::shared_ptr<Image>> m_top_level_images;

  std::shared_ptr<Image> m_primary_image; // shortcut to primary image

  std::shared_ptr<HeifFile> m_heif_file;

  int m_max_decoding_threads = 4;

  // Maximum image size in pixels (width * height).
  uint64_t m_maximum_image_size_limit;

  std::vector<std::shared_ptr<RegionItem>> m_region_items;

  Error interpret_heif_file();

  void remove_top_level_image(const std::shared_ptr<Image>& image);

  Error decode_full_grid_image(heif_item_id ID,
                               std::shared_ptr<HeifPixelImage>& img,
                               const heif_decoding_options& options) const;

  Error decode_and_paste_tile_image(heif_item_id tileID,
                                    const std::shared_ptr<HeifPixelImage>& out_image,
                                    uint32_t x0, uint32_t y0,
                                    const heif_decoding_options& options) const;

  Error decode_derived_image(heif_item_id ID,
                             std::shared_ptr<HeifPixelImage>& img,
                             const heif_decoding_options& options) const;

  Error decode_overlay_image(heif_item_id ID,
                             std::shared_ptr<HeifPixelImage>& img,
                             const std::vector<uint8_t>& overlay_data,
                             const heif_decoding_options& options) const;

  Error get_id_of_non_virtual_child_image(heif_item_id in, heif_item_id& out) const;
};

#endif
