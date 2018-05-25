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

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include "box.h"

#include <map>
#include <memory>
#include <string>
#include <map>
#include <vector>

#if ENABLE_PARALLEL_TILE_DECODING
#include <mutex>
#endif


namespace heif {

  class HeifPixelImage;
  class HeifImage;
  class HeifReader;


  class HeifFile {
  public:
    HeifFile();
    ~HeifFile();

    Error read(HeifReader* reader);

    void new_empty_file();

    void write(StreamWriter& writer);

    int get_num_images() const { return static_cast<int>(m_infe_boxes.size()); }

    heif_item_id get_primary_image_ID() const { return m_pitm_box->get_item_ID(); }

    std::vector<heif_item_id> get_item_IDs() const;

    bool image_exists(heif_item_id ID) const;

    std::string get_item_type(heif_item_id ID) const;

    Error get_compressed_image_data(heif_item_id ID, std::vector<uint8_t>* out_data) const;



    std::shared_ptr<Box_infe> get_infe_box(heif_item_id imageID) {
      auto iter = m_infe_boxes.find(imageID);
      if (iter == m_infe_boxes.end()) {
        return nullptr;
      }

      return iter->second;
    }

    std::shared_ptr<Box_iref> get_iref_box() { return m_iref_box; }

    std::shared_ptr<Box_ipco> get_ipco_box() { return m_ipco_box; }

    std::shared_ptr<Box_ipma> get_ipma_box() { return m_ipma_box; }

    Error get_properties(heif_item_id imageID,
                         std::vector<Box_ipco::Property>& properties) const;

    std::string debug_dump_boxes() const;


    // --- writing ---

    heif_item_id get_unused_item_id() const;

    heif_item_id add_new_image(const char* item_type);

    void add_hvcC_property(heif_item_id id);
    Error append_hvcC_nal_data(heif_item_id id, const std::vector<uint8_t>& data);
    Error append_hvcC_nal_data(heif_item_id id, const uint8_t* data, size_t size);
    Error set_hvcC_configuration(heif_item_id id, const Box_hvcC::configuration& config);

    void add_ispe_property(heif_item_id id, uint32_t width, uint32_t height);

    void append_iloc_data(heif_item_id id, const std::vector<uint8_t>& nal_packets);
    void append_iloc_data_with_4byte_size(heif_item_id id, const uint8_t* data, size_t size);

    void set_primary_item_id(heif_item_id id);

    void add_iref_reference(heif_item_id from, uint32_t type,
                            std::vector<heif_item_id> to);

    void set_auxC_property(heif_item_id id, std::string type);

  private:
#if ENABLE_PARALLEL_TILE_DECODING
    mutable std::mutex m_read_mutex;
#endif

    HeifReader* m_reader = nullptr;

    std::vector<std::shared_ptr<Box> > m_top_level_boxes;

    std::shared_ptr<Box_ftyp> m_ftyp_box;
    std::shared_ptr<Box_hdlr> m_hdlr_box;
    std::shared_ptr<Box_meta> m_meta_box;

    std::shared_ptr<Box_ipco> m_ipco_box;
    std::shared_ptr<Box_ipma> m_ipma_box;
    std::shared_ptr<Box_iloc> m_iloc_box;
    std::shared_ptr<Box_idat> m_idat_box;
    std::shared_ptr<Box_iref> m_iref_box;
    std::shared_ptr<Box_pitm> m_pitm_box;
    std::shared_ptr<Box_iinf> m_iinf_box;

    std::shared_ptr<Box_iprp> m_iprp_box;

    std::map<heif_item_id, std::shared_ptr<Box_infe> > m_infe_boxes;

    // list of image items (does not include hidden images or Exif data)
    //std::vector<heif_item_id> m_valid_image_IDs;


    Error parse_heif_file(BitstreamRange& bitstream);

    std::shared_ptr<Box_infe> get_infe(heif_item_id ID) const;
  };

}

#endif
