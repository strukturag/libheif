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

#include <stddef.h>
#include <stdint.h>

#include "heif-version.h"

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

/* === version numbers === */

// Version string of linked libde265 library.
LIBHEIF_API const char *heif_get_version(void);
// Numeric version of linked libde265 library, encoded as 0xHHMMLL00 = HH.MM.LL.
LIBHEIF_API uint32_t heif_get_version_number(void);

// Numeric part "HH" from above.
LIBHEIF_API int heif_get_version_number_major(void);
// Numeric part "MM" from above.
LIBHEIF_API int heif_get_version_number_minor(void);
// Numeric part "LL" from above.
LIBHEIF_API int heif_get_version_number_maintenance(void);

// Helper macros to check for given versions of libde265 at compile time.
#define LIBHEIF_ENCODED_VERSION(h, m, l) ((h) << 24 | (m) << 16 | (l) << 8)
#define LIBHEIF_CHECK_VERSION(h, m, l) (LIBHEIF_NUMERIC_VERSION >= LIBHEIF_ENCODED_VERSION(h, m, l))

struct heif_context;
struct heif_image_handle;
struct heif_image;


enum heif_error_code {
  // Everything ok, no error occurred.
  heif_error_Ok = 0,

  // Input file does not exist.
  heif_error_Input_does_not_exist = 1,

  // Error in input file. Corrupted or invalid content.
  heif_error_Invalid_input = 2,

  // Input file type is not supported.
  heif_error_Unsupported_filetype = 3,

  // Image requires an unsupported decoder feature.
  heif_error_Unsupported_feature = 4,

  // Library API has been used in an invalid way.
  heif_error_Usage_error = 5,

  // Could not allocate enough memory.
  heif_error_Memory_allocation_error = 6,

  // The decoder plugin generated an error
  heif_error_Decoder_plugin_error = 7
};


enum heif_suberror_code {
  // no further information available
  heif_suberror_Unspecified = 0,

  // --- Invalid_input ---

  // End of data reached unexpectedly.
  heif_suberror_End_of_data = 100,

  // Size of box (defined in header) is wrong
  heif_suberror_Invalid_box_size = 101,

  // Mandatory 'ftyp' box is missing
  heif_suberror_No_ftyp_box = 102,

  heif_suberror_No_idat_box = 103,

  heif_suberror_No_meta_box = 104,

  heif_suberror_No_hdlr_box = 105,

  heif_suberror_No_hvcC_box = 106,

  heif_suberror_No_pitm_box = 107,

  heif_suberror_No_ipco_box = 108,

  heif_suberror_No_ipma_box = 109,

  heif_suberror_No_iloc_box = 110,

  heif_suberror_No_iinf_box = 111,

  heif_suberror_No_iprp_box = 112,

  heif_suberror_No_iref_box = 113,

  heif_suberror_No_pict_handler = 114,

  // An item property referenced in the 'ipma' box is not existing in the 'ipco' container.
  heif_suberror_Ipma_box_references_nonexisting_property = 115,

  // No properties have been assigned to an item.
  heif_suberror_No_properties_assigned_to_item = 116,

  // Image has no (compressed) data
  heif_suberror_No_item_data = 117,

  // Invalid specification of image grid (tiled image)
  heif_suberror_Invalid_grid_data = 118,

  // Tile-images in a grid image are missing
  heif_suberror_Missing_grid_images = 119,

  heif_suberror_Invalid_clean_aperture = 120,

  // Invalid specification of overlay image
  heif_suberror_Invalid_overlay_data = 121,

  // Overlay image completely outside of visible canvas area
  heif_suberror_Overlay_image_outside_of_canvas = 122,

  heif_suberror_Auxiliary_image_type_unspecified = 123,

  heif_suberror_No_or_invalid_primary_image = 124,

  heif_suberror_No_infe_box = 125,


  // --- Memory_allocation_error ---

  // A security limit preventing unreasonable memory allocations was exceeded by the input file.
  // Please check whether the file is valid. If it is, contact us so that we could increase the
  // security limits further.
  heif_suberror_Security_limit_exceeded = 1000,


  // --- Usage_error ---

  // An image ID was used that is not present in the file.
  heif_suberror_Nonexisting_image_referenced = 2000, // also used for Invalid_input

  // An API argument was given a NULL pointer, which is not allowed for that function.
  heif_suberror_Null_pointer_argument = 2001,

  // Image channel referenced that does not exist in the image
  heif_suberror_Nonexisting_image_channel_referenced = 2002,

  // The version of the passed plugin is not supported.
  heif_suberror_Unsupported_plugin_version = 2003,

  heif_suberror_Index_out_of_range = 2004,


  // --- Unsupported_feature ---

  // Image was coded with an unsupported compression method.
  heif_suberror_Unsupported_codec = 3000,

  // Image is specified in an unknown way, e.g. as tiled grid image (which is supported)
  heif_suberror_Unsupported_image_type = 3001,

  heif_suberror_Unsupported_data_version = 3002,

  // The conversion of the source image to the requested chroma / colorspace is not supported.
  heif_suberror_Unsupported_color_conversion = 3003
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

typedef uint32_t heif_image_id;

// ========================= heif_context =========================

// Allocate a new context for reading HEIF files.
// Has to be freed again with heif_context_free().
LIBHEIF_API
struct heif_context* heif_context_alloc(void);

// Free a previously allocated HEIF context. You should not free a context twice.
LIBHEIF_API
void heif_context_free(struct heif_context*);

// Read a HEIF file from a named disk file.
LIBHEIF_API
struct heif_error heif_context_read_from_file(struct heif_context*, const char* filename);

// Read a HEIF file stored completely in memory.
LIBHEIF_API
struct heif_error heif_context_read_from_memory(struct heif_context*,
                                                const void* mem, size_t size);

// Print information about the boxes of a HEIF file to stdout.
LIBHEIF_API
void heif_context_debug_dump_boxes(struct heif_context* ctx);

// Number of top-level image in the HEIF file. This does not include the thumbnails or the
// tile images that are composed to an image grid. You can get access to the thumbnails via
// the main image handle.
LIBHEIF_API
int heif_context_get_number_of_top_level_images(struct heif_context* ctx);

LIBHEIF_API
int heif_context_is_top_level_image_ID(struct heif_context* ctx, heif_image_id id);

// Fills in image IDs into the user-supplied int-array 'ID_array', preallocated with 'size' entries.
// Function returns the total number of IDs filled into the array.
LIBHEIF_API
int heif_context_get_list_of_top_level_image_IDs(struct heif_context* ctx, heif_image_id* ID_array, int size);

// Get the handle for a specific top-level image.
LIBHEIF_API
struct heif_error heif_context_get_image_handle(struct heif_context* ctx,
                                                int idx,
                                                struct heif_image_handle**);


LIBHEIF_API
struct heif_error heif_context_get_primary_image_ID(struct heif_context* ctx, heif_image_id* id);

// Get a handle to the primary image of the HEIF file.
// This is the image that should be displayed primarily when there are several images in the file.
LIBHEIF_API
struct heif_error heif_context_get_primary_image_handle(struct heif_context* ctx,
                                                        struct heif_image_handle**);

// Get the handle for a specific top-level image from an image ID.
LIBHEIF_API
struct heif_error heif_context_get_image_handle_for_ID(struct heif_context* ctx,
                                                       heif_image_id id,
                                                       struct heif_image_handle**);


// ========================= heif_image_handle =========================

// An heif_image_handle is a handle to a logical image in the HEIF file.
// To get the actual pixel data, you have to decode the handle to an heif_image.


// Release image handle.
LIBHEIF_API
void heif_image_handle_release(const struct heif_image_handle*);

// Check whether the given image_handle is the primary image of the file.
LIBHEIF_API
int heif_image_handle_is_primary_image(const struct heif_image_handle* handle);

// Get the resolution of an image.
LIBHEIF_API
void heif_image_handle_get_resolution(const struct heif_image_handle* handle,
                                      int* width, int* height);

LIBHEIF_API
int heif_image_handle_has_alpha_channel(const struct heif_image_handle*);


// List the number of thumbnails assigned to this image handle. Usually 0 or 1.
LIBHEIF_API
int heif_image_handle_get_number_of_thumbnails(const struct heif_image_handle* handle);

// Get the image handle of a thumbnail image.
LIBHEIF_API
struct heif_error heif_image_handle_get_thumbnail(const struct heif_image_handle* main_image_handle,
                                                  int thumbnail_idx,
                                                  struct heif_image_handle** out_thumbnail_handle);


LIBHEIF_API
int heif_image_handle_get_number_of_metadata_blocks(const struct heif_image_handle* handle);

// Get the size of type of the raw metadata, as stored in the HEIF file.
LIBHEIF_API
size_t heif_image_handle_get_metadata_size(const struct heif_image_handle* handle,
                                           int metadata_index);

// Return a string indicating the type of the metadata, as specified in the HEIF file.
// Exif data will have the type string "Exif".
// This string will be valid until the next call to a libheif function.
// You do not have to free this string.
LIBHEIF_API
const char* heif_image_handle_get_metadata_type(const struct heif_image_handle* handle,
                                                int metadata_index);

// out_data must point to a memory area of the size reported by heif_image_handle_get_metadata_size().
LIBHEIF_API
struct heif_error heif_image_handle_get_metadata(const struct heif_image_handle* handle,
                                                 int metadata_index,
                                                 void* out_data);


// ========================= heif_image =========================

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
  heif_chroma_monochrome=0,
  heif_chroma_420=1,
  heif_chroma_422=2,
  heif_chroma_444=3,
  heif_chroma_interleaved_24bit=10,
  heif_chroma_interleaved_32bit=11
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


struct heif_decoding_options
{
  uint8_t ignore_transformations;
};


LIBHEIF_API
heif_decoding_options* heif_decoding_options_alloc();

LIBHEIF_API
void heif_decoding_options_free(heif_decoding_options*);

// If colorspace or chroma is set up heif_colorspace_undefined or heif_chroma_undefined,
// respectively, the original colorspace is taken.
// Decoding options may be NULL.
LIBHEIF_API
struct heif_error heif_decode_image(const struct heif_image_handle* in_handle,
                                    struct heif_image** out_img,
                                    enum heif_colorspace colorspace,
                                    enum heif_chroma chroma,
                                    const struct heif_decoding_options* options);

// Get the colorspace format of the image.
LIBHEIF_API
enum heif_colorspace heif_image_get_colorspace(const struct heif_image*);

// Get the chroma format of the image.
LIBHEIF_API
enum heif_chroma heif_image_get_chroma_format(const struct heif_image*);

// TODO
//LIBHEIF_API
//enum heif_compression_format heif_image_get_compression_format(struct heif_image*);

// Get width of the given image channel in pixels. Returns -1 if a non-existing
// channel was given.
LIBHEIF_API
int heif_image_get_width(const struct heif_image*,enum heif_channel channel);

// Get height of the given image channel in pixels. Returns -1 if a non-existing
// channel was given.
LIBHEIF_API
int heif_image_get_height(const struct heif_image*,enum heif_channel channel);

// Get the number of bits per pixel in the given image channel. Returns -1 if
// a non-existing channel was given.
// Note that the number of bits per pixel may be different for each color channel.
LIBHEIF_API
int heif_image_get_bits_per_pixel(const struct heif_image*,enum heif_channel channel);

// The 'out_stride' is returned as "bytes per line".
// When out_stride is NULL, no value will be written.
// Returns NULL if a non-existing channel was given.
LIBHEIF_API
const uint8_t* heif_image_get_plane_readonly(const struct heif_image*,
                                             enum heif_channel channel,
                                             int* out_stride);

LIBHEIF_API
uint8_t* heif_image_get_plane(struct heif_image*,
                              enum heif_channel channel,
                              int* out_stride);


struct heif_scaling_options;

// Currently, heif_scaling_options is not defined yet. Pass a NULL pointer.
LIBHEIF_API
struct heif_error heif_image_scale_image(const struct heif_image* input,
                                         struct heif_image** output,
                                         int width, int height,
                                         const struct heif_scaling_options* options);

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
struct heif_error heif_image_create(int width, int height,
                                    enum heif_colorspace colorspace,
                                    enum heif_chroma chroma,
                                    struct heif_image** image);

LIBHEIF_API
struct heif_error heif_image_add_plane(struct heif_image* image,
                                       enum heif_channel channel,
                                       int width, int height, int bit_depth);



struct heif_decoder_plugin
{
  // API version supported by this plugin
  int plugin_api_version;


  // --- version 1 functions ---

  // Human-readable name of the plugin
  const char* (*get_plugin_name)();

  // Query whether the plugin supports decoding of the given format
  // Result is a priority value. The plugin with the largest value wins.
  // Default priority is 100.
  int (*does_support_format)(uint32_t format);

  // Create a new decoder context for decoding an image
  struct heif_error (*new_decoder)(void** decoder);

  // Free the decoder context (heif_image can still be used after destruction)
  void (*free_decoder)(void* decoder);

  // Push more data into the decoder. This can be called multiple times.
  // This may not be called after any decode_*() function has been called.
  struct heif_error (*push_data)(void* decoder, const void* data, size_t size);


  // --- After pushing the data into the decoder, the decode functions may be called only once.

  // Decode data into a full image. All data has to be pushed into the decoder before calling this.
  struct heif_error (*decode_image)(void* decoder, struct heif_image** out_img);


  // --- version 2 functions will follow below ... ---



  // Reset decoder, such that we can feed in new data for another image.
  // void (*reset_image)(void* decoder);
};



LIBHEIF_API
struct heif_error heif_register_decoder(struct heif_context* heif, const struct heif_decoder_plugin*);

// TODO void heif_register_encoder(heif_file* heif, uint32_t type, const heif_encoder_plugin*);

#ifdef __cplusplus
}
#endif

#endif
