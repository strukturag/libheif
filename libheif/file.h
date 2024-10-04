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

#ifndef LIBHEIF_FILE_H
#define LIBHEIF_FILE_H

#include "box.h"
#include "nclx.h"
#include "codecs/avif.h"
#include "codecs/hevc.h"
#include "codecs/vvc.h"
#include "codecs/uncompressed/unc_boxes.h"
#include "file_layout.h"

#include <map>
#include <memory>
#include <string>
#include <map>
#include <vector>
#include <unordered_set>
#include <limits>

#if ENABLE_PARALLEL_TILE_DECODING

#include <mutex>

#endif


class HeifPixelImage;

class Box_j2kH;


class HeifFile
{
public:
  HeifFile();

  ~HeifFile();

  Error read(const std::shared_ptr<StreamReader>& reader);

  Error read_from_file(const char* input_filename);

  Error read_from_memory(const void* data, size_t size, bool copy);

  std::shared_ptr<StreamReader> get_reader() { return m_input_stream; }

  void new_empty_file();

  void set_brand(heif_compression_format format, bool miaf_compatible);

  void write(StreamWriter& writer);

  int get_num_images() const { return static_cast<int>(m_infe_boxes.size()); }

  heif_item_id get_primary_image_ID() const { return m_pitm_box->get_item_ID(); }

  size_t get_number_of_items() const { return m_infe_boxes.size(); }

  std::vector<heif_item_id> get_item_IDs() const;

  bool image_exists(heif_item_id ID) const;

  bool has_item_with_id(heif_item_id ID) const;

  uint32_t get_item_type_4cc(heif_item_id ID) const;

  std::string get_content_type(heif_item_id ID) const;

  std::string get_item_uri_type(heif_item_id ID) const;

  Error get_uncompressed_item_data(heif_item_id ID, std::vector<uint8_t>* data) const;

  Error append_data_from_iloc(heif_item_id ID, std::vector<uint8_t>& out_data, uint64_t offset, uint64_t size) const;

  Error append_data_from_iloc(heif_item_id ID, std::vector<uint8_t>& out_data) const {
    return append_data_from_iloc(ID, out_data, 0, std::numeric_limits<uint64_t>::max());
  }

  Error get_item_data(heif_item_id ID, std::vector<uint8_t> *out_data, heif_metadata_compression* out_compression) const;

  std::shared_ptr<Box_ftyp> get_ftyp_box() { return m_ftyp_box; }

  std::shared_ptr<const Box_infe> get_infe_box(heif_item_id imageID) const;

  std::shared_ptr<Box_infe> get_infe_box(heif_item_id imageID);

  std::shared_ptr<Box_iref> get_iref_box() { return m_iref_box; }

  std::shared_ptr<const Box_iref> get_iref_box() const { return m_iref_box; }

  std::shared_ptr<Box_ipco> get_ipco_box() { return m_ipco_box; }

  std::shared_ptr<Box_ipco> get_ipco_box() const { return m_ipco_box; }

  std::shared_ptr<Box_ipma> get_ipma_box() { return m_ipma_box; }

  std::shared_ptr<Box_ipma> get_ipma_box() const { return m_ipma_box; }

  std::shared_ptr<Box_grpl> get_grpl_box() const { return m_grpl_box; }

  std::shared_ptr<Box_EntityToGroup> get_entity_group(heif_entity_group_id id);

  Error get_properties(heif_item_id imageID,
                       std::vector<std::shared_ptr<Box>>& properties) const;

  template<class BoxType>
  std::shared_ptr<BoxType> get_property(heif_item_id imageID) const
  {
    std::vector<std::shared_ptr<Box>> properties;
    Error err = get_properties(imageID, properties);
    if (err) {
      return nullptr;
    }

    for (auto& property : properties) {
      if (auto box = std::dynamic_pointer_cast<BoxType>(property)) {
        return box;
      }
    }

    return nullptr;
  }

  std::string debug_dump_boxes() const;


  // --- writing ---

  heif_item_id get_unused_item_id() const;

  heif_item_id add_new_image(uint32_t item_type);

  std::shared_ptr<Box_infe> add_new_infe_box(uint32_t item_type);

  void add_ispe_property(heif_item_id id, uint32_t width, uint32_t height, bool essential);

  // set irot/imir according to heif_orientation
  void add_orientation_properties(heif_item_id id, heif_orientation);

  // TODO: can we remove the 'essential' parameter and take this from the box? Or is that depending on the context?
  heif_property_id add_property(heif_item_id id, const std::shared_ptr<Box>& property, bool essential);

  heif_property_id add_property_without_deduplication(heif_item_id id, const std::shared_ptr<Box>& property, bool essential);

  Result<heif_item_id> add_infe(uint32_t item_type, const uint8_t* data, size_t size);

  Result<heif_item_id> add_infe_mime(const char* content_type, heif_metadata_compression content_encoding, const uint8_t* data, size_t size);

  Result<heif_item_id> add_precompressed_infe_mime(const char* content_type, std::string content_encoding, const uint8_t* data, size_t size);

  Result<heif_item_id> add_infe_uri(const char* item_uri_type, const uint8_t* data, size_t size);

  Error set_item_data(const std::shared_ptr<Box_infe>& item, const uint8_t* data, size_t size, heif_metadata_compression compression);

  Error set_precompressed_item_data(const std::shared_ptr<Box_infe>& item, const uint8_t* data, size_t size, std::string content_encoding);

  void append_iloc_data(heif_item_id id, const std::vector<uint8_t>& nal_packets, uint8_t construction_method);

  void replace_iloc_data(heif_item_id id, uint64_t offset, const std::vector<uint8_t>& data, uint8_t construction_method = 0);

  void set_primary_item_id(heif_item_id id);

  void add_iref_reference(heif_item_id from, uint32_t type,
                          const std::vector<heif_item_id>& to);

  void add_entity_group_box(const std::shared_ptr<Box>& entity_group_box);

  void set_auxC_property(heif_item_id id, const std::string& type);

#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_MSC_VER)
  static std::wstring convert_utf8_path_to_utf16(std::string pathutf8);
#endif

private:
#if ENABLE_PARALLEL_TILE_DECODING
  mutable std::mutex m_read_mutex;
#endif

  std::shared_ptr<FileLayout> m_file_layout;

  std::shared_ptr<StreamReader> m_input_stream;

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
  std::shared_ptr<Box_grpl> m_grpl_box;

  std::shared_ptr<Box_iprp> m_iprp_box;

  std::map<heif_item_id, std::shared_ptr<Box_infe> > m_infe_boxes;

  Error parse_heif_file();

  Error check_for_ref_cycle(heif_item_id ID,
                            const std::shared_ptr<Box_iref>& iref_box) const;

  Error check_for_ref_cycle_recursion(heif_item_id ID,
                                      const std::shared_ptr<Box_iref>& iref_box,
                                      std::unordered_set<heif_item_id>& parent_items) const;
};

#endif
