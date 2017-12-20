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

#ifndef LIBHEIF_HEIF_H
#define LIBHEIF_HEIF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct heif;
struct heif_image;

heif* heif_read_from_file(const char* filename);

heif* heif_read_from_memory(const uint8_t* mem, uint64_t size);

heif* heif_read_from_file_descriptor(int fd);

void heif_free(heif*);

heif_image* heif_get_primary_image(heif* h);

int heif_get_number_of_images(heif* h);

heif_image* heif_get_image(heif* h, int image_index);


// --- heif_image

enum heif_compression_format {
  heif_compression_HEVC,
  heif_compression_AVC,
  heif_compression_JPEG
};


enum heif_compression_format heif_image_get_compression_format(heif_image*)

int  heif_image_get_number_of_data_chunks(heif_image* img);

void heif_image_get_data_chunk(heif_image* img, int chunk_index,
                               uint8_t const*const* dataptr,
                               int const* data_size);

void heif_image_free_data_chunk(heif_image* img, int chunk_index);

void heif_image_free(heif_image* img);


struct de265_image* heif_decode_hevc_image(heif_image* img);


#ifdef __cplusplus
}
#endif

#endif
