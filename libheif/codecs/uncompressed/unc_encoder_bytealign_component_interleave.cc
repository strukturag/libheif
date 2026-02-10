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


void unc_encoder_bytealign_component_interleave::add_channel_if_exists(const std::shared_ptr<const HeifPixelImage>& image, heif_channel channel)
{
  if (image->has_channel(channel)) {
    m_components.push_back({channel, heif_channel_to_component_type(channel)});
  }
}


unc_encoder_bytealign_component_interleave::unc_encoder_bytealign_component_interleave(const std::shared_ptr<const HeifPixelImage>& image,
                                       const heif_encoding_options& options)
{
  // Special case for heif_channel_Y:
  // - if this an YCbCr image, use component_type_Y,
  // - otherwise, use component_type_monochrome

  if (image->has_channel(heif_channel_Y)) {
    if (image->has_channel(heif_channel_Cb) && image->has_channel(heif_channel_Cr)) {
      m_components.push_back({heif_channel_Y, heif_uncompressed_component_type::component_type_Y});
    }
    else {
      m_components.push_back({heif_channel_Y, heif_uncompressed_component_type::component_type_monochrome});
    }
  }

  add_channel_if_exists(image, heif_channel_Cb);
  add_channel_if_exists(image, heif_channel_Cr);
  add_channel_if_exists(image, heif_channel_R);
  add_channel_if_exists(image, heif_channel_G);
  add_channel_if_exists(image, heif_channel_B);
  add_channel_if_exists(image, heif_channel_Alpha);
  add_channel_if_exists(image, heif_channel_filter_array);
  add_channel_if_exists(image, heif_channel_depth);
  add_channel_if_exists(image, heif_channel_disparity);


  // if we have any component > 8 bits, we enable this
  bool little_endian = false;

  uint16_t index = 0;
  for (channel_component channelcomponent : m_components) {
    m_cmpd->add_component({channelcomponent.component_type});

    uint8_t bpp = image->get_bits_per_pixel(channelcomponent.channel);
    uint8_t component_align_size = static_cast<uint8_t>((bpp + 7) / 8);

    if (bpp % 8 == 0) {
      component_align_size = 0;
    }

    if (bpp > 8) {
      little_endian = true; // TODO: depending on the host endianness
    }

    m_uncC->add_component({index, bpp, component_format_unsigned, component_align_size});
    index++;
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

  for (channel_component channelcomponent : m_components) {
    int bpp = image->get_bits_per_pixel(channelcomponent.channel);
    int bytes_per_pixel = 4 * (bpp + 7) / 8;

    if (channelcomponent.channel == heif_channel_Cb ||
        channelcomponent.channel == heif_channel_Cr) {
      int downsampling = chroma_h_subsampling(image->get_chroma_format()) * chroma_v_subsampling(image->get_chroma_format());
      bytes_per_pixel /= downsampling;
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
  std::vector<uint8_t> data;

  // compute total size of all components

  uint64_t total_size = 0;

  for (channel_component channelcomponent : m_components) {
    int bpp = src_image->get_bits_per_pixel(channelcomponent.channel);
    int bytes_per_pixel = (bpp + 7) / 8;

    total_size += static_cast<uint64_t>(src_image->get_height(channelcomponent.channel)) * src_image->get_width(channelcomponent.channel) * bytes_per_pixel;
  }

  data.resize(total_size);

  // output all component planes

  uint64_t out_data_start_pos = 0;

  for (channel_component channelcomponent : m_components) {
    int bpp = src_image->get_bits_per_pixel(channelcomponent.channel);
    int bytes_per_pixel = (bpp + 7) / 8;

    size_t src_stride;
    const uint8_t* src_data = src_image->get_plane(channelcomponent.channel, &src_stride);

    for (uint32_t y = 0; y < src_image->get_height(channelcomponent.channel); y++) {
      uint32_t width = src_image->get_width(channelcomponent.channel);
      memcpy(data.data() + out_data_start_pos, src_data + src_stride * y, width * bytes_per_pixel);
      out_data_start_pos += width * bytes_per_pixel;
    }
  }

  return data;
}
