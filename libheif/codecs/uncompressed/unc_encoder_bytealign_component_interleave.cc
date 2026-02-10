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

#include "unc_encoder_bytealign_component_interleave.h"

#include <cstring>

#include "pixelimage.h"
#include "unc_boxes.h"


bool unc_encoder_factory_bytealign_component_interleave::can_encode(const std::shared_ptr<const HeifPixelImage>& image,
                                            const heif_encoding_options& options) const
{
  if (image->has_channel(heif_channel_interleaved)) {
    return false;
  }

  return true;
}


std::unique_ptr<const unc_encoder> unc_encoder_factory_bytealign_component_interleave::create(const std::shared_ptr<const HeifPixelImage>& image,
                                                                      const heif_encoding_options& options) const
{
  return std::make_unique<unc_encoder_bytealign_component_interleave>(image, options);
}


unc_encoder_bytealign_component_interleave::unc_encoder_bytealign_component_interleave(const std::shared_ptr<const HeifPixelImage>& image,
                                       const heif_encoding_options& options)
{
  bool is_nonvisual = (image->get_colorspace() == heif_colorspace_nonvisual);
  uint32_t num_components = image->get_number_of_components();

  for (uint32_t idx = 0; idx < num_components; idx++) {
    heif_uncompressed_component_type comp_type;

    if (is_nonvisual) {
      comp_type = static_cast<heif_uncompressed_component_type>(image->get_component_type(idx));
    }
    else {
      heif_channel ch = image->get_component_channel(idx);
      if (ch == heif_channel_Y && !image->has_channel(heif_channel_Cb)) {
        comp_type = component_type_monochrome;
      }
      else {
        comp_type = heif_channel_to_component_type(ch);
      }
    }

    uint8_t bpp = image->get_component_bits_per_pixel(idx);
    auto datatype = image->get_component_datatype(idx);
    auto comp_format = to_unc_component_format(datatype);

    m_components.push_back({idx, comp_type, comp_format, bpp});
  }

  // Build cmpd/uncC boxes
  bool little_endian = false;

  uint16_t box_index = 0;
  for (const auto& comp : m_components) {
    m_cmpd->add_component({comp.component_type});

    uint8_t component_align_size = static_cast<uint8_t>((comp.bpp + 7) / 8);
    if (comp.bpp % 8 == 0) {
      component_align_size = 0;
    }

    if (comp.bpp > 8) {
      little_endian = true;
    }

    m_uncC->add_component({box_index, comp.bpp, comp.component_format, component_align_size});
    box_index++;
  }

  m_uncC->set_interleave_type(interleave_mode_component);
  m_uncC->set_components_little_endian(little_endian);

  if (image->get_chroma_format() == heif_chroma_420) {
    m_uncC->set_sampling_type(sampling_mode_420);
  }
  else if (image->get_chroma_format() == heif_chroma_422) {
    m_uncC->set_sampling_type(sampling_mode_422);
  }
  else {
    m_uncC->set_sampling_type(sampling_mode_no_subsampling);
  }

  // --- compute bytes per pixel

  m_bytes_per_pixel_x4 = 0;

  for (const auto& comp : m_components) {
    int bytes_per_pixel = 4 * (comp.bpp + 7) / 8;

    if (!is_nonvisual) {
      heif_channel ch = image->get_component_channel(comp.component_idx);
      if (ch == heif_channel_Cb || ch == heif_channel_Cr) {
        int downsampling = chroma_h_subsampling(image->get_chroma_format())
                         * chroma_v_subsampling(image->get_chroma_format());
        bytes_per_pixel /= downsampling;
      }
    }

    m_bytes_per_pixel_x4 += bytes_per_pixel;
  }
}


uint64_t unc_encoder_bytealign_component_interleave::compute_tile_data_size_bytes(uint32_t tile_width, uint32_t tile_height) const
{
  return tile_width * tile_height * m_bytes_per_pixel_x4 / 4;
}


std::vector<uint8_t> unc_encoder_bytealign_component_interleave::encode_tile(const std::shared_ptr<const HeifPixelImage>& src_image) const
{
  // compute total size of all components

  uint64_t total_size = 0;

  for (const auto& comp : m_components) {
    int bytes_per_pixel = (comp.bpp + 7) / 8;
    uint32_t w = src_image->get_component_width(comp.component_idx);
    uint32_t h = src_image->get_component_height(comp.component_idx);
    total_size += static_cast<uint64_t>(h) * w * bytes_per_pixel;
  }

  std::vector<uint8_t> data;
  data.resize(total_size);

  // output all component planes

  uint64_t out_data_start_pos = 0;

  for (const auto& comp : m_components) {
    int bytes_per_pixel = (comp.bpp + 7) / 8;
    uint32_t w = src_image->get_component_width(comp.component_idx);
    uint32_t h = src_image->get_component_height(comp.component_idx);

    size_t src_stride;
    const uint8_t* src_data = src_image->get_component(comp.component_idx, &src_stride);

    for (uint32_t y = 0; y < h; y++) {
      memcpy(data.data() + out_data_start_pos,
             src_data + src_stride * y,
             w * bytes_per_pixel);
      out_data_start_pos += w * bytes_per_pixel;
    }
  }

  return data;
}
