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

#ifndef LIBHEIF_COMMON_UTILS_H
#define LIBHEIF_COMMON_UTILS_H

#include <cinttypes>
#include <libheif/heif.h>

// Functions for common use in libheif and the plugins.

uint8_t chroma_h_subsampling(heif_chroma c);

uint8_t chroma_v_subsampling(heif_chroma c);

void get_subsampled_size(int width, int height,
                         heif_channel channel,
                         heif_chroma chroma,
                         int* subsampled_width, int* subsampled_height);

uint8_t compute_avif_profile(int bits_per_pixel, heif_chroma chroma);


#endif //LIBHEIF_COMMON_UTILS_H
