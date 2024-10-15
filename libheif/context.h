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
#include "libheif/heif_experimental.h"
#include "libheif/heif_plugin.h"
#include "bitstream.h"

#include "box.h" // only for color_profile, TODO: maybe move the color_profiles to its own header

#include "region.h"

class HeifFile;

class HeifPixelImage;

class StreamWriter;

class ImageItem;


// This is a higher-level view than HeifFile.
// Images are grouped logically into main images and their thumbnails.
// The class also handles automatic color-space conversion.
class HeifContext : public ErrorBuffer
{
public:
  HeifContext();

  ~HeifContext();

  void set_max_decoding_threads(int max_threads) { m_max_decoding_threads = max_threads; }

  int get_max_decoding_threads() const { return m_max_decoding_threads; }

  void set_security_limits(const heif_security_limits* limits);

  [[nodiscard]] heif_security_limits* get_security_limits() { return &m_limits; }

  [[nodiscard]] const heif_security_limits* get_security_limits() const { return &m_limits; }

  Error read(const std::shared_ptr<StreamReader>& reader);

  Error read_from_file(const char* input_filename);

  Error read_from_memory(const void* data, size_t size, bool copy);

  std::shared_ptr<HeifFile> get_heif_file() const { return m_heif_file; }


  // === image items ===

  std::vector<std::shared_ptr<ImageItem>> get_top_level_images(bool return_error_images);

  void insert_image_item(heif_item_id id, const std::shared_ptr<ImageItem>& img) {
    m_all_images.insert(std::make_pair(id, img));
  }

  std::shared_ptr<ImageItem> get_image(heif_item_id id, bool return_error_images);

  std::shared_ptr<const ImageItem> get_image(heif_item_id id, bool return_error_images) const
  {
    return const_cast<HeifContext*>(this)->get_image(id, return_error_images);
  }

  std::shared_ptr<ImageItem> get_primary_image(bool return_error_image);

  bool is_image(heif_item_id ID) const;

  bool has_alpha(heif_item_id ID) const;

  Result<std::shared_ptr<HeifPixelImage>> decode_image(heif_item_id ID,
                                                       heif_colorspace out_colorspace,
                                                       heif_chroma out_chroma,
                                                       const struct heif_decoding_options& options,
                                                       bool decode_only_tile, uint32_t tx, uint32_t ty) const;

  Error get_id_of_non_virtual_child_image(heif_item_id in, heif_item_id& out) const;

  std::string debug_dump_boxes() const;


  // === writing ===

  void write(StreamWriter& writer);

  // Create all boxes necessary for an empty HEIF file.
  // Note that this is no valid HEIF file, since some boxes (e.g. pitm) are generated, but
  // contain no valid data yet.
  void reset_to_empty_heif();

  Error encode_image(const std::shared_ptr<HeifPixelImage>& image,
                     struct heif_encoder* encoder,
                     const struct heif_encoding_options& options,
                     enum heif_image_input_class input_class,
                     std::shared_ptr<ImageItem>& out_image);

  void set_primary_image(const std::shared_ptr<ImageItem>& image);

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
                             uint32_t item_type, const char* content_type, const char* item_uri_type,
                             heif_metadata_compression compression, heif_item_id* out_item_id);

  heif_property_id add_property(heif_item_id targetItem, std::shared_ptr<Box> property, bool essential);

  Result<heif_item_id> add_pyramid_group(const std::vector<heif_item_id>& layers);

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

private:
  std::map<heif_item_id, std::shared_ptr<ImageItem>> m_all_images;

  // We store this in a vector because we need stable indices for the C API.
  // TODO: stable indices are obsolet now...
  std::vector<std::shared_ptr<ImageItem>> m_top_level_images;

  std::shared_ptr<ImageItem> m_primary_image; // shortcut to primary image

  std::shared_ptr<HeifFile> m_heif_file;

  int m_max_decoding_threads = 4;

  heif_security_limits m_limits;

  std::vector<std::shared_ptr<RegionItem>> m_region_items;

  Error interpret_heif_file();

  void remove_top_level_image(const std::shared_ptr<ImageItem>& image);
};

#endif
