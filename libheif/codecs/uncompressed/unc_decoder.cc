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

#include <algorithm>
#include <cassert>
#include <sstream>
#include <utility>

#include "unc_decoder.h"
#include "unc_decoder_component_interleave.h"
#include "unc_decoder_pixel_interleave.h"
#include "unc_decoder_mixed_interleave.h"
#include "unc_decoder_row_interleave.h"
#include "unc_decoder_block_pixel_interleave.h"
#include "unc_decoder_bytealign_component_interleave.h"
#include "unc_decoder_block_component_interleave.h"
#include "unc_codec.h"
#include "unc_boxes.h"
#include "compression.h"
#include "codecs/decoder.h"
#include "security_limits.h"
#include <string>


// --- unc_decoder ---

unc_decoder::unc_decoder(uint32_t width, uint32_t height,
                         const std::shared_ptr<const Box_cmpd>& cmpd,
                         const std::shared_ptr<const Box_uncC>& uncC,
                         const std::vector<uint32_t>& uncC_index_to_comp_ids)
  : m_width(width),
    m_height(height),
    m_cmpd(cmpd),
    m_uncC(uncC),
    m_uncC_index_to_comp_ids(uncC_index_to_comp_ids)
{
  m_tile_height = m_height / m_uncC->get_number_of_tile_rows();
  m_tile_width = m_width / m_uncC->get_number_of_tile_columns();

  assert(m_tile_width > 0);
  assert(m_tile_height > 0);
}


// The size of the data of one tile, as the sum of the sizes from get_tile_data_sizes().
static Result<uint64_t> sum_tile_data_sizes(const std::vector<uint64_t>& sizes)
{
  uint64_t tile_size = 0;
  for (uint64_t size : sizes) {
    if (size > UINT64_MAX - tile_size) {
      return Error{heif_error_Invalid_input, heif_suberror_Invalid_image_size,
                   "uncompressed tile size exceeds 64-bit range"};
    }
    tile_size += size;
  }

  return tile_size;
}


Error unc_decoder::fetch_tile_data(const DataExtent& dataExtent,
                                   const UncompressedImageCodec::unci_properties& properties,
                                   uint32_t tile_x, uint32_t tile_y,
                                   std::vector<uint8_t>& tile_data,
                                   MemoryHandle& tile_data_memory_handle)
{
  if (m_tile_width == 0 || m_tile_height == 0) {
    return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "Internal error: unc_decoder tile dimensions are 0"};
  }

  auto sizesResult = get_tile_data_sizes();
  if (sizesResult.is_error()) {
    return sizesResult.error();
  }
  const auto& sizes = *sizesResult;
  uint32_t tileIdx = tile_x + tile_y * (m_width / m_tile_width);

  // May be nullptr for a raw data extent, in which case no limit applies.
  const heif_security_limits* limits = dataExtent.m_file ? dataExtent.m_file->get_security_limits() : nullptr;

  const auto tile_access = UncompressedImageCodec::get_tile_access(m_uncC, properties.cmpC, properties.icef);

  if (tile_access == UncompressedImageCodec::TileAccess::whole_item) {
    // decode_image() decompresses such an item once for all tiles, and the image item
    // does not offer decoding of individual tiles (GHSA-6fqc-p7r8-2g36).
    return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified,
            "Internal error: unci tiles cannot be decoded independently for this generic compression"};
  }
  else if (tile_access == UncompressedImageCodec::TileAccess::compressed_units) {
    // The compressed units are the complete tile, and get_compressed_image_data_uncompressed()
    // returns the whole tile irrespective of the requested range. Fetch it only once.
    // The scattered per-component reads below would decompress the same units again for
    // each component and concatenate the copies, growing 'tile_data' to num_components
    // times the tile size without any memory accounting (GHSA-fcmw-5764-7rq8).

    Result<uint64_t> tileSizeResult = sum_tile_data_sizes(sizes);
    if (!tileSizeResult) {
      return tileSizeResult.error();
    }

    // The requested size is the size of the whole tile. Units that decompress to
    // a different size are rejected.
    Error err = get_compressed_image_data_uncompressed(dataExtent, properties, &tile_data, 0, *tileSizeResult, tileIdx, nullptr);
    if (err) {
      return err;
    }
  }
  else if (sizes.size() == 1) {
    // Single contiguous read (component, pixel, mixed, row interleave)
    uint64_t tile_start_offset = sizes[0] * tileIdx;
    Error err = get_compressed_image_data_uncompressed(dataExtent, properties, &tile_data, tile_start_offset, sizes[0], tileIdx, nullptr);
    if (err) {
      return err;
    }
  }
  else {
    // Scattered per-component reads (tile_component interleave)
    uint32_t num_tiles = (m_width / m_tile_width) * (m_height / m_tile_height);
    uint64_t component_offset = 0;

    for (uint64_t size : sizes) {
      uint64_t tile_start = component_offset + size * tileIdx;

      std::vector<uint8_t> channel_data;
      Error err = get_compressed_image_data_uncompressed(dataExtent, properties, &channel_data, tile_start, size, tileIdx, nullptr);
      if (err) {
        return err;
      }

      // Charge the component data to the memory budget before appending it, so that
      // the accumulated tile data cannot grow beyond the security limits.
      if (Error memErr = tile_data_memory_handle.alloc(channel_data.size(), limits, "unci tile data")) {
        return memErr;
      }

      tile_data.insert(tile_data.end(), channel_data.begin(), channel_data.end());
      component_offset += size * num_tiles;
    }

    return Error::Ok;
  }

  // The reading and decompression functions only bound the memory while they run.
  // 'tile_data' outlives them, so charge it to the memory budget for as long as the
  // caller keeps it.
  return tile_data_memory_handle.alloc(tile_data.size(), limits, "unci tile data");
}


const Error unc_decoder::get_compressed_image_data_uncompressed(const DataExtent& dataExtent,
                                                                const UncompressedImageCodec::unci_properties& properties,
                                                                std::vector<uint8_t>* data,
                                                                uint64_t range_start_offset, uint64_t range_size,
                                                                uint32_t tile_idx,
                                                                const Box_iloc::Item* item) const
{
  // --- get codec configuration

  std::shared_ptr<const Box_cmpC> cmpC_box = properties.cmpC;
  std::shared_ptr<const Box_icef> icef_box = properties.icef;

  // Security limits used to bound the (potentially highly amplified) decompressed
  // output. May be nullptr for a raw data extent, in which case no limit applies.
  const heif_security_limits* limits = dataExtent.m_file ? dataExtent.m_file->get_security_limits() : nullptr;

  if (!cmpC_box) {
    // assume no generic compression
    auto readResult = dataExtent.read_data(range_start_offset, range_size);
    if (!readResult) {
      return readResult.error();
    }

    data->insert(data->end(), readResult->begin(), readResult->end());

    return Error::Ok;
  }

  uint32_t units_per_tile = 0;
  const auto tile_access = UncompressedImageCodec::get_tile_access(m_uncC, cmpC_box, icef_box, &units_per_tile);

  if (tile_access != UncompressedImageCodec::TileAccess::compressed_units) {
    // An item that has to be decompressed as a whole is handled by decode_image(),
    // which decompresses it once for all tiles (GHSA-6fqc-p7r8-2g36).
    return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified,
            "Internal error: unci tiles cannot be decoded independently for this generic compression"};
  }

  // --- the tile consists of 'units_per_tile' consecutive compressed units

  const auto& units = icef_box->get_units();

  const uint64_t first_unit = static_cast<uint64_t>(tile_idx) * units_per_tile;
  if (first_unit >= units.size() ||
      units_per_tile > units.size() - first_unit) {
    return {
      heif_error_Invalid_input,
      heif_suberror_Unspecified,
      "no icef-box entry for tile index"
    };
  }

  // Decompress only the units of this tile. 'range_size' is the size of the tile, so
  // there cannot be more data than this in a valid file. Stopping there prevents small
  // units from inflating far beyond the tile size (GHSA-fcmw-5764-7rq8).
  uint64_t remaining_size = range_size;

  // The decompression functions only bound the memory of the unit they are working on.
  // This handle accounts for the units of the tile that have been decompressed already.
  // The complete tile is charged by the caller afterwards.
  MemoryHandle accumulated_memory_handle;

  for (uint32_t i = 0; i < units_per_tile; i++) {
    const auto unit = units[first_unit + i];

    Result<std::vector<uint8_t> > readingResult = dataExtent.read_data(unit.unit_offset, unit.unit_size);
    if (!readingResult) {
      return readingResult.error();
    }

    auto dataResult = do_decompress_data(cmpC_box, *readingResult, limits, remaining_size);
    if (!dataResult) {
      return dataResult.error();
    }

    if (dataResult->size() > remaining_size) {
      return {
        heif_error_Invalid_input,
        heif_suberror_Decompression_invalid_data,
        "compressed units of unci image contain more data than the image tile"
      };
    }

    remaining_size -= dataResult->size();

    if (Error memErr = accumulated_memory_handle.alloc(dataResult->size(), limits,
                                                       "unci icef decompressed units")) {
      return memErr;
    }

    if (units_per_tile == 1) {
      *data = std::move(*dataResult);
    }
    else {
      data->insert(data->end(), dataResult->begin(), dataResult->end());
    }
  }

  // Too little data is rejected as well. The tile decoders would read the missing
  // data as zeros.
  if (remaining_size != 0) {
    return {
      heif_error_Invalid_input,
      heif_suberror_End_of_data,
      "compressed units of unci image contain less data than the image tile"
    };
  }

  return Error::Ok;
}


Result<std::vector<uint8_t> > unc_decoder::decompress_whole_item(const DataExtent& dataExtent,
                                                                 const UncompressedImageCodec::unci_properties& properties,
                                                                 uint64_t expected_size) const
{
  std::shared_ptr<const Box_cmpC> cmpC_box = properties.cmpC;
  std::shared_ptr<const Box_icef> icef_box = properties.icef;

  // Security limits used to bound the (potentially highly amplified) decompressed
  // output. May be nullptr for a raw data extent, in which case no limit applies.
  const heif_security_limits* limits = dataExtent.m_file ? dataExtent.m_file->get_security_limits() : nullptr;

  Result<std::vector<uint8_t>*> readResult = dataExtent.read_data();
  if (!readResult) {
    return readResult.error();
  }

  // Do not move the data out of the extent. This is the read cache of the extent and
  // an emptied cache makes the next access read the data again, which a raw extent cannot do.
  const std::vector<uint8_t>& compressed_bytes = **readResult;

  // 'expected_size' is the size of the data of all tiles, so there cannot be more data
  // than this in a valid file. Stopping there bounds the decompression by the image
  // size instead of the memory budget.

  std::vector<uint8_t> data;

  if (icef_box) {
    // Bound the total decompressed output accumulated across all units. The units
    // may overlap (nothing forces them to be disjoint), so N units can point at the
    // same compressed slice and be decompressed N times into `data`. Without this
    // accounting, that amplifies a tiny icef box into an unbounded allocation
    // (GHSA-24wx-9w62-c96w). The handle is local, so it only bounds the peak while
    // building `data`; the result is accounted by the caller.
    MemoryHandle accumulated_memory_handle;

    for (Box_icef::CompressedUnitInfo unit_info : icef_box->get_units()) {
      // Use subtraction form to avoid a uint64_t wrap in 'unit_offset + unit_size',
      // which could otherwise pass this check and lead to an out-of-bounds read when
      // constructing the iterators below (GHSA-r7qj-cg5r-r6vf).
      if (unit_info.unit_offset > compressed_bytes.size() ||
          unit_info.unit_size > compressed_bytes.size() - unit_info.unit_offset) {
        return Error{
          heif_error_Invalid_input,
          heif_suberror_Unspecified,
          "incomplete data in unci image"
        };
      }

      auto unit_start = compressed_bytes.begin() + unit_info.unit_offset;
      auto unit_end = unit_start + unit_info.unit_size;
      std::vector<uint8_t> compressed_unit_data = std::vector<uint8_t>(unit_start, unit_end);

      auto dataResult = do_decompress_data(cmpC_box, compressed_unit_data, limits, expected_size - data.size());
      if (!dataResult) {
        return dataResult.error();
      }

      if (dataResult->size() > expected_size - data.size()) {
        return Error{
          heif_error_Invalid_input,
          heif_suberror_Decompression_invalid_data,
          "compressed unci image contains more data than the image tiles"
        };
      }

      if (Error memErr = accumulated_memory_handle.alloc(dataResult->size(), limits,
                                                         "unci icef decompressed units")) {
        return memErr;
      }

      data.insert(data.end(), dataResult->begin(), dataResult->end());
    }
  }
  else {
    // Decode as a single blob
    auto dataResult = do_decompress_data(cmpC_box, compressed_bytes, limits, expected_size);
    if (!dataResult) {
      return dataResult.error();
    }

    data = std::move(*dataResult);
  }

  // Too little data is rejected as well. The tile decoders would read the missing
  // data as zeros.
  if (data.size() < expected_size) {
    return Error{
      heif_error_Invalid_input,
      heif_suberror_End_of_data,
      "compressed unci image contains less data than the image tiles"
    };
  }

  return data;
}


Result<std::vector<uint8_t> > unc_decoder::do_decompress_data(std::shared_ptr<const Box_cmpC>& cmpC_box,
                                                              const std::vector<uint8_t>& compressed_data,
                                                              const heif_security_limits* limits,
                                                              uint64_t max_output_size) const
{
  if (cmpC_box->get_compression_type() == fourcc("brot")) {
#if HAVE_BROTLI
    return decompress_brotli(compressed_data, limits, max_output_size);
#else
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_generic_compression_method,
                 "cannot decode unci item with brotli compression - not enabled");
#endif
  }
  else if (cmpC_box->get_compression_type() == fourcc("zlib")) {
#if HAVE_ZLIB
    return decompress_zlib(compressed_data, limits, max_output_size);
#else
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_generic_compression_method,
                 "cannot decode unci item with zlib compression - not enabled");
#endif
  }
  else if (cmpC_box->get_compression_type() == fourcc("defl")) {
#if HAVE_ZLIB
    return decompress_deflate(compressed_data, limits, max_output_size);
#else
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_generic_compression_method,
                 "cannot decode unci item with deflate compression - not enabled");
#endif
  }
  else {
    std::stringstream sstr;
    sstr << "cannot decode unci item with unsupported compression type: " << cmpC_box->get_compression_type() << '\n';
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_generic_compression_method,
                 sstr.str());
  }
}


Error unc_decoder::decode_image(const DataExtent& extent,
                                const UncompressedImageCodec::unci_properties& properties,
                                std::shared_ptr<HeifPixelImage>& img)
{
  uint32_t tile_width = m_width / m_uncC->get_number_of_tile_columns();
  uint32_t tile_height = m_height / m_uncC->get_number_of_tile_rows();

  ensure_channel_list(img);

  // The extent and properties the tile data is fetched from.
  const DataExtent* tile_extent = &extent;
  const UncompressedImageCodec::unci_properties* tile_properties = &properties;

  DataExtent decompressed_extent;
  UncompressedImageCodec::unci_properties decompressed_properties;

  if (UncompressedImageCodec::get_tile_access(m_uncC, properties.cmpC, properties.icef) ==
      UncompressedImageCodec::TileAccess::whole_item) {
    // The compressed units do not correspond to the tiles, so the item has to be
    // decompressed completely to get the data of any tile. Do this once for all tiles
    // and continue as if the item was not compressed. Decompressing the item again for
    // each tile needs time proportional to num_tiles * item_size (GHSA-6fqc-p7r8-2g36).

    auto sizesResult = get_tile_data_sizes();
    if (!sizesResult) {
      return sizesResult.error();
    }

    Result<uint64_t> tileSizeResult = sum_tile_data_sizes(*sizesResult);
    if (!tileSizeResult) {
      return tileSizeResult.error();
    }

    const uint64_t num_tiles = static_cast<uint64_t>(m_uncC->get_number_of_tile_columns()) * m_uncC->get_number_of_tile_rows();
    if (num_tiles != 0 && *tileSizeResult > UINT64_MAX / num_tiles) {
      return {heif_error_Invalid_input, heif_suberror_Invalid_image_size,
              "uncompressed image data size exceeds 64-bit range"};
    }

    auto dataResult = decompress_whole_item(extent, properties, *tileSizeResult * num_tiles);
    if (!dataResult) {
      return dataResult.error();
    }

    // May be nullptr for a raw data extent, in which case no limit applies.
    const heif_security_limits* limits = extent.m_file ? extent.m_file->get_security_limits() : nullptr;

    // The decompressed data is kept until all tiles are decoded, so charge it to the memory budget.
    if (Error memErr = decompressed_extent.m_raw_memory_handle.alloc(dataResult->size(), limits,
                                                                     "decompressed unci image data")) {
      return memErr;
    }

    decompressed_extent.m_file = extent.m_file;
    decompressed_extent.m_raw = std::move(*dataResult);

    decompressed_properties = properties;
    decompressed_properties.cmpC = nullptr;
    decompressed_properties.icef = nullptr;

    tile_extent = &decompressed_extent;
    tile_properties = &decompressed_properties;
  }

  for (uint32_t tile_y0 = 0; tile_y0 < m_height; tile_y0 += tile_height)
    for (uint32_t tile_x0 = 0; tile_x0 < m_width; tile_x0 += tile_width) {
      std::vector<uint8_t> tile_data;
      MemoryHandle tile_data_memory_handle;
      Error error = fetch_tile_data(*tile_extent, *tile_properties, tile_x0 / tile_width, tile_y0 / tile_height, tile_data, tile_data_memory_handle);
      if (error) {
        return error;
      }

      error = decode_tile(tile_data, img, tile_x0, tile_y0);
      if (error) {
        return error;
      }
    }

  return Error::Ok;
}


// --- unc_decoder_factory ---

bool unc_decoder_factory::check_common_requirements(const std::shared_ptr<const Box_uncC>& uncC)
{
  for (const auto& component : uncC->get_components()) {
    if (component.component_bit_depth > 16) {
      return false;
    }
    if (component.component_format != component_format_unsigned) {
      return false;
    }
    if (component.component_align_size > 2) {
      return false;
    }
  }

  if (uncC->get_block_size() != 0) {
    return false;
  }
  if (uncC->is_block_pad_lsb()) {
    return false;
  }
  if (uncC->is_block_little_endian()) {
    return false;
  }
  if (uncC->is_block_reversed()) {
    return false;
  }

#if 0
  if (uncC->is_components_little_endian()) {
    const auto& comps = uncC->get_components();
    bool all_8_bit = std::all_of(comps.begin(), comps.end(),
                                 [](const Box_uncC::Component& c) { return c.component_bit_depth == 8; });
    if (!all_8_bit) {
      return false;
    }
  }
#endif

  return true;
}


bool unc_decoder_factory::has_any_multi_byte_components(const std::shared_ptr<const Box_uncC>& uncC)
{
  const auto& comps = uncC->get_components();
  return std::any_of(comps.begin(), comps.end(),
                     [](const Box_uncC::Component& c) { return c.component_bit_depth > 8; });
}


Error check_hard_limits(const std::shared_ptr<const Box_uncC>& uncC)
{
  const auto& components = uncC->get_components();

  for (const auto& component : components) {
    switch (component.component_format) {
      case heif_uncompressed_component_format::component_format_signed:
      case heif_uncompressed_component_format::component_format_unsigned:
        if (component.component_bit_depth > 64) {
          return Error{heif_error_Unsupported_feature, heif_suberror_Unspecified, "Maximum supported integer bit-depth is 64 bits."};
        }
        break;

      case heif_uncompressed_component_format::component_format_float:
        if (component.component_bit_depth != 32 &&
            component.component_bit_depth != 64) {
          return Error{heif_error_Unsupported_feature, heif_suberror_Unspecified, "Only 32 bit and 64 bit floats are supported."};
        }
        break;

      case heif_uncompressed_component_format::component_format_complex:
        if (component.component_bit_depth != 64 &&
            component.component_bit_depth != 128) {
          return Error{heif_error_Unsupported_feature, heif_suberror_Unspecified, "Only 2x32 bit and 2x64 bit complex values are supported."};
        }
        break;

      default:
        return Error{heif_error_Unsupported_feature, heif_suberror_Unspecified, "Unknown component format."};
    }
  }

  if (uncC->get_interleave_type() != 1 &&
      uncC->get_interleave_type() != 5 &&
      uncC->get_pixel_size() != 0) {
        return Error{heif_error_Invalid_input, heif_suberror_Unspecified, "uncC pixel_size must be 0 for interleave_types other than 1 or 5."};
  }

  if (uncC->get_interleave_type() == interleave_mode_pixel &&
      uncC->get_pixel_size() != 0) {
    uint32_t total_pixel_bits = 0;
    for (const auto& component : uncC->get_components()) {
      total_pixel_bits += component.component_bit_depth;
    }

    // TODO: we do not consider padding or block sizes yet
    uint32_t PixelBytes = (total_pixel_bits + 7)/8;
    if (PixelBytes > uncC->get_pixel_size()) {
      return Error{heif_error_Invalid_input, heif_suberror_Unspecified, "uncC pixel_size smaller than sum of component sizes."};
    }
  }

  return {};
}


Result<std::unique_ptr<unc_decoder> > unc_decoder_factory::get_unc_decoder(
  uint32_t width, uint32_t height,
  const std::shared_ptr<const Box_cmpd>& cmpd,
  const std::shared_ptr<const Box_uncC>& uncC,
  const std::vector<uint32_t>& uncC_index_to_comp_ids
)
{
  static unc_decoder_factory_component_interleave dec_component;
  static unc_decoder_factory_bytealign_component_interleave dec_bytealign_component;
  static unc_decoder_factory_block_component_interleave dec_block_component;
  static unc_decoder_factory_pixel_interleave dec_pixel;
  static unc_decoder_factory_block_pixel_interleave dec_block_pixel;
  static unc_decoder_factory_mixed_interleave dec_mixed;
  static unc_decoder_factory_row_interleave dec_row;

  static const unc_decoder_factory* decoders[]{
    &dec_bytealign_component, &dec_block_component, &dec_component, &dec_pixel, &dec_block_pixel, &dec_mixed, &dec_row
  };

  for (const unc_decoder_factory* dec : decoders) {
    if (dec->can_decode(uncC)) {
      return {dec->create(width, height, cmpd, uncC, uncC_index_to_comp_ids)};
    }
  }

  std::stringstream sstr;
  sstr << "No decoder found for uncompressed format (interleave_type of " << ((int) uncC->get_interleave_type()) << ")";
  return Error{heif_error_Unsupported_feature, heif_suberror_Unsupported_data_version, sstr.str()};
}


// --- decode orchestration ---

Result<std::shared_ptr<HeifPixelImage> > unc_decoder::decode_full_image(
  const UncompressedImageCodec::unci_properties& properties,
  const DataExtent& extent,
  const heif_security_limits* limits)
{
  const std::shared_ptr<const Box_ispe>& ispe = properties.ispe;
  const std::shared_ptr<const Box_cmpd>& cmpd = properties.cmpd;
  const std::shared_ptr<const Box_uncC>& uncC = properties.uncC;

  assert(ispe);
  uint32_t width = ispe->get_width();
  uint32_t height = ispe->get_height();

  Error global_limit_error = check_hard_limits(uncC);
  if (global_limit_error) {
    return {global_limit_error};
  }

  std::vector<uint32_t> uncC_index_to_comp_ids;

  Result<std::shared_ptr<HeifPixelImage> > createImgResult = UncompressedImageCodec::create_image(properties, width, height, uncC_index_to_comp_ids, limits);
  if (!createImgResult) {
    return createImgResult.error();
  }

  auto img = *createImgResult;


  auto decoderResult = unc_decoder_factory::get_unc_decoder(width, height, cmpd, uncC, uncC_index_to_comp_ids);
  if (!decoderResult) {
    return decoderResult.error();
  }

  auto& decoder = *decoderResult;

  Error error = decoder->decode_image(extent, properties, img);
  if (error) {
    return error;
  }

  return img;
}
