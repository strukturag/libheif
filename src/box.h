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

#ifndef LIBHEIF_BOX_H
#define LIBHEIF_BOX_H

#include <inttypes.h>
#include <vector>
#include <string>
#include <memory>
#include <limits>
#include <istream>

#include "error.h"
#include "logging.h"
#include "bitstream.h"


namespace heif {

  constexpr uint32_t fourcc(const char* string)
  {
    return ((string[0]<<24) |
            (string[1]<<16) |
            (string[2]<< 8) |
            (string[3]));
  }


  class BoxHeader {
  public:
    BoxHeader();
    ~BoxHeader() { }

    constexpr static uint64_t size_until_end_of_file = 0;

    uint64_t get_box_size() const { return m_size; }

    uint32_t get_header_size() const { return m_header_size; }

    uint32_t get_short_type() const { return m_type; }

    std::vector<uint8_t> get_type() const;

    std::string get_type_string() const;

    Error parse(BitstreamRange& range);

    Error write(std::ostream& ostr) const;

    virtual std::string dump(Indent&) const;


    // --- full box

    Error parse_full_box_header(BitstreamRange& range);

    uint8_t get_version() const { return m_version; }

    uint32_t get_flags() const { return m_flags; }

  private:
    uint64_t m_size = 0;
    uint32_t m_header_size = 0;

    uint32_t m_type = 0;
    std::vector<uint8_t> m_uuid_type;


    bool m_is_full_box = false;

    uint8_t m_version = 0;
    uint32_t m_flags = 0;
  };



  class Box : public BoxHeader {
  public:
    Box(const BoxHeader& hdr) : BoxHeader(hdr) { }
    virtual ~Box() { }

    static Error read(BitstreamRange& range, std::shared_ptr<heif::Box>* box);

    virtual Error write(std::ostream& ostr) const { return Error::OK; }

    virtual std::string dump(Indent&) const;

    std::shared_ptr<Box> get_child_box(uint32_t short_type) const;
    std::vector<std::shared_ptr<Box>> get_child_boxes(uint32_t short_type) const;

  protected:
    virtual Error parse(BitstreamRange& range);

    std::vector<std::shared_ptr<Box>> m_children;

    Error read_children(BitstreamRange& range);

    std::string dump_children(Indent&) const;
  };


  class Box_ftyp : public Box {
  public:
  Box_ftyp(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    uint32_t m_major_brand;
    uint32_t m_minor_version;
    std::vector<uint32_t> m_compatible_brands;
  };


  class Box_meta : public Box {
  public:
  Box_meta(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    bool get_images(std::istream& istr, std::vector<std::vector<uint8_t>>* images) const;

  protected:
    Error parse(BitstreamRange& range) override;
  };


  class Box_hdlr : public Box {
  public:
  Box_hdlr(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    uint32_t m_pre_defined;
    uint32_t m_handler_type;
    uint32_t m_reserved[3];
    std::string m_name;
  };


  class Box_pitm : public Box {
  public:
  Box_pitm(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    uint16_t m_item_ID;
  };


  class Box_iloc : public Box {
  public:
  Box_iloc(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    struct Extent {
      uint64_t offset;
      uint64_t length;
    };

    struct Item {
      uint16_t item_ID;
      uint16_t data_reference_index;
      uint64_t base_offset;

      std::vector<Extent> extents;
    };

    const std::vector<Item>& get_items() const { return m_items; }

    bool read_data(const Item& item, std::istream& istr, std::vector<uint8_t>* dest) const;
    bool read_all_data(std::istream& istr, std::vector<uint8_t>* dest) const;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    std::vector<Item> m_items;
  };


  class Box_infe : public Box {
  public:
  Box_infe(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    bool is_hidden_item() const { return m_hidden_item; }

  protected:
    Error parse(BitstreamRange& range) override;

  private:
      uint16_t m_item_ID;
      uint16_t m_item_protection_index;

      std::string m_item_type;
      std::string m_item_name;
      std::string m_content_type;
      std::string m_content_encoding;
      std::string m_item_uri_type;

      // if set, this item should not be part of the presentation (i.e. hidden)
      bool m_hidden_item = false;
    };


  class Box_iinf : public Box {
  public:
  Box_iinf(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    //std::vector< std::shared_ptr<Box_infe> > m_iteminfos;
  };


  class Box_iprp : public Box {
  public:
  Box_iprp(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;
  };


  class Box_ipco : public Box {
  public:
  Box_ipco(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;
  };


  class Box_ispe : public Box {
  public:
  Box_ispe(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    uint32_t m_image_width;
    uint32_t m_image_height;
  };


  class Box_ipma : public Box {
  public:
  Box_ipma(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;

    struct Entry {
      struct PropertyAssociation {
        bool essential;
        uint16_t property_index;
      };

      uint32_t item_ID;
      std::vector<PropertyAssociation> associations;
    };

    std::vector<Entry> m_entries;
  };


  class Box_auxC : public Box {
  public:
  Box_auxC(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    std::string m_aux_type;
    std::vector<uint8_t> m_aux_subtypes;
  };


  class Box_irot : public Box {
  public:
  Box_irot(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    int m_rotation; // in degrees (CCW)
  };


  class Box_iref : public Box {
  public:
  Box_iref(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    struct Reference {
      BoxHeader header;

      uint32_t from_item_ID;
      std::vector<uint32_t> to_item_ID;
    };

    std::vector<Reference> m_references;
  };


  class Box_hvcC : public Box {
  public:
    Box_hvcC(const BoxHeader& hdr) : Box(hdr) {
#if defined(__EMSCRIPTEN__)
      m_general_constraint_indicator_flags.resize(NUM_CONSTRAINT_INDICATOR_FLAGS);
#endif
    }

    std::string dump(Indent&) const override;

    bool get_headers(std::vector<uint8_t>* dest) const;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    static const size_t NUM_CONSTRAINT_INDICATOR_FLAGS = 48;
    uint8_t  m_configuration_version;
    uint8_t  m_general_profile_space;
    bool     m_general_tier_flag;
    uint8_t  m_general_profile_idc;
    uint32_t m_general_profile_compatibility_flags;
#if !defined(__EMSCRIPTEN__)
    std::array<bool,NUM_CONSTRAINT_INDICATOR_FLAGS> m_general_constraint_indicator_flags;
#else
    std::vector<bool> m_general_constraint_indicator_flags;
#endif
    uint8_t  m_general_level_idc;

    uint16_t m_min_spatial_segmentation_idc;
    uint8_t  m_parallelism_type;
    uint8_t  m_chroma_format;
    uint8_t  m_bit_depth_luma;
    uint8_t  m_bit_depth_chroma;
    uint16_t m_avg_frame_rate;

    uint8_t  m_constant_frame_rate;
    uint8_t  m_num_temporal_layers;
    uint8_t  m_temporal_id_nested;
    uint8_t  m_length_size;

    struct NalArray {
      uint8_t m_array_completeness;
      uint8_t m_NAL_unit_type;

      std::vector< std::vector<uint8_t> > m_nal_units;
    };

    std::vector<NalArray> m_nal_array;
  };


  class Box_grpl : public Box {
  public:
  Box_grpl(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;

    struct EntityGroup {
      BoxHeader header;
      uint32_t group_id;

      std::vector<uint32_t> entity_ids;
    };

    std::vector<EntityGroup> m_entity_groups;
  };
}

#endif
