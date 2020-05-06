/*
 * HEIF codec.
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
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

#include <assert.h>
#include <math.h>

#include "heif_image.h"
#include "heif_avif.h"
#include "bitstream.h"

using namespace heif;

// https://aomediacodec.github.io/av1-spec/av1-spec.pdf


Error heif::fill_av1C_configuration(Box_av1C::configuration* inout_config, std::shared_ptr<HeifPixelImage> image)
{
  int bpp = image->get_bits_per_pixel(heif_channel_Y);
  heif_chroma chroma = image->get_chroma_format();

  uint8_t profile;
  
  if (bpp <= 10 &&
      (chroma == heif_chroma_420 ||
       chroma == heif_chroma_monochrome)) {
    profile = 0;
  }
  else if (bpp <= 10 &&
	   chroma == heif_chroma_444) {
    profile = 1;
  }
  else {
    profile = 2;
  }

  int width = image->get_width(heif_channel_Y);
  int height = image->get_height(heif_channel_Y);

  uint8_t level;

  if (width<=8192 && height <= 4352 && (width*height) <= 8912896) {
    level = 13; // 5.1
  }
  else if (width<=16384 && height <= 8704 && (width*height) <= 35651584) {
    level = 17; // 6.1
  }
  else {
    level = 31; // maximum
  }

  inout_config->seq_profile = profile;
  inout_config->seq_level_idx_0 = level;
  inout_config->high_bitdepth = (bpp>8) ? 1 : 0;
  inout_config->twelve_bit = (bpp>=12) ? 1 : 0;
  inout_config->monochrome = (chroma == heif_chroma_monochrome) ? 1 : 0;
  inout_config->chroma_subsampling_x = uint8_t(chroma_h_subsampling(chroma) >> 1);
  inout_config->chroma_subsampling_y = uint8_t(chroma_v_subsampling(chroma) >> 1);

  // 0 - CSP_UNKNOWN
  // 1 - CSP_VERTICAL
  // 2 - CSP_COLOCATED
  // 3 - CSP_RESERVED

  inout_config->chroma_sample_position = 0;

  
  return Error::Ok;
}
