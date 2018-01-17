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

#ifndef LIBHEIF_HEIF_FILE_H
#define LIBHEIF_HEIF_FILE_H

#include "box.h"

#include <map>
#include <memory>
#include <string>
#include <map>
#include <vector>

namespace heif {

  class HeifPixelImage;
  class HeifImage;


  class HeifFile {
  public:
    HeifFile();
    ~HeifFile();

    Error read_from_file(const char* input_filename);
    Error read_from_memory(const void* data, size_t size);

    int get_num_images() const { return m_images.size(); }

    heif_image_id get_primary_image_ID() const { return m_primary_image_ID; }

    std::vector<heif_image_id> get_item_IDs() const;

    bool image_exists(heif_image_id ID) const;

    std::string get_item_type(heif_image_id ID) const;

    Error get_compressed_image_data(heif_image_id ID, std::vector<uint8_t>* out_data) const;



    std::shared_ptr<Box_infe> get_infe_box(heif_image_id imageID) {
      auto iter = m_images.find(imageID);
      if (iter == m_images.end()) {
        return nullptr;
      }

      return iter->second.m_infe_box;
    }

    std::shared_ptr<Box_iref> get_iref_box() { return m_iref_box; }

    std::shared_ptr<Box_ipco> get_ipco_box() { return m_ipco_box; }

    std::shared_ptr<Box_ipma> get_ipma_box() { return m_ipma_box; }

    Error get_properties(heif_image_id imageID,
                         std::vector<Box_ipco::Property>& properties) const;

    std::string debug_dump_boxes() const;

  private:
    std::unique_ptr<std::istream> m_input_stream;

    std::vector<std::shared_ptr<Box> > m_top_level_boxes;

    std::shared_ptr<Box_ftyp> m_ftyp_box;
    std::shared_ptr<Box_meta> m_meta_box;

    std::shared_ptr<Box_ipco> m_ipco_box;
    std::shared_ptr<Box_ipma> m_ipma_box;
    std::shared_ptr<Box_iloc> m_iloc_box;
    std::shared_ptr<Box_idat> m_idat_box;
    std::shared_ptr<Box_iref> m_iref_box;

    struct Image {
      std::shared_ptr<Box_infe> m_infe_box;
    };

    std::map<heif_image_id, Image> m_images;  // map from image ID to info structure

    // list of image items (does not include hidden images or Exif data)
    std::vector<heif_image_id> m_valid_image_IDs;

    heif_image_id m_primary_image_ID;


    Error parse_heif_file(BitstreamRange& bitstream);

    bool get_image_info(heif_image_id ID, const Image** image) const;
  };

}

#endif
