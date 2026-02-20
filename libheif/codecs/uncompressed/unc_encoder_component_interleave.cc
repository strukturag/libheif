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

#include "unc_encoder_component_interleave.h"

#include <cstring>

#include "pixelimage.h"
#include "unc_boxes.h"


bool unc_encoder_factory_component_interleave::can_encode(const std::shared_ptr<const HeifPixelImage>& image,
                                                          const heif_encoding_options& options) const
{
  if (image->has_channel(heif_channel_interleaved)) {
    return false;
  }

  return true;
}


std::unique_ptr<const unc_encoder> unc_encoder_factory_component_interleave::create(const std::shared_ptr<const HeifPixelImage>& image,
                                                                                    const heif_encoding_options& options) const
{
  return std::make_unique<unc_encoder_component_interleave>(image, options);
}


unc_encoder_component_interleave::unc_encoder_component_interleave(const std::shared_ptr<const HeifPixelImage>& image,
                                                                   const heif_encoding_options& options)
{
  bool is_nonvisual = (image->get_colorspace() == heif_colorspace_nonvisual);
  uint32_t num_components = image->get_number_of_components();

  for (uint32_t idx = 0; idx < num_components; idx++) {
    heif_uncompressed_component_type comp_type;
    heif_channel ch = heif_channel_Y; // default for nonvisual

    if (is_nonvisual) {
      comp_type = static_cast<heif_uncompressed_component_type>(image->get_component_type(idx));
    }
    else {
      ch = image->get_component_channel(idx);
      if (ch == heif_channel_Y && !image->has_channel(heif_channel_Cb)) {
        comp_type = component_type_monochrome;
      }
      else {
        comp_type = heif_channel_to_component_type(ch);
      }
    }

    uint8_t bpp = image->get_component_bits_per_pixel(idx);
    auto comp_format = to_unc_component_format(image->get_component_datatype(idx));
    bool aligned = (bpp % 8 == 0);

    m_components.push_back({idx, ch, comp_type, comp_format, bpp, aligned});
  }

  // Build cmpd/uncC boxes
  bool little_endian = false;

  uint16_t box_index = 0;
  for (const auto& comp : m_components) {
    m_cmpd->add_component({comp.component_type});

    uint8_t component_align_size = 0;

    if (comp.byte_aligned && comp.bpp > 8) {
      little_endian = true;
    }

    m_uncC->add_component({box_index, comp.bpp, comp.component_format, component_align_size});
    box_index++;
  }

  m_uncC->set_interleave_type(interleave_mode_component);
  m_uncC->set_components_little_endian(little_endian);
  m_uncC->set_block_size(0);

  if (image->get_chroma_format() == heif_chroma_420) {
    m_uncC->set_sampling_type(sampling_mode_420);
  }
  else if (image->get_chroma_format() == heif_chroma_422) {
    m_uncC->set_sampling_type(sampling_mode_422);
  }
  else {
    m_uncC->set_sampling_type(sampling_mode_no_subsampling);
  }
}


uint64_t unc_encoder_component_interleave::compute_tile_data_size_bytes(uint32_t tile_width, uint32_t tile_height) const
{
  uint64_t total = 0;
  for (const auto& comp : m_components) {
    uint32_t plane_width = tile_width;
    uint32_t plane_height = tile_height;

    if (comp.channel == heif_channel_Cb || comp.channel == heif_channel_Cr) {
      // Adjust for chroma subsampling
      if (m_uncC->get_sampling_type() == sampling_mode_420) {
        plane_width = (plane_width + 1) / 2;
        plane_height = (plane_height + 1) / 2;
      }
      else if (m_uncC->get_sampling_type() == sampling_mode_422) {
        plane_width = (plane_width + 1) / 2;
      }
    }

    uint64_t row_bytes;
    if (comp.byte_aligned) {
      row_bytes = static_cast<uint64_t>(plane_width) * ((comp.bpp + 7) / 8);
    }
    else {
      row_bytes = (static_cast<uint64_t>(plane_width) * comp.bpp + 7) / 8;
    }
    total += row_bytes * plane_height;
  }
  return total;
}


std::vector<uint8_t> unc_encoder_component_interleave::encode_tile(const std::shared_ptr<const HeifPixelImage>& src_image) const
{
  uint64_t total_size = compute_tile_data_size_bytes(src_image->get_width(), src_image->get_height());
  std::vector<uint8_t> data;
  data.resize(total_size);

  uint64_t out_pos = 0;

  for (const auto& comp : m_components) {
    uint32_t plane_width = src_image->get_component_width(comp.component_idx);
    uint32_t plane_height = src_image->get_component_height(comp.component_idx);
    uint8_t bpp = comp.bpp;

    size_t src_stride;
    const uint8_t* src_data = src_image->get_component(comp.component_idx, &src_stride);

    if (comp.byte_aligned) {
      // Byte-aligned path: memcpy per row
      int bytes_per_pixel = (bpp + 7) / 8;

      for (uint32_t y = 0; y < plane_height; y++) {
        memcpy(data.data() + out_pos,
               src_data + src_stride * y,
               plane_width * bytes_per_pixel);
        out_pos += plane_width * bytes_per_pixel;
      }
    }
    else {
      // Bit-packed path: bit accumulator with row-end flush
      for (uint32_t y = 0; y < plane_height; y++) {
        const uint8_t* row = src_data + src_stride * y;

        uint64_t accumulator = 0;
        int accumulated_bits = 0;

        for (uint32_t x = 0; x < plane_width; x++) {
          uint32_t sample;

          if (bpp <= 8) {
            sample = row[x];
          }
          else if (bpp <= 16) {
            sample = reinterpret_cast<const uint16_t*>(row)[x];
          }
          else {
            sample = reinterpret_cast<const uint32_t*>(row)[x];
          }

          accumulator = (accumulator << bpp) | sample;
          accumulated_bits += bpp;

          while (accumulated_bits >= 8) {
            accumulated_bits -= 8;
            data[out_pos++] = static_cast<uint8_t>(accumulator >> accumulated_bits);
            accumulator &= (uint64_t{1} << accumulated_bits) - 1;
          }
        }

        // Flush partial byte at row end (pad with zeros in LSBs)
        if (accumulated_bits > 0) {
          data[out_pos++] = static_cast<uint8_t>(accumulator << (8 - accumulated_bits));
        }
      }
    }
  }

  return data;
}
