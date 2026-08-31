/*
 * HEIF codec.
 * Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>
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

#include "unc_decoder_block_component_interleave.h"
#include "unc_decoder_legacybase.h" // for skip_to_alignment
#include "unc_codec.h"
#include "unc_types.h"
#include "error.h"

#include <bit>
#include <cassert>
#include <cstring>
#include <vector>


unc_decoder_block_component_interleave::unc_decoder_block_component_interleave(
    uint32_t width, uint32_t height,
    const std::shared_ptr<const Box_cmpd>& cmpd,
    const std::shared_ptr<const Box_uncC>& uncC,
    const std::vector<uint32_t>& uncC_index_to_comp_ids)
    : unc_decoder(width, height, cmpd, uncC, uncC_index_to_comp_ids)
{
}


Result<std::vector<uint64_t>> unc_decoder_block_component_interleave::get_tile_data_sizes() const
{
  const uint32_t block_size = m_uncC->get_block_size();
  assert(block_size > 0);

  if (m_tile_width > UINT32_MAX / block_size) {
    return Error{heif_error_Invalid_input, heif_suberror_Invalid_image_size,
                 "uncompressed tile row size exceeds 32-bit range"};
  }

  uint64_t total_tile_size = 0;

  for (const auto& component : m_uncC->get_components()) {
    (void) component;
    uint32_t bytes_per_row = block_size * m_tile_width;
    skip_to_alignment(bytes_per_row, m_uncC->get_row_align_size());
    total_tile_size += static_cast<uint64_t>(bytes_per_row) * m_tile_height;
  }

  if (m_uncC->get_tile_align_size() != 0) {
    skip_to_alignment(total_tile_size, m_uncC->get_tile_align_size());
  }

  return std::vector<uint64_t>{total_tile_size};
}


Error unc_decoder_block_component_interleave::decode_tile(const std::vector<uint8_t>& tile_data,
                                                           std::shared_ptr<HeifPixelImage>& img,
                                                           uint32_t out_x0, uint32_t out_y0)
{
  const uint32_t block_size = m_uncC->get_block_size();
  const bool little_endian = m_uncC->is_block_little_endian();
  const bool pad_lsb = m_uncC->is_block_pad_lsb();

  // Build per-component info
  struct ComponentInfo {
    uint32_t shift;
    uint64_t mask;
    uint32_t bytes_per_sample;
    bool use;
    uint8_t* dst_plane;
    size_t dst_plane_stride;
  };

  const auto& components = m_uncC->get_components();
  const uint32_t num_components = static_cast<uint32_t>(components.size());
  std::vector<ComponentInfo> comp(num_components);

  for (uint32_t i = 0; i < num_components; i++) {
    const auto& c = components[i];
    comp[i].bytes_per_sample = (c.component_bit_depth + 7) / 8;
    comp[i].mask = (uint64_t{1} << c.component_bit_depth) - 1;

    // Each component is alone in its block, so shift depends only on padding.
    if (pad_lsb) {
      comp[i].shift = block_size * 8 - c.component_bit_depth;
    }
    else {
      comp[i].shift = 0;
    }

    comp[i].dst_plane = img->get_component(m_uncC_index_to_comp_ids[i], &comp[i].dst_plane_stride);
    comp[i].use = true;

#if 0
    heif_channel channel;
    comp[i].use = map_uncompressed_component_to_channel(m_cmpd, c, &channel);
    if (comp[i].use) {
      comp[i].dst_plane = img->get_channel_memory(channel, &comp[i].dst_plane_stride);
    }
    else {
      comp[i].dst_plane = nullptr;
      comp[i].dst_plane_stride = 0;
    }
#endif
  }

  // Address tile_data by 64-bit offset, not by raw pointer. A crafted
  // row_align_size can inflate bytes_per_row to hundreds of megabytes, so the
  // per-row `src += bytes_per_row - pixel_bytes` skip would overflow a 32-bit
  // pointer and wrap to a small in-range address, defeating the src_end check
  // and causing an out-of-bounds read on 32-bit builds. Offset arithmetic in
  // uint64_t cannot wrap; the bound is checked before the pointer is formed.
  const uint8_t* const src_base = tile_data.data();
  const uint64_t src_size = tile_data.size();
  uint64_t src_offset = 0;

  // Process components sequentially (component interleave)
  for (uint32_t c = 0; c < num_components; c++) {
    uint64_t bytes_per_row = static_cast<uint64_t>(block_size) * m_tile_width;
    skip_to_alignment(bytes_per_row, m_uncC->get_row_align_size());

    for (uint32_t tile_y = 0; tile_y < m_tile_height; tile_y++) {
      const uint64_t row_start_offset = src_offset;

      for (uint32_t tile_x = 0; tile_x < m_tile_width; tile_x++) {
        if (src_offset > src_size || block_size > src_size - src_offset) {
          return {heif_error_Invalid_input, heif_suberror_Unspecified,
                  "Block-component interleave: insufficient data"};
        }

        const uint8_t* src = src_base + src_offset;

        if (comp[c].use) {
          // Read block_size bytes into a uint64_t.
          uint64_t block_val = 0;
          if (little_endian) {
            for (uint32_t b = 0; b < block_size; b++) {
              block_val |= static_cast<uint64_t>(src[b]) << (b * 8);
            }
          }
          else {
            for (uint32_t b = 0; b < block_size; b++) {
              block_val = (block_val << 8) | src[b];
            }
          }

          uint32_t value = static_cast<uint32_t>((block_val >> comp[c].shift) & comp[c].mask);
          uint32_t dst_x = out_x0 + tile_x;
          uint32_t dst_y = out_y0 + tile_y;
          uint64_t dst_offset = static_cast<uint64_t>(dst_y) * comp[c].dst_plane_stride
                                + static_cast<uint64_t>(dst_x) * comp[c].bytes_per_sample;
          uint8_t* dst = comp[c].dst_plane + dst_offset;

          if (comp[c].bytes_per_sample == 1) {
            *dst = static_cast<uint8_t>(value);
          }
          else {
            // Store in native endian (2 bytes).
            if constexpr (std::endian::native == std::endian::little) {
              dst[0] = static_cast<uint8_t>(value & 0xFF);
              dst[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
            }
            else {
              dst[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
              dst[1] = static_cast<uint8_t>(value & 0xFF);
            }
          }
        }

        src_offset += block_size;
      }

      // Skip to the next row (includes any row-alignment padding).
      src_offset = row_start_offset + bytes_per_row;
    }
  }

  return Error::Ok;
}


// --- Factory ---

bool unc_decoder_factory_block_component_interleave::can_decode(const std::shared_ptr<const Box_uncC>& uncC) const
{
  if (uncC->get_interleave_type() != interleave_mode_component) {
    return false;
  }

  if (uncC->get_block_size() == 0 || uncC->get_block_size() > 8) {
    return false;
  }

  if (uncC->get_pixel_size() != 0) {
    return false;
  }

  if (uncC->get_sampling_type() != sampling_mode_no_subsampling) {
    return false;
  }

  if (uncC->is_components_little_endian() == true) {
    return false;
  }

  uint32_t block_bits = uncC->get_block_size() * 8;

  for (const auto& component : uncC->get_components()) {
    if (component.component_bit_depth > 16) {
      return false;
    }
    if (component.component_format != component_format_unsigned) {
      return false;
    }
    // Each component must fit in the block, and must be larger than half the
    // block so that exactly one component occupies each block.
    // Each component must be larger than half the block so that exactly one
    // component occupies each block, and must fit within the block.
    if (component.component_bit_depth > block_bits ||
        component.component_bit_depth <= block_bits / 2) {
      return false;
    }
  }

  return true;
}


std::unique_ptr<unc_decoder> unc_decoder_factory_block_component_interleave::create(
    uint32_t width, uint32_t height,
    const std::shared_ptr<const Box_cmpd>& cmpd,
    const std::shared_ptr<const Box_uncC>& uncC,
    const std::vector<uint32_t>& uncC_index_to_comp_ids) const
{
  return std::make_unique<unc_decoder_block_component_interleave>(width, height, cmpd, uncC, uncC_index_to_comp_ids);
}
