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

struct heif_context;
struct heif_image;

heif_context* heif_read_from_file(const char* filename);

heif_context* heif_read_from_memory(const uint8_t* mem, uint64_t size);

heif_context* heif_read_from_file_descriptor(int fd);

void heif_free(heif_context*);

heif_image* heif_get_primary_image(heif_context* h);

int heif_get_number_of_images(heif_context* h);

heif_image* heif_get_image(heif_context* h, int image_index);


// --- heif_image

enum heif_compression_format {
  heif_compression_undefined = 0,
  heif_compression_HEVC = 1,
  heif_compression_AVC = 2,
  heif_compression_JPEG = 3
};


enum heif_chroma {
  heif_chroma_undefined=99,
  heif_chroma_mono=0,
  heif_chroma_420=1,
  heif_chroma_422=2,
  heif_chroma_444=3
};

enum heif_colorspace {
  heif_colorspace_undefined=99,
  heif_colorspace_YCbCr=0,
  heif_colorspace_GBR  =1
};

enum heif_channel {
  heif_channel_Y = 0,
  heif_channel_Cb = 1,
  heif_channel_Cr = 2,
  heif_channel_R = 3,
  heif_channel_G = 4,
  heif_channel_B = 5,
  heif_channel_Alpha = 6
};


int heif_image_get_width(const struct heif_image*,enum heif_channel channel);

int heif_image_get_height(const struct heif_image*,enum heif_channel channel);

enum heif_chroma heif_image_get_chroma_format(const struct heif_image*);

int heif_image_get_bits_per_pixel(const struct heif_image*,enum heif_channel channel);

/* The |out_stride| is returned as "bytes per line".
   When out_stride is NULL, no value will be written. */
const uint8_t* heif_image_get_plane(const struct heif_image*,
                                    enum heif_channel channel,
                                    int* out_stride);

void heif_image_release(const struct heif_image*);



enum heif_compression_format heif_image_get_compression_format(heif_image*);


/*
int  heif_image_get_number_of_data_chunks(heif_image* img);

void heif_image_get_data_chunk(heif_image* img, int chunk_index,
                               uint8_t const*const* dataptr,
                               int const* data_size);

void heif_image_free_data_chunk(heif_image* img, int chunk_index);
*/


//struct de265_image* heif_decode_hevc_image(heif_image* img);

#ifdef __cplusplus
}
#endif

#endif
