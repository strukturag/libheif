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

#include "uncompressed_image.h"


enum heif_component_type
{
  heif_component_type_monochrome = 0,
  heif_component_type_Y = 1,
  heif_component_type_Cb = 2,
  heif_component_type_Cr = 3,
  heif_component_type_red = 4,
  heif_component_type_green = 5,
  heif_component_type_blue = 6,
  heif_component_type_alpha = 7,
  heif_component_type_depth = 8,
  heif_component_type_disparity = 9,
  heif_component_type_palette = 10,
  heif_component_type_filter_array = 11,
  heif_component_type_padded = 12
};


enum heif_uncompressed_interleave_type
{
  heif_uncompressed_interleave_type_component = 0,
  heif_uncompressed_interleave_type_pixel = 1,
  heif_uncompressed_interleave_type_mixed = 2,
  heif_uncompressed_interleave_type_row = 3,
  heif_uncompressed_interleave_type_tile_component = 4,
  heif_uncompressed_interleave_type_multi_y = 5
};


Error Box_cmpd::parse(BitstreamRange& range)
{
  int component_count = range.read16();

  for (int i = 0; i < component_count && !range.error() && !range.eof(); i++) {
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

std::string Box_cmpd::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  for (const auto& component : m_components) {
    sstr << indent << "component_type: " << component.component_type << "\n";
    if (component.component_type >= 0x8000) {
      sstr << indent << "| component_type_uri: " << component.component_type_uri << "\n";
    }
  }

  return sstr.str();
}

Error Box_cmpd::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write16((uint16_t) m_components.size());
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

  int component_count = range.read16();

  for (int i = 0; i < component_count && !range.error() && !range.eof(); i++) {
    Component component;
    component.component_index = range.read16();
    component.component_bit_depth_minus_one = range.read8();
    component.component_format = range.read8();
    component.component_align_size = range.read8();
    m_components.push_back(component);
  }

  m_sampling_type = range.read8();

  m_interleave_type = range.read8();

  m_block_size = range.read8();

  uint8_t flags = range.read8();
  m_components_little_endian = !!(flags & 0x80);
  m_block_pad_lsb = !!(flags & 0x40);
  m_block_little_endian = !!(flags & 0x20);
  m_block_reversed = !!(flags & 0x10);
  m_pad_unknown = !!(flags & 0x08);

  m_pixel_size = range.read8();

  m_row_align_size = range.read32();

  m_tile_align_size = range.read32();

  m_num_tile_cols_minus_one = range.read32();

  m_num_tile_rows_minus_one = range.read32();

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
    sstr << indent << "component_bit_depth_minus_one: " << (int) component.component_bit_depth_minus_one << "\n";
    sstr << indent << "component_format: " << (int) component.component_format << "\n";
    sstr << indent << "component_align_size: " << (int) component.component_align_size << "\n";
  }

  sstr << indent << "sampling_type: " << (int) m_sampling_type << "\n";

  sstr << indent << "interleave_type: " << (int) m_interleave_type << "\n";

  sstr << indent << "block_size: " << (int) m_block_size << "\n";

  sstr << indent << "components_little_endian: " << m_components_little_endian << "\n";
  sstr << indent << "block_pad_lsb: " << m_block_pad_lsb << "\n";
  sstr << indent << "block_little_endian: " << m_block_little_endian << "\n";
  sstr << indent << "block_reversed: " << m_block_reversed << "\n";
  sstr << indent << "pad_unknown: " << m_pad_unknown << "\n";

  sstr << indent << "pixel_size: " << (int) m_pixel_size << "\n";

  sstr << indent << "row_align_size: " << m_row_align_size << "\n";

  sstr << indent << "tile_align_size: " << m_tile_align_size << "\n";

  sstr << indent << "num_tile_cols_minus_one: " << m_num_tile_cols_minus_one << "\n";

  sstr << indent << "num_tile_rows_minus_one: " << m_num_tile_rows_minus_one << "\n";

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
  // TODO: this is not complete
  prepend_header(writer, box_start);

  return Error::Ok;
}


static Error uncompressed_image_type_is_supported(std::shared_ptr<Box_uncC>& uncC, std::shared_ptr<Box_cmpd>& cmpd)
{
  for (Box_uncC::Component component : uncC->get_components()) {
    uint16_t component_index = component.component_index;
    uint16_t component_type = cmpd->get_components()[component_index].component_type;
    if ((component_type == 0) || (component_type > 7)) {
      std::stringstream sstr;
      sstr << "Uncompressed image with component_type " << ((int) component_type) << " is not implemented yet";
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   sstr.str());
    }
    if (component.component_bit_depth_minus_one + 1 != 8) {
      std::stringstream sstr;
      sstr << "Uncompressed image with component_bit_depth_minus_one " << ((int) component.component_bit_depth_minus_one) << " is not implemented yet";
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   sstr.str());
    }
    if (component.component_format != 0) {
      std::stringstream sstr;
      sstr << "Uncompressed image with component_format " << ((int) component.component_format) << " is not implemented yet";
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   sstr.str());
    }
    if (component.component_align_size != 0) {
      std::stringstream sstr;
      sstr << "Uncompressed image with component_align_size " << ((int) component.component_align_size) << " is not implemented yet";
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   sstr.str());
    }
  }
  if (uncC->get_sampling_type() != 0) {
    std::stringstream sstr;
    sstr << "Uncompressed sampling_type of " << ((int) uncC->get_sampling_type()) << " is not implemented yet";
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 sstr.str());
  }
  if ((uncC->get_interleave_type() != heif_uncompressed_interleave_type_component)
      && (uncC->get_interleave_type() != heif_uncompressed_interleave_type_pixel)
      && (uncC->get_interleave_type() != heif_uncompressed_interleave_type_row)
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

    componentSet |= (1 << component_type);
  }

  if (componentSet == ((1 << heif_component_type_red) | (1 << heif_component_type_green) | (1 << heif_component_type_blue)) ||
      componentSet == ((1 << heif_component_type_red) | (1 << heif_component_type_green) | (1 << heif_component_type_blue) | (1 << heif_component_type_alpha))) {
    *out_chroma = heif_chroma_444;
    *out_colourspace = heif_colorspace_RGB;
  }

  if (componentSet == ((1 << heif_component_type_Y) | (1 << heif_component_type_Cb) | (1 << heif_component_type_Cr))) {
    if (uncC->get_interleave_type() == 0) {
      // Planar YCbCr
      *out_chroma = heif_chroma_444;
      *out_colourspace = heif_colorspace_YCbCr;
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
      case heif_component_type_monochrome:
      case heif_component_type_red:
      case heif_component_type_green:
      case heif_component_type_blue:
        alternate_channel_bits = std::max(alternate_channel_bits, component.component_bit_depth_minus_one + 1);
        break;
      case heif_component_type_Y:
        luma_bits = std::max(luma_bits, component.component_bit_depth_minus_one + 1);
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


static long unsigned int get_tile_base_offset(uint32_t col, uint32_t row, const std::shared_ptr<Box_uncC>& uncC, const std::vector<heif_channel>& channels, uint32_t width, uint32_t height)
{
  uint32_t numTileColumns = uncC->get_number_of_tile_columns();
  uint32_t numTileRows = uncC->get_number_of_tile_rows();
  uint32_t tile_width = width / numTileColumns;
  uint32_t tile_height = height / numTileRows;
  // TODO: assumes 8 bits per channel
  long unsigned int content_bytes_per_tile = tile_width * tile_height * channels.size();
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
    if (component_type == heif_component_type_Y) {
      img->add_plane(heif_channel_Y, width, height, component.component_bit_depth_minus_one + 1);
      channels.push_back(heif_channel_Y);
      channel_to_pixelOffset.emplace(heif_channel_Y, componentOffset);
    }
    else if (component_type == heif_component_type_Cb) {
      img->add_plane(heif_channel_Cb, width, height, component.component_bit_depth_minus_one + 1);
      channels.push_back(heif_channel_Cb);
      channel_to_pixelOffset.emplace(heif_channel_Cb, componentOffset);
    }
    else if (component_type == heif_component_type_Cr) {
      img->add_plane(heif_channel_Cr, width, height, component.component_bit_depth_minus_one + 1);
      channels.push_back(heif_channel_Cr);
      channel_to_pixelOffset.emplace(heif_channel_Cr, componentOffset);
    }
    else if (component_type == heif_component_type_red) {
      img->add_plane(heif_channel_R, width, height, component.component_bit_depth_minus_one + 1);
      channels.push_back(heif_channel_R);
      channel_to_pixelOffset.emplace(heif_channel_R, componentOffset);
    }
    else if (component_type == heif_component_type_green) {
      img->add_plane(heif_channel_G, width, height, component.component_bit_depth_minus_one + 1);
      channels.push_back(heif_channel_G);
      channel_to_pixelOffset.emplace(heif_channel_G, componentOffset);
    }
    else if (component_type == heif_component_type_blue) {
      img->add_plane(heif_channel_B, width, height, component.component_bit_depth_minus_one + 1);
      channels.push_back(heif_channel_B);
      channel_to_pixelOffset.emplace(heif_channel_B, componentOffset);
    }
    else if (component_type == heif_component_type_alpha) {
      img->add_plane(heif_channel_Alpha, width, height, component.component_bit_depth_minus_one + 1);
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
  if (uncC->get_interleave_type() == heif_uncompressed_interleave_type_component) {
    // Source is planar
    // TODO: assumes 8 bits
    long unsigned int content_bytes_per_tile = tile_width * tile_height * channels.size();
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
            memcpy(dst + dst_offset, uncompressed_data.data() + src_offset, tile_width);
          }
        }
      }
    }
  }
  else if (uncC->get_interleave_type() == heif_uncompressed_interleave_type_pixel) {
    // TODO: we need to be smarter about block size, etc

    // TODO: we can only do this if we are 8 bits
    long unsigned int pixel_stride = channel_to_pixelOffset.size();
    const uint8_t* src = uncompressed_data.data();
    for (uint32_t c = 0; c < channels.size(); c++) {
      int pixel_offset = channel_to_pixelOffset[channels[c]];
      int stride;
      uint8_t* dst = img->get_plane(channels[c], &stride);
      for (uint32_t row = 0; row < height; row++) {
        long unsigned int tile_row_idx = row % tile_height;
        long unsigned int tile_row_offset = tile_width * tile_row_idx * channels.size();
        uint32_t col = 0;
        for (col = 0; col < width; col++) {
          long unsigned int tile_base_offset = get_tile_base_offset(col, row, uncC, channels, width, height);
          long unsigned int tile_col = col % tile_width;
          long unsigned int tile_offset = tile_row_offset + tile_col * pixel_stride + pixel_offset;
          long unsigned int src_offset = tile_base_offset + tile_offset;
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
  else if (uncC->get_interleave_type() == heif_uncompressed_interleave_type_row) {
    // TODO: we need to be smarter about block size, etc

    // TODO: we can only do this if we are 8 bits
    for (uint32_t c = 0; c < channels.size(); c++) {
      int pixel_offset = channel_to_pixelOffset[channels[c]];
      int stride;
      uint8_t* dst = img->get_plane(channels[c], &stride);
      for (uint32_t row = 0; row < height; row++) {
        long unsigned int tile_row_idx = row % tile_height;
        long unsigned int tile_row_offset = tile_width * (tile_row_idx * channels.size() + pixel_offset);
        uint32_t col = 0;
        for (col = 0; col < width; col += tile_width) {
          long unsigned int tile_base_offset = get_tile_base_offset(col, row, uncC, channels, width, height);
          long unsigned int tile_col = col % tile_width;
          long unsigned int tile_offset = tile_row_offset + tile_col;
          long unsigned int src_offset = tile_base_offset + tile_offset;
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

Error UncompressedImageCodec::encode_uncompressed_image(const std::shared_ptr<HeifFile>& heif_file,
                                                        const std::shared_ptr<HeifPixelImage>& src_image,
                                                        void* encoder_struct,
                                                        const struct heif_encoding_options& options,
                                                        std::shared_ptr<HeifContext::Image> out_image)
{
  //encoder_struct_uncompressed* encoder = (encoder_struct_uncompressed*)encoder_struct;

  printf("UNCOMPRESSED\n");

#if 0
  Box_uncC::configuration config;

  // Fill preliminary av1C in case we cannot parse the sequence_header() correctly in the code below.
  // TODO: maybe we can remove this later.
  fill_av1C_configuration(&config, src_image);

  heif_image c_api_image;
  c_api_image.image = src_image;

  struct heif_error err = encoder->plugin->encode_image(encoder->encoder, &c_api_image, input_class);
  if (err.code) {
    return Error(err.code,
                 err.subcode,
                 err.message);
  }

  for (;;) {
    uint8_t* data;
    int size;

    encoder->plugin->get_compressed_data(encoder->encoder, &data, &size, nullptr);

    bool found_config = fill_av1C_configuration_from_stream(&config, data, size);
    (void) found_config;

    if (data == nullptr) {
      break;
    }

    std::vector<uint8_t> vec;
    vec.resize(size);
    memcpy(vec.data(), data, size);

    m_heif_file->append_iloc_data(image_id, vec);
  }

  m_heif_file->add_av1C_property(image_id);
  m_heif_file->set_av1C_configuration(image_id, config);

  //m_heif_file->add_orientation_properties(image_id, options->image_orientation);

  uint32_t input_width, input_height;
  input_width = src_image->get_width();
  input_height = src_image->get_height();
  m_heif_file->add_ispe_property(image_id,
                                 get_rotated_width(options->image_orientation, input_width, input_height),
                                 get_rotated_height(options->image_orientation, input_width, input_height));
#endif

  return Error::Ok;
}
