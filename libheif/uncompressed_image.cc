/*
 * HEIF codec.
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
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


#include <cstdint>
#include <cstring>
#include <algorithm>
#include <map>
#include <iostream>
#include <cassert>

#include "libheif/heif.h"
#include "uncompressed_image.h"


enum heif_uncompressed_component_type
{
  component_type_monochrome = 0,
  component_type_Y = 1,
  component_type_Cb = 2,
  component_type_Cr = 3,
  component_type_red = 4,
  component_type_green = 5,
  component_type_blue = 6,
  component_type_alpha = 7,
  component_type_depth = 8,
  component_type_disparity = 9,
  component_type_palette = 10,
  component_type_filter_array = 11,
  component_type_padded = 12,
  component_type_cyan = 13,
  component_type_magenta = 14,
  component_type_yellow = 15,
  component_type_key_black = 16,
  component_type_max_valid = component_type_key_black
};

bool is_predefined_component_type(uint16_t type)
{
  // check whether the component type can be mapped to heif_uncompressed_component_type and we have a name defined for
  // it in sNames_uncompressed_component_type.
  return (type >= 0 && type <= 16);
}

static std::map<heif_uncompressed_component_type, const char*> sNames_uncompressed_component_type{
    {component_type_monochrome,   "monochrome"},
    {component_type_Y,            "Y"},
    {component_type_Cb,           "Cb"},
    {component_type_Cr,           "Cr"},
    {component_type_red,          "red"},
    {component_type_green,        "green"},
    {component_type_blue,         "blue"},
    {component_type_alpha,        "alpha"},
    {component_type_depth,        "depth"},
    {component_type_disparity,    "disparity"},
    {component_type_palette,      "palette"},
    {component_type_filter_array, "filter-array"},
    {component_type_padded,       "padded"},
    {component_type_cyan,         "cyan"},
    {component_type_magenta,      "magenta"},
    {component_type_yellow,       "yellow"},
    {component_type_key_black,    "key (black)"}
};

enum heif_uncompressed_component_format
{
  component_format_unsigned = 0,
  component_format_float = 1,
  component_format_complex = 2,
};

bool is_valid_component_format(uint8_t format)
{
  return format <= 2;
}

static std::map<heif_uncompressed_component_format, const char*> sNames_uncompressed_component_format{
    {component_format_unsigned, "unsigned"},
    {component_format_float,    "float"},
    {component_format_complex,  "complex"}
};


enum heif_uncompressed_sampling_type
{
  sampling_type_no_subsampling = 0,
  sampling_type_422 = 1,
  sampling_type_420 = 2,
  sampling_type_411 = 3
};

bool is_valid_sampling_type(uint8_t sampling)
{
  return sampling <= 3;
}

static std::map<heif_uncompressed_sampling_type, const char*> sNames_uncompressed_sampling_type{
    {sampling_type_no_subsampling, "no subsampling"},
    {sampling_type_422,            "4:2:2"},
    {sampling_type_420,            "4:2:0"},
    {sampling_type_411,            "4:1:1"}
};

enum heif_uncompressed_interleave_type
{
  interleave_type_component = 0,
  interleave_type_pixel = 1,
  interleave_type_mixed = 2,
  interleave_type_row = 3,
  interleave_type_tile_component = 4,
  interleave_type_multi_y = 5
};

bool is_valid_interleave_type(uint8_t sampling)
{
  return sampling <= 5;
}

static std::map<heif_uncompressed_interleave_type, const char*> sNames_uncompressed_interleave_type{
    {interleave_type_component,      "component"},
    {interleave_type_pixel,          "pixel"},
    {interleave_type_mixed,          "mixed"},
    {interleave_type_row,            "row"},
    {interleave_type_tile_component, "tile-component"},
    {interleave_type_multi_y,        "multi-y"}
};

template <typename T> const char* get_name(T val, const std::map<T, const char*>& table)
{
  auto iter = table.find(val);
  if (iter == table.end()) {
    return "unknown";
  }
  else {
    return iter->second;
  }
}


Error Box_cmpd::parse(BitstreamRange& range)
{
  unsigned int component_count = range.read32();

  for (unsigned int i = 0; i < component_count && !range.error() && !range.eof(); i++) {
    Component component;
    component.component_type = range.read16();
    if (component.component_type >= 0x8000) {
      component.component_type_uri = range.read_string();
    }
    else {
      component.component_type_uri = std::string();
    }
    m_components.push_back(component);
  }

  return range.get_error();
}

std::string Box_cmpd::Component::get_component_type_name(uint16_t component_type)
{
  std::stringstream sstr;

  if (is_predefined_component_type(component_type)) {
    sstr << get_name(heif_uncompressed_component_type(component_type), sNames_uncompressed_component_type) << "\n";
  }
  else {
    sstr << "0x" << std::hex << component_type << std::dec << "\n";
  }

  return sstr.str();
}


std::string Box_cmpd::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  for (const auto& component : m_components) {
    sstr << indent << "component_type: " << component.get_component_type_name() << "\n";

    if (component.component_type >= 0x8000) {
      sstr << indent << "| component_type_uri: " << component.component_type_uri << "\n";
    }
  }

  return sstr.str();
}

Error Box_cmpd::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write32((uint32_t) m_components.size());
  for (const auto& component : m_components) {
    writer.write16(component.component_type);
    if (component.component_type >= 0x8000) {
      writer.write(component.component_type_uri);
    }
  }

  prepend_header(writer, box_start);

  return Error::Ok;
}

Error Box_uncC::parse(BitstreamRange& range)
{
  parse_full_box_header(range);
  m_profile = range.read32();

  unsigned int component_count = range.read32();

  for (unsigned int i = 0; i < component_count && !range.error() && !range.eof(); i++) {
    Component component;
    component.component_index = range.read16();
    component.component_bit_depth = uint16_t(range.read8() + 1);
    component.component_format = range.read8();
    component.component_align_size = range.read8();
    m_components.push_back(component);

    if (!is_valid_component_format(component.component_format)) {
      return Error{heif_error_Invalid_input, heif_suberror_Invalid_parameter_value, "Invalid component format"};
    }
  }

  m_sampling_type = range.read8();
  if (!is_valid_sampling_type(m_sampling_type)) {
    return Error{heif_error_Invalid_input, heif_suberror_Invalid_parameter_value, "Invalid sampling type"};
  }

  m_interleave_type = range.read8();
  if (!is_valid_interleave_type(m_interleave_type)) {
    return Error{heif_error_Invalid_input, heif_suberror_Invalid_parameter_value, "Invalid interleave type"};
  }

  m_block_size = range.read8();

  uint8_t flags = range.read8();
  m_components_little_endian = !!(flags & 0x80);
  m_block_pad_lsb = !!(flags & 0x40);
  m_block_little_endian = !!(flags & 0x20);
  m_block_reversed = !!(flags & 0x10);
  m_pad_unknown = !!(flags & 0x08);

  m_pixel_size = range.read32();

  m_row_align_size = range.read32();

  m_tile_align_size = range.read32();

  m_num_tile_cols = range.read32() + 1;

  m_num_tile_rows = range.read32() + 1;

  return range.get_error();
}


std::string Box_uncC::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "profile: " << m_profile;
  if (m_profile != 0) {
    sstr << " (" << to_fourcc(m_profile) << ")";
  }
  sstr << "\n";

  for (const auto& component : m_components) {
    sstr << indent << "component_index: " << component.component_index << "\n";
    sstr << indent << "component_bit_depth: " << (int) component.component_bit_depth << "\n";
    sstr << indent << "component_format: " << get_name(heif_uncompressed_component_format(component.component_format), sNames_uncompressed_component_format) << "\n";
    sstr << indent << "component_align_size: " << (int) component.component_align_size << "\n";
  }

  sstr << indent << "sampling_type: " << get_name(heif_uncompressed_sampling_type(m_sampling_type), sNames_uncompressed_sampling_type) << "\n";

  sstr << indent << "interleave_type: " << get_name(heif_uncompressed_interleave_type(m_interleave_type), sNames_uncompressed_interleave_type) << "\n";

  sstr << indent << "block_size: " << (int) m_block_size << "\n";

  sstr << indent << "components_little_endian: " << m_components_little_endian << "\n";
  sstr << indent << "block_pad_lsb: " << m_block_pad_lsb << "\n";
  sstr << indent << "block_little_endian: " << m_block_little_endian << "\n";
  sstr << indent << "block_reversed: " << m_block_reversed << "\n";
  sstr << indent << "pad_unknown: " << m_pad_unknown << "\n";

  sstr << indent << "pixel_size: " << m_pixel_size << "\n";

  sstr << indent << "row_align_size: " << m_row_align_size << "\n";

  sstr << indent << "tile_align_size: " << m_tile_align_size << "\n";

  sstr << indent << "num_tile_cols: " << m_num_tile_cols << "\n";

  sstr << indent << "num_tile_rows: " << m_num_tile_rows << "\n";

  return sstr.str();
}

bool Box_uncC::get_headers(std::vector<uint8_t>* dest) const
{
  // TODO: component_bit_depth?
  return true;
}

Error Box_uncC::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write32(m_profile);
  writer.write32((uint32_t) m_components.size());
  for (const auto& component : m_components) {
    if (component.component_bit_depth < 1 || component.component_bit_depth > 256) {
      return {heif_error_Invalid_input, heif_suberror_Invalid_parameter_value, "component bit-depth out of range [1..256]"};
    }

    writer.write16(component.component_index);
    writer.write8(uint8_t(component.component_bit_depth - 1));
    writer.write8(component.component_format);
    writer.write8(component.component_align_size);
  }
  writer.write8(m_sampling_type);
  writer.write8(m_interleave_type);
  writer.write8(m_block_size);
  uint8_t flags = 0;
  flags |= (m_components_little_endian ? 0x80 : 0);
  flags |= (m_block_pad_lsb ? 0x40 : 0);
  flags |= (m_block_little_endian ? 0x20 : 0);
  flags |= (m_block_reversed ? 0x10 : 0);
  flags |= (m_pad_unknown ? 0x08 : 0);
  writer.write8(flags);
  writer.write32(m_pixel_size);
  writer.write32(m_row_align_size);
  writer.write32(m_tile_align_size);
  writer.write32(m_num_tile_cols - 1);
  writer.write32(m_num_tile_rows - 1);
  prepend_header(writer, box_start);

  return Error::Ok;
}


static Error uncompressed_image_type_is_supported(std::shared_ptr<Box_uncC>& uncC, std::shared_ptr<Box_cmpd>& cmpd)
{
  for (Box_uncC::Component component : uncC->get_components()) {
    uint16_t component_index = component.component_index;
    uint16_t component_type = cmpd->get_components()[component_index].component_type;
    if (component_type > 7) {
      std::stringstream sstr;
      sstr << "Uncompressed image with component_type " << ((int) component_type) << " is not implemented yet";
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   sstr.str());
    }
    if (component.component_bit_depth > 16) {
      std::stringstream sstr;
      sstr << "Uncompressed image with component_bit_depth " << ((int) component.component_bit_depth) << " is not implemented yet";
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   sstr.str());
    }
    if (component.component_format != component_format_unsigned) {
      std::stringstream sstr;
      sstr << "Uncompressed image with component_format " << ((int) component.component_format) << " is not implemented yet";
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   sstr.str());
    }
    if (component.component_align_size > 2) {
      std::stringstream sstr;
      sstr << "Uncompressed image with component_align_size " << ((int) component.component_align_size) << " is not implemented yet";
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   sstr.str());
    }
  }
  if (uncC->get_sampling_type() != sampling_type_no_subsampling) {
    std::stringstream sstr;
    sstr << "Uncompressed sampling_type of " << ((int) uncC->get_sampling_type()) << " is not implemented yet";
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 sstr.str());
  }
  if ((uncC->get_interleave_type() != interleave_type_component)
      && (uncC->get_interleave_type() != interleave_type_pixel)
      && (uncC->get_interleave_type() != interleave_type_row)
      ) {
    std::stringstream sstr;
    sstr << "Uncompressed interleave_type of " << ((int) uncC->get_interleave_type()) << " is not implemented yet";
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 sstr.str());
  }
  if (uncC->get_block_size() != 0) {
    std::stringstream sstr;
    sstr << "Uncompressed block_size of " << ((int) uncC->get_block_size()) << " is not implemented yet";
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 sstr.str());
  }
  if (uncC->is_components_little_endian()) {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Uncompressed components_little_endian == 1 is not implemented yet");
  }
  if (uncC->is_block_pad_lsb()) {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Uncompressed block_pad_lsb == 1 is not implemented yet");
  }
  if (uncC->is_block_little_endian()) {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Uncompressed block_little_endian == 1 is not implemented yet");
  }
  if (uncC->is_block_reversed()) {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Uncompressed block_reversed == 1 is not implemented yet");
  }
  if (uncC->get_pixel_size() != 0) {
    std::stringstream sstr;
    sstr << "Uncompressed pixel_size of " << ((int) uncC->get_pixel_size()) << " is not implemented yet";
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 sstr.str());
  }
  if (uncC->get_row_align_size() != 0) {
    std::stringstream sstr;
    sstr << "Uncompressed row_align_size of " << ((int) uncC->get_row_align_size()) << " is not implemented yet";
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 sstr.str());
  }
  return Error::Ok;
}


static Error get_heif_chroma_uncompressed(std::shared_ptr<Box_uncC>& uncC, std::shared_ptr<Box_cmpd>& cmpd, heif_chroma* out_chroma, heif_colorspace* out_colourspace)
{
  *out_chroma = heif_chroma_undefined;
  *out_colourspace = heif_colorspace_undefined;

  // each 1-bit represents an existing component in the image
  uint16_t componentSet = 0;

  for (Box_uncC::Component component : uncC->get_components()) {
    uint16_t component_index = component.component_index;
    uint16_t component_type = cmpd->get_components()[component_index].component_type;

    if (component_type > component_type_max_valid) {
      std::stringstream sstr;
      sstr << "a component_type > " << component_type_max_valid << " is not supported";
      return { heif_error_Unsupported_feature, heif_suberror_Invalid_parameter_value, sstr.str()};
    }

    componentSet |= (1 << component_type);
  }

  if (componentSet == ((1 << component_type_red) | (1 << component_type_green) | (1 << component_type_blue)) ||
      componentSet == ((1 << component_type_red) | (1 << component_type_green) | (1 << component_type_blue) | (1 << component_type_alpha))) {
    *out_chroma = heif_chroma_444;
    *out_colourspace = heif_colorspace_RGB;
  }

  if (componentSet == ((1 << component_type_Y) | (1 << component_type_Cb) | (1 << component_type_Cr))) {
    if (uncC->get_interleave_type() == 0) {
      // Planar YCbCr
      *out_chroma = heif_chroma_444;
      *out_colourspace = heif_colorspace_YCbCr;
    }
  }

  if (componentSet == ((1 << component_type_monochrome)) || componentSet == ((1 << component_type_monochrome) | (1 << component_type_alpha))) {
    if (uncC->get_interleave_type() == 0) {
      // Planar mono or planar mono + alpha
      *out_chroma = heif_chroma_monochrome;
      *out_colourspace = heif_colorspace_monochrome;
    }
  }

  // TODO: more combinations

  if (*out_chroma == heif_chroma_undefined) {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Could not determine chroma");
  }
  else if (*out_colourspace == heif_colorspace_undefined) {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Could not determine colourspace");
  }
  else {
    return Error::Ok;
  }
}


int UncompressedImageCodec::get_luma_bits_per_pixel_from_configuration_unci(const HeifFile& heif_file, heif_item_id imageID)
{
  auto ipco = heif_file.get_ipco_box();
  auto ipma = heif_file.get_ipma_box();

  auto box1 = ipco->get_property_for_item_ID(imageID, ipma, fourcc("uncC"));
  std::shared_ptr<Box_uncC> uncC_box = std::dynamic_pointer_cast<Box_uncC>(box1);
  auto box2 = ipco->get_property_for_item_ID(imageID, ipma, fourcc("cmpd"));
  std::shared_ptr<Box_cmpd> cmpd_box = std::dynamic_pointer_cast<Box_cmpd>(box2);
  if (!uncC_box || !cmpd_box) {
    return -1;
  }

  int luma_bits = 0;
  int alternate_channel_bits = 0;
  for (Box_uncC::Component component : uncC_box->get_components()) {
    uint16_t component_index = component.component_index;
    auto component_type = cmpd_box->get_components()[component_index].component_type;
    switch (component_type) {
      case component_type_monochrome:
      case component_type_red:
      case component_type_green:
      case component_type_blue:
        alternate_channel_bits = std::max(alternate_channel_bits, (int)component.component_bit_depth);
        break;
      case component_type_Y:
        luma_bits = std::max(luma_bits, (int)component.component_bit_depth);
        break;
        // TODO: there are other things we'll need to handle eventually, like palette.
    }
  }
  if (luma_bits > 0) {
    return luma_bits;
  }
  else if (alternate_channel_bits > 0) {
    return alternate_channel_bits;
  }
  else {
    return 8;
  }
}

static unsigned int get_bytes_per_pixel(const std::shared_ptr<Box_uncC>& uncC)
{
  unsigned int bytes = 0;
  for (Box_uncC::Component component: uncC->get_components())
  {
    if (component.component_align_size == 0)
    {
      // TODO: needs more work when we have components with padding
      unsigned int component_bytes = (component.component_bit_depth + 7) / 8;
      bytes += component_bytes;
    }
    else
    {
      bytes += component.component_align_size;
    }
  }
  return bytes;
}

static long unsigned int get_tile_base_offset(uint32_t col, uint32_t row, const std::shared_ptr<Box_uncC>& uncC, const std::vector<heif_channel>& channels, uint32_t width, uint32_t height)
{
  uint32_t numTileColumns = uncC->get_number_of_tile_columns();
  uint32_t numTileRows = uncC->get_number_of_tile_rows();
  uint32_t tile_width = width / numTileColumns;
  uint32_t tile_height = height / numTileRows;
  long unsigned int content_bytes_per_tile = tile_width * tile_height * get_bytes_per_pixel(uncC);
  uint32_t tile_align_size = uncC->get_tile_align_size();
  long unsigned int tile_padding = 0;
  if (tile_align_size > 0) {
    tile_padding = tile_align_size - (content_bytes_per_tile % tile_align_size);
  }
  long unsigned int bytes_per_tile = content_bytes_per_tile + tile_padding;
  uint32_t tile_idx_y = row / tile_height;
  uint32_t tile_idx_x = col / tile_width;
  uint32_t tile_idx = tile_idx_y * numTileColumns + tile_idx_x;
  long unsigned int tile_base_offset = tile_idx * bytes_per_tile;
  return tile_base_offset;
}


Error UncompressedImageCodec::decode_uncompressed_image(const std::shared_ptr<const HeifFile>& heif_file,
                                                        heif_item_id ID,
                                                        std::shared_ptr<HeifPixelImage>& img,
                                                        uint32_t maximum_image_width_limit,
                                                        uint32_t maximum_image_height_limit,
                                                        const std::vector<uint8_t>& uncompressed_data)
{
  // Get the properties for this item
  // We need: ispe, cmpd, uncC
  std::vector<std::shared_ptr<Box>> item_properties;
  Error error = heif_file->get_properties(ID, item_properties);
  if (error) {
    return error;
  }
  uint32_t width = 0;
  uint32_t height = 0;
  bool found_ispe = false;
  std::shared_ptr<Box_cmpd> cmpd;
  std::shared_ptr<Box_uncC> uncC;
  for (const auto& prop : item_properties) {
    auto ispe = std::dynamic_pointer_cast<Box_ispe>(prop);
    if (ispe) {
      width = ispe->get_width();
      height = ispe->get_height();

      if (width >= maximum_image_width_limit || height >= maximum_image_height_limit) {
        std::stringstream sstr;
        sstr << "Image size " << width << "x" << height << " exceeds the maximum image size "
             << maximum_image_width_limit << "x" << maximum_image_height_limit << "\n";

        return Error(heif_error_Memory_allocation_error,
                     heif_suberror_Security_limit_exceeded,
                     sstr.str());
      }
      found_ispe = true;
    }

    auto maybe_cmpd = std::dynamic_pointer_cast<Box_cmpd>(prop);
    if (maybe_cmpd) {
      cmpd = maybe_cmpd;
    }

    auto maybe_uncC = std::dynamic_pointer_cast<Box_uncC>(prop);
    if (maybe_uncC) {
      uncC = maybe_uncC;
    }
  }


  // if we miss a required box, show error

  if (!found_ispe || !cmpd || !uncC) {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Missing required box for uncompressed codec");
  }


  // check if we support the type of image

  error = uncompressed_image_type_is_supported(uncC, cmpd);
  if (error) {
    return error;
  }

  img = std::make_shared<HeifPixelImage>();
  heif_chroma chroma;
  heif_colorspace colourspace;
  error = get_heif_chroma_uncompressed(uncC, cmpd, &chroma, &colourspace);
  if (error) {
    return error;
  }
  img->create(width, height,
              colourspace,
              chroma);

  std::vector<heif_channel> channels;
  std::map<heif_channel, uint32_t> channel_to_pixelOffset;

  uint32_t componentOffset = 0;
  for (Box_uncC::Component component : uncC->get_components()) {
    uint16_t component_index = component.component_index;
    uint16_t component_type = cmpd->get_components()[component_index].component_type;
    if (component_type == component_type_Y ||
        component_type == component_type_monochrome) {
      img->add_plane(heif_channel_Y, width, height, component.component_bit_depth);
      channels.push_back(heif_channel_Y);
      channel_to_pixelOffset.emplace(heif_channel_Y, componentOffset);
    }
    else if (component_type == component_type_Cb) {
      img->add_plane(heif_channel_Cb, width, height, component.component_bit_depth);
      channels.push_back(heif_channel_Cb);
      channel_to_pixelOffset.emplace(heif_channel_Cb, componentOffset);
    }
    else if (component_type == component_type_Cr) {
      img->add_plane(heif_channel_Cr, width, height, component.component_bit_depth);
      channels.push_back(heif_channel_Cr);
      channel_to_pixelOffset.emplace(heif_channel_Cr, componentOffset);
    }
    else if (component_type == component_type_red) {
      img->add_plane(heif_channel_R, width, height, component.component_bit_depth);
      channels.push_back(heif_channel_R);
      channel_to_pixelOffset.emplace(heif_channel_R, componentOffset);
    }
    else if (component_type == component_type_green) {
      img->add_plane(heif_channel_G, width, height, component.component_bit_depth);
      channels.push_back(heif_channel_G);
      channel_to_pixelOffset.emplace(heif_channel_G, componentOffset);
    }
    else if (component_type == component_type_blue) {
      img->add_plane(heif_channel_B, width, height, component.component_bit_depth);
      channels.push_back(heif_channel_B);
      channel_to_pixelOffset.emplace(heif_channel_B, componentOffset);
    }
    else if (component_type == component_type_alpha) {
      img->add_plane(heif_channel_Alpha, width, height, component.component_bit_depth);
      channels.push_back(heif_channel_Alpha);
      channel_to_pixelOffset.emplace(heif_channel_Alpha, componentOffset);
    }

    // TODO: other component types
    componentOffset++;
  }

  // TODO: properly interpret uncompressed_data per uncC config, subsampling etc.
  uint32_t bytes_per_channel = width * height;
  uint32_t numTileColumns = uncC->get_number_of_tile_columns();
  uint32_t numTileRows = uncC->get_number_of_tile_rows();
  uint32_t tile_width = width / numTileColumns;
  uint32_t tile_height = height / numTileRows;
  if (uncC->get_interleave_type() == interleave_type_component) {
    // Source is planar
    // TODO: assumes 8 bits
    long unsigned int content_bytes_per_tile = tile_width * tile_height * get_bytes_per_pixel(uncC);
    uint32_t tile_align_size = uncC->get_tile_align_size();
    long unsigned int tile_padding = 0;
    if (tile_align_size > 0) {
      tile_padding = tile_align_size - (content_bytes_per_tile % tile_align_size);
    };
    long unsigned int bytes_per_tile = content_bytes_per_tile + tile_padding;
    for (uint32_t c = 0; c < channels.size(); c++) {
      int stride;
      uint8_t* dst = img->get_plane(channels[c], &stride);
      if ((numTileRows == 1) && (numTileColumns == 1) && (((uint32_t) stride) == width)) {
        memcpy(dst, uncompressed_data.data() + c * bytes_per_channel, bytes_per_channel);
      }
      else {
        int pixel_offset = channel_to_pixelOffset[channels[c]];
        for (uint32_t row = 0; row < height; row++) {
          for (uint32_t col = 0; col < width; col += tile_width) {
            uint32_t tile_idx_y = row / tile_height;
            uint32_t tile_idx_x = col / tile_width;
            uint32_t tile_idx = tile_idx_y * numTileColumns + tile_idx_x;
            long unsigned int tile_base_offset = tile_idx * bytes_per_tile;
            long unsigned int src_offset = tile_base_offset + pixel_offset * tile_width * tile_height;
            long unsigned int dst_offset = row * stride + col;
            memcpy(dst + dst_offset, uncompressed_data.data() + src_offset /** + row * tile_width **/, tile_width);   // TODO: ** is a hack
          }
        }
      }
    }
  }
  else if (uncC->get_interleave_type() == interleave_type_pixel) {
    // TODO: we need to be smarter about block size, etc

    // TODO: we can only do this if we are 8 bits
    long unsigned int pixel_stride = get_bytes_per_pixel(uncC);
    const uint8_t* src = uncompressed_data.data();
    for (uint32_t c = 0; c < channels.size(); c++) {
      int pixel_offset = channel_to_pixelOffset[channels[c]];
      int stride;
      uint8_t* dst = img->get_plane(channels[c], &stride);
      for (uint32_t row = 0; row < height; row++) {
        long unsigned int tile_row_idx = row % tile_height;
        size_t tile_row_offset = tile_width * tile_row_idx * channels.size();
        uint32_t col = 0;
        for (col = 0; col < width; col++) {
          long unsigned int tile_base_offset = get_tile_base_offset(col, row, uncC, channels, width, height);
          long unsigned int tile_col = col % tile_width;
          size_t tile_offset = tile_row_offset + tile_col * pixel_stride + pixel_offset;
          size_t src_offset = tile_base_offset + tile_offset;
          uint32_t dstPixelIndex = row * stride + col;
          dst[dstPixelIndex] = src[src_offset];
        }
        for (; col < (uint32_t) stride; col++) {
          uint32_t dstPixelIndex = row * stride + col;
          dst[dstPixelIndex] = 0;
        }
      }
    }
  }
  else if (uncC->get_interleave_type() == interleave_type_row) {
    // TODO: we need to be smarter about block size, etc

    // TODO: we can only do this if we are 8 bits
    for (uint32_t c = 0; c < channels.size(); c++) {
      int pixel_offset = channel_to_pixelOffset[channels[c]];
      int stride;
      uint8_t* dst = img->get_plane(channels[c], &stride);
      for (uint32_t row = 0; row < height; row++) {
        long unsigned int tile_row_idx = row % tile_height;
        size_t tile_row_offset = tile_width * (tile_row_idx * channels.size() + pixel_offset);
        uint32_t col = 0;
        for (col = 0; col < width; col += tile_width) {
          long unsigned int tile_base_offset = get_tile_base_offset(col, row, uncC, channels, width, height);
          long unsigned int tile_col = col % tile_width;
          size_t tile_offset = tile_row_offset + tile_col;
          size_t src_offset = tile_base_offset + tile_offset;
          uint32_t dst_offset = row * stride + col;
          memcpy(dst + dst_offset, uncompressed_data.data() + src_offset, tile_width);
        }
        for (; col < (uint32_t) stride; col++) {
          uint32_t dstPixelIndex = row * stride + col;
          dst[dstPixelIndex] = 0;
        }
      }
    }
  }
  return Error::Ok;
}

Error fill_cmpd_and_uncC(std::shared_ptr<Box_cmpd>& cmpd, std::shared_ptr<Box_uncC>& uncC, const std::shared_ptr<HeifPixelImage>& image)
{
  const heif_colorspace colourspace = image->get_colorspace();
  if (colourspace == heif_colorspace_YCbCr) {
    if (!(image->has_channel(heif_channel_Y) && image->has_channel(heif_channel_Cb) && image->has_channel(heif_channel_Cr)))
    {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   "Invalid colourspace / channel combination - YCbCr");
    }
    Box_cmpd::Component yComponent = {component_type_Y};
    cmpd->add_component(yComponent);
    Box_cmpd::Component cbComponent = {component_type_Cb};
    cmpd->add_component(cbComponent);
    Box_cmpd::Component crComponent = {component_type_Cr};
    cmpd->add_component(crComponent);
    int bpp_y = image->get_bits_per_pixel(heif_channel_Y);
    Box_uncC::Component component0 = {0, (uint8_t)(bpp_y - 1), component_format_unsigned, 0};
    uncC->add_component(component0);
    int bpp_cb = image->get_bits_per_pixel(heif_channel_Cb);
    Box_uncC::Component component1 = {1, (uint8_t)(bpp_cb - 1), component_format_unsigned, 0};
    uncC->add_component(component1);
    int bpp_cr = image->get_bits_per_pixel(heif_channel_Cr);
    Box_uncC::Component component2 = {2, (uint8_t)(bpp_cr - 1), component_format_unsigned, 0};
    uncC->add_component(component2);
    if (image->get_chroma_format() == heif_chroma_444)
    {
      uncC->set_sampling_type(sampling_type_no_subsampling);
    }
    else if (image->get_chroma_format() == heif_chroma_422)
    {
      uncC->set_sampling_type(sampling_type_422);
    }
    else if (image->get_chroma_format() == heif_chroma_420)
    {
      uncC->set_sampling_type(sampling_type_420);
    }
    else
    {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   "Unsupported YCbCr sub-sampling type");
    }
    uncC->set_interleave_type(interleave_type_component);
    uncC->set_block_size(0);
    uncC->set_components_little_endian(false);
    uncC->set_block_pad_lsb(false);
    uncC->set_block_little_endian(false);
    uncC->set_block_reversed(false);
    uncC->set_pad_unknown(false);
    uncC->set_pixel_size(0);
    uncC->set_row_align_size(0);
    uncC->set_tile_align_size(0);
    uncC->set_number_of_tile_columns(1);
    uncC->set_number_of_tile_rows(1);
  }
  else if (colourspace == heif_colorspace_RGB)
  {
    if (!((image->get_chroma_format() == heif_chroma_444) ||
          (image->get_chroma_format() == heif_chroma_interleaved_RGB) ||
          (image->get_chroma_format() == heif_chroma_interleaved_RGBA) ||
          (image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_BE) ||
          (image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_LE) ||
          (image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_BE) ||
          (image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE))) {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   "Unsupported colourspace / chroma combination - RGB");
    }
    Box_cmpd::Component rComponent = {component_type_red};
    cmpd->add_component(rComponent);
    Box_cmpd::Component gComponent = {component_type_green};
    cmpd->add_component(gComponent);
    Box_cmpd::Component bComponent = {component_type_blue};
    cmpd->add_component(bComponent);
    if ((image->get_chroma_format() == heif_chroma_interleaved_RGBA) ||
        (image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_BE) ||
        (image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE) ||
        (image->has_channel(heif_channel_Alpha)))
    {
      Box_cmpd::Component alphaComponent = {component_type_alpha};
      cmpd->add_component(alphaComponent);
    }
    if ((image->get_chroma_format() == heif_chroma_interleaved_RGB) ||
        (image->get_chroma_format() == heif_chroma_interleaved_RGBA) ||
        (image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_BE) ||
        (image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_LE) ||
        (image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_BE) ||
        (image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE))
    {
      uncC->set_interleave_type(interleave_type_pixel);
      int bpp = image->get_bits_per_pixel(heif_channel_interleaved);
      uint8_t component_align = 1;
      if (bpp == 8)
      {
        component_align = 0;
      }
      else if (bpp > 8)
      {
        component_align = 2;
      }
      Box_uncC::Component component0 = {0, (uint8_t)(bpp), component_format_unsigned, component_align};
      uncC->add_component(component0);
      Box_uncC::Component component1 = {1, (uint8_t)(bpp), component_format_unsigned, component_align};
      uncC->add_component(component1);
      Box_uncC::Component component2 = {2, (uint8_t)(bpp), component_format_unsigned, component_align};
      uncC->add_component(component2);
      if ((image->get_chroma_format() == heif_chroma_interleaved_RGBA) ||
          (image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_BE) ||
          (image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE))
      {
        Box_uncC::Component component3 = {
            3, (uint8_t)(bpp), component_format_unsigned, component_align};
        uncC->add_component(component3);
      }
    } else {
      uncC->set_interleave_type(interleave_type_component);
      int bpp_red = image->get_bits_per_pixel(heif_channel_R);
      Box_uncC::Component component0 = {0, (uint8_t)(bpp_red), component_format_unsigned, 0};
      uncC->add_component(component0);
      int bpp_green = image->get_bits_per_pixel(heif_channel_G);
      Box_uncC::Component component1 = {1, (uint8_t)(bpp_green), component_format_unsigned, 0};
      uncC->add_component(component1);
      int bpp_blue = image->get_bits_per_pixel(heif_channel_B);
      Box_uncC::Component component2 = {2, (uint8_t)(bpp_blue), component_format_unsigned, 0};
      uncC->add_component(component2);
      if(image->has_channel(heif_channel_Alpha))
      {
        int bpp_alpha = image->get_bits_per_pixel(heif_channel_Alpha);
        Box_uncC::Component component3 = {3, (uint8_t)(bpp_alpha), component_format_unsigned, 0};
        uncC->add_component(component3);
      }
    }
    uncC->set_sampling_type(sampling_type_no_subsampling);
    uncC->set_block_size(0);
    if ((image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_LE) ||
        (image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE))
    {
      uncC->set_components_little_endian(true);
    } else {
      uncC->set_components_little_endian(false);
    }
    uncC->set_block_pad_lsb(false);
    uncC->set_block_little_endian(false);
    uncC->set_block_reversed(false);
    uncC->set_pad_unknown(false);
    uncC->set_pixel_size(0);
    uncC->set_row_align_size(0);
    uncC->set_tile_align_size(0);
    uncC->set_number_of_tile_columns(1);
    uncC->set_number_of_tile_rows(1);
  }
  else if (colourspace == heif_colorspace_monochrome)
  {
    Box_cmpd::Component monoComponent = {component_type_monochrome};
    cmpd->add_component(monoComponent);
    if (image->has_channel(heif_channel_Alpha))
    {
      Box_cmpd::Component alphaComponent = {component_type_alpha};
      cmpd->add_component(alphaComponent);
    }
    int bpp = image->get_bits_per_pixel(heif_channel_Y);
    Box_uncC::Component component0 = {0, (uint8_t)(bpp), component_format_unsigned, 0};
    uncC->add_component(component0);
    if (image->has_channel(heif_channel_Alpha))
    {
      bpp = image->get_bits_per_pixel(heif_channel_Alpha);
      Box_uncC::Component component1 = {1, (uint8_t)(bpp), component_format_unsigned, 0};
      uncC->add_component(component1);
    }
    uncC->set_sampling_type(sampling_type_no_subsampling);
    uncC->set_interleave_type(interleave_type_component);
    uncC->set_block_size(0);
    uncC->set_components_little_endian(false);
    uncC->set_block_pad_lsb(false);
    uncC->set_block_little_endian(false);
    uncC->set_block_reversed(false);
    uncC->set_pad_unknown(false);
    uncC->set_pixel_size(0);
    uncC->set_row_align_size(0);
    uncC->set_tile_align_size(0);
    uncC->set_number_of_tile_columns(1);
    uncC->set_number_of_tile_rows(1);
  }
  else
  {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Unsupported colourspace");
  }
  return Error::Ok;
}


Error UncompressedImageCodec::encode_uncompressed_image(const std::shared_ptr<HeifFile>& heif_file,
                                                        const std::shared_ptr<HeifPixelImage>& src_image,
                                                        void* encoder_struct,
                                                        const struct heif_encoding_options& options,
                                                        std::shared_ptr<HeifContext::Image>& out_image)
{
  std::shared_ptr<Box_cmpd> cmpd = std::make_shared<Box_cmpd>();
  std::shared_ptr<Box_uncC> uncC = std::make_shared<Box_uncC>();
  Error error = fill_cmpd_and_uncC(cmpd, uncC, src_image);
  if (error)
  {
    return error;
  }
  heif_file->add_property(out_image->get_id(), cmpd, true);
  heif_file->add_property(out_image->get_id(), uncC, true);

  std::vector<uint8_t> data;
  if (src_image->get_colorspace() == heif_colorspace_YCbCr)
  {
    uint64_t offset = 0;
    for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr})
    {
      int src_stride;
      uint8_t* src_data = src_image->get_plane(channel, &src_stride);
      uint64_t out_size = src_image->get_height() * src_image->get_width();
      data.resize(data.size() + out_size);
      for (int y = 0; y < src_image->get_height(); y++) {
        memcpy(data.data() + offset + y * src_image->get_width(), src_data + src_stride * y, src_image->get_width());
      }
      offset += out_size;
    }
    heif_file->append_iloc_data(out_image->get_id(), data, 0);
  }
  else if (src_image->get_colorspace() == heif_colorspace_RGB)
  {
    if (src_image->get_chroma_format() == heif_chroma_444)
    {
      uint64_t offset = 0;
      std::vector<heif_channel> channels = {heif_channel_R, heif_channel_G, heif_channel_B};
      if (src_image->has_channel(heif_channel_Alpha))
      {
        channels.push_back(heif_channel_Alpha);
      }
      for (heif_channel channel : channels)
      {
        int src_stride;
        uint8_t* src_data = src_image->get_plane(channel, &src_stride);
        uint64_t out_size = src_image->get_height() * src_stride;
        data.resize(data.size() + out_size);
        memcpy(data.data() + offset, src_data, out_size);
        offset += out_size;
      }
      heif_file->append_iloc_data(out_image->get_id(), data, 0);
    }
    else if ((src_image->get_chroma_format() == heif_chroma_interleaved_RGB) ||
             (src_image->get_chroma_format() == heif_chroma_interleaved_RGBA) ||
             (src_image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_BE) ||
             (src_image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_LE) ||
             (src_image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_BE) ||
             (src_image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE))
    {
      int bytes_per_pixel = 0;
      switch (src_image->get_chroma_format()) {
        case heif_chroma_interleaved_RGB:
          bytes_per_pixel=3;
          break;
        case heif_chroma_interleaved_RGBA:
          bytes_per_pixel=4;
          break;
        case heif_chroma_interleaved_RRGGBB_BE:
        case heif_chroma_interleaved_RRGGBB_LE:
          bytes_per_pixel=6;
          break;
        case heif_chroma_interleaved_RRGGBBAA_BE:
        case heif_chroma_interleaved_RRGGBBAA_LE:
          bytes_per_pixel=8;
          break;
        default:
          assert(false);
      }

      int src_stride;
      uint8_t* src_data = src_image->get_plane(heif_channel_interleaved, &src_stride);
      uint64_t out_size = src_image->get_height() * src_image->get_width() * bytes_per_pixel;
      data.resize(out_size);
      for (int y = 0; y < src_image->get_height(); y++) {
        memcpy(data.data() + y * src_image->get_width() * bytes_per_pixel, src_data + src_stride * y, src_image->get_width() * bytes_per_pixel);
      }
      heif_file->append_iloc_data(out_image->get_id(), data, 0);
    }
    else
    {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   "Unsupported RGB chroma");
    }
  }
  else if (src_image->get_colorspace() == heif_colorspace_monochrome)
  {
    uint64_t offset = 0;
    std::vector<heif_channel> channels;
    if (src_image->has_channel(heif_channel_Alpha))
    {
      channels = {heif_channel_Y, heif_channel_Alpha};
    }
    else
    {
      channels = {heif_channel_Y};
    }
    for (heif_channel channel : channels)
    {
      int src_stride;
      uint8_t* src_data = src_image->get_plane(channel, &src_stride);
      uint64_t out_size = src_image->get_height() * src_stride;
      data.resize(data.size() + out_size);
      memcpy(data.data() + offset, src_data, out_size);
      offset += out_size;
    }
    heif_file->append_iloc_data(out_image->get_id(), data, 0);
  }
  else
  {
    return Error(heif_error_Unsupported_feature,
              heif_suberror_Unsupported_data_version,
              "Unsupported colourspace");
  }
  // We need to ensure ispe is essential for the uncompressed case
  std::shared_ptr<Box_ispe> ispe = std::make_shared<Box_ispe>();
  ispe->set_size(src_image->get_width(), src_image->get_height());
  heif_file->add_property(out_image->get_id(), ispe, true);

  return Error::Ok;
}
