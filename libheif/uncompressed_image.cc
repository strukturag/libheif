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


#include <cstring>
#include <algorithm>
#include <map>

#include "uncompressed_image.h"

enum Components {
  Component_Monochrome = 0,
  Component_Y = 1,
  Component_Cb = 2,
  Component_Cr = 3,
  Component_Red = 4,
  Component_Green = 5,
  Component_Blue = 6,
  Component_Alpha = 7
};


namespace heif {

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
    if ((uncC->get_interleave_type() != 0) && (uncC->get_interleave_type() != 1)) {
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
    if (uncC->get_tile_align_size() != 0) {
      std::stringstream sstr;
      sstr << "Uncompressed tile_align_size of " << ((int) uncC->get_tile_align_size()) << " is not implemented yet";
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   sstr.str());
    }
    if ((uncC->get_number_of_tile_columns() != 1) || (uncC->get_number_of_tile_rows() != 1)) {
      std::stringstream sstr;
      sstr << "Uncompressed tiled images with " << uncC->get_number_of_tile_columns() << " columns by " << uncC->get_number_of_tile_rows() << " rows is not implemented yet";
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

    // TODO: make this work for any order
    if (componentSet == ((1 << Component_Red) | (1 << Component_Green) | (1 << Component_Blue)) ||
        componentSet == ((1 << Component_Red) | (1 << Component_Green) | (1 << Component_Blue) | (1 << Component_Alpha))) {
      *out_chroma = heif_chroma_444;
      *out_colourspace = heif_colorspace_RGB;
    }

    if (componentSet == ((1 << Component_Y) | (1 << Component_Cb) | (1 << Component_Cr))) {
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


  int UncompressedImageDecoder::get_luma_bits_per_pixel_from_configuration_unci(const HeifFile& heif_file, heif_item_id imageID)
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
        case Component_Monochrome:
        case Component_Red:
        case Component_Green:
        case Component_Blue:
          alternate_channel_bits = std::max(alternate_channel_bits, component.component_bit_depth_minus_one + 1);
          break;
        case Component_Y:
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


  Error UncompressedImageDecoder::decode_uncompressed_image(const std::shared_ptr<const HeifFile>& heif_file,
                                                            heif_item_id ID,
                                                            std::shared_ptr<HeifPixelImage>& img,
                                                            uint32_t maximum_image_width_limit,
                                                            uint32_t maximum_image_height_limit,
                                                            const std::vector<uint8_t>& uncompressed_data)
  {
    // Get the properties for this item
    // We need: ispe, cmpd, uncC

    std::vector<heif::Box_ipco::Property> item_properties;
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
      auto ispe = std::dynamic_pointer_cast<Box_ispe>(prop.property);
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

      auto maybe_cmpd = std::dynamic_pointer_cast<Box_cmpd>(prop.property);
      if (maybe_cmpd) {
        cmpd = maybe_cmpd;
      }

      auto maybe_uncC = std::dynamic_pointer_cast<Box_uncC>(prop.property);
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
      if (component_type == Component_Y) {
        img->add_plane(heif_channel_Y, width, height, component.component_bit_depth_minus_one + 1);
        channels.push_back(heif_channel_Y);
        channel_to_pixelOffset.emplace(heif_channel_Y, componentOffset);
      }
      else if (component_type == Component_Cb) {
        img->add_plane(heif_channel_Cb, width, height, component.component_bit_depth_minus_one + 1);
        channels.push_back(heif_channel_Cb);
        channel_to_pixelOffset.emplace(heif_channel_Cb, componentOffset);
      }
      else if (component_type == Component_Cr) {
        img->add_plane(heif_channel_Cr, width, height, component.component_bit_depth_minus_one + 1);
        channels.push_back(heif_channel_Cr);
        channel_to_pixelOffset.emplace(heif_channel_Cr, componentOffset);
      }
      else if (component_type == Component_Red) {
        img->add_plane(heif_channel_R, width, height, component.component_bit_depth_minus_one + 1);
        channels.push_back(heif_channel_R);
        channel_to_pixelOffset.emplace(heif_channel_R, componentOffset);
      }
      else if (component_type == Component_Green) {
        img->add_plane(heif_channel_G, width, height, component.component_bit_depth_minus_one + 1);
        channels.push_back(heif_channel_G);
        channel_to_pixelOffset.emplace(heif_channel_G, componentOffset);
      }
      else if (component_type == Component_Blue) {
        img->add_plane(heif_channel_B, width, height, component.component_bit_depth_minus_one + 1);
        channels.push_back(heif_channel_B);
        channel_to_pixelOffset.emplace(heif_channel_B, componentOffset);
      }
      else if (component_type == Component_Alpha) {
        img->add_plane(heif_channel_Alpha, width, height, component.component_bit_depth_minus_one + 1);
        channels.push_back(heif_channel_Alpha);
        channel_to_pixelOffset.emplace(heif_channel_Alpha, componentOffset);
      }

      // TODO: other component types
      componentOffset++;
    }

    // TODO: properly interpret uncompressed_data per uncC config, subsampling etc.
    uint32_t bytes_per_channel = width * height;
    if (uncC->get_interleave_type() == 0) {
      // Source is planar
      for (uint32_t c = 0; c < channels.size(); c++) {
        int stride;
        uint8_t* dst = img->get_plane(channels[c], &stride);
        memcpy(dst, uncompressed_data.data() + c * bytes_per_channel, bytes_per_channel);
      }
    }
    else if (uncC->get_interleave_type() == 1) {
      // Source is pixel interleaved

      // TODO: we need to be smarter about block size, etc

      // TODO: we can only do this if we are 8 bits
      long unsigned int pixel_stride = channel_to_pixelOffset.size();
      const uint8_t* src = uncompressed_data.data();
      for (uint32_t c = 0; c < channels.size(); c++) {
        int pixel_offset = channel_to_pixelOffset[channels[c]];
        int stride;
        uint8_t* dst = img->get_plane(channels[c], &stride);
        for (uint32_t pixelIndex = 0; pixelIndex < width * height; pixelIndex++) {
          dst[pixelIndex] = src[pixel_stride * pixelIndex + pixel_offset];
        }
      }
    }

    return Error::Ok;
  }
}