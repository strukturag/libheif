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

static bool isKnownUncompressedFrameConfigurationBoxProfile(const std::shared_ptr<const Box_uncC>& uncC)
{
  return ((uncC != nullptr) && (uncC->get_version() == 1) && ((uncC->get_profile() == fourcc("rgb3")) || (uncC->get_profile() == fourcc("rgba")) || (uncC->get_profile() == fourcc("abgr"))));
}


static Error uncompressed_image_type_is_supported(const std::shared_ptr<const Box_uncC>& uncC,
                                                  const std::shared_ptr<const Box_cmpd>& cmpd)
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


Error UncompressedImageCodec::get_heif_chroma_uncompressed(const std::shared_ptr<const Box_uncC>& uncC,
                                                           const std::shared_ptr<const Box_cmpd>& cmpd,
                                                           heif_chroma* out_chroma, heif_colorspace* out_colourspace)
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
  std::shared_ptr<Box_uncC> uncC_box = heif_file.get_property<Box_uncC>(imageID);
  std::shared_ptr<Box_cmpd> cmpd_box = heif_file.get_property<Box_cmpd>(imageID);

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
  std::shared_ptr<Box_uncC> uncC_box = heif_file.get_property<Box_uncC>(imageID);
  std::shared_ptr<Box_cmpd> cmpd_box = heif_file.get_property<Box_cmpd>(imageID);

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

static bool map_uncompressed_component_to_channel(const std::shared_ptr<const Box_cmpd> &cmpd,
                                                  const std::shared_ptr<const Box_uncC> &uncC,
                                                  Box_uncC::Component component,
                                                  heif_channel *channel)
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


template <typename T> T nAlignmentSkipBytes(uint32_t alignment, T size) {
  if (alignment==0) {
    return 0;
  }

  T residual = size % alignment;
  if (residual==0) {
    return 0;
  }

  return alignment - residual;
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
  virtual ~AbstractDecoder() = default;

  virtual Error decode_tile(const HeifContext* context,
                            heif_item_id item_id,
                            std::shared_ptr<HeifPixelImage>& img,
                            uint32_t out_x0, uint32_t out_y0,
                            uint32_t image_width, uint32_t image_height,
                            uint32_t tile_x, uint32_t tile_y) { assert(false); return Error{heif_error_Unsupported_feature,
                                                                                            heif_suberror_Unsupported_image_type,
                                                                                            "unci tile decoding not supported for this image type"};}

protected:
  AbstractDecoder(uint32_t width, uint32_t height, const std::shared_ptr<Box_cmpd> cmpd, const std::shared_ptr<Box_uncC> uncC):
    m_width(width),
    m_height(height),
    m_cmpd(std::move(cmpd)),
    m_uncC(std::move(uncC))
  {
    m_tile_height = m_height / m_uncC->get_number_of_tile_rows();
    m_tile_width = m_width / m_uncC->get_number_of_tile_columns();

    assert(m_tile_width > 0);
    assert(m_tile_height > 0);
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
    uint32_t dst_plane_stride;
    uint32_t other_chroma_dst_plane_stride;
    uint32_t tile_width;
    uint32_t tile_height;
    uint32_t bytes_per_component_sample;
    uint16_t bits_per_component_sample;
    uint8_t component_alignment;
    uint32_t bytes_per_tile_row_src;
    bool use_channel;
  };

  std::vector<ChannelListEntry> channelList;

public:
  void buildChannelList(std::shared_ptr<HeifPixelImage>& img) {
    for (Box_uncC::Component component : m_uncC->get_components()) {
      ChannelListEntry entry = buildChannelListEntry(component, img);
      channelList.push_back(entry);
    }
  }

protected:
    void processComponentSample(UncompressedBitReader &srcBits, const ChannelListEntry &entry, uint64_t dst_row_offset, uint32_t tile_column,  uint32_t tile_x) {
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

  void processComponentTileSample(UncompressedBitReader &srcBits, const ChannelListEntry &entry, uint64_t dst_offset, uint32_t tile_x) {
    uint64_t dst_sample_offset = tile_x * entry.bytes_per_component_sample;
    int val = srcBits.get_bits(entry.bits_per_component_sample);
    memcpy(entry.dst_plane + dst_offset + dst_sample_offset, &val, entry.bytes_per_component_sample);
  }

  // Handles the case where a row consists of a single component type
  // Not valid for Pixel interleave
  // Not valid for the Cb/Cr channels in Mixed Interleave
  // Not valid for multi-Y pixel interleave
  void processComponentTileRow(ChannelListEntry &entry, UncompressedBitReader &srcBits, uint64_t dst_offset)
  {
    for (uint32_t tile_x = 0; tile_x < entry.tile_width; tile_x++) {
      if (entry.component_alignment != 0) {
        srcBits.skip_to_byte_boundary();
        int numPadBits = (entry.component_alignment * 8) - entry.bits_per_component_sample;
        srcBits.skip_bits(numPadBits);
      }
      processComponentTileSample(srcBits, entry, dst_offset, tile_x);
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

protected:

  // generic compression and uncompressed, per 23001-17
  const Error get_compressed_image_data_uncompressed(const HeifContext* context, heif_item_id ID,
                                                     std::vector<uint8_t> *data,
                                                     uint64_t range_start_offset, uint64_t range_size,
                                                     uint32_t tile_idx,
                                                     const Box_iloc::Item *item) const
  {
    // --- get codec configuration

    std::shared_ptr<const Box_cmpC> cmpC_box = context->get_heif_file()->get_property<const Box_cmpC>(ID);
    std::shared_ptr<const Box_icef> icef_box = context->get_heif_file()->get_property<const Box_icef>(ID);

    if (!cmpC_box) {
      // assume no generic compression
      return context->get_heif_file()->append_data_from_iloc(ID, *data, range_start_offset, range_size);
    }

    if (icef_box && cmpC_box->get_compressed_unit_type() == heif_cmpC_compressed_unit_type_image_tile) {
      const auto& units = icef_box->get_units();
      if (tile_idx >= units.size()) {
        return {heif_error_Invalid_input,
                heif_suberror_Unspecified,
                "no icef-box entry for tile index"};
      }

      const auto unit = units[tile_idx];

      // get all data and decode all
      std::vector<uint8_t> compressed_bytes;
      Error err = context->get_heif_file()->append_data_from_iloc(ID, compressed_bytes, unit.unit_offset, unit.unit_size);
      if (err) {
        return err;
      }

      // decompress only the unit
      err = do_decompress_data(cmpC_box, compressed_bytes, data);
      if (err) {
        return err;
      }
    }
    else if (icef_box) {
      // get all data and decode all
      std::vector<uint8_t> compressed_bytes;
      Error err = context->get_heif_file()->append_data_from_iloc(ID, compressed_bytes); // image_id, src_data, tile_start_offset, total_tile_size);
      if (err) {
        return err;
      }

      for (Box_icef::CompressedUnitInfo unit_info : icef_box->get_units()) {
        auto unit_start = compressed_bytes.begin() + unit_info.unit_offset;
        auto unit_end = unit_start + unit_info.unit_size;
        std::vector<uint8_t> compressed_unit_data = std::vector<uint8_t>(unit_start, unit_end);
        std::vector<uint8_t> uncompressed_unit_data;
        err = do_decompress_data(cmpC_box, compressed_unit_data, &uncompressed_unit_data);
        if (err) {
          return err;
        }
        data->insert(data->end(), uncompressed_unit_data.data(), uncompressed_unit_data.data() + uncompressed_unit_data.size());
      }

      // cut out the range that we actually need
      memcpy(data->data(), data->data() + range_start_offset, range_size);
      data->resize(range_size);
    }
    else {
      // get all data and decode all
      std::vector<uint8_t> compressed_bytes;
      Error err = context->get_heif_file()->append_data_from_iloc(ID, compressed_bytes); // image_id, src_data, tile_start_offset, total_tile_size);
      if (err) {
        return err;
      }

      // Decode as a single blob
      err = do_decompress_data(cmpC_box, compressed_bytes, data);
      if (err) {
        return err;
      }

      // cut out the range that we actually need
      memcpy(data->data(), data->data() + range_start_offset, range_size);
      data->resize(range_size);
    }

    return Error::Ok;
  }

  const Error do_decompress_data(std::shared_ptr<const Box_cmpC> &cmpC_box,
                                 std::vector<uint8_t> compressed_data,
                                 std::vector<uint8_t> *data) const
  {
    if (cmpC_box->get_compression_type() == fourcc("brot")) {
#if HAVE_BROTLI
      return decompress_brotli(compressed_data, data);
#else
      std::stringstream sstr;
    sstr << "cannot decode unci item with brotli compression - not enabled" << std::endl;
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_generic_compression_method,
                 sstr.str());
#endif
    } else if (cmpC_box->get_compression_type() == fourcc("zlib")) {
#if HAVE_ZLIB
      return decompress_zlib(compressed_data, data);
#else
      std::stringstream sstr;
    sstr << "cannot decode unci item with zlib compression - not enabled" << std::endl;
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_generic_compression_method,
                 sstr.str());
#endif
    } else if (cmpC_box->get_compression_type() == fourcc("defl")) {
#if HAVE_ZLIB
      return decompress_deflate(compressed_data, data);
#else
      std::stringstream sstr;
    sstr << "cannot decode unci item with deflate compression - not enabled" << std::endl;
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_generic_compression_method,
                 sstr.str());
#endif
    } else {
      std::stringstream sstr;
      sstr << "cannot decode unci item with unsupported compression type: " << cmpC_box->get_compression_type() << std::endl;
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_generic_compression_method,
                   sstr.str());
    }
  }
};


class ComponentInterleaveDecoder : public AbstractDecoder
{
public:
  ComponentInterleaveDecoder(uint32_t width, uint32_t height, std::shared_ptr<Box_cmpd> cmpd, std::shared_ptr<Box_uncC> uncC):
    AbstractDecoder(width, height, std::move(cmpd), std::move(uncC))
  {}

  Error decode_tile(const HeifContext* context,
                    heif_item_id image_id,
                    std::shared_ptr<HeifPixelImage>& img,
                    uint32_t out_x0, uint32_t out_y0,
                    uint32_t image_width, uint32_t image_height,
                    uint32_t tile_x, uint32_t tile_y) override
  {
    if (m_tile_width == 0) {
      return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "Internal error: ComponentInterleaveDecoder tile_width=0"};
    }

    // --- compute which file range we need to read for the tile

    uint64_t total_tile_size = 0;

    for (ChannelListEntry& entry : channelList) {
      uint32_t bits_per_component = entry.bits_per_component_sample;
      if (entry.component_alignment > 0) {
        uint32_t bytes_per_component = (bits_per_component + 7)/8;
        bytes_per_component += nAlignmentSkipBytes(entry.component_alignment, bytes_per_component);
        bits_per_component = bytes_per_component * 8;
      }

      uint32_t bytes_per_tile_row = (bits_per_component * entry.tile_width + 7)/8;
      bytes_per_tile_row += nAlignmentSkipBytes(m_uncC->get_row_align_size(), bytes_per_tile_row);
      uint64_t bytes_per_tile = bytes_per_tile_row * entry.tile_height;
      total_tile_size += bytes_per_tile;
    }

    if (m_uncC->get_tile_align_size() != 0) {
      total_tile_size += nAlignmentSkipBytes(m_uncC->get_tile_align_size(), total_tile_size);
    }

    assert(m_tile_width > 0);
    uint32_t tileIdx = tile_x + tile_y * (image_width / m_tile_width);
    uint64_t tile_start_offset = total_tile_size * tileIdx;


    // --- read required file range

    std::vector<uint8_t> src_data;
    //Error err = context->get_heif_file()->append_data_from_iloc(image_id, src_data, tile_start_offset, total_tile_size);
    Error err = get_compressed_image_data_uncompressed(context, image_id, &src_data, tile_start_offset, total_tile_size, tileIdx, nullptr);
    if (err) {
      return err;
    }

    UncompressedBitReader srcBits(src_data);


    // --- decode tile

    for (ChannelListEntry& entry : channelList) {
      for (uint32_t y = 0; y < entry.tile_height; y++) {
        srcBits.markRowStart();
        if (entry.use_channel) {
          uint64_t dst_row_offset = (out_y0 + y) * entry.dst_plane_stride;
          processComponentTileRow(entry, srcBits, dst_row_offset + out_x0 * entry.bytes_per_component_sample);
        }
        else {
          srcBits.skip_bytes(entry.bytes_per_tile_row_src);
        }
        srcBits.handleRowAlignment(m_uncC->get_row_align_size());
      }
    }

    return Error::Ok;
  }
};


class PixelInterleaveDecoder : public AbstractDecoder
{
public:
  PixelInterleaveDecoder(uint32_t width, uint32_t height, std::shared_ptr<Box_cmpd> cmpd, std::shared_ptr<Box_uncC> uncC):
    AbstractDecoder(width, height, std::move(cmpd), std::move(uncC))
  {}

  Error decode_tile(const HeifContext* context,
                    heif_item_id image_id,
                    std::shared_ptr<HeifPixelImage>& img,
                    uint32_t out_x0, uint32_t out_y0,
                    uint32_t image_width, uint32_t image_height,
                    uint32_t tile_x, uint32_t tile_y) override
  {
    if (m_tile_width == 0) {
      return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "Internal error: PixelInterleaveDecoder tile_width=0"};
    }

    // --- compute which file range we need to read for the tile

    uint32_t bits_per_row = 0;
    for (uint32_t x = 0 ; x<m_tile_width;x++) {
      uint32_t bits_per_pixel = 0;

      for (ChannelListEntry& entry : channelList) {
        uint32_t bits_per_component = entry.bits_per_component_sample;
        if (entry.component_alignment > 0) {
          // start at byte boundary
          bits_per_row = (bits_per_row + 7) & ~7U;

          uint32_t bytes_per_component = (bits_per_component + 7)/8;
          bytes_per_component += nAlignmentSkipBytes(entry.component_alignment, bytes_per_component);
          bits_per_component = bytes_per_component * 8;
        }

        bits_per_pixel += bits_per_component;
      }

      if (m_uncC->get_pixel_size() != 0) {
        uint32_t bytes_per_pixel = (bits_per_pixel + 7) / 8;
        bytes_per_pixel += nAlignmentSkipBytes(m_uncC->get_pixel_size(), bytes_per_pixel);
        bits_per_pixel = bytes_per_pixel * 8;
      }

      bits_per_row += bits_per_pixel;
    }

    uint32_t bytes_per_row = (bits_per_row + 7)/8;
    bytes_per_row += nAlignmentSkipBytes(m_uncC->get_row_align_size(), bytes_per_row);

    uint64_t total_tile_size = bytes_per_row * static_cast<uint64_t>(m_tile_height);
    if (m_uncC->get_tile_align_size() != 0) {
      total_tile_size += nAlignmentSkipBytes(m_uncC->get_tile_align_size(), total_tile_size);
    }

    assert(m_tile_width > 0);
    uint32_t tileIdx = tile_x + tile_y * (image_width / m_tile_width);
    uint64_t tile_start_offset = total_tile_size * tileIdx;


    // --- read required file range

    std::vector<uint8_t> src_data;
    Error err = get_compressed_image_data_uncompressed(context, image_id, &src_data, tile_start_offset, total_tile_size, tileIdx, nullptr);
    //Error err = context->get_heif_file()->append_data_from_iloc(image_id, src_data, tile_start_offset, total_tile_size);
    if (err) {
      return err;
    }

    UncompressedBitReader srcBits(src_data);

    processTile(srcBits, tile_y, tile_x, out_x0, out_y0);

    return Error::Ok;
  }

  void processTile(UncompressedBitReader &srcBits, uint32_t tile_row, uint32_t tile_column, uint32_t out_x0, uint32_t out_y0) {
    for (uint32_t tile_y = 0; tile_y < m_tile_height; tile_y++) {
      srcBits.markRowStart();
      for (uint32_t tile_x = 0; tile_x < m_tile_width; tile_x++) {
        srcBits.markPixelStart();
        for (ChannelListEntry &entry : channelList) {
          if (entry.use_channel) {
            uint64_t dst_row_offset = entry.getDestinationRowOffset(0, tile_y + out_y0);
            if (entry.component_alignment != 0) {
              srcBits.skip_to_byte_boundary();
              int numPadBits = (entry.component_alignment * 8) - entry.bits_per_component_sample;
              srcBits.skip_bits(numPadBits);
            }
            processComponentSample(srcBits, entry, dst_row_offset, 0, out_x0 + tile_x);
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

  Error decode_tile(const HeifContext* context,
                    heif_item_id image_id,
                    std::shared_ptr<HeifPixelImage>& img,
                    uint32_t out_x0, uint32_t out_y0,
                    uint32_t image_width, uint32_t image_height,
                    uint32_t tile_x, uint32_t tile_y) override
  {
    if (m_tile_width == 0) {
      return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "Internal error: MixedInterleaveDecoder tile_width=0"};
    }

    // --- compute which file range we need to read for the tile

    uint64_t tile_size = 0;

    for (ChannelListEntry& entry : channelList) {
      if (entry.channel == heif_channel_Cb || entry.channel == heif_channel_Cr) {
        uint32_t bits_per_row = entry.bits_per_component_sample * entry.tile_width;
        bits_per_row = (bits_per_row+7) & ~7U; // align to byte boundary

        tile_size += bits_per_row / 8 * entry.tile_height;
      }
      else {
        uint32_t bits_per_component = entry.bits_per_component_sample;
        if (entry.component_alignment > 0) {
          uint32_t bytes_per_component = (bits_per_component + 7)/8;
          bytes_per_component += nAlignmentSkipBytes(entry.component_alignment, bytes_per_component);
          bits_per_component = bytes_per_component * 8;
        }

        uint32_t bits_per_row = bits_per_component * entry.tile_width;
        bits_per_row = (bits_per_row+7) & ~7U; // align to byte boundary

        tile_size += bits_per_row / 8 * entry.tile_height;
      }
    }


    if (m_uncC->get_tile_align_size() != 0) {
      tile_size += nAlignmentSkipBytes(m_uncC->get_tile_align_size(), tile_size);
    }

    assert(m_tile_width > 0);
    uint32_t tileIdx = tile_x + tile_y * (image_width / m_tile_width);
    uint64_t tile_start_offset = tile_size * tileIdx;


    // --- read required file range

    std::vector<uint8_t> src_data;
    Error err = get_compressed_image_data_uncompressed(context, image_id, &src_data, tile_start_offset, tile_size, tileIdx, nullptr);
    //Error err = context->get_heif_file()->append_data_from_iloc(image_id, src_data, tile_start_offset, tile_size);
    if (err) {
      return err;
    }

    UncompressedBitReader srcBits(src_data);

    processTile(srcBits, tile_y, tile_x, out_x0, out_y0);

    return Error::Ok;
  }


  void processTile(UncompressedBitReader &srcBits, uint32_t tile_row, uint32_t tile_column, uint32_t out_x0, uint32_t out_y0) {
    bool haveProcessedChromaForThisTile = false;
    for (ChannelListEntry &entry : channelList) {
      if (entry.use_channel) {
        if ((entry.channel == heif_channel_Cb) || (entry.channel == heif_channel_Cr)) {
          if (!haveProcessedChromaForThisTile) {
            for (uint32_t tile_y = 0; tile_y < entry.tile_height; tile_y++) {
              // TODO: row padding
              uint64_t dst_row_number = tile_y + out_y0;
              uint64_t dst_row_offset = dst_row_number * entry.dst_plane_stride;
              for (uint32_t tile_x = 0; tile_x < entry.tile_width; tile_x++) {
                uint64_t dst_column_number = out_x0 + tile_x;
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

  Error decode_tile(const HeifContext* context,
                    heif_item_id image_id,
                    std::shared_ptr<HeifPixelImage>& img,
                    uint32_t out_x0, uint32_t out_y0,
                    uint32_t image_width, uint32_t image_height,
                    uint32_t tile_x, uint32_t tile_y) override
  {
    if (m_tile_width == 0) {
      return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "Internal error: RowInterleaveDecoder tile_width=0"};
    }

    // --- compute which file range we need to read for the tile

    uint32_t bits_per_row = 0;
    for (ChannelListEntry& entry : channelList) {
      uint32_t bits_per_component = entry.bits_per_component_sample;
      if (entry.component_alignment > 0) {
        // start at byte boundary
        bits_per_row = (bits_per_row + 7) & ~7U;

        uint32_t bytes_per_component = (bits_per_component + 7)/8;
        bytes_per_component += nAlignmentSkipBytes(entry.component_alignment, bytes_per_component);
        bits_per_component = bytes_per_component * 8;
      }

      if (m_uncC->get_row_align_size() != 0) {
        uint32_t bytes_this_row = (bits_per_component * m_tile_width + 7) / 8;
        bytes_this_row += nAlignmentSkipBytes(m_uncC->get_row_align_size(), bytes_this_row);
        bits_per_row += bytes_this_row * 8;
      }
      else {
        bits_per_row += bits_per_component * m_tile_width;
      }

      bits_per_row = (bits_per_row + 7) & ~7U;
    }

    uint32_t bytes_per_row = (bits_per_row + 7) / 8;
    if (m_uncC->get_row_align_size()) {
      bytes_per_row += nAlignmentSkipBytes(m_uncC->get_row_align_size(), bytes_per_row);
    }

    uint64_t total_tile_size = 0;
    total_tile_size += bytes_per_row * static_cast<uint64_t>(m_tile_height);

    if (m_uncC->get_tile_align_size() != 0) {
      total_tile_size += nAlignmentSkipBytes(m_uncC->get_tile_align_size(), total_tile_size);
    }

    assert(m_tile_width > 0);
    uint32_t tileIdx = tile_x + tile_y * (image_width / m_tile_width);
    uint64_t tile_start_offset = total_tile_size * tileIdx;


    // --- read required file range

    std::vector<uint8_t> src_data;
    Error err = get_compressed_image_data_uncompressed(context, image_id, &src_data, tile_start_offset, total_tile_size, tileIdx, nullptr);
    //Error err = context->get_heif_file()->append_data_from_iloc(image_id, src_data, tile_start_offset, total_tile_size);
    if (err) {
      return err;
    }

    UncompressedBitReader srcBits(src_data);

    processTile(srcBits, tile_y, tile_x, out_x0, out_y0);

    return Error::Ok;
  }

private:
  void processTile(UncompressedBitReader &srcBits, uint32_t tile_row, uint32_t tile_column, uint32_t out_x0, uint32_t out_y0) {
    for (uint32_t tile_y = 0; tile_y < m_tile_height; tile_y++) {
      for (ChannelListEntry &entry : channelList) {
        srcBits.markRowStart();
        if (entry.use_channel) {
          uint64_t dst_row_offset = entry.getDestinationRowOffset(0, tile_y + out_y0);
          processComponentRow(entry, srcBits, dst_row_offset + out_x0 * entry.bytes_per_component_sample, 0);
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

  Error decode_tile(const HeifContext* context,
                    heif_item_id image_id,
                    std::shared_ptr<HeifPixelImage>& img,
                    uint32_t out_x0, uint32_t out_y0,
                    uint32_t image_width, uint32_t image_height,
                    uint32_t tile_column, uint32_t tile_row) override
  {
    if (m_tile_width == 0) {
      return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "Internal error: TileComponentInterleaveDecoder tile_width=0"};
    }
    if (m_tile_height == 0) {
      return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "Internal error: TileComponentInterleaveDecoder tile_height=0"};
    }

    // --- compute which file range we need to read for the tile

    std::map<heif_channel, uint64_t> channel_tile_size;

    //uint64_t total_tile_size = 0;

    for (ChannelListEntry& entry : channelList) {
      uint32_t bits_per_pixel = entry.bits_per_component_sample;
      if (entry.component_alignment > 0) {
        // start at byte boundary
        //bits_per_row = (bits_per_row + 7) & ~7U;

        uint32_t bytes_per_component = (bits_per_pixel + 7)/8;
        bytes_per_component += nAlignmentSkipBytes(entry.component_alignment, bytes_per_component);
        bits_per_pixel = bytes_per_component * 8;
      }

      uint32_t bytes_per_row;
      if (m_uncC->get_pixel_size() != 0) { // TODO: does pixel_size apply here?
        uint32_t bytes_per_pixel = (bits_per_pixel + 7) / 8;
        bytes_per_pixel += nAlignmentSkipBytes(m_uncC->get_pixel_size(), bytes_per_pixel);
        bytes_per_row = bytes_per_pixel * m_tile_width;
      }
      else {
        bytes_per_row = (bits_per_pixel * m_tile_width + 7) / 8;
      }

      bytes_per_row += nAlignmentSkipBytes(m_uncC->get_row_align_size(), bytes_per_row);

      uint64_t component_tile_size = bytes_per_row * static_cast<uint64_t>(m_tile_height);

      if (m_uncC->get_tile_align_size() != 0) {
        component_tile_size += nAlignmentSkipBytes(m_uncC->get_tile_align_size(), component_tile_size);
      }

      channel_tile_size[entry.channel] = component_tile_size;

      //total_tile_size += component_tile_size;
    }

    uint64_t component_start_offset = 0;

    assert(m_tile_width > 0);
    assert(m_tile_height > 0);

    for (ChannelListEntry& entry : channelList) {
      //processTile(srcBits, tile_y, tile_x, out_x0, out_y0);

      if (!entry.use_channel) {
        //uint64_t bytes_per_component = entry.get_bytes_per_tile() * m_uncC->get_number_of_tile_columns() * m_uncC->get_number_of_tile_rows();
        //srcBits.skip_bytes((int)bytes_per_component);

        component_start_offset += channel_tile_size[entry.channel] * (m_width / m_tile_width) * (m_height / m_tile_height);
        continue;
      }

      // --- read required file range

      uint32_t tileIdx = tile_column + tile_row * (image_width / m_tile_width);
      uint64_t tile_start_offset = component_start_offset + channel_tile_size[entry.channel] * tileIdx;

      std::vector<uint8_t> src_data;
      Error err = get_compressed_image_data_uncompressed(context, image_id, &src_data, tile_start_offset, channel_tile_size[entry.channel], tileIdx, nullptr);
      //Error err = context->get_heif_file()->append_data_from_iloc(image_id, src_data, tile_start_offset, channel_tile_size[entry.channel]);
      if (err) {
        return err;
      }

      UncompressedBitReader srcBits(src_data);

      srcBits.markTileStart();
      for (uint32_t tile_y = 0; tile_y < entry.tile_height; tile_y++) {
        srcBits.markRowStart();
        uint64_t dst_row_offset = entry.getDestinationRowOffset(0, tile_y + out_y0);
        processComponentRow(entry, srcBits, dst_row_offset + out_x0 * entry.bytes_per_component_sample, 0);
        srcBits.handleRowAlignment(m_uncC->get_row_align_size());
      }
      srcBits.handleTileAlignment(m_uncC->get_tile_align_size());


      component_start_offset += channel_tile_size[entry.channel] * (m_width / m_tile_width) * (m_height / m_tile_height);
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


Result<std::shared_ptr<HeifPixelImage>> UncompressedImageCodec::create_image(const std::shared_ptr<const Box_cmpd> cmpd,
                                                                             const std::shared_ptr<const Box_uncC> uncC,
                                                                             uint32_t width,
                                                                             uint32_t height)
{
  auto img = std::make_shared<HeifPixelImage>();
  heif_chroma chroma;
  heif_colorspace colourspace;
  Error error = get_heif_chroma_uncompressed(uncC, cmpd, &chroma, &colourspace);
  if (error) {
    return error;
  }
  img->create(width, height,
              colourspace,
              chroma);

  for (Box_uncC::Component component : uncC->get_components()) {
    heif_channel channel;
    if (map_uncompressed_component_to_channel(cmpd, uncC, component, &channel)) {
      if (img->has_channel(channel)) {
        return Error{heif_error_Unsupported_feature,
                     heif_suberror_Unspecified,
                     "Cannot generate image with several similar heif_channels."};
      }

      if ((channel == heif_channel_Cb) || (channel == heif_channel_Cr)) {
        img->add_plane(channel, (width / chroma_h_subsampling(chroma)), (height / chroma_v_subsampling(chroma)), component.component_bit_depth);
      } else {
        img->add_plane(channel, width, height, component.component_bit_depth);
      }
    }
  }

  return img;
}


Error UncompressedImageCodec::decode_uncompressed_image_tile(const HeifContext* context,
                                                             heif_item_id ID,
                                                             std::shared_ptr<HeifPixelImage>& img,
                                                             uint32_t tile_x0, uint32_t tile_y0)
{
  auto file = context->get_heif_file();
  std::shared_ptr<Box_ispe> ispe = file->get_property<Box_ispe>(ID);
  std::shared_ptr<Box_cmpd> cmpd = file->get_property<Box_cmpd>(ID);
  std::shared_ptr<Box_uncC> uncC = file->get_property<Box_uncC>(ID);

  Error error = check_header_validity(ispe, cmpd, uncC);
  if (error) {
    return error;
  }

  uint32_t tile_width = ispe->get_width() / uncC->get_number_of_tile_columns();
  uint32_t tile_height = ispe->get_height() / uncC->get_number_of_tile_rows();

  Result<std::shared_ptr<HeifPixelImage>> createImgResult = create_image(cmpd, uncC, tile_width, tile_height);
  if (createImgResult.error) {
    return createImgResult.error;
  }

  img = createImgResult.value;


  AbstractDecoder *decoder = makeDecoder(ispe->get_width(), ispe->get_height(), cmpd, uncC);
  if (decoder == nullptr) {
    std::stringstream sstr;
    sstr << "Uncompressed interleave_type of " << ((int) uncC->get_interleave_type()) << " is not implemented yet";
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 sstr.str());
  }

  decoder->buildChannelList(img);

  Error result = decoder->decode_tile(context, ID, img, 0, 0,
                                      ispe->get_width(), ispe->get_height(),
                                      tile_x0 / tile_width, tile_y0 / tile_height);
  delete decoder;
  return result;
}


Error UncompressedImageCodec::check_header_validity(const std::shared_ptr<const Box_ispe>& ispe,
                                                    const std::shared_ptr<const Box_cmpd>& cmpd,
                                                    const std::shared_ptr<const Box_uncC>& uncC)
{
  // if we miss a required box, show error

  if (!ispe) {
    return {heif_error_Unsupported_feature,
            heif_suberror_Unsupported_data_version,
            "Missing required ispe box for uncompressed codec"};
  }

  if (!uncC) {
    return {heif_error_Unsupported_feature,
            heif_suberror_Unsupported_data_version,
            "Missing required uncC box for uncompressed codec"};
  }

  if (!cmpd && (uncC->get_version() != 1)) {
    return {heif_error_Unsupported_feature,
            heif_suberror_Unsupported_data_version,
            "Missing required cmpd or uncC version 1 box for uncompressed codec"};
  }

  if (cmpd) {
    if (uncC->get_components().size() != cmpd->get_components().size()) {
      return {heif_error_Invalid_input,
              heif_suberror_Unspecified,
              "Number of components in uncC and cmpd do not match"};
    }

    for (const auto& comp : uncC->get_components()) {
      if (comp.component_index > cmpd->get_components().size()) {
        return {heif_error_Invalid_input,
                heif_suberror_Unspecified,
                "Invalid component index in uncC box"};
      }
    }
  }

  if (uncC->get_number_of_tile_rows() > ispe->get_height() ||
      uncC->get_number_of_tile_columns() > ispe->get_width()) {
    return {heif_error_Invalid_input,
            heif_suberror_Unspecified,
            "More tiles than pixels in uncC box"};
  }

  if (ispe->get_height() % uncC->get_number_of_tile_rows() != 0 ||
      ispe->get_width() % uncC->get_number_of_tile_columns() != 0) {
    return {heif_error_Invalid_input,
            heif_suberror_Unspecified,
            "Invalid tile size (image size not a multiple of the tile size)"};
  }

  return Error::Ok;
}


Error UncompressedImageCodec::decode_uncompressed_image(const HeifContext* context,
                                                        heif_item_id ID,
                                                        std::shared_ptr<HeifPixelImage>& img)
{
  // Get the properties for this item
  // We need: ispe, cmpd, uncC
  std::vector<std::shared_ptr<Box>> item_properties;
  Error error = context->get_heif_file()->get_properties(ID, item_properties);
  if (error) {
    return error;
  }

  std::shared_ptr<Box_ispe> ispe = context->get_heif_file()->get_property<Box_ispe>(ID);
  std::shared_ptr<Box_cmpd> cmpd = context->get_heif_file()->get_property<Box_cmpd>(ID);
  std::shared_ptr<Box_uncC> uncC = context->get_heif_file()->get_property<Box_uncC>(ID);

  error = check_header_validity(ispe, cmpd, uncC);
  if (error) {
    return error;
  }

  // check if we support the type of image

  error = uncompressed_image_type_is_supported(uncC, cmpd); // TODO TODO TODO
  if (error) {
    return error;
  }

  assert(ispe);
  uint32_t width = ispe->get_width();
  uint32_t height = ispe->get_height();
  error = context->check_resolution(width, height);
  if (error) {
    return error;
  }

  Result<std::shared_ptr<HeifPixelImage>> createImgResult = create_image(cmpd, uncC, width, height);
  if (createImgResult.error) {
    return createImgResult.error;
  }
  else {
    img = *createImgResult;
  }

  AbstractDecoder *decoder = makeDecoder(width, height, cmpd, uncC);
  if (decoder == nullptr) {
    std::stringstream sstr;
    sstr << "Uncompressed interleave_type of " << ((int) uncC->get_interleave_type()) << " is not implemented yet";
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 sstr.str());
  }

  decoder->buildChannelList(img);

  uint32_t tile_width = width / uncC->get_number_of_tile_columns();
  uint32_t tile_height = height / uncC->get_number_of_tile_rows();

  for (uint32_t tile_y0 = 0; tile_y0 < height; tile_y0 += tile_height)
    for (uint32_t tile_x0 = 0; tile_x0 < width; tile_x0 += tile_width) {
      error = decoder->decode_tile(context, ID, img, tile_x0, tile_y0,
                                   width, height,
                                   tile_x0 / tile_width, tile_y0 / tile_height);
      if (error) {
        delete decoder;
        return error;
      }
    }

  //Error result = decoder->decode(source_data, img);
  delete decoder;
  return Error::Ok;
}

Error fill_cmpd_and_uncC(std::shared_ptr<Box_cmpd>& cmpd,
                         std::shared_ptr<Box_uncC>& uncC,
                         const std::shared_ptr<const HeifPixelImage>& image,
                         const heif_unci_image_parameters* parameters)
{
  uint32_t nTileColumns = parameters->image_width / parameters->tile_width;
  uint32_t nTileRows = parameters->image_height / parameters->tile_height;

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
    uncC->set_number_of_tile_columns(nTileColumns);
    uncC->set_number_of_tile_rows(nTileRows);
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
    uncC->set_number_of_tile_columns(nTileColumns);
    uncC->set_number_of_tile_rows(nTileRows);
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
    uncC->set_number_of_tile_columns(nTileColumns);
    uncC->set_number_of_tile_rows(nTileRows);
  }
  else
  {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Unsupported colourspace");
  }
  return Error::Ok;
}


static void maybe_make_minimised_uncC(std::shared_ptr<Box_uncC>& uncC, const std::shared_ptr<const HeifPixelImage>& image)
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


Result<std::shared_ptr<HeifPixelImage>> ImageItem_uncompressed::decode_compressed_image(const struct heif_decoding_options& options,
                                                                                bool decode_tile_only, uint32_t tile_x0, uint32_t tile_y0) const
{
  std::shared_ptr<HeifPixelImage> img;

  std::vector<uint8_t> data;

  Error err;

  if (decode_tile_only) {
    err = UncompressedImageCodec::decode_uncompressed_image_tile(get_context(),
                                                                 get_id(),
                                                                 img,
                                                                 tile_x0, tile_y0);
  }
  else {
    err = UncompressedImageCodec::decode_uncompressed_image(get_context(),
                                                            get_id(),
                                                            img);
  }

  if (err) {
    return err;
  }
  else {
    return img;
  }
}


struct unciHeaders
{
  std::shared_ptr<Box_uncC> uncC;
  std::shared_ptr<Box_cmpd> cmpd;
};


static Result<unciHeaders> generate_headers(const std::shared_ptr<const HeifPixelImage>& src_image,
                                            const heif_unci_image_parameters* parameters,
                                            const struct heif_encoding_options* options)
{
  unciHeaders headers;

  std::shared_ptr<Box_uncC> uncC = std::make_shared<Box_uncC>();
  if (options && options->prefer_uncC_short_form) {
    maybe_make_minimised_uncC(uncC, src_image);
  }

  if (uncC->get_version() == 1) {
    headers.uncC = uncC;
  } else {
    std::shared_ptr<Box_cmpd> cmpd = std::make_shared<Box_cmpd>();

    Error error = fill_cmpd_and_uncC(cmpd, uncC, src_image, parameters);
    if (error) {
      return error;
    }

    headers.cmpd = cmpd;
    headers.uncC = uncC;
  }

  return headers;
}


Result<std::vector<uint8_t>> encode_image_tile(const std::shared_ptr<const HeifPixelImage>& src_image)
{
  std::vector<uint8_t> data;

  if (src_image->get_colorspace() == heif_colorspace_YCbCr)
  {
    uint64_t offset = 0;
    for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr})
    {
      uint32_t src_stride;
      uint32_t src_width = src_image->get_width(channel);
      uint32_t src_height = src_image->get_height(channel);
      const uint8_t* src_data = src_image->get_plane(channel, &src_stride);
      uint64_t out_size = src_width * src_height;
      data.resize(data.size() + out_size);
      for (uint32_t y = 0; y < src_height; y++) {
        memcpy(data.data() + offset + y * src_width, src_data + src_stride * y, src_width);
      }
      offset += out_size;
    }

    return data;
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
        uint32_t src_stride;
        const uint8_t* src_data = src_image->get_plane(channel, &src_stride);
        uint64_t out_size = src_image->get_height() * src_stride;
        data.resize(data.size() + out_size);
        memcpy(data.data() + offset, src_data, out_size);
        offset += out_size;
      }

      return data;
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

      uint32_t src_stride;
      const uint8_t* src_data = src_image->get_plane(heif_channel_interleaved, &src_stride);
      uint64_t out_size = src_image->get_height() * src_image->get_width() * bytes_per_pixel;
      data.resize(out_size);
      for (uint32_t y = 0; y < src_image->get_height(); y++) {
        memcpy(data.data() + y * src_image->get_width() * bytes_per_pixel, src_data + src_stride * y, src_image->get_width() * bytes_per_pixel);
      }

      return data;
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
      uint32_t src_stride;
      const uint8_t* src_data = src_image->get_plane(channel, &src_stride);
      uint64_t out_size = src_image->get_height() * src_stride;
      data.resize(data.size() + out_size);
      memcpy(data.data() + offset, src_data, out_size);
      offset += out_size;
    }

    return data;
  }
  else
  {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Unsupported colourspace");
  }

}


Result<ImageItem::CodedImageData> ImageItem_uncompressed::encode(const std::shared_ptr<HeifPixelImage>& src_image,
                                                                 struct heif_encoder* encoder,
                                                                 const struct heif_encoding_options& options,
                                                                 enum heif_image_input_class input_class)
{
  heif_unci_image_parameters parameters{};
  parameters.image_width = src_image->get_width();
  parameters.image_height = src_image->get_height();
  parameters.tile_width = parameters.image_width;
  parameters.tile_height = parameters.image_height;


  // --- generate configuration property boxes

  Result<unciHeaders> genHeadersResult = generate_headers(src_image, &parameters, &options);
  if (genHeadersResult.error) {
    return genHeadersResult.error;
  }

  const unciHeaders& headers = *genHeadersResult;

  CodedImageData codedImageData;
  if (headers.uncC) {
    codedImageData.properties.push_back(headers.uncC);
  }
  if (headers.cmpd) {
    codedImageData.properties.push_back(headers.cmpd);
  }


  // --- encode image

  Result<std::vector<uint8_t>> codedBitstreamResult = encode_image_tile(src_image);
  if (codedBitstreamResult.error) {
    return codedBitstreamResult.error;
  }

  codedImageData.bitstream = *codedBitstreamResult;

  return codedImageData;
}


Result<std::shared_ptr<ImageItem_uncompressed>> ImageItem_uncompressed::add_unci_item(HeifContext* ctx,
                                                                                      const heif_unci_image_parameters* parameters,
                                                                                      const struct heif_encoding_options* encoding_options,
                                                                                      const std::shared_ptr<const HeifPixelImage>& prototype)
{
  // Create 'tild' Item

  auto file = ctx->get_heif_file();

  heif_item_id unci_id = ctx->get_heif_file()->add_new_image("unci");
  auto unci_image = std::make_shared<ImageItem_uncompressed>(ctx, unci_id);
  ctx->insert_new_image(unci_id, unci_image);


  // Generate headers

  Result<unciHeaders> genHeadersResult = generate_headers(prototype, parameters, encoding_options);
  if (genHeadersResult.error) {
    return genHeadersResult.error;
  }

  const unciHeaders& headers = *genHeadersResult;

  if (headers.uncC) {
    file->add_property(unci_id, headers.uncC, true);
  }

  if (headers.cmpd) {
    file->add_property(unci_id, headers.cmpd, true);
  }

  // Add `ispe` property

  file->add_ispe_property(unci_id,
                          static_cast<uint32_t>(parameters->image_width),
                          static_cast<uint32_t>(parameters->image_height));

  // Create empty image

  uint64_t tile_size = headers.uncC->compute_tile_data_size_bytes(parameters->image_width / headers.uncC->get_number_of_tile_columns(),
                                                                  parameters->image_height / headers.uncC->get_number_of_tile_rows());

  std::cout << "tile size: " << tile_size << "\n";

  std::vector<uint8_t> dummydata;
  dummydata.resize(tile_size);

  for (uint64_t i = 0; i < tile_size; i++) {
    const int construction_method = 0; // 0=mdat 1=idat
    file->append_iloc_data(unci_id, dummydata, construction_method);
  }


  // Set Brands
  ctx->get_heif_file()->set_brand(heif_compression_uncompressed, unci_image->is_miaf_compatible());

  return {unci_image};
}


Error ImageItem_uncompressed::add_image_tile(uint32_t tile_x, uint32_t tile_y, const std::shared_ptr<const HeifPixelImage>& image)
{
  std::shared_ptr<Box_uncC> uncC = get_file()->get_property<Box_uncC>(get_id());
  assert(uncC);

  uint32_t tile_width = image->get_width();
  uint32_t tile_height = image->get_height();

  uint64_t tile_data_size = uncC->compute_tile_data_size_bytes(tile_width, tile_height);

  tile_x /= tile_width;
  tile_y /= tile_height;

  uint32_t tile_idx = tile_y * uncC->get_number_of_tile_columns() + tile_x;

  Result<std::vector<uint8_t>> codedBitstreamResult = encode_image_tile(image);
  if (codedBitstreamResult.error) {
    return codedBitstreamResult.error;
  }

  get_file()->replace_iloc_data(get_id(), tile_idx * tile_data_size, *codedBitstreamResult, 0);

  return Error::Ok;
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


void ImageItem_uncompressed::get_tile_size(uint32_t& w, uint32_t& h) const
{
  auto ispe = get_file()->get_property<Box_ispe>(get_id());
  auto uncC = get_file()->get_property<Box_uncC>(get_id());

  if (!ispe || !uncC) {
    w=h=0;
  }

  w = ispe->get_width() / uncC->get_number_of_tile_columns();
  h = ispe->get_height() / uncC->get_number_of_tile_rows();
}
