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

#include "heif_avif_utils.h"


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
