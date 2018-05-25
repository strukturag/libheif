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


// API versions table
//
// release    depth.rep   dec.options   heif_writer
// ------------------------------------------------
//  1.0          1             1           N/A
//  1.1          1             1            1



#if defined(_MSC_VER) && !defined(LIBHEIF_STATIC_BUILD)
  #ifdef LIBHEIF_EXPORTS
  #define LIBHEIF_API __declspec(dllexport)
  #else
  #define LIBHEIF_API __declspec(dllimport)
  #endif
#elif defined(HAVE_VISIBILITY) && HAVE_VISIBILITY
  #ifdef LIBHEIF_EXPORTS
  #define LIBHEIF_API __attribute__((__visibility__("default")))
  #else
  #define LIBHEIF_API
  #endif
#else
  #define LIBHEIF_API
#endif

/* === version numbers === */

// Version string of linked libheif library.
LIBHEIF_API const char *heif_get_version(void);
// Numeric version of linked libheif library, encoded as 0xHHMMLL00 = HH.MM.LL.
LIBHEIF_API uint32_t heif_get_version_number(void);

// Numeric part "HH" from above.
LIBHEIF_API int heif_get_version_number_major(void);
// Numeric part "MM" from above.
LIBHEIF_API int heif_get_version_number_minor(void);
// Numeric part "LL" from above.
LIBHEIF_API int heif_get_version_number_maintenance(void);

// Helper macros to check for given versions of libheif at compile time.
#define LIBHEIF_MAKE_VERSION(h, m, l) ((h) << 24 | (m) << 16 | (l) << 8)
#define LIBHEIF_HAVE_VERSION(h, m, l) (LIBHEIF_NUMERIC_VERSION >= LIBHEIF_MAKE_VERSION(h, m, l))

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
  heif_error_Decoder_plugin_error = 7,

  // The decoder plugin generated an error
  heif_error_Encoder_plugin_error = 8,

  // Error during encoding or when writing to the output
  heif_error_Encoding_error = 9
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

  heif_suberror_No_or_invalid_primary_item = 124,

  heif_suberror_No_infe_box = 125,


  // --- Memory_allocation_error ---

  // A security limit preventing unreasonable memory allocations was exceeded by the input file.
  // Please check whether the file is valid. If it is, contact us so that we could increase the
  // security limits further.
  heif_suberror_Security_limit_exceeded = 1000,


  // --- Usage_error ---

  // An item ID was used that is not present in the file.
  heif_suberror_Nonexisting_item_referenced = 2000, // also used for Invalid_input

  // An API argument was given a NULL pointer, which is not allowed for that function.
  heif_suberror_Null_pointer_argument = 2001,

  // Image channel referenced that does not exist in the image
  heif_suberror_Nonexisting_image_channel_referenced = 2002,

  // The version of the passed plugin is not supported.
  heif_suberror_Unsupported_plugin_version = 2003,

  // The version of the passed writer is not supported.
  heif_suberror_Unsupported_writer_version = 2004,

  // The given (encoder) parameter name does not exist.
  heif_suberror_Unsupported_parameter = 2005,

  // The value for the given parameter is not in the valid range.
  heif_suberror_Invalid_parameter_value = 2006,


  // --- Unsupported_feature ---

  // Image was coded with an unsupported compression method.
  heif_suberror_Unsupported_codec = 3000,

  // Image is specified in an unknown way, e.g. as tiled grid image (which is supported)
  heif_suberror_Unsupported_image_type = 3001,

  heif_suberror_Unsupported_data_version = 3002,

  // The conversion of the source image to the requested chroma / colorspace is not supported.
  heif_suberror_Unsupported_color_conversion = 3003,

  heif_suberror_Unsupported_item_construction_method = 3004,


  // --- Encoder_plugin_error ---


  // --- Encoding_error ---

  heif_suberror_Cannot_write_output_data = 5000,
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


typedef uint32_t heif_item_id;


// ========================= heif_context =========================
// A heif_context represents a HEIF file that has been read.
// In the future, you will also be able to add pictures to a heif_context
// and write it into a file again.


// Allocate a new context for reading HEIF files.
// Has to be freed again with heif_context_free().
LIBHEIF_API
struct heif_context* heif_context_alloc(void);

// Free a previously allocated HEIF context. You should not free a context twice.
LIBHEIF_API
void heif_context_free(struct heif_context*);


struct heif_reading_options;

enum heif_reader_offset {
  heif_seek_start,
  heif_seek_current,
  heif_seek_end
};

struct heif_reader {
  // API version supported by this reader
  int reader_api_version;

  // --- version 1 functions ---
  int64_t (*get_length)(struct heif_context* ctx, void* userdata);
  int64_t (*get_position)(struct heif_context* ctx, void* userdata);
  int (*read)(struct heif_context* ctx,
              void* data,
              size_t size,
              void* userdata);
  int (*seek)(struct heif_context* ctx,
              int64_t position,
              enum heif_reader_offset offset,
              void* userdata);
};

// The heif_reading_options should currently be set to NULL.
LIBHEIF_API
struct heif_error heif_context_read(struct heif_context* ctx,
                                    struct heif_reader* reader,
                                    const struct heif_reading_options* options,
                                    void* userdata);

// Read a HEIF file from a named disk file.
// The heif_reading_options should currently be set to NULL.
LIBHEIF_API
struct heif_error heif_context_read_from_file(struct heif_context*, const char* filename,
                                              const struct heif_reading_options*);

// Read a HEIF file stored completely in memory.
// The heif_reading_options should currently be set to NULL.
LIBHEIF_API
struct heif_error heif_context_read_from_memory(struct heif_context*,
                                                const void* mem, size_t size,
                                                const struct heif_reading_options*);

// Number of top-level images in the HEIF file. This does not include the thumbnails or the
// tile images that are composed to an image grid. You can get access to the thumbnails via
// the main image handle.
LIBHEIF_API
int heif_context_get_number_of_top_level_images(struct heif_context* ctx);

LIBHEIF_API
int heif_context_is_top_level_image_ID(struct heif_context* ctx, heif_item_id id);

// Fills in image IDs into the user-supplied int-array 'ID_array', preallocated with 'count' entries.
// Function returns the total number of IDs filled into the array.
LIBHEIF_API
int heif_context_get_list_of_top_level_image_IDs(struct heif_context* ctx,
                                                 heif_item_id* ID_array,
                                                 int count);

LIBHEIF_API
struct heif_error heif_context_get_primary_image_ID(struct heif_context* ctx, heif_item_id* id);

// Get a handle to the primary image of the HEIF file.
// This is the image that should be displayed primarily when there are several images in the file.
LIBHEIF_API
struct heif_error heif_context_get_primary_image_handle(struct heif_context* ctx,
                                                        struct heif_image_handle**);

// Get the handle for a specific top-level image from an image ID.
LIBHEIF_API
struct heif_error heif_context_get_image_handle(struct heif_context* ctx,
                                                heif_item_id id,
                                                struct heif_image_handle**);

// Print information about the boxes of a HEIF file to file descriptor.
// This is for debugging and informational purposes only. You should not rely on
// the output having a specific format. At best, you should not use this at all.
LIBHEIF_API
void heif_context_debug_dump_boxes_to_file(struct heif_context* ctx, int fd);


// ========================= heif_image_handle =========================

// An heif_image_handle is a handle to a logical image in the HEIF file.
// To get the actual pixel data, you have to decode the handle to an heif_image.
// An heif_image_handle also gives you access to the thumbnails and Exif data
// associated with an image.

// Once you obtained an heif_image_handle, you can already release the heif_context,
// since it is internally ref-counted.

// Release image handle.
LIBHEIF_API
void heif_image_handle_release(const struct heif_image_handle*);

// Check whether the given image_handle is the primary image of the file.
LIBHEIF_API
int heif_image_handle_is_primary_image(const struct heif_image_handle* handle);

// Get the resolution of an image.
LIBHEIF_API
int heif_image_handle_get_width(const struct heif_image_handle* handle);

LIBHEIF_API
int heif_image_handle_get_height(const struct heif_image_handle* handle);

LIBHEIF_API
int heif_image_handle_has_alpha_channel(const struct heif_image_handle*);


// ------------------------- depth images -------------------------

LIBHEIF_API
int heif_image_handle_has_depth_image(const struct heif_image_handle*);

LIBHEIF_API
int heif_image_handle_get_number_of_depth_images(const struct heif_image_handle* handle);

LIBHEIF_API
int heif_image_handle_get_list_of_depth_image_IDs(const struct heif_image_handle* handle,
                                                  heif_item_id* ids, int count);

LIBHEIF_API
struct heif_error heif_image_handle_get_depth_image_handle(const struct heif_image_handle* handle,
                                                           heif_item_id depth_image_id,
                                                           struct heif_image_handle** out_depth_handle);


enum heif_depth_representation_type {
  heif_depth_representation_type_uniform_inverse_Z = 0,
  heif_depth_representation_type_uniform_disparity = 1,
  heif_depth_representation_type_uniform_Z = 2,
  heif_depth_representation_type_nonuniform_disparity = 3
};

struct heif_depth_representation_info {
  uint8_t version;

  // version 1 fields

  uint8_t has_z_near;
  uint8_t has_z_far;
  uint8_t has_d_min;
  uint8_t has_d_max;

  double z_near;
  double z_far;
  double d_min;
  double d_max;

  enum heif_depth_representation_type depth_representation_type;
  uint32_t disparity_reference_view;

  uint32_t depth_nonlinear_representation_model_size;
  uint8_t* depth_nonlinear_representation_model;

  // version 2 fields below
};


LIBHEIF_API
void heif_depth_representation_info_free(const struct heif_depth_representation_info* info);

// Returns true when there is depth_representation_info available
LIBHEIF_API
int heif_image_handle_get_depth_image_representation_info(const struct heif_image_handle* handle,
                                                          heif_item_id depth_image_id,
                                                          const struct heif_depth_representation_info** out);



// ------------------------- thumbnails -------------------------

// List the number of thumbnails assigned to this image handle. Usually 0 or 1.
LIBHEIF_API
int heif_image_handle_get_number_of_thumbnails(const struct heif_image_handle* handle);

LIBHEIF_API
int heif_image_handle_get_list_of_thumbnail_IDs(const struct heif_image_handle* handle,
                                                heif_item_id* ids, int count);

// Get the image handle of a thumbnail image.
LIBHEIF_API
struct heif_error heif_image_handle_get_thumbnail(const struct heif_image_handle* main_image_handle,
                                                  heif_item_id thumbnail_id,
                                                  struct heif_image_handle** out_thumbnail_handle);


// ------------------------- metadata (Exif / XMP) -------------------------

// How many metadata blocks are attached to an image. Usually, the only metadata is
// an "Exif" block.
LIBHEIF_API
int heif_image_handle_get_number_of_metadata_blocks(const struct heif_image_handle* handle,
                                                    const char* type_filter);

// 'type_filter' can be used to get only metadata of specific types, like "Exif".
// If 'type_filter' is NULL, it will return all types of metadata IDs.
LIBHEIF_API
int heif_image_handle_get_list_of_metadata_block_IDs(const struct heif_image_handle* handle,
                                                     const char* type_filter,
                                                     heif_item_id* ids, int count);

// Return a string indicating the type of the metadata, as specified in the HEIF file.
// Exif data will have the type string "Exif".
// This string will be valid until the next call to a libheif function.
// You do not have to free this string.
LIBHEIF_API
const char* heif_image_handle_get_metadata_type(const struct heif_image_handle* handle,
                                                heif_item_id metadata_id);

// Get the size of the raw metadata, as stored in the HEIF file.
LIBHEIF_API
size_t heif_image_handle_get_metadata_size(const struct heif_image_handle* handle,
                                           heif_item_id metadata_id);

// 'out_data' must point to a memory area of the size reported by heif_image_handle_get_metadata_size().
// The data is returned exactly as stored in the HEIF file.
// For Exif data, you probably have to skip the first four bytes of the data, since they
// indicate the offset to the start of the TIFF header of the Exif data.
LIBHEIF_API
struct heif_error heif_image_handle_get_metadata(const struct heif_image_handle* handle,
                                                 heif_item_id metadata_id,
                                                 void* out_data);


// ========================= heif_image =========================

// An heif_image contains a decoded pixel image in various colorspaces, chroma formats,
// and bit depths.

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
  heif_chroma_interleaved_RGB =10,
  heif_chroma_interleaved_RGBA=11
};

// DEPRECTATED ENUM NAMES
#define heif_chroma_interleaved_24bit  heif_chroma_interleaved_RGB
#define heif_chroma_interleaved_32bit  heif_chroma_interleaved_RGBA


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


enum heif_progress_step {
  heif_progress_step_total = 0,
  heif_progress_step_load_tile = 1
};


struct heif_decoding_options
{
  uint8_t version;

  // version 1 options

  // Ignore geometric transformations like cropping, rotation, mirroring.
  // Default: false (do not ignore).
  uint8_t ignore_transformations;

  void (*start_progress)(enum heif_progress_step step, int max_progress, void* progress_user_data);
  void (*on_progress)(enum heif_progress_step step, int progress, void* progress_user_data);
  void (*end_progress)(enum heif_progress_step step, void* progress_user_data);
  void* progress_user_data;
};


// Allocate decoding options and fill with default values.
// Note: you should always get the decoding options through this function since the
// option structure may grow in size in future versions.
LIBHEIF_API
struct heif_decoding_options* heif_decoding_options_alloc();

LIBHEIF_API
void heif_decoding_options_free(struct heif_decoding_options*);

// Decode an heif_image_handle into the actual pixel image and also carry out
// all geometric transformations specified in the HEIF file (rotation, cropping, mirroring).
//
// If colorspace or chroma is set to heif_colorspace_undefined or heif_chroma_undefined,
// respectively, the original colorspace is taken.
// Decoding options may be NULL. If you want to supply options, always use
// heif_decoding_options_alloc() to get the structure.
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

// Get a pointer to the actual pixel data.
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
//  Encoding API

LIBHEIF_API
struct heif_error heif_context_write_to_file(struct heif_context*,
                                             const char* filename);

struct heif_writer {
  // API version supported by this writer
  int writer_api_version;

  // --- version 1 functions ---
  struct heif_error (*write)(struct heif_context* ctx, // TODO: why do we need this parameter?
                             const void* data,
                             size_t size,
                             void* userdata);
};

LIBHEIF_API
struct heif_error heif_context_write(struct heif_context*,
                                     struct heif_writer* writer,
                                     void* userdata);


// ----- encoder -----

// The encoder used for actually encoding an image.
struct heif_encoder;

// A description of the encoder's capabilities and name.
struct heif_encoder_descriptor;

// A configuration parameter of the encoder. Each encoder implementation may have a different
// set of parameters. For the most common settings (e.q. quality), special functions to set
// the parameters are provided.
struct heif_encoder_parameter;


// Get a list of available encoders. You can filter the encoders by compression format and name.
// Use format_filter==heif_compression_undefined and name_filter==NULL as wildcards.
// The returned list of encoders is sorted by their priority (which is a plugin property).
// Note: to get the actual encoder from the descriptors returned here, use heif_context_get_encoder().
LIBHEIF_API
int heif_context_get_encoder_descriptors(struct heif_context*, // TODO: why do we need this parameter?
                                         enum heif_compression_format format_filter,
                                         const char* name_filter,
                                         const struct heif_encoder_descriptor** out_encoders,
                                         int count);

// Return a long, descriptive name of the encoder (including version information).
LIBHEIF_API
const char* heif_encoder_descriptor_get_name(const struct heif_encoder_descriptor*);

// Return a short, symbolic name for identifying the encoder.
// This name should stay constant over different encoder versions.
LIBHEIF_API
const char* heif_encoder_descriptor_get_id_name(const struct heif_encoder_descriptor*);

LIBHEIF_API
enum heif_compression_format
heif_encoder_descriptor_get_compression_format(const struct heif_encoder_descriptor*);

LIBHEIF_API
int heif_encoder_descriptor_supportes_lossy_compression(const struct heif_encoder_descriptor*);

LIBHEIF_API
int heif_encoder_descriptor_supportes_lossless_compression(const struct heif_encoder_descriptor*);


// Get an encoder instance that can be used to actually encode images from a descriptor.
// TODO: why do we need the context here? I think we should remove this. You may pass a NULL context.
LIBHEIF_API
struct heif_error heif_context_get_encoder(struct heif_context* context,
                                           const struct heif_encoder_descriptor*,
                                           struct heif_encoder** out_encoder);

// Get an encoder for the given compression format. If there are several encoder plugins
// for this format, the encoder with the highest plugin priority will be returned.
// TODO: why do we need the context here? I think we should remove this. You may pass a NULL context.
LIBHEIF_API
struct heif_error heif_context_get_encoder_for_format(struct heif_context* context,
                                                      enum heif_compression_format format,
                                                      struct heif_encoder**);

// You have to release the encoder after use.
LIBHEIF_API
void heif_encoder_release(struct heif_encoder*);

// Get the encoder name from the encoder itself.
LIBHEIF_API
const char* heif_encoder_get_name(const struct heif_encoder*);


// --- Encoder Parameters ---

// Libheif supports settings parameters through specialized functions and through
// generic functions by parameter name. Sometimes, the same parameter can be set
// in both ways.
// We consider it best practice to use the generic parameter functions only in
// dynamically generated user interfaces, as no guarantees are made that some specific
// parameter names are supported by all plugins.


// Set a 'quality' factor (0-100). How this is mapped to actual encoding parameters is
// encoder dependent.
LIBHEIF_API
struct heif_error heif_encoder_set_lossy_quality(struct heif_encoder*, int quality);

LIBHEIF_API
struct heif_error heif_encoder_set_lossless(struct heif_encoder*, int enable);

// level should be between 0 (= none) to 4 (= full)
LIBHEIF_API
struct heif_error heif_encoder_set_logging_level(struct heif_encoder*, int level);

// Get a generic list of encoder parameters.
// Each encoder may define its own, additional set of parameters.
// You do not have to free the returned list.
LIBHEIF_API
const struct heif_encoder_parameter*const* heif_encoder_list_parameters(struct heif_encoder*);

// Return the parameter name.
LIBHEIF_API
const char* heif_encoder_parameter_get_name(const struct heif_encoder_parameter*);


enum heif_encoder_parameter_type {
  heif_encoder_parameter_type_integer = 1,
  heif_encoder_parameter_type_boolean = 2,
  heif_encoder_parameter_type_string = 3
};

// Return the parameter type.
LIBHEIF_API
enum heif_encoder_parameter_type heif_encoder_parameter_get_type(const struct heif_encoder_parameter*);

LIBHEIF_API
struct heif_error heif_encoder_parameter_get_valid_integer_range(const struct heif_encoder_parameter*,
                                                                 int* have_minimum_maximum,
                                                                 int* minimum, int* maximum);

LIBHEIF_API
struct heif_error heif_encoder_parameter_get_valid_string_values(const struct heif_encoder_parameter*,
                                                                 const char*const** out_stringarray);


LIBHEIF_API
struct heif_error heif_encoder_set_parameter_integer(struct heif_encoder*,
                                                     const char* parameter_name,
                                                     int value);

LIBHEIF_API
struct heif_error heif_encoder_get_parameter_integer(struct heif_encoder*,
                                                     const char* parameter_name,
                                                     int* value);

// TODO: name should be changed to heif_encoder_get_valid_integer_parameter_range
LIBHEIF_API // DEPRECATED.
struct heif_error heif_encoder_parameter_integer_valid_range(struct heif_encoder*,
                                                             const char* parameter_name,
                                                             int* have_minimum_maximum,
                                                             int* minimum, int* maximum);

LIBHEIF_API
struct heif_error heif_encoder_set_parameter_boolean(struct heif_encoder*,
                                                     const char* parameter_name,
                                                     int value);

LIBHEIF_API
struct heif_error heif_encoder_get_parameter_boolean(struct heif_encoder*,
                                                     const char* parameter_name,
                                                     int* value);

LIBHEIF_API
struct heif_error heif_encoder_set_parameter_string(struct heif_encoder*,
                                                    const char* parameter_name,
                                                    const char* value);

LIBHEIF_API
struct heif_error heif_encoder_get_parameter_string(struct heif_encoder*,
                                                    const char* parameter_name,
                                                    char* value, int value_size);

// returns a NULL-terminated list of valid strings or NULL if all values are allowed
LIBHEIF_API
struct heif_error heif_encoder_parameter_string_valid_values(struct heif_encoder*,
                                                             const char* parameter_name,
                                                             const char*const** out_stringarray);

// Set a parameter of any type to the string value.
// Integer values are parsed from the string.
// Boolean values can be "true"/"false"/"1"/"0"
LIBHEIF_API
struct heif_error heif_encoder_set_parameter(struct heif_encoder*,
                                             const char* parameter_name,
                                             const char* value);

// Get the current value of a parameter of any type as a human readable string.
// The returned string is compatible with heif_encoder_set_parameter().
LIBHEIF_API
struct heif_error heif_encoder_get_parameter(struct heif_encoder*,
                                             const char* parameter_name,
                                             char* value_ptr, int value_size);


struct heif_encoding_options;

// Compress the input image.
// Returns a handle to the coded image in 'out_image_handle' unless out_image_handle = NULL.
// 'options' should be NULL for now.
LIBHEIF_API
struct heif_error heif_context_encode_image(struct heif_context*,
                                            const struct heif_image* image,
                                            struct heif_encoder* encoder,
                                            const struct heif_encoding_options* options,
                                            struct heif_image_handle** out_image_handle);


// --- heif_image allocation

// Create a new image of the specified resolution and colorspace.
// Note: no memory for the actual image data is reserved yet. You have to use
// heif_image_add_plane() to add the image planes required by your colorspace/chroma.
LIBHEIF_API
struct heif_error heif_image_create(int width, int height,
                                    enum heif_colorspace colorspace,
                                    enum heif_chroma chroma,
                                    struct heif_image** out_image);

LIBHEIF_API
struct heif_error heif_image_add_plane(struct heif_image* image,
                                       enum heif_channel channel,
                                       int width, int height, int bit_depth);




// --- register plugins

struct heif_decoder_plugin;
struct heif_encoder_plugin;

// DEPRECATED. Use heif_register_decoder_plugin(const struct heif_decoder_plugin*) instead.
LIBHEIF_API
struct heif_error heif_register_decoder(struct heif_context* heif, const struct heif_decoder_plugin*);

LIBHEIF_API
struct heif_error heif_register_decoder_plugin(const struct heif_decoder_plugin*);

LIBHEIF_API
struct heif_error heif_register_encoder_plugin(const struct heif_encoder_plugin*);

#ifdef __cplusplus
}
#endif

#endif
