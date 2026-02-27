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
#include <map>
#include <set>

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
  uint32_t num_components = image->get_number_of_used_components();

  for (uint32_t idx = 0; idx < num_components; idx++) {
    heif_uncompressed_component_type comp_type;
    heif_channel ch = heif_channel_Y; // default for nonvisual

    if (is_nonvisual) {
      comp_type = static_cast<heif_uncompressed_component_type>(image->get_component_type(idx));
    }
    else {
      ch = image->get_component_channel(idx);
      if (ch == heif_channel_Y && !image->has_channel(heif_channel_Cb)) {
        comp_type = heif_uncompressed_component_type_monochrome;
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
    m_cmpd->add_component({static_cast<uint16_t>(comp.component_type)});

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

  // --- Bayer pattern: add reference components to cmpd and generate cpat box

  if (image->has_bayer_pattern()) {
    const BayerPattern& bayer = image->get_bayer_pattern();

    // The bayer pattern stores component_index values. When the image has a cmpd
    // table (add_component path), we look up the component type from it. When it
    // doesn't (legacy add_plane path), the component_index IS the component type.

    // Collect unique component types from the pattern (in order of first appearance)
    std::vector<uint16_t> unique_types;
    std::set<uint16_t> seen;
    for (const auto& pixel : bayer.pixels) {
      uint16_t comp_type = pixel.component_index;  // legacy: index IS the type
      if (seen.insert(comp_type).second) {
        unique_types.push_back(comp_type);
      }
    }

    // Add reference components to cmpd (these have no uncC entries).
    // box_index is already at the next available index after data components.
    std::map<uint16_t, uint16_t> type_to_cmpd_index;
    for (uint16_t type : unique_types) {
      type_to_cmpd_index[type] = box_index;
      m_cmpd->add_component({type});
      box_index++;
    }

    // Build cpat box with resolved cmpd indices
    BayerPattern cpat_pattern;
    cpat_pattern.pattern_width = bayer.pattern_width;
    cpat_pattern.pattern_height = bayer.pattern_height;
    cpat_pattern.pixels.resize(bayer.pixels.size());
    for (size_t i = 0; i < bayer.pixels.size(); i++) {
      uint16_t comp_type = bayer.pixels[i].component_index;  // legacy: index IS the type
      cpat_pattern.pixels[i].component_index = type_to_cmpd_index[comp_type];
      cpat_pattern.pixels[i].component_gain = bayer.pixels[i].component_gain;
    }

    m_cpat = std::make_shared<Box_cpat>();
    m_cpat->set_pattern(cpat_pattern);
  }

  if (image->has_polarization_patterns()) {
    for (const auto& pol : image->get_polarization_patterns()) {
      auto splz = std::make_shared<Box_splz>();
      splz->set_pattern(pol);
      m_splz.push_back(splz);
    }
  }

  if (image->has_sensor_bad_pixels_maps()) {
    for (const auto& bpm : image->get_sensor_bad_pixels_maps()) {
      auto sbpm = std::make_shared<Box_sbpm>();
      sbpm->set_bad_pixels_map(bpm);
      m_sbpm.push_back(sbpm);
    }
  }

  if (image->has_sensor_nuc()) {
    for (const auto& nuc : image->get_sensor_nuc()) {
      auto snuc = std::make_shared<Box_snuc>();
      snuc->set_nuc(nuc);
      m_snuc.push_back(snuc);
    }
  }

  if (image->has_chroma_location()) {
    m_cloc = std::make_shared<Box_cloc>();
    m_cloc->set_chroma_location(image->get_chroma_location());
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
