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

#ifndef UNCI_DECODER_ABSTRACT_H
#define UNCI_DECODER_ABSTRACT_H

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <map>
#include <iostream>
#include <cassert>
#include <utility>
#include <vector>
#include <memory>

#include "common_utils.h"
#include "context.h"
#include "compression.h"
#include "error.h"
#include "libheif/heif.h"
#include "unc_types.h"
#include "unc_boxes.h"
#include "unc_codec.h"


class UncompressedBitReader : public BitReader
{
public:
  UncompressedBitReader(const std::vector<uint8_t>& data) : BitReader(data.data(), (int) data.size()) {}

  void markPixelStart()
  {
    m_pixelStartOffset = get_current_byte_index();
  }

  void markRowStart()
  {
    m_rowStartOffset = get_current_byte_index();
  }

  void markTileStart()
  {
    m_tileStartOffset = get_current_byte_index();
  }

  inline void handlePixelAlignment(uint32_t pixel_size)
  {
    if (pixel_size != 0) {
      uint32_t bytes_in_pixel = get_current_byte_index() - m_pixelStartOffset;
      uint32_t padding = pixel_size - bytes_in_pixel;
      skip_bytes(padding);
    }
  }

  void handleRowAlignment(uint32_t alignment)
  {
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

  void handleTileAlignment(uint32_t alignment)
  {
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


template<typename T> void skip_to_alignment(T& position, uint32_t alignment)
{
  if (alignment == 0) {
    return;
  }

  T residual = position % alignment;
  if (residual == 0) {
    return;
  }

  position += alignment - residual;
}


class AbstractDecoder
{
public:
  virtual ~AbstractDecoder() = default;

  virtual Error decode_tile(const HeifContext* context,
                            heif_item_id item_id,
                            std::shared_ptr<HeifPixelImage>& img,
                            uint32_t out_x0, uint32_t out_y0,
                            uint32_t image_width, uint32_t image_height,
                            uint32_t tile_x, uint32_t tile_y) = 0;

  void buildChannelList(std::shared_ptr<HeifPixelImage>& img);

protected:
  AbstractDecoder(uint32_t width, uint32_t height,
                  const std::shared_ptr<Box_cmpd> cmpd,
                  const std::shared_ptr<Box_uncC> uncC);

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

    inline uint64_t getDestinationRowOffset(uint32_t tile_row, uint32_t tile_y) const
    {
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

  void processComponentSample(UncompressedBitReader& srcBits, const ChannelListEntry& entry, uint64_t dst_row_offset, uint32_t tile_column, uint32_t tile_x);

  // Handles the case where a row consists of a single component type
  // Not valid for Pixel interleave
  // Not valid for the Cb/Cr channels in Mixed Interleave
  // Not valid for multi-Y pixel interleave
  void processComponentRow(ChannelListEntry& entry, UncompressedBitReader& srcBits, uint64_t dst_row_offset, uint32_t tile_column);

  void processComponentTileSample(UncompressedBitReader& srcBits, const ChannelListEntry& entry, uint64_t dst_offset, uint32_t tile_x);

  // Handles the case where a row consists of a single component type
  // Not valid for Pixel interleave
  // Not valid for the Cb/Cr channels in Mixed Interleave
  // Not valid for multi-Y pixel interleave
  void processComponentTileRow(ChannelListEntry& entry, UncompressedBitReader& srcBits, uint64_t dst_offset);

  // generic compression and uncompressed, per 23001-17
  const Error get_compressed_image_data_uncompressed(const HeifContext* context, heif_item_id ID,
                                                     std::vector<uint8_t>* data,
                                                     uint64_t range_start_offset, uint64_t range_size,
                                                     uint32_t tile_idx,
                                                     const Box_iloc::Item* item) const;

  const Error do_decompress_data(std::shared_ptr<const Box_cmpC>& cmpC_box,
                                 std::vector<uint8_t> compressed_data,
                                 std::vector<uint8_t>* data) const;

private:
  ChannelListEntry buildChannelListEntry(Box_uncC::Component component, std::shared_ptr<HeifPixelImage>& img);
};

#endif
