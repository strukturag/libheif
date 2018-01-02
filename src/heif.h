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

struct heif_context;  // TODO  heif_context == HeifFile, which is not so nice
struct heif_image;
struct heif_pixel_image;

struct heif_error
{
  int code;
  int subcode;
  const char* message;
};


struct heif_context* heif_context_alloc();

void heif_context_free(struct heif_context*);

struct heif_error heif_context_read_from_file(struct heif_context*, const char* filename);

struct heif_error heif_context_read_from_memory(struct heif_context*, const uint8_t* mem, uint64_t size);

// TODO
struct heif_error heif_context_read_from_file_descriptor(struct heif_context*, int fd);

// NOTE: data types will change ! (TODO)
struct heif_error heif_context_get_primary_image(struct heif_context* h, struct heif_pixel_image**);

int heif_context_get_number_of_images(struct heif_context* h);

// NOTE: data types will change ! (TODO)
struct heif_error heif_get_image(struct heif_context* h, int image_index, struct heif_pixel_image**);


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


int heif_pixel_image_get_width(const struct heif_pixel_image*,enum heif_channel channel);

int heif_pixel_image_get_height(const struct heif_pixel_image*,enum heif_channel channel);

enum heif_chroma heif_image_get_chroma_format(const struct heif_image*);

int heif_image_get_bits_per_pixel(const struct heif_image*,enum heif_channel channel);

/* The |out_stride| is returned as "bytes per line".
   When out_stride is NULL, no value will be written. */
const uint8_t* heif_pixel_image_get_plane_readonly(const struct heif_pixel_image*,
                                                   enum heif_channel channel,
                                                   int* out_stride);

uint8_t* heif_pixel_image_get_plane(struct heif_pixel_image*,
                                    enum heif_channel channel,
                                    int* out_stride);

void heif_image_release(const struct heif_image*);



enum heif_compression_format heif_image_get_compression_format(struct heif_image*);


/*
int  heif_image_get_number_of_data_chunks(heif_image* img);

void heif_image_get_data_chunk(heif_image* img, int chunk_index,
                               uint8_t const*const* dataptr,
                               int const* data_size);

void heif_image_free_data_chunk(heif_image* img, int chunk_index);
*/


//struct de265_image* heif_decode_hevc_image(heif_image* img);




struct heif_pixel_image* heif_pixel_image_create(int width, int height,
                                                 enum heif_colorspace colorspace,
                                                 enum heif_chroma chroma);

void heif_pixel_image_add_plane(struct heif_pixel_image* image,
                                enum heif_channel channel, int width, int height, int bit_depth);




struct heif_decoder_plugin
{
  // Create a new decoder context for decoding an image
  void* (*new_decoder)();

  // Free the decoder context (heif_image can still be used after destruction)
  void (*free_decoder)(void* decoder);

  // Push more data into the decoder. This can be called multiple times.
  // This may not be called after any decode_*() function has been called.
  void (*push_data)(void* decoder, uint8_t* data,uint32_t size);


  // --- After pushing the data into the decoder, exactly one of the decode functions may be called once.

  // Decode data into a full image. All data has to be pushed into the decoder before calling this.
  void (*decode_image)(void* decoder, struct heif_pixel_image** out_img);

  // Decode only part of the image.
  // May be useful if the input image is tiled and we only need part of it.
  /*
  heif_image* (*decode_partial)(void* decoder,
                                int x_left, int y_top,
                                int width, int height);
  */

  // Reset decoder, such that we can feed in new data for another image.
  // void (*reset_image)(void* decoder);
};


const struct heif_decoder_plugin* get_decoder_plugin_libde265();


void heif_register_decoder(struct heif_context* heif, uint32_t type, const struct heif_decoder_plugin*);

// TODO void heif_register_encoder(heif_file* heif, uint32_t type, const heif_encoder_plugin*);

#ifdef __cplusplus
}
#endif

#endif
