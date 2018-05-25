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
#include <bitset>

#include "error.h"
#include "heif.h"
#include "logging.h"
#include "bitstream.h"

#if !defined(__EMSCRIPTEN__) && !defined(_MSC_VER)
// std::array<bool> is not supported on some older compilers.
#define HAS_BOOL_ARRAY 1
#endif

namespace heif {

  class HeifReader;

#define fourcc(id) (((uint32_t)(id[0])<<24) | (id[1]<<16) | (id[2]<<8) | (id[3]))

  /*
  constexpr uint32_t fourcc(const char* string)
  {
    return ((string[0]<<24) |
            (string[1]<<16) |
            (string[2]<< 8) |
            (string[3]));
  }
  */


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

    void set_short_type(uint32_t type) { m_type = type; }


    Error parse(BitstreamRange& range);

    virtual std::string dump(Indent&) const;


    // --- full box

    Error parse_full_box_header(BitstreamRange& range);

    uint8_t get_version() const { return m_version; }

    void set_version(uint8_t version) { m_version=version; }

    uint32_t get_flags() const { return m_flags; }

    void set_flags(uint32_t flags) { m_flags = flags; }

    void set_is_full_box(bool flag=true) { m_is_full_box=flag; }

    bool is_full_box_header() const { return m_is_full_box; }


    // --- writing

    size_t reserve_box_header_space(StreamWriter& writer) const;
    Error prepend_header(StreamWriter&, size_t box_start) const;

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
    Box() { }
    Box(const BoxHeader& hdr) : BoxHeader(hdr) { }
    virtual ~Box() { }

    static Error read(BitstreamRange& range, std::shared_ptr<heif::Box>* box);

    virtual Error write(StreamWriter& writer) const;

    // check, which box version is required and set this in the (full) box header
    virtual void derive_box_version() { set_version(0); }

    void derive_box_version_recursive();

    virtual std::string dump(Indent&) const;

    std::shared_ptr<Box> get_child_box(uint32_t short_type) const;
    std::vector<std::shared_ptr<Box>> get_child_boxes(uint32_t short_type) const;

    const std::vector<std::shared_ptr<Box>>& get_all_child_boxes() const { return m_children; }

    int append_child_box(std::shared_ptr<Box> box) {
      m_children.push_back(box);
      return (int)m_children.size()-1;
    }

  protected:
    virtual Error parse(BitstreamRange& range);

    std::vector<std::shared_ptr<Box>> m_children;

    const static int READ_CHILDREN_ALL = -1;

    Error read_children(BitstreamRange& range, int number = READ_CHILDREN_ALL);

    Error write_children(StreamWriter& writer) const;

    std::string dump_children(Indent&) const;
  };


  class Box_ftyp : public Box {
  public:
    Box_ftyp() { set_short_type(fourcc("ftyp")); set_is_full_box(false); }
    Box_ftyp(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    bool has_compatible_brand(uint32_t brand) const;


    void set_major_brand(uint32_t major_brand) { m_major_brand=major_brand; }
    void set_minor_version(uint32_t minor_version) { m_minor_version=minor_version; }

    void clear_compatible_brands() { m_compatible_brands.clear(); }
    void add_compatible_brand(uint32_t brand);

    Error write(StreamWriter& writer) const override;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    uint32_t m_major_brand;
    uint32_t m_minor_version;
    std::vector<uint32_t> m_compatible_brands;
  };


  class Box_meta : public Box {
  public:
  Box_meta() { set_short_type(fourcc("meta")); set_is_full_box(true); }
  Box_meta(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;
  };


  class Box_hdlr : public Box {
  public:
    Box_hdlr() { set_short_type(fourcc("hdlr")); set_is_full_box(true); }
    Box_hdlr(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    uint32_t get_handler_type() const { return m_handler_type; }

    void set_handler_type(uint32_t handler) { m_handler_type = handler; }

    Error write(StreamWriter& writer) const override;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    uint32_t m_pre_defined = 0;
    uint32_t m_handler_type = fourcc("pict");
    uint32_t m_reserved[3];
    std::string m_name;
  };


  class Box_pitm : public Box {
  public:
    Box_pitm() { set_short_type(fourcc("pitm")); set_is_full_box(true); }
    Box_pitm(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    heif_item_id get_item_ID() const { return m_item_ID; }

    void set_item_ID(heif_item_id id) { m_item_ID = id; }

    void derive_box_version() override;

    Error write(StreamWriter& writer) const override;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    heif_item_id m_item_ID;
  };


  class Box_iloc : public Box {
  public:
    Box_iloc() { set_short_type(fourcc("iloc")); set_is_full_box(true); }
    Box_iloc(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    struct Extent {
      uint64_t index = 0;
      uint64_t offset = 0;
      uint64_t length = 0;

      std::vector<uint8_t> data; // only used when writing data
    };

    struct Item {
      heif_item_id item_ID;
      uint8_t  construction_method = 0; // >= version 1
      uint16_t data_reference_index = 0;
      uint64_t base_offset = 0;

      std::vector<Extent> extents;
    };

    const std::vector<Item>& get_items() const { return m_items; }

    Error read_data(const Item& item, HeifReader* reader,
                    const std::shared_ptr<class Box_idat>&,
                    std::vector<uint8_t>* dest) const;

    void set_min_version(uint8_t min_version) { m_user_defined_min_version=min_version; }

    // append bitstream data that will be written later (after iloc box)
    Error append_data(heif_item_id item_ID,
                      const std::vector<uint8_t>& data,
                      uint8_t construction_method=0);

    // append bitstream data that already has been written (before iloc box)
    // Error write_mdat_before_iloc(heif_image_id item_ID,
    //                              std::vector<uint8_t>& data)

    // reserve data entry that will be written later
    // Error reserve_mdat_item(heif_image_id item_ID,
    //                         uint8_t construction_method,
    //                         uint32_t* slot_ID);
    // void patch_mdat_slot(uint32_t slot_ID, size_t start, size_t length);

    void derive_box_version() override;

    Error write(StreamWriter& writer) const override;

    Error write_mdat_after_iloc(StreamWriter& writer);

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    std::vector<Item> m_items;

    mutable size_t m_iloc_box_start = 0;
    uint8_t m_user_defined_min_version = 0;
    uint8_t m_offset_size = 0;
    uint8_t m_length_size = 0;
    uint8_t m_base_offset_size = 0;
    uint8_t m_index_size = 0;

    void patch_iloc_header(StreamWriter& writer) const;
  };


  class Box_infe : public Box {
  public:
    Box_infe() { set_short_type(fourcc("infe")); set_is_full_box(true); }
  Box_infe(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    bool is_hidden_item() const { return m_hidden_item; }

    void set_hidden_item(bool hidden);

    heif_item_id get_item_ID() const { return m_item_ID; }

    void set_item_ID(heif_item_id id) { m_item_ID = id; }

    std::string get_item_type() const { return m_item_type; }

    void set_item_type(std::string type) { m_item_type = type; }

    void set_item_name(std::string name) { m_item_name = name; }

    void derive_box_version() override;

    Error write(StreamWriter& writer) const override;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
      heif_item_id m_item_ID;
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
  Box_iinf() { set_short_type(fourcc("iinf")); set_is_full_box(true); }
  Box_iinf(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    void derive_box_version() override;
    Error write(StreamWriter& writer) const override;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    //std::vector< std::shared_ptr<Box_infe> > m_iteminfos;
  };


  class Box_iprp : public Box {
  public:
  Box_iprp() { set_short_type(fourcc("iprp")); set_is_full_box(false); }
  Box_iprp(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;
  };


  class Box_ipco : public Box {
  public:
  Box_ipco() { set_short_type(fourcc("ipco")); set_is_full_box(false); }
  Box_ipco(const BoxHeader& hdr) : Box(hdr) { }

    struct Property {
      bool essential;
      std::shared_ptr<Box> property;
    };

    Error get_properties_for_item_ID(heif_item_id itemID,
                                     const std::shared_ptr<class Box_ipma>&,
                                     std::vector<Property>& out_properties) const;

    std::shared_ptr<Box> get_property_for_item_ID(heif_item_id itemID,
                                                  const std::shared_ptr<class Box_ipma>&,
                                                  uint32_t property_box_type) const;

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;
  };


  class Box_ispe : public Box {
  public:
  Box_ispe() { set_short_type(fourcc("ispe")); set_is_full_box(true); }
  Box_ispe(const BoxHeader& hdr) : Box(hdr) { }

    uint32_t get_width() const { return m_image_width; }
    uint32_t get_height() const { return m_image_height; }

    void set_size(uint32_t width, uint32_t height) {
      m_image_width = width;
      m_image_height = height;
    }

    std::string dump(Indent&) const override;

    Error write(StreamWriter& writer) const override;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    uint32_t m_image_width;
    uint32_t m_image_height;
  };


  class Box_ipma : public Box {
  public:
  Box_ipma() { set_short_type(fourcc("ipma")); set_is_full_box(true); }
  Box_ipma(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    struct PropertyAssociation {
      bool essential;
      uint16_t property_index;
    };

    const std::vector<PropertyAssociation>* get_properties_for_item_ID(heif_item_id itemID) const;

    void add_property_for_item_ID(heif_item_id itemID,
                                  PropertyAssociation assoc);

    void derive_box_version() override;

    Error write(StreamWriter& writer) const override;

  protected:
    Error parse(BitstreamRange& range) override;

    struct Entry {
      heif_item_id item_ID;
      std::vector<PropertyAssociation> associations;
    };

    std::vector<Entry> m_entries;
  };


  class Box_auxC : public Box {
  public:
  Box_auxC() { set_short_type(fourcc("auxC")); set_is_full_box(true); }
  Box_auxC(const BoxHeader& hdr) : Box(hdr) { }

    std::string get_aux_type() const { return m_aux_type; }
    void set_aux_type(std::string type) { m_aux_type = type; }

    std::vector<uint8_t> get_subtypes() const { return m_aux_subtypes; }

    std::string dump(Indent&) const override;

  protected:
    Error parse(BitstreamRange& range) override;
    Error write(StreamWriter& writer) const override;

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
    Box_iref() { set_short_type(fourcc("iref")); set_is_full_box(true); }
    Box_iref(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    bool has_references(heif_item_id itemID) const;
    uint32_t get_reference_type(heif_item_id itemID) const;
    std::vector<heif_item_id> get_references(heif_item_id itemID) const;

    void add_reference(heif_item_id from_id, uint32_t type, std::vector<heif_item_id> to_ids);

  protected:
    Error parse(BitstreamRange& range) override;
    Error write(StreamWriter& writer) const override;

    void derive_box_version() override;

  private:
    struct Reference {
      BoxHeader header;

      heif_item_id from_item_ID;
      std::vector<heif_item_id> to_item_ID;
    };

    std::vector<Reference> m_references;
  };


  class Box_hvcC : public Box {
  public:
    Box_hvcC() { set_short_type(fourcc("hvcC")); set_is_full_box(false); }
    Box_hvcC(const BoxHeader& hdr) : Box(hdr) { }

    struct configuration {
      uint8_t  configuration_version;
      uint8_t  general_profile_space;
      bool     general_tier_flag;
      uint8_t  general_profile_idc;
      uint32_t general_profile_compatibility_flags;

      static const int NUM_CONSTRAINT_INDICATOR_FLAGS = 48;
      std::bitset<NUM_CONSTRAINT_INDICATOR_FLAGS> general_constraint_indicator_flags;

      uint8_t  general_level_idc;

      uint16_t min_spatial_segmentation_idc;
      uint8_t  parallelism_type;
      uint8_t  chroma_format;
      uint8_t  bit_depth_luma;
      uint8_t  bit_depth_chroma;
      uint16_t avg_frame_rate;

      uint8_t  constant_frame_rate;
      uint8_t  num_temporal_layers;
      uint8_t  temporal_id_nested;
    };


    std::string dump(Indent&) const override;

    bool get_headers(std::vector<uint8_t>* dest) const;

    void set_configuration(const configuration& config) { m_configuration=config; }

    void append_nal_data(const std::vector<uint8_t>& nal);
    void append_nal_data(const uint8_t* data, size_t size);

    Error write(StreamWriter& writer) const override;

  protected:
    Error parse(BitstreamRange& range) override;

  private:
    struct NalArray {
      uint8_t m_array_completeness;
      uint8_t m_NAL_unit_type;

      std::vector< std::vector<uint8_t> > m_nal_units;
    };

    configuration m_configuration;
    uint8_t  m_length_size = 4; // default: 4 bytes for NAL unit lengths

    std::vector<NalArray> m_nal_array;
  };


  class Box_idat : public Box {
  public:
  Box_idat(const BoxHeader& hdr) : Box(hdr) { }

    std::string dump(Indent&) const override;

    Error read_data(HeifReader* reader, uint64_t start, uint64_t length,
                    std::vector<uint8_t>& out_data) const;

  protected:
    Error parse(BitstreamRange& range) override;

    uint64_t m_data_start_pos;
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

      std::vector<heif_item_id> entity_ids;
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
