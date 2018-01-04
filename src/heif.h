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

#if defined(_MSC_VER) && !defined(LIBHEIF_STATIC_BUILD)
  #ifdef LIBHEIF_EXPORTS
  #define LIBHEIF_API __declspec(dllexport)
  #else
  #define LIBHEIF_API __declspec(dllimport)
  #endif
#elif HAVE_VISIBILITY
  #ifdef LIBHEIF_EXPORTS
  #define LIBHEIF_API __attribute__((__visibility__("default")))
  #else
  #define LIBHEIF_API
  #endif
#else
  #define LIBHEIF_API
#endif

struct heif_context;  // TODO  heif_context == HeifFile, which is not so nice
struct heif_image_handle;
struct heif_image;


enum heif_error_code {
  // Everything ok, no error occurred.
  heif_error_Ok = 0,

  // Error in input file. Corrupted or invalid content.
  heif_error_Invalid_input = 1,

  // Input file type is not supported.
  heif_error_Unsupported_filetype = 2,

  // Image requires an unsupported decoder feature.
  heif_error_Unsupported_feature = 3,

  // Library API has been used in an invalid way.
  heif_error_Usage_error = 4,

  // Could not allocate enough memory.
  heif_error_Memory_allocation_error = 5,
};


  enum heif_suberror_code {
    // no further information available
    heif_suberror_Unspecified = 0,

    // --- Invalid_input ---

    // End of data reached unexpectedly.
    heif_suberror_End_of_data = 100,

    heif_suberror_Invalid_box_size,

    heif_suberror_Invalid_grid_data,

    heif_suberror_Missing_grid_images,

    heif_suberror_No_ftyp_box,

    heif_suberror_No_idat_box,

    heif_suberror_No_meta_box,

    heif_suberror_No_hdlr_box,

    heif_suberror_No_pitm_box,

    heif_suberror_No_ipco_box,

    heif_suberror_No_ipma_box,

    heif_suberror_No_iloc_box,

    heif_suberror_No_iinf_box,

    heif_suberror_No_iprp_box,

    heif_suberror_No_iref_box,

    heif_suberror_No_pict_handler,

    heif_suberror_Ipma_box_references_nonexisting_property,

    heif_suberror_No_properties_assigned_to_item,

    heif_suberror_No_item_data,


    // --- Memory_allocation_error ---

    heif_suberror_Security_limit_exceeded,


    // --- Usage_error ---

    heif_suberror_Nonexisting_image_referenced,


    // --- Unsupported_feature ---

    heif_suberror_Unsupported_codec,
    heif_suberror_Unsupported_image_type
  };



struct heif_error
{
  enum heif_error_code code;
  int subcode;
  const char* message;
};


LIBHEIF_API
struct heif_context* heif_context_alloc();

LIBHEIF_API
void heif_context_free(struct heif_context*);

LIBHEIF_API
struct heif_error heif_context_read_from_file(struct heif_context*, const char* filename);

LIBHEIF_API
struct heif_error heif_context_read_from_memory(struct heif_context*,
                                                const uint8_t* mem, uint64_t size);

// TODO
LIBHEIF_API
struct heif_error heif_context_read_from_file_descriptor(struct heif_context*, int fd);

LIBHEIF_API
struct heif_error heif_context_get_primary_image_handle(struct heif_context* h,
                                                        struct heif_image_handle**);


// --- heif_image_handle

LIBHEIF_API
int heif_context_get_number_of_images(struct heif_context* h);

LIBHEIF_API
struct heif_error heif_context_get_image_handle(struct heif_context* h,
                                                int image_index,
                                                struct heif_image_handle**);

LIBHEIF_API
int heif_image_handle_is_primary_image(const struct heif_context* h,
                                       const struct heif_image_handle* handle);

LIBHEIF_API
int heif_image_handle_get_number_of_thumbnails(const struct heif_context* h,
                                               const struct heif_image_handle* handle);

LIBHEIF_API
void heif_image_handle_get_thumbnail(const struct heif_context* h,
                                     const struct heif_image_handle* handle,
                                     int thumbnail_idx,
                                     struct heif_image_handle** out_thumbnail_handle);

LIBHEIF_API
void heif_image_handle_get_resolution(const struct heif_context* h,
                                      const struct heif_image_handle* handle,
                                      int* width, int* height);

LIBHEIF_API
int  heif_image_handle_get_exif_data_size(const struct heif_context* h,
                                          const struct heif_image_handle* handle);

// out_data must point to a memory area of size heif_image_handle_get_exif_data_size().
LIBHEIF_API
void heif_image_handle_get_exif_data(const struct heif_context* h,
                                     const struct heif_image_handle* handle,
                                     uint8_t* out_data);


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
  heif_chroma_444=3,
  heif_chroma_interleaved_24bit=10
};

enum heif_colorspace {
  heif_colorspace_undefined=99,
  heif_colorspace_YCbCr=0,
  heif_colorspace_RGB  =1,
  heif_colorspace_monochrome=2
};

enum heif_channel {
  heif_channel_Y = 0,
  heif_channel_Cb = 1,
  heif_channel_Cr = 2,
  heif_channel_R = 3,
  heif_channel_G = 4,
  heif_channel_B = 5,
  heif_channel_Alpha = 6,
  heif_channel_interleaved = 10
};


// If colorspace or chroma is set up heif_colorspace_undefined or heif_chroma_undefined,
// respectively, the original colorspace is taken.
LIBHEIF_API
struct heif_error heif_decode_image(struct heif_context* ctx,
                                    const struct heif_image_handle* in_handle,
                                    struct heif_image** out_img,
                                    enum heif_colorspace colorspace,
                                    enum heif_chroma chroma);

LIBHEIF_API
enum heif_compression_format heif_image_get_compression_format(struct heif_image*);

LIBHEIF_API
int heif_image_get_width(const struct heif_image*,enum heif_channel channel);

LIBHEIF_API
int heif_image_get_height(const struct heif_image*,enum heif_channel channel);

LIBHEIF_API
enum heif_chroma heif_image_get_chroma_format(const struct heif_image*);

LIBHEIF_API
int heif_image_get_bits_per_pixel(const struct heif_image*,enum heif_channel channel);

/* The |out_stride| is returned as "bytes per line".
   When out_stride is NULL, no value will be written. */
LIBHEIF_API
const uint8_t* heif_image_get_plane_readonly(const struct heif_image*,
                                             enum heif_channel channel,
                                             int* out_stride);

LIBHEIF_API
uint8_t* heif_image_get_plane(struct heif_image*,
                              enum heif_channel channel,
                              int* out_stride);

LIBHEIF_API
void heif_image_release(const struct heif_image*);

LIBHEIF_API
void heif_image_handle_release(const struct heif_image_handle*);




/*
int  heif_image_get_number_of_data_chunks(heif_image* img);

void heif_image_get_data_chunk(heif_image* img, int chunk_index,
                               uint8_t const*const* dataptr,
                               int const* data_size);

void heif_image_free_data_chunk(heif_image* img, int chunk_index);
*/





// --- heif_image allocation (you probably only need these functions if you are writing a plugin)

// Create a new image of the specified resolution and colorspace.
// Note: no memory for the actual image data is reserved yet. You have to use
// heif_image_add_plane() to add the image planes required by your colorspace/chroma.
LIBHEIF_API
struct heif_image* heif_image_create(int width, int height,
                                     enum heif_colorspace colorspace,
                                     enum heif_chroma chroma);

LIBHEIF_API
void heif_image_add_plane(struct heif_image* image,
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
  void (*decode_image)(void* decoder, struct heif_image** out_img);

  // Reset decoder, such that we can feed in new data for another image.
  // void (*reset_image)(void* decoder);
};



LIBHEIF_API
void heif_register_decoder(struct heif_context* heif, uint32_t type, const struct heif_decoder_plugin*);

// TODO void heif_register_encoder(heif_file* heif, uint32_t type, const heif_encoder_plugin*);

#ifdef __cplusplus
}
#endif

#endif
