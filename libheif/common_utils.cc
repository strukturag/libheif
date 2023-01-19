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

#include "common_utils.h"
#include <cassert>


uint8_t chroma_h_subsampling(heif_chroma c)
{
  switch (c) {
    case heif_chroma_monochrome:
    case heif_chroma_444:
      return 1;

    case heif_chroma_420:
    case heif_chroma_422:
      return 2;

    case heif_chroma_interleaved_RGB:
    case heif_chroma_interleaved_RGBA:
    default:
      assert(false);
      return 0;
  }
}


uint8_t chroma_v_subsampling(heif_chroma c)
{
  switch (c) {
    case heif_chroma_monochrome:
    case heif_chroma_444:
    case heif_chroma_422:
      return 1;

    case heif_chroma_420:
      return 2;

    case heif_chroma_interleaved_RGB:
    case heif_chroma_interleaved_RGBA:
    default:
      assert(false);
      return 0;
  }
}


void get_subsampled_size(int width, int height,
                               heif_channel channel,
                               heif_chroma chroma,
                               int* subsampled_width, int* subsampled_height)
{
  if (channel == heif_channel_Cb ||
      channel == heif_channel_Cr) {
    uint8_t chromaSubH = chroma_h_subsampling(chroma);
    uint8_t chromaSubV = chroma_v_subsampling(chroma);

    // NOLINTNEXTLINE(clang-analyzer-core.DivideZero)
    *subsampled_width = (width + chromaSubH - 1) / chromaSubH;
    // NOLINTNEXTLINE(clang-analyzer-core.DivideZero)
    *subsampled_height = (height + chromaSubV - 1) / chromaSubV;
  }
  else {
    *subsampled_width = width;
    *subsampled_height = height;
  }
}



uint8_t compute_avif_profile(int bits_per_pixel, heif_chroma chroma)
{
  if (bits_per_pixel <= 10 &&
      (chroma == heif_chroma_420 ||
       chroma == heif_chroma_monochrome)) {
    return 0;
  }
  else if (bits_per_pixel <= 10 &&
           chroma == heif_chroma_444) {
    return 1;
  }
  else {
    return 2;
  }
}
