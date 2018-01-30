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

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#if defined(HAVE_INTTYPES_H)
#include <inttypes.h>
#endif
#if defined(HAVE_STDDEF_H)
#include <stddef.h>
#endif
#include <vector>
#include <string>
#include <memory>
#include <limits>
#include <istream>

#include "error.h"
#include "heif.h"
#include "logging.h"
#include "bitstream.h"

#if !defined(__EMSCRIPTEN__) && !defined(_MSC_VER)
// std::array<bool> is not supported on some older compilers.
#define HAS_BOOL_ARRAY 1
#endif

namespace heif {

  constexpr uint32_t fourcc(const char* string)
  {
    return ((string[0]<<24) |
            (string[1]<<16) |
            (string[2]<< 8) |
            (string[3]));
  }


  class Fraction {
  public:
    Fraction() { }
  Fraction(int num,int den) : numerator(num), denominator(den) { }

    Fraction operator+(const Fraction&) const;
    Fraction operator-(const Fraction&) const;
    Fraction operator-(int) const;
    Fraction operator/(int) const;

    int round_down() const;
    int round_up() const;
    int round() const;

    int numerator, denominator;
  };


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

    virtual std::string dump(Indent&) const;


    // --- full box

    Error parse_full_box_header(BitstreamRange& range);

    uint8_t get_version() const { return m_version; }

    uint32_t get_flags() const { return m_flags; }



    // --- writing

    Error prepend_header(StreamWriter&, bool full_header) const;

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

    virtual Error write(std::ostream& ostr) const { return Error::Ok; }

    virtual std::string dump(Indent&) const;

    std::shared_ptr<Box> get_child_box(uint32_t short_type) const;
    std::vector<std::shared_ptr<Box>> get_child_boxes(uint32_t short_type) const;

    const std::vector<std::shared_ptr<Box>>& get_all_child_boxes() const { return m_children; }

  protected:
    virtual Error parse(BitstreamRange& range);

    std::vector<std::shared_ptr<Box>> m_children;

    const static int READ_CHILDREN_ALL = -1;

    Error read_children(BitstreamRange& range, int number = READ_CHILDREN_ALL);

    std::string dump_children(Indent&) const;
  };


  class Box_ftyp : public Box {
  public:
  Box_ftyp(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    bool has_compatible_brand(uint32_t brand) const;

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

    //bool get_images(std::istream& istr, std::vector<std::vector<uint8_t>>* images) const;

  protected:
    Error parse(BitstreamRange& range) override;
  };


  class Box_hdlr : public Box {
  public:
  Box_hdlr(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    uint32_t get_handler_type() const { return m_handler_type; }

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

    heif_image_id get_item_ID() const { return m_item_ID; }

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    heif_image_id m_item_ID;
  };


  class Box_iloc : public Box {
  public:
  Box_iloc(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    struct Extent {
      uint64_t index = 0;
      uint64_t offset;
      uint64_t length;
    };

    struct Item {
      heif_image_id item_ID;
      uint8_t  construction_method = 0; // >= V1
      uint16_t data_reference_index;
      uint64_t base_offset = 0;

      std::vector<Extent> extents;
    };

    const std::vector<Item>& get_items() const { return m_items; }

    Error read_data(const Item& item, std::istream& istr,
                    const std::shared_ptr<class Box_idat>&,
                    std::vector<uint8_t>* dest) const;
    //Error read_all_data(std::istream& istr, std::vector<uint8_t>* dest) const;

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

    heif_image_id get_item_ID() const { return m_item_ID; }

    std::string get_item_type() const { return m_item_type; }

  protected:
    Error parse(BitstreamRange& range) override;

  private:
      heif_image_id m_item_ID;
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

    struct Property {
      bool essential;
      std::shared_ptr<Box> property;
    };

    Error get_properties_for_item_ID(heif_image_id itemID,
                                     const std::shared_ptr<class Box_ipma>&,
                                     std::vector<Property>& out_properties) const;

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;
  };


  class Box_ispe : public Box {
  public:
  Box_ispe(const BoxHeader& hdr) : Box(hdr) { }

    uint32_t get_width() const { return m_image_width; }
    uint32_t get_height() const { return m_image_height; }

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

    struct PropertyAssociation {
      bool essential;
      uint16_t property_index;
    };

    const std::vector<PropertyAssociation>* get_properties_for_item_ID(heif_image_id itemID) const;

  protected:
    Error parse(BitstreamRange& range) override;

    struct Entry {
      heif_image_id item_ID;
      std::vector<PropertyAssociation> associations;
    };

    std::vector<Entry> m_entries;
  };


  class Box_auxC : public Box {
  public:
  Box_auxC(const BoxHeader& hdr) : Box(hdr) { }

    std::string get_aux_type() const { return m_aux_type; }

    std::vector<uint8_t> get_subtypes() const { return m_aux_subtypes; }

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

    int get_rotation() const { return m_rotation; }

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    int m_rotation; // in degrees (CCW)
  };


  class Box_imir : public Box {
  public:
  Box_imir(const BoxHeader& hdr) : Box(hdr) { }

    enum class MirrorAxis : uint8_t {
      Vertical = 0,
      Horizontal = 1
    };

    MirrorAxis get_mirror_axis() const { return m_axis; }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    MirrorAxis m_axis;
  };


  class Box_clap : public Box {
  public:
  Box_clap(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    int left_rounded(int image_width) const;  // first column
    int right_rounded(int image_width) const; // last column that is part of the cropped image
    int top_rounded(int image_height) const;   // first row
    int bottom_rounded(int image_height) const; // last row included in the cropped image

    int get_width_rounded() const;
    int get_height_rounded() const;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    Fraction m_clean_aperture_width;
    Fraction m_clean_aperture_height;
    Fraction m_horizontal_offset;
    Fraction m_vertical_offset;
  };


  class Box_iref : public Box {
  public:
  Box_iref(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    bool has_references(heif_image_id itemID) const;
    uint32_t get_reference_type(heif_image_id itemID) const;
    std::vector<heif_image_id> get_references(heif_image_id itemID) const;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    struct Reference {
      BoxHeader header;

      heif_image_id from_item_ID;
      std::vector<heif_image_id> to_item_ID;
    };

    std::vector<Reference> m_references;
  };


  class Box_hvcC : public Box {
  public:
    Box_hvcC(const BoxHeader& hdr) : Box(hdr) {
#if !defined(HAS_BOOL_ARRAY)
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
#if defined(HAS_BOOL_ARRAY)
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


  class Box_idat : public Box {
  public:
  Box_idat(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    Error read_data(std::istream& istr, uint64_t start, uint64_t length,
                    std::vector<uint8_t>& out_data) const;

  protected:
    Error parse(BitstreamRange& range) override;

    std::streampos m_data_start_pos;
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

      std::vector<heif_image_id> entity_ids;
    };

    std::vector<EntityGroup> m_entity_groups;
  };


  class Box_dinf : public Box {
  public:
  Box_dinf(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;
  };


  class Box_dref : public Box {
  public:
  Box_dref(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;
  };

  class Box_url : public Box {
  public:
  Box_url(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;

    std::string m_location;
  };

}

#endif
