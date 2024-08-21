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
#include <utility>

#include "common_utils.h"
#include "context.h"
#include "compression.h"
#include "error.h"
#include "libheif/heif.h"
#include "uncompressed.h"
#include "uncompressed_box.h"
#include "uncompressed_image.h"
#include <codecs/image_item.h>

static bool isKnownUncompressedFrameConfigurationBoxProfile(const std::shared_ptr<Box_uncC> &uncC)
{
  return ((uncC != nullptr) && (uncC->get_version() == 1) && ((uncC->get_profile() == fourcc("rgb3")) || (uncC->get_profile() == fourcc("rgba")) || (uncC->get_profile() == fourcc("abgr"))));
}

static Error uncompressed_image_type_is_supported(std::shared_ptr<Box_uncC>& uncC, std::shared_ptr<Box_cmpd>& cmpd)
{
    if (isKnownUncompressedFrameConfigurationBoxProfile(uncC))
    {
      return Error::Ok;
    }
    if (!cmpd) {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   "Missing required cmpd box (no match in uncC box) for uncompressed codec");
    }

  for (Box_uncC::Component component : uncC->get_components()) {
    uint16_t component_index = component.component_index;
    uint16_t component_type = cmpd->get_components()[component_index].component_type;
    if ((component_type > 7) && (component_type != component_type_padded)) {
      std::stringstream sstr;
      sstr << "Uncompressed image with component_type " << ((int) component_type) << " is not implemented yet";
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   sstr.str());
    }
    if ((component.component_bit_depth > 8) && (component.component_bit_depth != 16)) {
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
  if ((uncC->get_sampling_type() != sampling_mode_no_subsampling)
      && (uncC->get_sampling_type() != sampling_mode_422)
      && (uncC->get_sampling_type() != sampling_mode_420)
   ) {
    std::stringstream sstr;
    sstr << "Uncompressed sampling_type of " << ((int) uncC->get_sampling_type()) << " is not implemented yet";
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 sstr.str());
  }
  if ((uncC->get_interleave_type() != interleave_mode_component)
      && (uncC->get_interleave_type() != interleave_mode_pixel)
      && (uncC->get_interleave_type() != interleave_mode_mixed)
      && (uncC->get_interleave_type() != interleave_mode_row)
      && (uncC->get_interleave_type() != interleave_mode_tile_component)
    ) {
    std::stringstream sstr;
    sstr << "Uncompressed interleave_type of " << ((int) uncC->get_interleave_type()) << " is not implemented yet";
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 sstr.str());
  }
  // Validity checks per ISO/IEC 23001-17 Section 5.2.1.5.3
  if (uncC->get_sampling_type() == sampling_mode_422) {
    // We check Y Cb and Cr appear in the chroma test
    // TODO: error for tile width not multiple of 2
    if ((uncC->get_interleave_type() != interleave_mode_component)
        && (uncC->get_interleave_type() != interleave_mode_mixed)
        && (uncC->get_interleave_type() != interleave_mode_multi_y))
    {
      std::stringstream sstr;
      sstr << "YCbCr 4:2:2 subsampling is only valid with component, mixed or multi-Y interleave mode (ISO/IEC 23001-17 5.2.1.5.3).";
      return Error(heif_error_Invalid_input,
                  heif_suberror_Invalid_parameter_value,
                  sstr.str());
    }
    if ((uncC->get_row_align_size() != 0) && (uncC->get_interleave_type() == interleave_mode_component)) {
      if (uncC->get_row_align_size() % 2 != 0) {
        std::stringstream sstr;
        sstr << "YCbCr 4:2:2 subsampling with component interleave requires row_align_size to be a multiple of 2 (ISO/IEC 23001-17 5.2.1.5.3).";
        return Error(heif_error_Invalid_input,
                  heif_suberror_Invalid_parameter_value,
                  sstr.str());
      }
    }
    if (uncC->get_tile_align_size() != 0) {
      if (uncC->get_tile_align_size() % 2 != 0) {
        std::stringstream sstr;
        sstr << "YCbCr 4:2:2 subsampling requires tile_align_size to be a multiple of 2 (ISO/IEC 23001-17 5.2.1.5.3).";
        return Error(heif_error_Invalid_input,
                  heif_suberror_Invalid_parameter_value,
                  sstr.str());
      }
    }
  }
  // Validity checks per ISO/IEC 23001-17 Section 5.2.1.5.4
  if (uncC->get_sampling_type() == sampling_mode_422) {
    // We check Y Cb and Cr appear in the chroma test
    // TODO: error for tile width not multiple of 2
    if ((uncC->get_interleave_type() != interleave_mode_component)
        && (uncC->get_interleave_type() != interleave_mode_mixed))
    {
      std::stringstream sstr;
      sstr << "YCbCr 4:2:0 subsampling is only valid with component or mixed interleave mode (ISO/IEC 23001-17 5.2.1.5.4).";
      return Error(heif_error_Invalid_input,
                  heif_suberror_Invalid_parameter_value,
                  sstr.str());
    }
    if ((uncC->get_row_align_size() != 0) && (uncC->get_interleave_type() == interleave_mode_component)) {
      if (uncC->get_row_align_size() % 2 != 0) {
        std::stringstream sstr;
        sstr << "YCbCr 4:2:2 subsampling with component interleave requires row_align_size to be a multiple of 2 (ISO/IEC 23001-17 5.2.1.5.4).";
        return Error(heif_error_Invalid_input,
                  heif_suberror_Invalid_parameter_value,
                  sstr.str());
      }
    }
    if (uncC->get_tile_align_size() != 0) {
      if (uncC->get_tile_align_size() % 4 != 0) {
        std::stringstream sstr;
        sstr << "YCbCr 4:2:2 subsampling requires tile_align_size to be a multiple of 4 (ISO/IEC 23001-17 5.2.1.5.3).";
        return Error(heif_error_Invalid_input,
                  heif_suberror_Invalid_parameter_value,
                  sstr.str());
      }
    }
  }
  if ((uncC->get_interleave_type() == interleave_mode_mixed) && (uncC->get_sampling_type() == sampling_mode_no_subsampling))
  {
    std::stringstream sstr;
    sstr << "Interleave interleave mode is not valid with subsampling mode (ISO/IEC 23001-17 5.2.1.6.4).";
    return Error(heif_error_Invalid_input,
                heif_suberror_Invalid_parameter_value,
                sstr.str());
  }
  if ((uncC->get_interleave_type() == interleave_mode_multi_y)
    && ((uncC->get_sampling_type() != sampling_mode_422) && (uncC->get_sampling_type() != sampling_mode_411)))
  {
    std::stringstream sstr;
    sstr << "Multi-Y interleave mode is only valid with 4:2:2 and 4:1:1 subsampling modes (ISO/IEC 23001-17 5.2.1.6.7).";
    return Error(heif_error_Invalid_input,
                heif_suberror_Invalid_parameter_value,
                sstr.str());
  }
  // TODO: throw error if mixed and Cb and Cr are not adjacent.

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
  if ((uncC->get_pixel_size() != 0) && ((uncC->get_interleave_type() != interleave_mode_pixel) && (uncC->get_interleave_type() != interleave_mode_multi_y))) {
    std::stringstream sstr;
    sstr << "Uncompressed pixel_size of " << ((int) uncC->get_pixel_size()) << " is only valid with interleave_type 1 or 5 (ISO/IEC 23001-17 5.2.1.7)";
    return Error(heif_error_Invalid_input,
                 heif_suberror_Invalid_parameter_value,
                 sstr.str());
  }
  return Error::Ok;
}


Error UncompressedImageCodec::get_heif_chroma_uncompressed(std::shared_ptr<Box_uncC>& uncC, std::shared_ptr<Box_cmpd>& cmpd, heif_chroma* out_chroma, heif_colorspace* out_colourspace)
{
  *out_chroma = heif_chroma_undefined;
  *out_colourspace = heif_colorspace_undefined;

  if (isKnownUncompressedFrameConfigurationBoxProfile(uncC)) {
    *out_chroma = heif_chroma_444;
    *out_colourspace = heif_colorspace_RGB;
    return Error::Ok;
  }

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
    if (component_type == component_type_padded) {
      // not relevant for determining chroma
      continue;
    }
    componentSet |= (1 << component_type);
  }

  if (componentSet == ((1 << component_type_red) | (1 << component_type_green) | (1 << component_type_blue)) ||
      componentSet == ((1 << component_type_red) | (1 << component_type_green) | (1 << component_type_blue) | (1 << component_type_alpha))) {
    *out_chroma = heif_chroma_444;
    *out_colourspace = heif_colorspace_RGB;
  }

  if (componentSet == ((1 << component_type_Y) | (1 << component_type_Cb) | (1 << component_type_Cr))) {
    switch (uncC->get_sampling_type()) {
      case sampling_mode_no_subsampling:
        *out_chroma = heif_chroma_444;
        break;
      case sampling_mode_422:
        *out_chroma = heif_chroma_422;
        break;
      case sampling_mode_420:
        *out_chroma = heif_chroma_420;
        break;
    }
    *out_colourspace = heif_colorspace_YCbCr;
  }

  if (componentSet == ((1 << component_type_monochrome)) || componentSet == ((1 << component_type_monochrome) | (1 << component_type_alpha))) {
    // mono or mono + alpha input, mono output.
    *out_chroma = heif_chroma_monochrome;
    *out_colourspace = heif_colorspace_monochrome;
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
  if (!uncC_box) {
    return -1;
  }
  if (!cmpd_box) {
    if (isKnownUncompressedFrameConfigurationBoxProfile(uncC_box)) {
      return 8;
    } else {
      return -1;
    }
  }

  int luma_bits = 0;
  int alternate_channel_bits = 0;
  for (Box_uncC::Component component : uncC_box->get_components()) {
    uint16_t component_index = component.component_index;
    if (component_index >= cmpd_box->get_components().size()) {
      return -1;
    }
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

int UncompressedImageCodec::get_chroma_bits_per_pixel_from_configuration_unci(const HeifFile& heif_file, heif_item_id imageID)
{
  auto ipco = heif_file.get_ipco_box();
  auto ipma = heif_file.get_ipma_box();

  auto box1 = ipco->get_property_for_item_ID(imageID, ipma, fourcc("uncC"));
  std::shared_ptr<Box_uncC> uncC_box = std::dynamic_pointer_cast<Box_uncC>(box1);
  auto box2 = ipco->get_property_for_item_ID(imageID, ipma, fourcc("cmpd"));
  std::shared_ptr<Box_cmpd> cmpd_box = std::dynamic_pointer_cast<Box_cmpd>(box2);
  if (uncC_box && uncC_box->get_version() == 1) {
    // All of the version 1 cases are 8 bit
    return 8;
  }
  if (!uncC_box || !cmpd_box) {
    return -1;
  }

  int chroma_bits = 0;
  int alternate_channel_bits = 0;
  for (Box_uncC::Component component : uncC_box->get_components()) {
    uint16_t component_index = component.component_index;
    if (component_index >= cmpd_box->get_components().size()) {
      return -1;
    }
    auto component_type = cmpd_box->get_components()[component_index].component_type;
    switch (component_type) {
      case component_type_monochrome:
      case component_type_red:
      case component_type_green:
      case component_type_blue:
        alternate_channel_bits = std::max(alternate_channel_bits, (int)component.component_bit_depth);
        break;
      case component_type_Cb:
      case component_type_Cr:
        chroma_bits = std::max(chroma_bits, (int)component.component_bit_depth);
        break;
        // TODO: there are other things we'll need to handle eventually, like palette.
    }
  }
  if (chroma_bits > 0) {
    return chroma_bits;
  }
  else if (alternate_channel_bits > 0) {
    return alternate_channel_bits;
  }
  else {
    return 8;
  }
}

static bool map_uncompressed_component_to_channel(const std::shared_ptr<Box_cmpd> &cmpd, const std::shared_ptr<Box_uncC> &uncC, Box_uncC::Component component, heif_channel *channel)
{
  uint16_t component_index = component.component_index;
  if (isKnownUncompressedFrameConfigurationBoxProfile(uncC)) {
    if (uncC->get_profile() == fourcc("rgb3")) {
      switch (component_index) {
      case 0:
        *channel = heif_channel_R;
        return true;
      case 1:
        *channel = heif_channel_G;
        return true;
      case 2:
        *channel = heif_channel_B;
        return true;
      }
    } else if (uncC->get_profile() == fourcc("rgba")) {
      switch (component_index) {
      case 0:
        *channel = heif_channel_R;
        return true;
      case 1:
        *channel = heif_channel_G;
        return true;
      case 2:
        *channel = heif_channel_B;
        return true;
      case 3:
        *channel = heif_channel_Alpha;
        return true;
      }
    } else if (uncC->get_profile() == fourcc("abgr")) {
      switch (component_index) {
      case 0:
        *channel = heif_channel_Alpha;
        return true;
      case 1:
        *channel = heif_channel_B;
        return true;
      case 2:
        *channel = heif_channel_G;
        return true;
        case 3:
        *channel = heif_channel_R;
        return true;
      }
    }
  }
  uint16_t component_type = cmpd->get_components()[component_index].component_type;

  switch (component_type) {
  case component_type_monochrome:
    *channel = heif_channel_Y;
    return true;
  case component_type_Y:
    *channel = heif_channel_Y;
    return true;
  case component_type_Cb:
    *channel = heif_channel_Cb;
    return true;
  case component_type_Cr:
    *channel = heif_channel_Cr;
    return true;
  case component_type_red:
    *channel = heif_channel_R;
    return true;
  case component_type_green:
    *channel = heif_channel_G;
    return true;
  case component_type_blue:
    *channel = heif_channel_B;
    return true;
  case component_type_alpha:
    *channel = heif_channel_Alpha;
    return true;
  case component_type_padded:
    return false;
  default:
    return false;
  }
}

  class UncompressedBitReader : public BitReader
  {
  public:
    UncompressedBitReader(const std::vector<uint8_t>& data) : BitReader(data.data(), (int)data.size())
    {}

    void markPixelStart() {
      m_pixelStartOffset = get_current_byte_index();
    }

    void markRowStart() {
      m_rowStartOffset = get_current_byte_index();
    }

    void markTileStart() {
      m_tileStartOffset = get_current_byte_index();
    }

    inline void handlePixelAlignment(uint32_t pixel_size) {
      if (pixel_size != 0) {
        uint32_t bytes_in_pixel = get_current_byte_index() - m_pixelStartOffset;
        uint32_t padding = pixel_size - bytes_in_pixel;
        skip_bytes(padding);
      }
    }

    void handleRowAlignment(uint32_t alignment) {
      skip_to_byte_boundary();
      if (alignment != 0) {
        uint32_t bytes_in_row = get_current_byte_index() - m_rowStartOffset;
        uint32_t residual = bytes_in_row % alignment;
        if (residual != 0) {
          uint32_t padding = alignment - residual;
          skip_bytes(padding);
        }
      }
    }

    void handleTileAlignment(uint32_t alignment) {
      if (alignment != 0) {
        uint32_t bytes_in_tile = get_current_byte_index() - m_tileStartOffset;
        uint32_t residual = bytes_in_tile % alignment;
        if (residual != 0) {
          uint32_t tile_padding = alignment - residual;
          skip_bytes(tile_padding);
        }
      }
    }

  private:
    int m_pixelStartOffset;
    int m_rowStartOffset;
    int m_tileStartOffset;
};


class AbstractDecoder
{
public:
  virtual Error decode(const std::vector<uint8_t>& uncompressed_data, std::shared_ptr<HeifPixelImage>& img) = 0;
  virtual ~AbstractDecoder() = default;
protected:
  AbstractDecoder(uint32_t width, uint32_t height, const std::shared_ptr<Box_cmpd> cmpd, const std::shared_ptr<Box_uncC> uncC):
    m_width(width),
    m_height(height),
    m_cmpd(std::move(cmpd)),
    m_uncC(std::move(uncC))
  {
    m_tile_height = m_height / m_uncC->get_number_of_tile_rows();
    m_tile_width = m_width / m_uncC->get_number_of_tile_columns();
  }

  const uint32_t m_width;
  const uint32_t m_height;
  const std::shared_ptr<Box_cmpd> m_cmpd;
  const std::shared_ptr<Box_uncC> m_uncC;
  // TODO: see if we can make this const
  uint32_t m_tile_height;
  uint32_t m_tile_width;

  class ChannelListEntry
  {
  public:
    uint32_t get_bytes_per_tile() const
    {
      return bytes_per_tile_row_src * tile_height;
    }

    inline uint64_t getDestinationRowOffset(uint32_t tile_row, uint32_t tile_y) const {
      uint64_t dst_row_number = tile_row * tile_height + tile_y;
      return dst_row_number * dst_plane_stride;
    }

    heif_channel channel = heif_channel_Y;
    uint8_t* dst_plane;
    uint8_t* other_chroma_dst_plane;
    int dst_plane_stride;
    int other_chroma_dst_plane_stride;
    uint32_t tile_width;
    uint32_t tile_height;
    uint32_t bytes_per_component_sample;
    uint16_t bits_per_component_sample;
    uint8_t component_alignment;
    uint32_t bytes_per_tile_row_src;
    bool use_channel;
  };

  std::vector<ChannelListEntry> channelList;

  void buildChannelList(std::shared_ptr<HeifPixelImage>& img) {
    for (Box_uncC::Component component : m_uncC->get_components()) {
      ChannelListEntry entry = buildChannelListEntry(component, img);
      channelList.push_back(entry);
    }
  }

  protected:
    void processComponentSample(UncompressedBitReader &srcBits, ChannelListEntry &entry, uint64_t dst_row_offset, uint32_t tile_column,  uint32_t tile_x) {
      uint64_t dst_col_number = tile_column * entry.tile_width + tile_x;
      uint64_t dst_column_offset = dst_col_number * entry.bytes_per_component_sample;
      int val = srcBits.get_bits(entry.bits_per_component_sample);
      memcpy(entry.dst_plane + dst_row_offset + dst_column_offset, &val, entry.bytes_per_component_sample);
    }

    // Handles the case where a row consists of a single component type
    // Not valid for Pixel interleave
    // Not valid for the Cb/Cr channels in Mixed Interleave
    // Not valid for multi-Y pixel interleave
    void processComponentRow(ChannelListEntry &entry, UncompressedBitReader &srcBits, uint64_t dst_row_offset, uint32_t tile_column)
    {
      for (uint32_t tile_x = 0; tile_x < entry.tile_width; tile_x++) {
        if (entry.component_alignment != 0) {
          srcBits.skip_to_byte_boundary();
          int numPadBits = (entry.component_alignment * 8) - entry.bits_per_component_sample;
          srcBits.skip_bits(numPadBits);
        }
        processComponentSample(srcBits, entry, dst_row_offset, tile_column, tile_x);
      }
      srcBits.skip_to_byte_boundary();
    }

  private:
    ChannelListEntry buildChannelListEntry(Box_uncC::Component component, std::shared_ptr<HeifPixelImage> &img) {
      ChannelListEntry entry;
      entry.use_channel = map_uncompressed_component_to_channel(m_cmpd, m_uncC, component, &(entry.channel));
      entry.dst_plane = img->get_plane(entry.channel, &(entry.dst_plane_stride));
      entry.tile_width = m_tile_width;
      entry.tile_height = m_tile_height;
      if ((entry.channel == heif_channel_Cb) || (entry.channel == heif_channel_Cr)) {
        if (m_uncC->get_sampling_type() == sampling_mode_422) {
          entry.tile_width /= 2;
        } else if (m_uncC->get_sampling_type() == sampling_mode_420) {
          entry.tile_width /= 2;
          entry.tile_height /= 2;
        }
        if (entry.channel == heif_channel_Cb) {
          entry.other_chroma_dst_plane = img->get_plane(heif_channel_Cr, &(entry.other_chroma_dst_plane_stride));
        } else if (entry.channel == heif_channel_Cr) {
          entry.other_chroma_dst_plane = img->get_plane(heif_channel_Cb, &(entry.other_chroma_dst_plane_stride));
        }
      }
      entry.bits_per_component_sample = component.component_bit_depth;
      entry.component_alignment = component.component_align_size;
      entry.bytes_per_component_sample = (component.component_bit_depth + 7) / 8;
      entry.bytes_per_tile_row_src = entry.tile_width * entry.bytes_per_component_sample;
      return entry;
    }
};


class ComponentInterleaveDecoder : public AbstractDecoder
{
public:
  ComponentInterleaveDecoder(uint32_t width, uint32_t height, std::shared_ptr<Box_cmpd> cmpd, std::shared_ptr<Box_uncC> uncC):
    AbstractDecoder(width, height, std::move(cmpd), std::move(uncC))
  {}

  Error decode(const std::vector<uint8_t>& uncompressed_data, std::shared_ptr<HeifPixelImage>& img) override {
    UncompressedBitReader srcBits(uncompressed_data);

    buildChannelList(img);

    for (uint32_t tile_row = 0; tile_row < m_uncC->get_number_of_tile_rows(); tile_row++) {
      for (uint32_t tile_column = 0; tile_column < m_uncC->get_number_of_tile_columns(); tile_column++) {
        srcBits.markTileStart();
        processTile(srcBits, tile_row, tile_column);
        srcBits.handleTileAlignment(m_uncC->get_tile_align_size());
      }
    }

    return Error::Ok;
  }

  void processTile(UncompressedBitReader &srcBits, uint32_t tile_row, uint32_t tile_column) {
    for (ChannelListEntry &entry : channelList) {
      for (uint32_t tile_y = 0; tile_y < entry.tile_height; tile_y++) {
        srcBits.markRowStart();
        if (entry.use_channel) {
          uint64_t dst_row_offset = entry.getDestinationRowOffset(tile_row, tile_y);
          processComponentRow(entry, srcBits, dst_row_offset, tile_column);
        } else {
          srcBits.skip_bytes(entry.bytes_per_tile_row_src);
        }
        srcBits.handleRowAlignment(m_uncC->get_row_align_size());
      }
    }
  }
};

class PixelInterleaveDecoder : public AbstractDecoder
{
public:
  PixelInterleaveDecoder(uint32_t width, uint32_t height, std::shared_ptr<Box_cmpd> cmpd, std::shared_ptr<Box_uncC> uncC):
    AbstractDecoder(width, height, std::move(cmpd), std::move(uncC))
  {}

  Error decode(const std::vector<uint8_t>& uncompressed_data, std::shared_ptr<HeifPixelImage>& img) override {
    UncompressedBitReader srcBits(uncompressed_data);

    buildChannelList(img);

    for (uint32_t tile_row = 0; tile_row < m_uncC->get_number_of_tile_rows(); tile_row++) {
      for (uint32_t tile_column = 0; tile_column < m_uncC->get_number_of_tile_columns(); tile_column++) {
        srcBits.markTileStart();
        processTile(srcBits, tile_row, tile_column);
        srcBits.handleTileAlignment(m_uncC->get_tile_align_size());
      }
    }

    return Error::Ok;
  }

  void processTile(UncompressedBitReader &srcBits, uint32_t tile_row, uint32_t tile_column) {
    for (uint32_t tile_y = 0; tile_y < m_tile_height; tile_y++) {
      srcBits.markRowStart();
      for (uint32_t tile_x = 0; tile_x < m_tile_width; tile_x++) {
        srcBits.markPixelStart();
        for (ChannelListEntry &entry : channelList) {
          if (entry.use_channel) {
            uint64_t dst_row_offset = entry.getDestinationRowOffset(tile_row, tile_y);
            if (entry.component_alignment != 0) {
              srcBits.skip_to_byte_boundary();
              int numPadBits = (entry.component_alignment * 8) - entry.bits_per_component_sample;
              srcBits.skip_bits(numPadBits);
            }
            processComponentSample(srcBits, entry, dst_row_offset, tile_column, tile_x);
          } else {
            srcBits.skip_bytes(entry.bytes_per_component_sample);
          }
        }
        srcBits.handlePixelAlignment(m_uncC->get_pixel_size());
      }
      srcBits.handleRowAlignment(m_uncC->get_row_align_size());
    }
  }
};

class MixedInterleaveDecoder : public AbstractDecoder
{
public:
  MixedInterleaveDecoder(uint32_t width, uint32_t height, std::shared_ptr<Box_cmpd> cmpd, std::shared_ptr<Box_uncC> uncC):
    AbstractDecoder(width, height, std::move(cmpd), std::move(uncC))
  {}

  Error decode(const std::vector<uint8_t>& uncompressed_data, std::shared_ptr<HeifPixelImage>& img) override {
    UncompressedBitReader srcBits(uncompressed_data);

    buildChannelList(img);

    for (uint32_t tile_row = 0; tile_row < m_uncC->get_number_of_tile_rows(); tile_row++) {
      for (uint32_t tile_column = 0; tile_column < m_uncC->get_number_of_tile_columns(); tile_column++) {
        srcBits.markTileStart();
        processTile(srcBits, tile_row, tile_column);
        srcBits.handleTileAlignment(m_uncC->get_tile_align_size());
      }
    }

    return Error::Ok;
  }

  void processTile(UncompressedBitReader &srcBits, uint32_t tile_row, uint32_t tile_column) {
    bool haveProcessedChromaForThisTile = false;
    for (ChannelListEntry &entry : channelList) {
      if (entry.use_channel) {
        if ((entry.channel == heif_channel_Cb) || (entry.channel == heif_channel_Cr)) {
          if (!haveProcessedChromaForThisTile) {
            for (uint32_t tile_y = 0; tile_y < entry.tile_height; tile_y++) {
              // TODO: row padding
              uint64_t dst_row_number = tile_row * entry.tile_width + tile_y;
              uint64_t dst_row_offset = dst_row_number * entry.dst_plane_stride;
              for (uint32_t tile_x = 0; tile_x < entry.tile_width; tile_x++) {
                uint64_t dst_column_number = tile_column * entry.tile_width + tile_x;
                uint64_t dst_column_offset = dst_column_number * entry.bytes_per_component_sample;
                int val = srcBits.get_bits(entry.bytes_per_component_sample * 8);
                memcpy(entry.dst_plane + dst_row_offset + dst_column_offset, &val, entry.bytes_per_component_sample);
                val = srcBits.get_bits(entry.bytes_per_component_sample * 8);
                memcpy(entry.other_chroma_dst_plane + dst_row_offset + dst_column_offset, &val, entry.bytes_per_component_sample);
              }
              haveProcessedChromaForThisTile = true;
            }
          }
        } else {
          for (uint32_t tile_y = 0; tile_y < entry.tile_height; tile_y++) {
            uint64_t dst_row_offset = entry.getDestinationRowOffset(tile_row, tile_y);
            processComponentRow(entry, srcBits, dst_row_offset, tile_column);
          }
        }
      } else {
        // skip over the data we are not using
        srcBits.skip_bytes(entry.get_bytes_per_tile());
        continue;
      }
    }
  }
};

class RowInterleaveDecoder : public AbstractDecoder
{
public:
  RowInterleaveDecoder(uint32_t width, uint32_t height, std::shared_ptr<Box_cmpd> cmpd, std::shared_ptr<Box_uncC> uncC):
    AbstractDecoder(width, height, std::move(cmpd), std::move(uncC))
  {}

  Error decode(const std::vector<uint8_t>& uncompressed_data, std::shared_ptr<HeifPixelImage>& img) override {
    UncompressedBitReader srcBits(uncompressed_data);
    buildChannelList(img);
    for (uint32_t tile_row = 0; tile_row < m_uncC->get_number_of_tile_rows(); tile_row++) {
      for (uint32_t tile_column = 0; tile_column < m_uncC->get_number_of_tile_columns(); tile_column++) {
        srcBits.markTileStart();
        processTile(srcBits, tile_row, tile_column);
        srcBits.handleTileAlignment(m_uncC->get_tile_align_size());
      }
    }
    return Error::Ok;
  }

private:
  void processTile(UncompressedBitReader &srcBits, uint32_t tile_row, uint32_t tile_column) {
    for (uint32_t tile_y = 0; tile_y < m_tile_height; tile_y++) {
      for (ChannelListEntry &entry : channelList) {
        srcBits.markRowStart();
        if (entry.use_channel) {
          uint64_t dst_row_offset = entry.getDestinationRowOffset(tile_row, tile_y);
          processComponentRow(entry, srcBits, dst_row_offset, tile_column);
        } else {
          srcBits.skip_bytes(entry.bytes_per_tile_row_src);
        }
        srcBits.handleRowAlignment(m_uncC->get_row_align_size());
      }
    }
  }
};


class TileComponentInterleaveDecoder : public AbstractDecoder
{
public:
  TileComponentInterleaveDecoder(uint32_t width, uint32_t height, std::shared_ptr<Box_cmpd> cmpd, std::shared_ptr<Box_uncC> uncC):
    AbstractDecoder(width, height, std::move(cmpd), std::move(uncC))
  {}

  Error decode(const std::vector<uint8_t>& uncompressed_data, std::shared_ptr<HeifPixelImage>& img) override {
    UncompressedBitReader srcBits(uncompressed_data);

    buildChannelList(img);

    for (ChannelListEntry &entry : channelList) {
      if (!entry.use_channel) {
        uint64_t bytes_per_component = entry.get_bytes_per_tile() * m_uncC->get_number_of_tile_columns() * m_uncC->get_number_of_tile_rows();
        srcBits.skip_bytes((int)bytes_per_component);
        continue;
      }
      for (uint32_t tile_row = 0; tile_row < m_uncC->get_number_of_tile_rows(); tile_row++) {
        for (uint32_t tile_column = 0; tile_column < m_uncC->get_number_of_tile_columns(); tile_column++) {
          srcBits.markTileStart();
          for (uint32_t tile_y = 0; tile_y < entry.tile_height; tile_y++) {
            srcBits.markRowStart();
            uint64_t dst_row_offset = entry.getDestinationRowOffset(tile_row, tile_y);
            processComponentRow(entry, srcBits, dst_row_offset, tile_column);
            srcBits.handleRowAlignment(m_uncC->get_row_align_size());
          }
          srcBits.handleTileAlignment(m_uncC->get_tile_align_size());
        }
      }
    }

    return Error::Ok;
  }
};

static AbstractDecoder* makeDecoder(uint32_t width, uint32_t height, const std::shared_ptr<Box_cmpd>& cmpd, const std::shared_ptr<Box_uncC>& uncC)
{
  if (uncC->get_interleave_type() == interleave_mode_component) {
    return new ComponentInterleaveDecoder(width, height, cmpd, uncC);
  } else if (uncC->get_interleave_type() == interleave_mode_pixel) {
    return new PixelInterleaveDecoder(width, height, cmpd, uncC);
  } else if (uncC->get_interleave_type() == interleave_mode_mixed) {
    return new MixedInterleaveDecoder(width, height, cmpd, uncC);
  } else if (uncC->get_interleave_type() == interleave_mode_row) {
    return new RowInterleaveDecoder(width, height, cmpd, uncC);
  } else if (uncC->get_interleave_type() == interleave_mode_tile_component) {
    return new TileComponentInterleaveDecoder(width, height, cmpd, uncC);
  } else {
    return nullptr;
  }
}

Error UncompressedImageCodec::decode_uncompressed_image(const HeifContext* context,
                                                        heif_item_id ID,
                                                        std::shared_ptr<HeifPixelImage>& img,
                                                        const std::vector<uint8_t>& source_data)
{
  if (source_data.empty()) {
    return {heif_error_Invalid_input,
            heif_suberror_Unspecified,
            "Uncompressed image data is empty"};
  }

  // Get the properties for this item
  // We need: ispe, cmpd, uncC
  std::vector<std::shared_ptr<Box>> item_properties;
  Error error = context->get_heif_file()->get_properties(ID, item_properties);
  if (error) {
    return error;
  }

  uint32_t width = 0;
  uint32_t height = 0;
  bool found_ispe = false;
  std::shared_ptr<Box_cmpd> cmpd;
  std::shared_ptr<Box_uncC> uncC;
  std::shared_ptr<Box_cmpC> cmpC;
  std::shared_ptr<Box_icef> icef;

  for (const auto& prop : item_properties) {
    auto ispe = std::dynamic_pointer_cast<Box_ispe>(prop);
    if (ispe) {
      width = ispe->get_width();
      height = ispe->get_height();
      error = context->check_resolution(width, height);
      if (error) {
        return error;
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

    auto maybe_cmpC = std::dynamic_pointer_cast<Box_cmpC>(prop);
    if (maybe_cmpC) {
      cmpC = maybe_cmpC;
    }

    auto maybe_icef = std::dynamic_pointer_cast<Box_icef>(prop);
    if (maybe_icef) {
      icef= maybe_icef;
    }

  }


  // if we miss a required box, show error
  if (!found_ispe) {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Missing required ispe box for uncompressed codec");
  }
  if (!uncC) {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Missing required uncC box for uncompressed codec");
  }
  if (!cmpd && (uncC->get_version() !=1)) {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Missing required cmpd or uncC version 1 box for uncompressed codec");
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

  for (Box_uncC::Component component : uncC->get_components()) {
    heif_channel channel;
    if (map_uncompressed_component_to_channel(cmpd, uncC, component, &channel)) {
      if ((channel == heif_channel_Cb) || (channel == heif_channel_Cr)) {
        img->add_plane(channel, (width / chroma_h_subsampling(chroma)), (height / chroma_v_subsampling(chroma)), component.component_bit_depth);
      } else {
        img->add_plane(channel, width, height, component.component_bit_depth);
      }
    }
  }

  AbstractDecoder *decoder = makeDecoder(width, height, cmpd, uncC);
  if (decoder != nullptr) {
    Error result = decoder->decode(source_data, img);
    delete decoder;
    return result;
  } else {
    std::stringstream sstr;
    sstr << "Uncompressed interleave_type of " << ((int) uncC->get_interleave_type()) << " is not implemented yet";
    return Error(heif_error_Unsupported_feature,
                heif_suberror_Unsupported_data_version,
               sstr.str());
  }
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
    uint8_t bpp_y = image->get_bits_per_pixel(heif_channel_Y);
    Box_uncC::Component component0 = {0, bpp_y, component_format_unsigned, 0};
    uncC->add_component(component0);
    uint8_t bpp_cb = image->get_bits_per_pixel(heif_channel_Cb);
    Box_uncC::Component component1 = {1, bpp_cb, component_format_unsigned, 0};
    uncC->add_component(component1);
    uint8_t bpp_cr = image->get_bits_per_pixel(heif_channel_Cr);
    Box_uncC::Component component2 = {2, bpp_cr, component_format_unsigned, 0};
    uncC->add_component(component2);
    if (image->get_chroma_format() == heif_chroma_444)
    {
      uncC->set_sampling_type(sampling_mode_no_subsampling);
    }
    else if (image->get_chroma_format() == heif_chroma_422)
    {
      uncC->set_sampling_type(sampling_mode_422);
    }
    else if (image->get_chroma_format() == heif_chroma_420)
    {
      uncC->set_sampling_type(sampling_mode_420);
    }
    else
    {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   "Unsupported YCbCr sub-sampling type");
    }
    uncC->set_interleave_type(interleave_mode_component);
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
      uncC->set_interleave_type(interleave_mode_pixel);
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
      uncC->set_interleave_type(interleave_mode_component);
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
    uncC->set_sampling_type(sampling_mode_no_subsampling);
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
    uncC->set_sampling_type(sampling_mode_no_subsampling);
    uncC->set_interleave_type(interleave_mode_component);
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


static void maybe_make_minimised_uncC(std::shared_ptr<Box_uncC>& uncC, const std::shared_ptr<HeifPixelImage>& image)
{
  uncC->set_version(0);
  if (image->get_colorspace() != heif_colorspace_RGB) {
    return;
  }
  if (!((image->get_chroma_format() == heif_chroma_interleaved_RGB) || (image->get_chroma_format() == heif_chroma_interleaved_RGBA))) {
    return;
  }
  if (image->get_bits_per_pixel(heif_channel_interleaved) != 8) {
    return;
  }
  if (image->get_chroma_format() == heif_chroma_interleaved_RGBA) {
    uncC->set_profile(fourcc_to_uint32("rgba"));
  } else {
    uncC->set_profile(fourcc_to_uint32("rgb3"));
  }
  uncC->set_version(1);
}


Result<ImageItem::CodedImageData> ImageItem_uncompressed::encode(const std::shared_ptr<HeifPixelImage>& src_image,
                                                                 struct heif_encoder* encoder,
                                                                 const struct heif_encoding_options& options,
                                                                 enum heif_image_input_class input_class)
{
  CodedImageData codedImageData;

#if WITH_UNCOMPRESSED_CODEC
  std::shared_ptr<Box_uncC> uncC = std::make_shared<Box_uncC>();
  if (options.prefer_uncC_short_form) {
    maybe_make_minimised_uncC(uncC, src_image);
  }
  if (uncC->get_version() == 1) {
    codedImageData.properties.push_back(uncC);
  } else {
    std::shared_ptr<Box_cmpd> cmpd = std::make_shared<Box_cmpd>();

    Error error = fill_cmpd_and_uncC(cmpd, uncC, src_image);
    if (error) {
      return error;
    }

    codedImageData.properties.push_back(cmpd);
    codedImageData.properties.push_back(uncC);
  }

  std::vector<uint8_t> data;

  if (src_image->get_colorspace() == heif_colorspace_YCbCr)
  {
    uint64_t offset = 0;
    for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr})
    {
      int src_stride;
      uint32_t src_width = src_image->get_width(channel);
      uint32_t src_height = src_image->get_height(channel);
      uint8_t* src_data = src_image->get_plane(channel, &src_stride);
      uint64_t out_size = src_width * src_height;
      data.resize(data.size() + out_size);
      for (uint32_t y = 0; y < src_height; y++) {
        memcpy(data.data() + offset + y * src_width, src_data + src_stride * y, src_width);
      }
      offset += out_size;
    }

    codedImageData.append(data.data(), data.size());
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

      codedImageData.append(data.data(), data.size());
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
      for (uint32_t y = 0; y < src_image->get_height(); y++) {
        memcpy(data.data() + y * src_image->get_width() * bytes_per_pixel, src_data + src_stride * y, src_image->get_width() * bytes_per_pixel);
      }

      codedImageData.append(data.data(), data.size());
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

    codedImageData.append(data.data(), data.size());
  }
  else
  {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Unsupported colourspace");
  }
#endif

  return codedImageData;
}


int ImageItem_uncompressed::get_luma_bits_per_pixel() const
{
  int bpp = UncompressedImageCodec::get_luma_bits_per_pixel_from_configuration_unci(*get_file(), get_id());
  return bpp;
}


int ImageItem_uncompressed::get_chroma_bits_per_pixel() const
{
  int bpp = UncompressedImageCodec::get_chroma_bits_per_pixel_from_configuration_unci(*get_file(), get_id());
  return bpp;
}
