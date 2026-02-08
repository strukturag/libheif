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

#include "unc_encoder_rrggbb.h"

#include <cstring>

#include "pixelimage.h"
#include "unc_boxes.h"
#include "unc_types.h"


bool unc_encoder_rrggbb::can_encode(const std::shared_ptr<const HeifPixelImage>& image,
                                    const heif_encoding_options& options) const
{
  if (image->get_colorspace() != heif_colorspace_RGB) {
    return false;
  }

  switch (image->get_chroma_format()) {
    case heif_chroma_interleaved_RRGGBB_LE:
    case heif_chroma_interleaved_RRGGBB_BE:
    case heif_chroma_interleaved_RRGGBBAA_LE:
    case heif_chroma_interleaved_RRGGBBAA_BE:
      break;
    default:
      return false;
  }

  return true;
}


void unc_encoder_rrggbb::fill_cmpd_and_uncC(std::shared_ptr<Box_cmpd>& cmpd,
                                            std::shared_ptr<Box_uncC>& uncC,
                                            const std::shared_ptr<const HeifPixelImage>& image,
                                            const heif_encoding_options& options) const
{
  cmpd->add_component({component_type_red});
  cmpd->add_component({component_type_green});
  cmpd->add_component({component_type_blue});

  bool save_alpha = image->has_alpha();

  if (save_alpha) {
    cmpd->add_component({component_type_alpha});
  }

  bool little_endian = (image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_LE ||
                        image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE);

  uint8_t bpp = image->get_bits_per_pixel(heif_channel_interleaved);

  uint8_t component_align_size = 2;
  if (bpp == 16) {
    component_align_size = 0;
  }

  uncC->set_interleave_type(interleave_mode_pixel);
  uncC->set_sampling_type(0);
  uncC->set_components_little_endian(little_endian);

  uncC->add_component({0, bpp, component_format_unsigned, component_align_size});
  uncC->add_component({1, bpp, component_format_unsigned, component_align_size});
  uncC->add_component({2, bpp, component_format_unsigned, component_align_size});
  if (save_alpha) {
    uncC->add_component({3, bpp, component_format_unsigned, component_align_size});
  }
}


std::vector<uint8_t> unc_encoder_rrggbb::encode_tile(const std::shared_ptr<const HeifPixelImage>& src_image,
                                                     const heif_encoding_options& options) const
{
  std::vector<uint8_t> data;

  bool save_alpha = src_image->has_alpha();

  int bytes_per_pixel = save_alpha ? 8 : 6;

  size_t src_stride;
  const uint8_t* src_data = src_image->get_plane(heif_channel_interleaved, &src_stride);

  uint64_t out_size = static_cast<uint64_t>(src_image->get_height()) * src_image->get_width() * bytes_per_pixel;
  data.resize(out_size);

  for (uint32_t y = 0; y < src_image->get_height(); y++) {
    memcpy(data.data() + y * src_image->get_width() * bytes_per_pixel, src_data + src_stride * y, src_image->get_width() * bytes_per_pixel);
  }

  return data;
}
