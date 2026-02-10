/*
 * ImageMeter confidential
 *
 * Copyright (C) 2026 by Dirk Farin, Kronenstr. 49b, 70174 Stuttgart, Germany
 * All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains the property
 * of Dirk Farin.  The intellectual and technical concepts contained
 * herein are proprietary to Dirk Farin and are protected by trade secret
 * and copyright law.
 * Dissemination of this information or reproduction of this material
 * is strictly forbidden unless prior written permission is obtained
 * from Dirk Farin.
 */

#include "unc_encoder_rgb_block_pixel_interleave.h"

#include "pixelimage.h"
#include "unc_boxes.h"
#include "unc_types.h"


bool unc_encoder_factory_rgb_block_pixel_interleave::can_encode(const std::shared_ptr<const HeifPixelImage>& image,
                                                               const heif_encoding_options& options) const
{
  if (image->get_colorspace() != heif_colorspace_RGB) {
    return false;
  }

  switch (image->get_chroma_format()) {
    case heif_chroma_interleaved_RRGGBB_LE:
    case heif_chroma_interleaved_RRGGBB_BE:
      break;
    default:
      return false;
  }

  if (image->get_bits_per_pixel(heif_channel_interleaved) >= 14) {
    return false;
  }

  return true;
}


std::unique_ptr<const unc_encoder> unc_encoder_factory_rgb_block_pixel_interleave::create(const std::shared_ptr<const HeifPixelImage>& image,
                                                                                         const heif_encoding_options& options) const
{
  return std::make_unique<unc_encoder_rgb_block_pixel_interleave>(image, options);
}


unc_encoder_rgb_block_pixel_interleave::unc_encoder_rgb_block_pixel_interleave(const std::shared_ptr<const HeifPixelImage>& image,
                                                                             const heif_encoding_options& options)
{
  m_cmpd->add_component({component_type_red});
  m_cmpd->add_component({component_type_green});
  m_cmpd->add_component({component_type_blue});

  uint8_t bpp = image->get_bits_per_pixel(heif_channel_interleaved);

  uint8_t nBits = static_cast<uint8_t>(3 * bpp);
  m_bytes_per_pixel = static_cast<uint8_t>((nBits + 7) / 8);

  m_uncC->set_interleave_type(interleave_mode_pixel);
  m_uncC->set_pixel_size(m_bytes_per_pixel);
  m_uncC->set_block_size(m_bytes_per_pixel);
  m_uncC->set_sampling_type(sampling_mode_no_subsampling);
  m_uncC->set_block_little_endian(true);

  m_uncC->add_component({0, bpp, component_format_unsigned, 0});
  m_uncC->add_component({1, bpp, component_format_unsigned, 0});
  m_uncC->add_component({2, bpp, component_format_unsigned, 0});
}


uint64_t unc_encoder_rgb_block_pixel_interleave::compute_tile_data_size_bytes(uint32_t tile_width, uint32_t tile_height) const
{
  return tile_width * tile_height * m_bytes_per_pixel;
}


std::vector<uint8_t> unc_encoder_rgb_block_pixel_interleave::encode_tile(const std::shared_ptr<const HeifPixelImage>& src_image) const
{
  std::vector<uint8_t> data;

  uint8_t bpp = src_image->get_bits_per_pixel(heif_channel_interleaved);

  size_t src_stride;
  const auto* src_data = reinterpret_cast<const uint16_t*>(src_image->get_plane(heif_channel_interleaved, &src_stride));
  src_stride /= 2;

  uint64_t out_size = static_cast<uint64_t>(src_image->get_height()) * src_image->get_width() * m_bytes_per_pixel;
  data.resize(out_size);

  uint8_t* p = data.data();

  for (uint32_t y = 0; y < src_image->get_height(); y++) {
    for (uint32_t x = 0; x < src_image->get_width(); x++) {
      uint16_t r = src_data[src_stride * y + 3 * x + 0];
      uint16_t g = src_data[src_stride * y + 3 * x + 1];
      uint16_t b = src_data[src_stride * y + 3 * x + 2];

      uint64_t combined_pixel = (r << (2 * bpp)) | (g << bpp) | b;

      *p++ = static_cast<uint8_t>(combined_pixel & 0xFF);
      *p++ = static_cast<uint8_t>((combined_pixel >> 8) & 0xFF);
      *p++ = static_cast<uint8_t>((combined_pixel >> 16) & 0xFF);
      *p++ = static_cast<uint8_t>((combined_pixel >> 24) & 0xFF);

      if (m_bytes_per_pixel > 4) {
        *p++ = static_cast<uint8_t>((combined_pixel >> 32) & 0xFF);
      }
    }
  }

  return data;
}
