/*
 * HEIF codec.
 * Copyright (c) 2017-2025 Dirk Farin <dirk.farin@gmail.com>
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

/*! \file heif.h
 *
 * Public API for libheif.
*/

#include <stddef.h>
#include <stdint.h>

#include <libheif/heif_version.h>
#include <libheif/heif_library.h>
#include <libheif/heif_image.h>
#include <libheif/heif_color.h>
#include <libheif/heif_error.h>
#include <libheif/heif_brands.h>
#include <libheif/heif_metadata.h>
#include <libheif/heif_aux_images.h>
#include <libheif/heif_entity_groups.h>


// ========================= enum types ======================

/**
 * libheif known compression formats.
 */
enum heif_compression_format
{
  /**
   * Unspecified / undefined compression format.
   *
   * This is used to mean "no match" or "any decoder" for some parts of the
   * API. It does not indicate a specific compression format.
   */
  heif_compression_undefined = 0,
  /**
   * HEVC compression, used for HEIC images.
   *
   * This is equivalent to H.265.
  */
  heif_compression_HEVC = 1,
  /**
   * AVC compression. (Currently unused in libheif.)
   *
   * The compression is defined in ISO/IEC 14496-10. This is equivalent to H.264.
   *
   * The encapsulation is defined in ISO/IEC 23008-12:2022 Annex E.
   */
  heif_compression_AVC = 2,
  /**
   * JPEG compression.
   *
   * The compression format is defined in ISO/IEC 10918-1. The encapsulation
   * of JPEG is specified in ISO/IEC 23008-12:2022 Annex H.
  */
  heif_compression_JPEG = 3,
  /**
   * AV1 compression, used for AVIF images.
   *
   * The compression format is provided at https://aomediacodec.github.io/av1-spec/
   *
   * The encapsulation is defined in https://aomediacodec.github.io/av1-avif/
   */
  heif_compression_AV1 = 4,
  /**
   * VVC compression.
   *
   * The compression format is defined in ISO/IEC 23090-3. This is equivalent to H.266.
   *
   * The encapsulation is defined in ISO/IEC 23008-12:2022 Annex L.
   */
  heif_compression_VVC = 5,
  /**
   * EVC compression. (Currently unused in libheif.)
   *
   * The compression format is defined in ISO/IEC 23094-1.
   *
   * The encapsulation is defined in ISO/IEC 23008-12:2022 Annex M.
   */
  heif_compression_EVC = 6,
  /**
   * JPEG 2000 compression.
   *
   * The encapsulation of JPEG 2000 is specified in ISO/IEC 15444-16:2021.
   * The core encoding is defined in ISO/IEC 15444-1, or ITU-T T.800.
  */
  heif_compression_JPEG2000 = 7,
  /**
   * Uncompressed encoding.
   *
   * This is defined in ISO/IEC 23001-17:2024.
  */
  heif_compression_uncompressed = 8,
  /**
   * Mask image encoding.
   *
   * See ISO/IEC 23008-12:2022 Section 6.10.2
   */
  heif_compression_mask = 9,
  /**
   * High Throughput JPEG 2000 (HT-J2K) compression.
   *
   * The encapsulation of HT-J2K is specified in ISO/IEC 15444-16:2021.
   * The core encoding is defined in ISO/IEC 15444-15, or ITU-T T.814.
  */
  heif_compression_HTJ2K = 10
};


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

enum heif_reader_grow_status
{
  heif_reader_grow_status_size_reached,    // requested size has been reached, we can read until this point
  heif_reader_grow_status_timeout,         // size has not been reached yet, but it may still grow further (deprecated)
  heif_reader_grow_status_size_beyond_eof, // size has not been reached and never will. The file has grown to its full size
  heif_reader_grow_status_error            // an error has occurred
};

struct heif_reader_range_request_result
{
  enum heif_reader_grow_status status; // should not return 'heif_reader_grow_status_timeout'

  // Indicates up to what position the file has been read.
  // If we cannot read the whole file range (status == 'heif_reader_grow_status_size_beyond_eof'), this is the actual end position.
  // On the other hand, it may be that the reader was reading more data than requested. In that case, it should indicate the full size here
  // and libheif may decide to make use of the additional data (e.g. for filling 'tili' offset tables).
  uint64_t range_end;

  // for status == 'heif_reader_grow_status_error'
  int reader_error_code;        // a reader specific error code
  const char* reader_error_msg; // libheif will call heif_reader.release_error_msg on this if it is not NULL
};


struct heif_reader
{
  // API version supported by this reader
  int reader_api_version;

  // --- version 1 functions ---
  int64_t (* get_position)(void* userdata);

  // The functions read(), and seek() return 0 on success.
  // Generally, libheif will make sure that we do not read past the file size.
  int (* read)(void* data,
               size_t size,
               void* userdata);

  int (* seek)(int64_t position,
               void* userdata);

  // When calling this function, libheif wants to make sure that it can read the file
  // up to 'target_size'. This is useful when the file is currently downloaded and may
  // grow with time. You may, for example, extract the image sizes even before the actual
  // compressed image data has been completely downloaded.
  //
  // Even if your input files will not grow, you will have to implement at least
  // detection whether the target_size is above the (fixed) file length
  // (in this case, return 'size_beyond_eof').
  enum heif_reader_grow_status (* wait_for_file_size)(int64_t target_size, void* userdata);

  // --- version 2 functions ---

  // These two functions are for applications that want to stream HEIF files on demand.
  // For example, a large HEIF file that is served over HTTPS and we only want to download
  // it partially to decode individual tiles.
  // If you do not have this use case, you do not have to implement these functions and
  // you can set them to NULL. For simple linear loading, you may use the 'wait_for_file_size'
  // function above instead.

  // If this function is defined, libheif will often request a file range before accessing it.
  // The purpose of this function is that libheif will usually read very small chunks of data with the
  // read() callback above. However, it is inefficient to request such a small chunk of data over a network
  // and the network delay will significantly increase the decoding time.
  // Thus, libheif will call request_range() with a larger block of data that should be preloaded and the
  // subsequent read() calls will work within the requested ranges.
  //
  // Note: `end_pos` is one byte after the last position to be read.
  // You should return
  // - 'heif_reader_grow_status_size_reached' if the requested range is available, or
  // - 'heif_reader_grow_status_size_beyond_eof' if the requested range exceeds the file size
  //   (the valid part of the range has been read).
  struct heif_reader_range_request_result (*request_range)(uint64_t start_pos, uint64_t end_pos, void* userdata);

  // libheif might issue hints when it assumes that a file range might be needed in the future.
  // This may happen, for example, when your are doing selective tile accesses and libheif proposes
  // to preload offset pointer tables.
  // Another difference to request_file_range() is that this call should be non-blocking.
  // If you preload any data, do this in a background thread.
  void (*preload_range_hint)(uint64_t start_pos, uint64_t end_pos, void* userdata);

  // If libheif does not need access to a file range anymore, it may call this function to
  // give a hint to the reader that it may release the range from a cache.
  // If you do not maintain a file cache that wants to reduce its size dynamically, you do not
  // need to implement this function.
  void (*release_file_range)(uint64_t start_pos, uint64_t end_pos, void* userdata);

  // Release an error message that was returned by heif_reader in an earlier call.
  // If this function is NULL, the error message string will not be released.
  // This is a viable option if you are only returning static strings.
  void (*release_error_msg)(const char* msg);
};


// Read a HEIF file from a named disk file.
// The heif_reading_options should currently be set to NULL.
LIBHEIF_API
struct heif_error heif_context_read_from_file(struct heif_context*, const char* filename,
                                              const struct heif_reading_options*);

// Read a HEIF file stored completely in memory.
// The heif_reading_options should currently be set to NULL.
// DEPRECATED: use heif_context_read_from_memory_without_copy() instead.
LIBHEIF_API
struct heif_error heif_context_read_from_memory(struct heif_context*,
                                                const void* mem, size_t size,
                                                const struct heif_reading_options*);

// Same as heif_context_read_from_memory() except that the provided memory is not copied.
// That means, you will have to keep the memory area alive as long as you use the heif_context.
LIBHEIF_API
struct heif_error heif_context_read_from_memory_without_copy(struct heif_context*,
                                                             const void* mem, size_t size,
                                                             const struct heif_reading_options*);

LIBHEIF_API
struct heif_error heif_context_read_from_reader(struct heif_context*,
                                                const struct heif_reader* reader,
                                                void* userdata,
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

// Get the image handle for a known image ID.
LIBHEIF_API
struct heif_error heif_context_get_image_handle(struct heif_context* ctx,
                                                heif_item_id id,
                                                struct heif_image_handle**);

// Print information about the boxes of a HEIF file to file descriptor.
// This is for debugging and informational purposes only. You should not rely on
// the output having a specific format. At best, you should not use this at all.
LIBHEIF_API
void heif_context_debug_dump_boxes_to_file(struct heif_context* ctx, int fd);


// Set the maximum image size security limit. This function will set the maximum image area (number of pixels)
// to maximum_width ^ 2. Alternatively to using this function, you can also set the maximum image area
// in the security limits structure returned by heif_context_get_security_limits().
LIBHEIF_API
void heif_context_set_maximum_image_size_limit(struct heif_context* ctx, int maximum_width);

// If the maximum threads number is set to 0, the image tiles are decoded in the main thread.
// This is different from setting it to 1, which will generate a single background thread to decode the tiles.
// Note that this setting only affects libheif itself. The codecs itself may still use multi-threaded decoding.
// You can use it, for example, in cases where you are decoding several images in parallel anyway you thus want
// to minimize parallelism in each decoder.
LIBHEIF_API
void heif_context_set_max_decoding_threads(struct heif_context* ctx, int max_threads);


// --- security limits

// If you set a limit to 0, the limit is disabled.
struct heif_security_limits {
  uint8_t version;

  // --- version 1

  // Limit on the maximum image size to avoid allocating too much memory.
  // For example, setting this to 32768^2 pixels = 1 Gigapixels results
  // in 1.5 GB memory need for YUV-4:2:0 or 4 GB for RGB32.
  uint64_t max_image_size_pixels;
  uint64_t max_number_of_tiles;
  uint32_t max_bayer_pattern_pixels;
  uint32_t max_items;

  uint32_t max_color_profile_size;
  uint64_t max_memory_block_size;

  uint32_t max_components;

  uint32_t max_iloc_extents_per_item;
  uint32_t max_size_entity_group;

  uint32_t max_children_per_box; // for all boxes that are not covered by other limits

  // --- version 2

  uint64_t max_total_memory;
  uint32_t max_sample_description_box_entries;
  uint32_t max_sample_group_description_box_entries;
};

// The global security limits are the default for new heif_contexts.
// These global limits cannot be changed, but you can override the limits for a specific heif_context.
LIBHEIF_API
const struct heif_security_limits* heif_get_global_security_limits();

// Returns a set of fully disabled security limits. Use with care and only after user confirmation.
LIBHEIF_API
const struct heif_security_limits* heif_get_disabled_security_limits();

// Returns the security limits for a heif_context.
// By default, the limits are set to the global limits, but you can change them in the returned object.
LIBHEIF_API
struct heif_security_limits* heif_context_get_security_limits(const struct heif_context*);

// Overwrites the security limits of a heif_context.
// This is a convenience function to easily copy limits.
LIBHEIF_API
struct heif_error heif_context_set_security_limits(struct heif_context*, const struct heif_security_limits*);


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

LIBHEIF_API
heif_item_id heif_image_handle_get_item_id(const struct heif_image_handle* handle);

// Get the resolution of an image.
LIBHEIF_API
int heif_image_handle_get_width(const struct heif_image_handle* handle);

LIBHEIF_API
int heif_image_handle_get_height(const struct heif_image_handle* handle);

LIBHEIF_API
int heif_image_handle_has_alpha_channel(const struct heif_image_handle*);

LIBHEIF_API
int heif_image_handle_is_premultiplied_alpha(const struct heif_image_handle*);

// Returns -1 on error, e.g. if this information is not present in the image.
// Only defined for images coded in the YCbCr or monochrome colorspace.
LIBHEIF_API
int heif_image_handle_get_luma_bits_per_pixel(const struct heif_image_handle*);

// Returns -1 on error, e.g. if this information is not present in the image.
// Only defined for images coded in the YCbCr colorspace.
LIBHEIF_API
int heif_image_handle_get_chroma_bits_per_pixel(const struct heif_image_handle*);

// Return the colorspace that libheif proposes to use for decoding.
// Usually, these will be either YCbCr or Monochrome, but it may also propose RGB for images
// encoded with matrix_coefficients=0 or for images coded natively in RGB.
// It may also return *_undefined if the file misses relevant information to determine this without decoding.
// These are only proposed values that avoid colorspace conversions as much as possible.
// You can still request the output in your preferred colorspace, but this may involve an internal conversion.
LIBHEIF_API
struct heif_error heif_image_handle_get_preferred_decoding_colorspace(const struct heif_image_handle* image_handle,
                                                                      enum heif_colorspace* out_colorspace,
                                                                      enum heif_chroma* out_chroma);

// Get the image width from the 'ispe' box. This is the original image size without
// any transformations applied to it. Do not use this unless you know exactly what
// you are doing.
LIBHEIF_API
int heif_image_handle_get_ispe_width(const struct heif_image_handle* handle);

LIBHEIF_API
int heif_image_handle_get_ispe_height(const struct heif_image_handle* handle);

// This gets the context associated with the image handle.
// Note that you have to release the returned context with heif_context_free() in any case.
//
// This means: when you have several image-handles that originate from the same file and you get the
// context of each of them, the returned pointer may be different even though it refers to the same
// logical context. You have to call heif_context_free() on all those context pointers.
// After you freed a context pointer, you can still use the context through a different pointer that you
// might have acquired from elsewhere.
LIBHEIF_API
struct heif_context* heif_image_handle_get_context(const struct heif_image_handle* handle);


struct heif_image_tiling
{
  int version;

  // --- version 1

  uint32_t num_columns;
  uint32_t num_rows;
  uint32_t tile_width;
  uint32_t tile_height;

  uint32_t image_width;
  uint32_t image_height;

  // Position of the top left tile.
  // Usually, this is (0;0), but if a tiled image is rotated or cropped, it may be that the top left tile should be placed at a negative position.
  // The offsets define this negative shift.
  uint32_t top_offset;
  uint32_t left_offset;

  uint8_t number_of_extra_dimensions;  // 0 for normal images, 1 for volumetric (3D), ...
  uint32_t extra_dimension_size[8];    // size of extra dimensions (first 8 dimensions)
};


// If 'process_image_transformations' is true, this returns modified sizes.
// If it is false, the top_offset and left_offset will always be (0;0).
LIBHEIF_API
struct heif_error heif_image_handle_get_image_tiling(const struct heif_image_handle* handle, int process_image_transformations, struct heif_image_tiling* out_tiling);


// For grid images, return the image item ID of a specific grid tile.
// If 'process_image_transformations' is true, the tile positions are given in the transformed image coordinate system and
// are internally mapped to the original image tile positions.
LIBHEIF_API
struct heif_error heif_image_handle_get_grid_image_tile_id(const struct heif_image_handle* handle,
                                                           int process_image_transformations,
                                                           uint32_t tile_x, uint32_t tile_y,
                                                           heif_item_id* out_tile_item_id);


struct heif_decoding_options;

// The tile position is given in tile indices, not in pixel coordinates.
// If the image transformations are processed (option->ignore_image_transformations==false), the tile position
// is given in the transformed coordinates.
LIBHEIF_API
struct heif_error heif_image_handle_decode_image_tile(const struct heif_image_handle* in_handle,
                                                      struct heif_image** out_img,
                                                      enum heif_colorspace colorspace,
                                                      enum heif_chroma chroma,
                                                      const struct heif_decoding_options* options,
                                                      uint32_t tile_x, uint32_t tile_y);


enum heif_progress_step
{
  heif_progress_step_total = 0,
  heif_progress_step_load_tile = 1
};


enum heif_chroma_downsampling_algorithm
{
  heif_chroma_downsampling_nearest_neighbor = 1,
  heif_chroma_downsampling_average = 2,

  // Combine with 'heif_chroma_upsampling_bilinear' for best quality.
  // Makes edges look sharper when using YUV 420 with bilinear chroma upsampling.
  heif_chroma_downsampling_sharp_yuv = 3
};

enum heif_chroma_upsampling_algorithm
{
  heif_chroma_upsampling_nearest_neighbor = 1,
  heif_chroma_upsampling_bilinear = 2
};


struct heif_color_conversion_options
{
  // 'version' must be 1.
  uint8_t version;

  // --- version 1 options

  enum heif_chroma_downsampling_algorithm preferred_chroma_downsampling_algorithm;
  enum heif_chroma_upsampling_algorithm preferred_chroma_upsampling_algorithm;

  // When set to 'false' libheif may also use a different algorithm if the preferred one is not available
  // or using a different algorithm is computationally less complex. Note that currently (v1.17.0) this
  // means that for RGB input it will usually choose nearest-neighbor sampling because this is computationally
  // the simplest.
  // Set this field to 'true' if you want to make sure that the specified algorithm is used even
  // at the cost of slightly higher computation times.
  uint8_t only_use_preferred_chroma_algorithm;

  // --- Note that we cannot extend this struct because it is embedded in
  //     other structs (heif_decoding_options and heif_encoding_options).
};


enum heif_alpha_composition_mode
{
  heif_alpha_composition_mode_none,
  heif_alpha_composition_mode_solid_color,
  heif_alpha_composition_mode_checkerboard,
};


struct heif_color_conversion_options_ext
{
  uint8_t version;

  // --- version 1 options

  enum heif_alpha_composition_mode alpha_composition_mode;

  // color values should be specified in the range [0, 65535]
  uint16_t background_red, background_green, background_blue;
  uint16_t secondary_background_red, secondary_background_green, secondary_background_blue;
  uint16_t checkerboard_square_size;
};


// Assumes that it is a version=1 struct.
LIBHEIF_API
void heif_color_conversion_options_set_defaults(struct heif_color_conversion_options*);


struct heif_decoding_options
{
  uint8_t version;

  // version 1 options

  // Ignore geometric transformations like cropping, rotation, mirroring.
  // Default: false (do not ignore).
  uint8_t ignore_transformations;

  // Any of the progress functions may be called from background threads.
  void (* start_progress)(enum heif_progress_step step, int max_progress, void* progress_user_data);

  void (* on_progress)(enum heif_progress_step step, int progress, void* progress_user_data);

  void (* end_progress)(enum heif_progress_step step, void* progress_user_data);

  void* progress_user_data;

  // version 2 options

  uint8_t convert_hdr_to_8bit;

  // version 3 options

  // When enabled, an error is returned for invalid input. Otherwise, it will try its best and
  // add decoding warnings to the decoded heif_image. Default is non-strict.
  uint8_t strict_decoding;

  // version 4 options

  // name_id of the decoder to use for the decoding.
  // If set to NULL (default), the highest priority decoder is chosen.
  // The priority is defined in the plugin.
  const char* decoder_id;

  // version 5 options

  struct heif_color_conversion_options color_conversion_options;

  // version 6 options

  int (* cancel_decoding)(void* progress_user_data);

  // version 7 options

  // When set to NULL, default options will be used
  struct heif_color_conversion_options_ext* color_conversion_options_ext;
};


// Allocate decoding options and fill with default values.
// Note: you should always get the decoding options through this function since the
// option structure may grow in size in future versions.
LIBHEIF_API
struct heif_decoding_options* heif_decoding_options_alloc(void);

LIBHEIF_API
void heif_decoding_options_free(struct heif_decoding_options*);

LIBHEIF_API
struct heif_color_conversion_options_ext* heif_color_conversion_options_ext_alloc(void);

LIBHEIF_API
void heif_color_conversion_options_ext_free(struct heif_color_conversion_options_ext*);

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

// ====================================================================================================
//  Encoding API

LIBHEIF_API
struct heif_error heif_context_write_to_file(struct heif_context*,
                                             const char* filename);

struct heif_writer
{
  // API version supported by this writer
  int writer_api_version;

  // --- version 1 functions ---

  // On success, the returned heif_error may have a NULL message. It will automatically be replaced with a "Success" string.
  struct heif_error (* write)(struct heif_context* ctx, // TODO: why do we need this parameter?
                              const void* data,
                              size_t size,
                              void* userdata);
};

LIBHEIF_API
struct heif_error heif_context_write(struct heif_context*,
                                     struct heif_writer* writer,
                                     void* userdata);

// Add a compatible brand that is now added automatically by libheif when encoding images (e.g. some application brands like 'geo1').
LIBHEIF_API
void heif_context_add_compatible_brand(struct heif_context* ctx,
                                       heif_brand2 compatible_brand);

// ----- encoder -----

// The encoder used for actually encoding an image.
struct heif_encoder;

// A description of the encoder's capabilities and name.
struct heif_encoder_descriptor;

// A configuration parameter of the encoder. Each encoder implementation may have a different
// set of parameters. For the most common settings (e.q. quality), special functions to set
// the parameters are provided.
struct heif_encoder_parameter;

struct heif_decoder_descriptor;

// Get a list of available decoders. You can filter the encoders by compression format.
// Use format_filter==heif_compression_undefined to get all available decoders.
// The returned list of decoders is sorted by their priority (which is a plugin property).
// The number of decoders is returned, which are not more than 'count' if (out_decoders != nullptr).
// By setting out_decoders==nullptr, you can query the number of decoders, 'count' is ignored.
LIBHEIF_API
int heif_get_decoder_descriptors(enum heif_compression_format format_filter,
                                 const struct heif_decoder_descriptor** out_decoders,
                                 int count);

// Return a long, descriptive name of the decoder (including version information).
LIBHEIF_API
const char* heif_decoder_descriptor_get_name(const struct heif_decoder_descriptor*);

// Return a short, symbolic name for identifying the decoder.
// This name should stay constant over different decoder versions.
// Note: the returned ID may be NULL for old plugins that don't support this yet.
LIBHEIF_API
const char* heif_decoder_descriptor_get_id_name(const struct heif_decoder_descriptor*);

// DEPRECATED: use heif_get_encoder_descriptors() instead.
// Get a list of available encoders. You can filter the encoders by compression format and name.
// Use format_filter==heif_compression_undefined and name_filter==NULL as wildcards.
// The returned list of encoders is sorted by their priority (which is a plugin property).
// The number of encoders is returned, which are not more than 'count' if (out_encoders != nullptr).
// By setting out_encoders==nullptr, you can query the number of encoders, 'count' is ignored.
// Note: to get the actual encoder from the descriptors returned here, use heif_context_get_encoder().
LIBHEIF_API
int heif_context_get_encoder_descriptors(struct heif_context*, // TODO: why do we need this parameter?
                                         enum heif_compression_format format_filter,
                                         const char* name_filter,
                                         const struct heif_encoder_descriptor** out_encoders,
                                         int count);

// Get a list of available encoders. You can filter the encoders by compression format and name.
// Use format_filter==heif_compression_undefined and name_filter==NULL as wildcards.
// The returned list of encoders is sorted by their priority (which is a plugin property).
// The number of encoders is returned, which are not more than 'count' if (out_encoders != nullptr).
// By setting out_encoders==nullptr, you can query the number of encoders, 'count' is ignored.
// Note: to get the actual encoder from the descriptors returned here, use heif_context_get_encoder().
LIBHEIF_API
int heif_get_encoder_descriptors(enum heif_compression_format format_filter,
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
int heif_encoder_descriptor_supports_lossy_compression(const struct heif_encoder_descriptor*);

LIBHEIF_API
int heif_encoder_descriptor_supports_lossless_compression(const struct heif_encoder_descriptor*);


// Get an encoder instance that can be used to actually encode images from a descriptor.
LIBHEIF_API
struct heif_error heif_context_get_encoder(struct heif_context* context,
                                           const struct heif_encoder_descriptor*,
                                           struct heif_encoder** out_encoder);

// Quick check whether there is a decoder available for the given format.
// Note that the decoder still may not be able to decode all variants of that format.
// You will have to query that further (todo) or just try to decode and check the returned error.
LIBHEIF_API
int heif_have_decoder_for_format(enum heif_compression_format format);

// Quick check whether there is an enoder available for the given format.
// Note that the encoder may be limited to a certain subset of features (e.g. only 8 bit, only lossy).
// You will have to query the specific capabilities further.
LIBHEIF_API
int heif_have_encoder_for_format(enum heif_compression_format format);

// Get an encoder for the given compression format. If there are several encoder plugins
// for this format, the encoder with the highest plugin priority will be returned.
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
const struct heif_encoder_parameter* const* heif_encoder_list_parameters(struct heif_encoder*);

// Return the parameter name.
LIBHEIF_API
const char* heif_encoder_parameter_get_name(const struct heif_encoder_parameter*);


enum heif_encoder_parameter_type
{
  heif_encoder_parameter_type_integer = 1,
  heif_encoder_parameter_type_boolean = 2,
  heif_encoder_parameter_type_string = 3
};

// Return the parameter type.
LIBHEIF_API
enum heif_encoder_parameter_type heif_encoder_parameter_get_type(const struct heif_encoder_parameter*);

// DEPRECATED. Use heif_encoder_parameter_get_valid_integer_values() instead.
LIBHEIF_API
struct heif_error heif_encoder_parameter_get_valid_integer_range(const struct heif_encoder_parameter*,
                                                                 int* have_minimum_maximum,
                                                                 int* minimum, int* maximum);

// If integer is limited by a range, have_minimum and/or have_maximum will be != 0 and *minimum, *maximum is set.
// If integer is limited by a fixed set of values, *num_valid_values will be >0 and *out_integer_array is set.
LIBHEIF_API
struct heif_error heif_encoder_parameter_get_valid_integer_values(const struct heif_encoder_parameter*,
                                                                  int* have_minimum, int* have_maximum,
                                                                  int* minimum, int* maximum,
                                                                  int* num_valid_values,
                                                                  const int** out_integer_array);

LIBHEIF_API
struct heif_error heif_encoder_parameter_get_valid_string_values(const struct heif_encoder_parameter*,
                                                                 const char* const** out_stringarray);


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
                                                             const char* const** out_stringarray);

LIBHEIF_API
struct heif_error heif_encoder_parameter_integer_valid_values(struct heif_encoder*,
                                                              const char* parameter_name,
                                                              int* have_minimum, int* have_maximum,
                                                              int* minimum, int* maximum,
                                                              int* num_valid_values,
                                                              const int** out_integer_array);

// Set a parameter of any type to the string value.
// Integer values are parsed from the string.
// Boolean values can be "true"/"false"/"1"/"0"
//
// x265 encoder specific note:
// When using the x265 encoder, you may pass any of its parameters by
// prefixing the parameter name with 'x265:'. Hence, to set the 'ctu' parameter,
// you will have to set 'x265:ctu' in libheif.
// Note that there is no checking for valid parameters when using the prefix.
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

// Query whether a specific parameter has a default value.
LIBHEIF_API
int heif_encoder_has_default(struct heif_encoder*,
                             const char* parameter_name);


// The orientation values are defined equal to the EXIF Orientation tag.
enum heif_orientation
{
  heif_orientation_normal = 1,
  heif_orientation_flip_horizontally = 2,
  heif_orientation_rotate_180 = 3,
  heif_orientation_flip_vertically = 4,
  heif_orientation_rotate_90_cw_then_flip_horizontally = 5,
  heif_orientation_rotate_90_cw = 6,
  heif_orientation_rotate_90_cw_then_flip_vertically = 7,
  heif_orientation_rotate_270_cw = 8
};


struct heif_encoding_options
{
  uint8_t version;

  // version 1 options

  uint8_t save_alpha_channel; // default: true

  // version 2 options

  // DEPRECATED. This option is not required anymore. Its value will be ignored.
  uint8_t macOS_compatibility_workaround;

  // version 3 options

  uint8_t save_two_colr_boxes_when_ICC_and_nclx_available; // default: false

  // version 4 options

  // Set this to the NCLX parameters to be used in the output image or set to NULL
  // when the same parameters as in the input image should be used.
  struct heif_color_profile_nclx* output_nclx_profile;

  uint8_t macOS_compatibility_workaround_no_nclx_profile;

  // version 5 options

  // libheif will generate irot/imir boxes to match these orientations
  enum heif_orientation image_orientation;

  // version 6 options

  struct heif_color_conversion_options color_conversion_options;

  // version 7 options

  // Set this to true to use compressed form of uncC where possible.
  uint8_t prefer_uncC_short_form;

  // TODO: we should add a flag to force MIAF compatible outputs. E.g. this will put restrictions on grid tile sizes and
  //       might add a clap box when the grid output size does not match the color subsampling factors.
  //       Since some of these constraints have to be known before actually encoding the image, "forcing MIAF compatibility"
  //       could also be a flag in the heif_context.
};

LIBHEIF_API
struct heif_encoding_options* heif_encoding_options_alloc(void);

LIBHEIF_API
void heif_encoding_options_copy(struct heif_encoding_options* dst, const struct heif_encoding_options*  src);

LIBHEIF_API
void heif_encoding_options_free(struct heif_encoding_options*);


// Compress the input image.
// Returns a handle to the coded image in 'out_image_handle' unless out_image_handle = NULL.
// 'options' should be NULL for now.
// The first image added to the context is also automatically set the primary image, but
// you can change the primary image later with heif_context_set_primary_image().
LIBHEIF_API
struct heif_error heif_context_encode_image(struct heif_context*,
                                            const struct heif_image* image,
                                            struct heif_encoder* encoder,
                                            const struct heif_encoding_options* options,
                                            struct heif_image_handle** out_image_handle);

/**
 * @brief Encodes an array of images into a grid.
 * 
 * @param ctx The file context
 * @param tiles User allocated array of images that will form the grid.
 * @param rows The number of rows in the grid.
 * @param columns The number of columns in the grid.
 * @param encoder Defines the encoder to use. See heif_context_get_encoder_for_format()
 * @param input_options Optional, may be nullptr.
 * @param out_image_handle Returns a handle to the grid. The caller is responsible for freeing it.
 * @return Returns an error if ctx, tiles, or encoder is nullptr. If rows or columns is 0. 
 */
LIBHEIF_API
struct heif_error heif_context_encode_grid(struct heif_context* ctx,
                                           struct heif_image** tiles,
                                           uint16_t rows,
                                           uint16_t columns,
                                           struct heif_encoder* encoder,
                                           const struct heif_encoding_options* input_options,
                                           struct heif_image_handle** out_image_handle);

LIBHEIF_API
struct heif_error heif_context_add_grid_image(struct heif_context* ctx,
                                              uint32_t image_width,
                                              uint32_t image_height,
                                              uint32_t tile_columns,
                                              uint32_t tile_rows,
                                              const struct heif_encoding_options* encoding_options,
                                              struct heif_image_handle** out_grid_image_handle);

LIBHEIF_API
struct heif_error heif_context_add_image_tile(struct heif_context* ctx,
                                              struct heif_image_handle* tiled_image,
                                              uint32_t tile_x, uint32_t tile_y,
                                              const struct heif_image* image,
                                              struct heif_encoder* encoder);

// offsets[] should either be NULL (all offsets==0) or an array of size 2*nImages with x;y offset pairs.
// If background_rgba is NULL, the background is transparent.
LIBHEIF_API
struct heif_error heif_context_add_overlay_image(struct heif_context* ctx,
                                                 uint32_t image_width,
                                                 uint32_t image_height,
                                                 uint16_t nImages,
                                                 const heif_item_id* image_ids,
                                                 int32_t* offsets,
                                                 const uint16_t background_rgba[4],
                                                 struct heif_image_handle** out_iovl_image_handle);

LIBHEIF_API
struct heif_error heif_context_set_primary_image(struct heif_context*,
                                                 struct heif_image_handle* image_handle);


// DEPRECATED, typo in function name
LIBHEIF_API
int heif_encoder_descriptor_supportes_lossy_compression(const struct heif_encoder_descriptor*);

// DEPRECATED, typo in function name
LIBHEIF_API
int heif_encoder_descriptor_supportes_lossless_compression(const struct heif_encoder_descriptor*);


#ifdef __cplusplus
}
#endif

#endif
