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
#include <iostream>
#include <cassert>
#include <utility>

#include "common_utils.h"
#include "context.h"
#include "compression.h"
#include "error.h"
#include "libheif/heif.h"
#include "unc_types.h"
#include "unc_boxes.h"
#include "unc_codec.h"
#include "decoder_abstract.h"


AbstractDecoder::AbstractDecoder(uint32_t width, uint32_t height, const std::shared_ptr<Box_cmpd> cmpd, const std::shared_ptr<Box_uncC> uncC) :
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

void AbstractDecoder::buildChannelList(std::shared_ptr<HeifPixelImage>& img)
{
  for (Box_uncC::Component component : m_uncC->get_components()) {
    ChannelListEntry entry = buildChannelListEntry(component, img);
    channelList.push_back(entry);
  }
}

void AbstractDecoder::processComponentSample(UncompressedBitReader& srcBits, const ChannelListEntry& entry, uint64_t dst_row_offset, uint32_t tile_column, uint32_t tile_x)
{
  uint64_t dst_col_number = tile_column * entry.tile_width + tile_x;
  uint64_t dst_column_offset = dst_col_number * entry.bytes_per_component_sample;
  int val = srcBits.get_bits(entry.bits_per_component_sample);
  memcpy(entry.dst_plane + dst_row_offset + dst_column_offset, &val, entry.bytes_per_component_sample);
}

// Handles the case where a row consists of a single component type
// Not valid for Pixel interleave
// Not valid for the Cb/Cr channels in Mixed Interleave
// Not valid for multi-Y pixel interleave
void AbstractDecoder::processComponentRow(ChannelListEntry& entry, UncompressedBitReader& srcBits, uint64_t dst_row_offset, uint32_t tile_column)
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

void AbstractDecoder::processComponentTileSample(UncompressedBitReader& srcBits, const ChannelListEntry& entry, uint64_t dst_offset, uint32_t tile_x)
{
  uint64_t dst_sample_offset = tile_x * entry.bytes_per_component_sample;
  int val = srcBits.get_bits(entry.bits_per_component_sample);
  memcpy(entry.dst_plane + dst_offset + dst_sample_offset, &val, entry.bytes_per_component_sample);
}

// Handles the case where a row consists of a single component type
// Not valid for Pixel interleave
// Not valid for the Cb/Cr channels in Mixed Interleave
// Not valid for multi-Y pixel interleave
void AbstractDecoder::processComponentTileRow(ChannelListEntry& entry, UncompressedBitReader& srcBits, uint64_t dst_offset)
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


AbstractDecoder::ChannelListEntry AbstractDecoder::buildChannelListEntry(Box_uncC::Component component,
                                                                         std::shared_ptr<HeifPixelImage>& img)
{
  ChannelListEntry entry;
  entry.use_channel = map_uncompressed_component_to_channel(m_cmpd, m_uncC, component, &(entry.channel));
  entry.dst_plane = img->get_plane(entry.channel, &(entry.dst_plane_stride));
  entry.tile_width = m_tile_width;
  entry.tile_height = m_tile_height;
  if ((entry.channel == heif_channel_Cb) || (entry.channel == heif_channel_Cr)) {
    if (m_uncC->get_sampling_type() == sampling_mode_422) {
      entry.tile_width /= 2;
    }
    else if (m_uncC->get_sampling_type() == sampling_mode_420) {
      entry.tile_width /= 2;
      entry.tile_height /= 2;
    }
    if (entry.channel == heif_channel_Cb) {
      entry.other_chroma_dst_plane = img->get_plane(heif_channel_Cr, &(entry.other_chroma_dst_plane_stride));
    }
    else if (entry.channel == heif_channel_Cr) {
      entry.other_chroma_dst_plane = img->get_plane(heif_channel_Cb, &(entry.other_chroma_dst_plane_stride));
    }
  }
  entry.bits_per_component_sample = component.component_bit_depth;
  entry.component_alignment = component.component_align_size;
  entry.bytes_per_component_sample = (component.component_bit_depth + 7) / 8;
  entry.bytes_per_tile_row_src = entry.tile_width * entry.bytes_per_component_sample;
  return entry;
}


// generic compression and uncompressed, per 23001-17
const Error AbstractDecoder::get_compressed_image_data_uncompressed(const HeifContext* context, heif_item_id ID,
                                                                    std::vector<uint8_t>* data,
                                                                    uint64_t range_start_offset, uint64_t range_size,
                                                                    uint32_t tile_idx,
                                                                    const Box_iloc::Item* item) const
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

const Error AbstractDecoder::do_decompress_data(std::shared_ptr<const Box_cmpC>& cmpC_box,
                                                std::vector<uint8_t> compressed_data,
                                                std::vector<uint8_t>* data) const
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
  }
  else if (cmpC_box->get_compression_type() == fourcc("zlib")) {
#if HAVE_ZLIB
    return decompress_zlib(compressed_data, data);
#else
    std::stringstream sstr;
    sstr << "cannot decode unci item with zlib compression - not enabled" << std::endl;
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_generic_compression_method,
                 sstr.str());
#endif
  }
  else if (cmpC_box->get_compression_type() == fourcc("defl")) {
#if HAVE_ZLIB
    return decompress_deflate(compressed_data, data);
#else
    std::stringstream sstr;
    sstr << "cannot decode unci item with deflate compression - not enabled" << std::endl;
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_generic_compression_method,
                 sstr.str());
#endif
  }
  else {
    std::stringstream sstr;
    sstr << "cannot decode unci item with unsupported compression type: " << cmpC_box->get_compression_type() << std::endl;
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_generic_compression_method,
                 sstr.str());
  }
}
