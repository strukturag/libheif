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

class ImageItem;
class ImageOverlay;



// TODO: move to image_item codecs
bool nclx_profile_matches_spec(heif_colorspace colorspace,
                               std::shared_ptr<const color_profile_nclx> image_nclx,
                               const struct heif_color_profile_nclx* spec_nclx);


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

  Error check_resolution(uint32_t width, uint32_t height) const;

  std::shared_ptr<HeifFile> get_heif_file() const { return m_heif_file; }

  std::vector<std::shared_ptr<ImageItem>> get_top_level_images() { return m_top_level_images; }

  std::shared_ptr<ImageItem> get_top_level_image(heif_item_id id);

  std::shared_ptr<const ImageItem> get_top_level_image(heif_item_id id) const
  {
    return const_cast<HeifContext*>(this)->get_top_level_image(id);
  }

  std::shared_ptr<ImageItem> get_image(heif_item_id id)
  {
    auto iter = m_all_images.find(id);
    if (iter == m_all_images.end()) {
      return nullptr;
    }
    else {
      return iter->second;
    }
  }

  std::shared_ptr<const ImageItem> get_image(heif_item_id id) const
  {
    return const_cast<HeifContext*>(this)->get_image(id);
  }

  std::shared_ptr<ImageItem> get_primary_image() { return m_primary_image; }

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
                     std::shared_ptr<ImageItem>& out_image);

  Error encode_grid(const std::vector<std::shared_ptr<HeifPixelImage>>& tiles,
                    uint16_t rows,
                    uint16_t columns,
                    struct heif_encoder* encoder,
                    const struct heif_encoding_options& options,
                    std::shared_ptr<ImageItem>& out_image);

  Error add_grid_item(const std::vector<heif_item_id>& tile_ids,
                      uint32_t output_width,
                      uint32_t output_height,
                      uint16_t tile_rows,
                      uint16_t tile_columns,
                      std::shared_ptr<ImageItem>& out_grid_image);

  Error add_iovl_item(const ImageOverlay& overlayspec,
                      std::shared_ptr<ImageItem>& out_iovl_image);

  Result<std::shared_ptr<ImageItem>> add_tild_item(const heif_tild_image_parameters* parameters);

  Error add_tild_image_tile(heif_item_id tild_id, uint32_t tile_x, uint32_t tile_y,
                            const std::shared_ptr<HeifPixelImage>& image,
                            struct heif_encoder* encoder);

  Error encode_image_as_jpeg2000(const std::shared_ptr<HeifPixelImage>& image,
                                 struct heif_encoder* encoder,
                                 const struct heif_encoding_options& options,
                                 enum heif_image_input_class input_class,
                                 std::shared_ptr<ImageItem>& out_image);

  Error encode_image_as_mask(const std::shared_ptr<HeifPixelImage>& src_image,
                             struct heif_encoder* encoder,
                             const struct heif_encoding_options& options,
                             enum heif_image_input_class input_class,
                             std::shared_ptr<ImageItem>& out_image);

  // write PIXI, CLLI, MDVC
  void write_image_metadata(std::shared_ptr<HeifPixelImage> src_image, heif_item_id image_id);

  void set_primary_image(const std::shared_ptr<ImageItem>& image);

  Error set_primary_item(heif_item_id id);

  bool is_primary_image_set() const { return m_primary_image != nullptr; }

  Error assign_thumbnail(const std::shared_ptr<ImageItem>& master_image,
                         const std::shared_ptr<ImageItem>& thumbnail_image);

  Error encode_thumbnail(const std::shared_ptr<HeifPixelImage>& image,
                         struct heif_encoder* encoder,
                         const struct heif_encoding_options& options,
                         int bbox_size,
                         std::shared_ptr<ImageItem>& out_image_handle);

  Error add_exif_metadata(const std::shared_ptr<ImageItem>& master_image, const void* data, int size);

  Error add_XMP_metadata(const std::shared_ptr<ImageItem>& master_image, const void* data, int size, heif_metadata_compression compression);

  Error add_generic_metadata(const std::shared_ptr<ImageItem>& master_image, const void* data, int size,
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

  Error get_id_of_non_virtual_child_image(heif_item_id in, heif_item_id& out) const;

private:
  std::map<heif_item_id, std::shared_ptr<ImageItem>> m_all_images;

  // We store this in a vector because we need stable indices for the C API.
  // TODO: stable indices are obsolet now...
  std::vector<std::shared_ptr<ImageItem>> m_top_level_images;

  std::shared_ptr<ImageItem> m_primary_image; // shortcut to primary image

  std::shared_ptr<HeifFile> m_heif_file;

  int m_max_decoding_threads = 4;

  // Maximum image size in pixels (width * height).
  uint64_t m_maximum_image_size_limit;

  std::vector<std::shared_ptr<RegionItem>> m_region_items;

  Error interpret_heif_file();

  void remove_top_level_image(const std::shared_ptr<ImageItem>& image);

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
};

#endif
