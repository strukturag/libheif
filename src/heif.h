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

struct heif_context;
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

    // Size of box (defined in header) is wrong
    heif_suberror_Invalid_box_size,

    // Mandatory 'ftyp' box is missing
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

    // An item property referenced in the 'ipma' box is not existing in the 'ipco' container.
    heif_suberror_Ipma_box_references_nonexisting_property,

    // No properties have been assigned to an item.
    heif_suberror_No_properties_assigned_to_item,

    // Image has no (compressed) data
    heif_suberror_No_item_data,

    // Invalid specification of image grid (tiled image)
    heif_suberror_Invalid_grid_data,

    // Tile-images in a grid image are missing
    heif_suberror_Missing_grid_images,


    // --- Memory_allocation_error ---

    // A security limit preventing unreasonable memory allocations was exceeded by the input file.
    // Please check whether the file is valid. If it is, contact us so that we could increase the
    // security limits further.
    heif_suberror_Security_limit_exceeded,


    // --- Usage_error ---

    // An image ID was used that is not present in the file.
    heif_suberror_Nonexisting_image_referenced,


    // --- Unsupported_feature ---

    // Image was coded with an unsupported compression method.
    heif_suberror_Unsupported_codec,

    // Image is specified in an unknown way, e.g. as tiled grid image (which is supported)
    heif_suberror_Unsupported_image_type
  };



struct heif_error
{
  // main error category
  enum heif_error_code code;

  // more detailed error code
  enum heif_suberror_code subcode;

  // textual error message (is always defined, you do not have to check for NULL)
  const char* message;
};


// Allocate a new context for reading HEIF files.
// Has to be freed again bei heif_context_free().
LIBHEIF_API
struct heif_context* heif_context_alloc();

// Free a previously allocated HEIF context. You should not free a context twice.
LIBHEIF_API
void heif_context_free(struct heif_context*);

// Read a HEIF file from a file.
LIBHEIF_API
struct heif_error heif_context_read_from_file(struct heif_context*, const char* filename);

// Read a HEIF file stored completely in memory.
LIBHEIF_API
struct heif_error heif_context_read_from_memory(struct heif_context*,
                                                const uint8_t* mem, uint64_t size);

// TODO
LIBHEIF_API
struct heif_error heif_context_read_from_file_descriptor(struct heif_context*, int fd);


// --- heif_image_handle

// An heif_image_handle is a handle to a logical image in the HEIF file.
// To get the actual pixel data, you have to decode the handle to an heif_image.

// Get a handle to the primary image of the HEIF file.
// This is the image that should be displayed primarily when there are several images in the file.
LIBHEIF_API
struct heif_error heif_context_get_primary_image_handle(struct heif_context* h,
                                                        struct heif_image_handle**);

// Number of top-level image in the HEIF file. This does not include the thumbnails or the
// tile images that are composed to an image grid. You can get access to the thumbnails via
// the main image handle.
LIBHEIF_API
int heif_context_get_number_of_images(struct heif_context* h);

// Get the handle for a specific top-level image.
LIBHEIF_API
struct heif_error heif_context_get_image_handle(struct heif_context* h,
                                                int image_index,
                                                struct heif_image_handle**);

// Release image handle.
LIBHEIF_API
void heif_image_handle_release(const struct heif_image_handle*);

// Check whether the given image_handle is the primary image of the file.
LIBHEIF_API
int heif_image_handle_is_primary_image(const struct heif_context* h,
                                       const struct heif_image_handle* handle);

// List the number of thumbnails assigned to this image handle. Usually 0 or 1.
LIBHEIF_API
int heif_image_handle_get_number_of_thumbnails(const struct heif_context* h,
                                               const struct heif_image_handle* handle);

// Get the image handle of a thumbnail image.
LIBHEIF_API
void heif_image_handle_get_thumbnail(const struct heif_context* h,
                                     const struct heif_image_handle* main_image_handle,
                                     int thumbnail_idx,
                                     struct heif_image_handle** out_thumbnail_handle);

// Get the resolution of an image.
LIBHEIF_API
void heif_image_handle_get_resolution(const struct heif_context* h,
                                      const struct heif_image_handle* handle,
                                      int* width, int* height);

// TODO
LIBHEIF_API
int  heif_image_handle_get_exif_data_size(const struct heif_context* h,
                                          const struct heif_image_handle* handle);

// TODO
// out_data must point to a memory area of size heif_image_handle_get_exif_data_size().
LIBHEIF_API
void heif_image_handle_get_exif_data(const struct heif_context* h,
                                     const struct heif_image_handle* handle,
                                     uint8_t* out_data);


// --- heif_image

// Note: when converting images to colorspace_RGB/chroma_interleaved_24bit, the resulting
// image contains only a single channel of type channel_interleaved with 3 bytes per pixel,
// containing the interleaved R,G,B values.

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


// --- heif_image ---

// If colorspace or chroma is set up heif_colorspace_undefined or heif_chroma_undefined,
// respectively, the original colorspace is taken.
LIBHEIF_API
struct heif_error heif_decode_image(struct heif_context* ctx,
                                    const struct heif_image_handle* in_handle,
                                    struct heif_image** out_img,
                                    enum heif_colorspace colorspace,
                                    enum heif_chroma chroma);

// Get the colorspace format of the image.
LIBHEIF_API
enum heif_colorspace heif_image_get_colorspace(const struct heif_image*);

// Get the chroma format of the image.
LIBHEIF_API
enum heif_chroma heif_image_get_chroma_format(const struct heif_image*);

// TODO
LIBHEIF_API
enum heif_compression_format heif_image_get_compression_format(struct heif_image*);

// Get width of the given image channel in pixels.
LIBHEIF_API
int heif_image_get_width(const struct heif_image*,enum heif_channel channel);

// Get height of the given image channel in pixels.
LIBHEIF_API
int heif_image_get_height(const struct heif_image*,enum heif_channel channel);

// Get the number of bits per pixel in the given image channel.
// Note that the number of bits per pixel may be different for each color channel.
LIBHEIF_API
int heif_image_get_bits_per_pixel(const struct heif_image*,enum heif_channel channel);

/* The 'out_stride' is returned as "bytes per line".
   When out_stride is NULL, no value will be written. */
LIBHEIF_API
const uint8_t* heif_image_get_plane_readonly(const struct heif_image*,
                                             enum heif_channel channel,
                                             int* out_stride);

LIBHEIF_API
uint8_t* heif_image_get_plane(struct heif_image*,
                              enum heif_channel channel,
                              int* out_stride);

// Release heif_image.
LIBHEIF_API
void heif_image_release(const struct heif_image*);




// ====================================================================================================
//  Decoder plugin API
//  In order to code images in other formats than HEVC, additional compression codecs can be
//  added as plugins. A plugin has to implement the functions specified in heif_decoder_plugin
//  and the plugin has to be registered to the libheif library using heif_register_decoder().


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
