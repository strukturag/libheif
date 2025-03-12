/*
 * HEIF codec.
 * Copyright (c) 2024 Dirk Farin <dirk.farin@gmail.com>
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

#ifndef LIBHEIF_HEIF_EXPERIMENTAL_H
#define LIBHEIF_HEIF_EXPERIMENTAL_H

#include "libheif/heif.h"

#ifdef __cplusplus
extern "C" {
#endif

#if HEIF_ENABLE_EXPERIMENTAL_FEATURES

  /* ===================================================================================
   *   This file contains candidate APIs that did not make it into the public API yet.
   * ===================================================================================
   */


  /*
  heif_item_property_type_camera_intrinsic_matrix = heif_fourcc('c', 'm', 'i', 'n'),
  heif_item_property_type_camera_extrinsic_matrix = heif_fourcc('c', 'm', 'e', 'x')
*/

struct heif_property_camera_intrinsic_matrix;
struct heif_property_camera_extrinsic_matrix;

//LIBHEIF_API
struct heif_error heif_item_get_property_camera_intrinsic_matrix(const struct heif_context* context,
                                                                 heif_item_id itemId,
                                                                 heif_property_id propertyId,
                                                                 struct heif_property_camera_intrinsic_matrix** out_matrix);

//LIBHEIF_API
void heif_property_camera_intrinsic_matrix_release(struct heif_property_camera_intrinsic_matrix* matrix);

//LIBHEIF_API
struct heif_error heif_property_camera_intrinsic_matrix_get_focal_length(const struct heif_property_camera_intrinsic_matrix* matrix,
                                                                int image_width, int image_height,
                                                                double* out_focal_length_x,
                                                                double* out_focal_length_y);

//LIBHEIF_API
struct heif_error heif_property_camera_intrinsic_matrix_get_principal_point(const struct heif_property_camera_intrinsic_matrix* matrix,
                                                                   int image_width, int image_height,
                                                                   double* out_principal_point_x,
                                                                   double* out_principal_point_y);

//LIBHEIF_API
struct heif_error heif_property_camera_intrinsic_matrix_get_skew(const struct heif_property_camera_intrinsic_matrix* matrix,
                                                        double* out_skew);

//LIBHEIF_API
struct heif_property_camera_intrinsic_matrix* heif_property_camera_intrinsic_matrix_alloc();

//LIBHEIF_API
void heif_property_camera_intrinsic_matrix_set_simple(struct heif_property_camera_intrinsic_matrix* matrix,
                                             int image_width, int image_height,
                                             double focal_length, double principal_point_x, double principal_point_y);

//LIBHEIF_API
void heif_property_camera_intrinsic_matrix_set_full(struct heif_property_camera_intrinsic_matrix* matrix,
                                           int image_width, int image_height,
                                           double focal_length_x,
                                           double focal_length_y,
                                           double principal_point_x, double principal_point_y,
                                           double skew);

//LIBHEIF_API
struct heif_error heif_item_add_property_camera_intrinsic_matrix(const struct heif_context* context,
                                                          heif_item_id itemId,
                                                          const struct heif_property_camera_intrinsic_matrix* matrix,
                                                          heif_property_id* out_propertyId);


//LIBHEIF_API
struct heif_error heif_item_get_property_camera_extrinsic_matrix(const struct heif_context* context,
                                                                 heif_item_id itemId,
                                                                 heif_property_id propertyId,
                                                                 struct heif_property_camera_extrinsic_matrix** out_matrix);

//LIBHEIF_API
void heif_property_camera_extrinsic_matrix_release(struct heif_property_camera_extrinsic_matrix* matrix);

// `out_matrix` must point to a 9-element matrix, which will be filled in row-major order.
//LIBHEIF_API
struct heif_error heif_property_camera_extrinsic_matrix_get_rotation_matrix(const struct heif_property_camera_extrinsic_matrix* matrix,
                                                                            double* out_matrix);

// `out_vector` must point to a 3-element vector, which will be filled with the (X,Y,Z) coordinates (in micrometers).
//LIBHEIF_API
struct heif_error heif_property_camera_extrinsic_matrix_get_position_vector(const struct heif_property_camera_extrinsic_matrix* matrix,
                                                                            int32_t* out_vector);

//LIBHEIF_API
struct heif_error heif_property_camera_extrinsic_matrix_get_world_coordinate_system_id(const struct heif_property_camera_extrinsic_matrix* matrix,
                                                                                       uint32_t* out_wcs_id);
#endif

// --- Tiled images

struct heif_tiled_image_parameters {
  int version;

  // --- version 1

  uint32_t image_width;
  uint32_t image_height;

  uint32_t tile_width;
  uint32_t tile_height;

  uint32_t compression_format_fourcc;  // will be set automatically when calling heif_context_add_tiled_image()

  uint8_t offset_field_length;   // one of: 32, 40, 48, 64
  uint8_t size_field_length;     // one of:  0, 24, 32, 64

  uint8_t number_of_extra_dimensions;  // 0 for normal images, 1 for volumetric (3D), ...
  uint32_t extra_dimensions[8];        // size of extra dimensions (first 8 dimensions)

  // boolean flags
  uint8_t tiles_are_sequential;  // TODO: can we derive this automatically
};

#if HEIF_ENABLE_EXPERIMENTAL_FEATURES
LIBHEIF_API
struct heif_error heif_context_add_tiled_image(struct heif_context* ctx,
                                               const struct heif_tiled_image_parameters* parameters,
                                               const struct heif_encoding_options* options, // TODO: do we need this?
                                               const struct heif_encoder* encoder,
                                               struct heif_image_handle** out_tiled_image_handle);
#endif

// --- 'unci' images

// This is similar to heif_metadata_compression. We should try to keep the integers compatible, but each enum will just
// contain the allowed values.
enum heif_unci_compression
{
  heif_unci_compression_off = 0,
  //heif_unci_compression_auto = 1,
  //heif_unci_compression_unknown = 2, // only used when reading unknown method from input file
  heif_unci_compression_deflate = 3,
  heif_unci_compression_zlib = 4,
  heif_unci_compression_brotli = 5
};


struct heif_unci_image_parameters {
  int version;

  // --- version 1

  uint32_t image_width;
  uint32_t image_height;

  uint32_t tile_width;
  uint32_t tile_height;

  enum heif_unci_compression compression; // TODO

  // TODO: interleave type, padding
};

#if HEIF_ENABLE_EXPERIMENTAL_FEATURES
LIBHEIF_API
struct heif_error heif_context_add_unci_image(struct heif_context* ctx,
                                              const struct heif_unci_image_parameters* parameters,
                                              const struct heif_encoding_options* encoding_options,
                                              const struct heif_image* prototype,
                                              struct heif_image_handle** out_unci_image_handle);
#endif

// --- 'pymd' entity group (pyramid layers)

struct heif_pyramid_layer_info {
  heif_item_id layer_image_id;
  uint16_t layer_binning;
  uint32_t tile_rows_in_layer;
  uint32_t tile_columns_in_layer;
};

#if HEIF_ENABLE_EXPERIMENTAL_FEATURES
// The input images are automatically sorted according to resolution. You can provide them in any order.
LIBHEIF_API
struct heif_error heif_context_add_pyramid_entity_group(struct heif_context* ctx,
                                                        const heif_item_id* layer_item_ids,
                                                        size_t num_layers,
                                                        heif_item_id* out_group_id);

LIBHEIF_API
struct heif_pyramid_layer_info* heif_context_get_pyramid_entity_group_info(struct heif_context*, heif_entity_group_id id, int* out_num_layers);

LIBHEIF_API
void heif_pyramid_layer_info_release(struct heif_pyramid_layer_info*);
#endif

// --- other pixel datatype support

enum heif_channel_datatype
{
  heif_channel_datatype_undefined = 0,
  heif_channel_datatype_unsigned_integer = 1,
  heif_channel_datatype_signed_integer = 2,
  heif_channel_datatype_floating_point = 3,
  heif_channel_datatype_complex_number = 4
};

#if HEIF_ENABLE_EXPERIMENTAL_FEATURES
LIBHEIF_API
struct heif_error heif_image_add_channel(struct heif_image* image,
                                         enum heif_channel channel,
                                         int width, int height,
                                         enum heif_channel_datatype datatype, int bit_depth);


LIBHEIF_API
int heif_image_list_channels(struct heif_image*,
                             enum heif_channel** out_channels);

LIBHEIF_API
void heif_channel_release_list(enum heif_channel** channels);
#endif

struct heif_complex32 {
  float real, imaginary;
};

struct heif_complex64 {
  double real, imaginary;
};

#if HEIF_ENABLE_EXPERIMENTAL_FEATURES
LIBHEIF_API
enum heif_channel_datatype heif_image_get_datatype(const struct heif_image* img, enum heif_channel channel);


// The 'stride' in all of these functions are in units of the underlying datatype.
LIBHEIF_API
const uint16_t* heif_image_get_channel_uint16_readonly(const struct heif_image*,
                                                       enum heif_channel channel,
                                                       size_t* out_stride);

LIBHEIF_API
const uint32_t* heif_image_get_channel_uint32_readonly(const struct heif_image*,
                                                       enum heif_channel channel,
                                                       size_t* out_stride);

LIBHEIF_API
const uint64_t* heif_image_get_channel_uint64_readonly(const struct heif_image*,
                                                       enum heif_channel channel,
                                                       size_t* out_stride);

LIBHEIF_API
const int16_t* heif_image_get_channel_int16_readonly(const struct heif_image*,
                                                     enum heif_channel channel,
                                                     size_t* out_stride);

LIBHEIF_API
const int32_t* heif_image_get_channel_int32_readonly(const struct heif_image*,
                                                     enum heif_channel channel,
                                                     size_t* out_stride);

LIBHEIF_API
const int64_t* heif_image_get_channel_int64_readonly(const struct heif_image*,
                                                     enum heif_channel channel,
                                                     size_t* out_stride);

LIBHEIF_API
const float* heif_image_get_channel_float32_readonly(const struct heif_image*,
                                                     enum heif_channel channel,
                                                     size_t* out_stride);

LIBHEIF_API
const double* heif_image_get_channel_float64_readonly(const struct heif_image*,
                                                      enum heif_channel channel,
                                                      size_t* out_stride);

LIBHEIF_API
const struct heif_complex32* heif_image_get_channel_complex32_readonly(const struct heif_image*,
                                                                       enum heif_channel channel,
                                                                       size_t* out_stride);

LIBHEIF_API
const struct heif_complex64* heif_image_get_channel_complex64_readonly(const struct heif_image*,
                                                                       enum heif_channel channel,
                                                                       size_t* out_stride);

LIBHEIF_API
uint16_t* heif_image_get_channel_uint16(struct heif_image*,
                                        enum heif_channel channel,
                                        size_t* out_stride);

LIBHEIF_API
uint32_t* heif_image_get_channel_uint32(struct heif_image*,
                                        enum heif_channel channel,
                                        size_t* out_stride);

LIBHEIF_API
uint64_t* heif_image_get_channel_uint64(struct heif_image*,
                                        enum heif_channel channel,
                                        size_t* out_stride);

LIBHEIF_API
int16_t* heif_image_get_channel_int16(struct heif_image*,
                                      enum heif_channel channel,
                                      size_t* out_stride);

LIBHEIF_API
int32_t* heif_image_get_channel_int32(struct heif_image*,
                                      enum heif_channel channel,
                                      size_t* out_stride);

LIBHEIF_API
int64_t* heif_image_get_channel_int64(struct heif_image*,
                                      enum heif_channel channel,
                                      size_t* out_stride);

LIBHEIF_API
float* heif_image_get_channel_float32(struct heif_image*,
                                      enum heif_channel channel,
                                      size_t* out_stride);

LIBHEIF_API
double* heif_image_get_channel_float64(struct heif_image*,
                                       enum heif_channel channel,
                                       size_t* out_stride);

LIBHEIF_API
struct heif_complex32* heif_image_get_channel_complex32(struct heif_image*,
                                                        enum heif_channel channel,
                                                        size_t* out_stride);

LIBHEIF_API
struct heif_complex64* heif_image_get_channel_complex64(struct heif_image*,
                                                        enum heif_channel channel,
                                                        size_t* out_stride);


// ========================= Timestamps =========================

LIBHEIF_API extern const uint64_t heif_tai_clock_info_unknown_time_uncertainty;
LIBHEIF_API extern const int32_t heif_tai_clock_info_unknown_drift_rate;
LIBHEIF_API extern const uint64_t heif_unknown_tai_timestamp;
#endif

struct heif_tai_clock_info
{
  uint8_t version;

  // version 1

  uint64_t time_uncertainty;
  uint32_t clock_resolution;
  int32_t clock_drift_rate;
  uint8_t clock_type;
};


#if HEIF_ENABLE_EXPERIMENTAL_FEATURES
int heif_is_tai_clock_info_drift_rate_undefined(int32_t drift_rate);



// Creates a new clock info property if it doesn't already exist.
LIBHEIF_API
struct heif_error heif_property_set_clock_info(struct heif_context* ctx,
                                               heif_item_id itemId,
                                               const struct heif_tai_clock_info* clock,
                                               heif_property_id* out_propertyId);

// The `out_clock` struct passed in needs to have the `version` field set so that this
// function knows which fields it is safe to fill.
// When the read property is a lower version, the version variable of out_clock will be reduced.
LIBHEIF_API
struct heif_error heif_property_get_clock_info(const struct heif_context* ctx,
                                               heif_item_id itemId,
                                               struct heif_tai_clock_info* out_clock);
#endif

struct heif_tai_timestamp_packet
{
  uint8_t version;

  // version 1

  uint64_t tai_timestamp;
  uint8_t synchronization_state;         // bool
  uint8_t timestamp_generation_failure;  // bool
  uint8_t timestamp_is_modified;         // bool
};

#if HEIF_ENABLE_EXPERIMENTAL_FEATURES

// Creates a new TAI timestamp property if one doesn't already exist for itemId.
// Creates a new clock info property if one doesn't already exist for itemId.
LIBHEIF_API
struct heif_error heif_property_set_tai_timestamp(struct heif_context* ctx,
                                                  heif_item_id itemId,
                                                  struct heif_tai_timestamp_packet* timestamp,
                                                  heif_property_id* out_propertyId);

// TODO: check whether it would be better to return an allocated heif_tai_timestamp_packet struct because if we pass it in, we have
//       to set the version field in the input and check the version of the output (in case the library is older than the application).
//       Pro: it would be more consistent
//       Contra: it is an unnecessary alloc/release memory operation
LIBHEIF_API
struct heif_error heif_property_get_tai_timestamp(const struct heif_context* ctx,
                                                  heif_item_id itemId,
                                                  struct heif_tai_timestamp_packet* out_timestamp);

LIBHEIF_API
struct heif_error heif_image_set_tai_timestamp(struct heif_image* img,
                                               const struct heif_tai_timestamp_packet* timestamp);

LIBHEIF_API
int heif_image_has_tai_timestamp(const struct heif_image* img);

LIBHEIF_API
struct heif_error heif_image_get_tai_timestamp(const struct heif_image* img,
                                               struct heif_tai_timestamp_packet* timestamp);

LIBHEIF_API
heif_tai_timestamp_packet* heif_tai_timestamp_packet_alloc();

LIBHEIF_API
void heif_tai_timestamp_packet_release(const heif_tai_timestamp_packet*);

// version field has to be set in both structs
LIBHEIF_API
void heif_tai_timestamp_packet_copy(heif_tai_timestamp_packet* dst, const heif_tai_timestamp_packet* src);

LIBHEIF_API
heif_error heif_image_extract_area(const heif_image*,
                                   uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                                   const heif_security_limits* limits,
                                   struct heif_image** out_image);

#endif

// --- --- sequences

// --- reading sequence tracks

/**
 * Check whether there is an image sequence in the HEIF file.
 *
 * @return A boolean whether there is an image sequence in the HEIF file.
 */
LIBHEIF_API
int heif_context_has_sequence(heif_context*);

/**
 * Get the timescale (clock ticks per second) for timing values in the sequence.
 *
 * @note Each track may have its independent timescale.
 *
 * @return Clock ticks per second. Returns 0 if there is no sequence in the file.
 */
LIBHEIF_API
uint32_t heif_context_get_sequence_timescale(heif_context*);

/**
 * Get the total duration of the sequence in timescale clock ticks.
 * Use `heif_context_get_sequence_timescale()` to get the clock ticks per second.
 *
 * @return Sequence duration in clock ticks. Returns 0 if there is no sequence in the file.
 */
LIBHEIF_API
uint64_t heif_context_get_sequence_duration(heif_context*);


// A track, which may be an image sequence, a video track or a metadata track.
struct heif_track;

/**
 * Free a `heif_track` object received from libheif.
 * Passing NULL is ok.
 */
LIBHEIF_API
void heif_track_release(heif_track*);

/**
 * Get the number of tracks in the HEIF file.
 *
 * @return Number of tracks or 0 if there is no sequence in the HEIF file.
 */
LIBHEIF_API
int heif_context_number_of_sequence_tracks(const struct heif_context*);

/**
 * Returns the IDs for each of the tracks stored in the HEIF file.
 * The output array must have heif_context_number_of_sequence_tracks() entries.
 */
LIBHEIF_API
void heif_context_get_track_ids(const struct heif_context* ctx, uint32_t out_track_id_array[]);

/**
 * Get the ID of the passed track.
 * The track ID will never be 0.
 */
LIBHEIF_API
uint32_t heif_track_get_id(const struct heif_track* track);

/**
 * Get the heif_track object for the given track ID.
 * If you pass `id=0`, the first visual track will be returned.
 * If there is no track with the given ID or if 0 is passed and there is no visual track, NULL will be returned.
 *
 * @note Tracks never have a zero ID. This is why we can use this as a special value to find the first visual track.
 *
 * @param id Track id or 0 for the first visual track.
 * @return heif_track object. You must free this after use.
 */
// Use id=0 for the first visual track.
LIBHEIF_API
struct heif_track* heif_context_get_track(const struct heif_context*, uint32_t id);


typedef uint32_t heif_track_type;

enum heif_track_type_4cc {
  heif_track_type_video = heif_fourcc('v', 'i', 'd', 'e'),
  heif_track_type_image_sequence = heif_fourcc('p', 'i', 'c', 't'),
  heif_track_type_metadata = heif_fourcc('m', 'e', 't', 'a')
};

/**
 * Get the four-cc track handler type.
 * Typical codes are "vide" for video sequences, "pict" for image sequences, "meta" for metadata tracks.
 * These are defined in heif_track_type_4cc, but files may also contain other types.
 *
 * @return four-cc handler type
 */
LIBHEIF_API
heif_track_type heif_track_get_track_handler_type(struct heif_track*);

/**
 * Get the timescale (clock ticks per second) for this track.
 * Note that this can be different from the timescale used at sequence level.
 *
 * @return clock ticks per second
 */
LIBHEIF_API
uint32_t heif_track_get_timescale(struct heif_track*);


// --- reading visual tracks

/**
 * Get the image resolution of the track.
 * If the passed track is no visual track, an error is returned.
 */
LIBHEIF_API
struct heif_error heif_track_get_image_resolution(heif_track*, uint16_t* out_width, uint16_t* out_height);

/**
 * Decode the next image in the passed sequence track.
 * If there is no more image in the sequence, `heif_error_End_of_sequence` is returned.
 * The parameters `colorspace`, `chroma` and `options` are similar to heif_decode_image().
 * If you want to let libheif decide the output colorspace and chroma, set these parameters
 * to heif_colorspace_undefined / heif_chroma_undefined. Usually, libheif will return the
 * image in the input colorspace, but it may also modify it for example when it has to rotate the image.
 * If you want to get the image in a specific colorspace/chroma format, you can specify this
 * and libheif will convert the image to match this format.
 */
LIBHEIF_API
struct heif_error heif_track_decode_next_image(struct heif_track* track,
                                               struct heif_image** out_img,
                                               enum heif_colorspace colorspace,
                                               enum heif_chroma chroma,
                                               const struct heif_decoding_options* options);

/**
 * Get the image display duration in clock ticks of this track.
 * Make sure to use the timescale of the track and not the timescale of the total sequence.
 */
LIBHEIF_API
uint32_t heif_image_get_duration(const heif_image*);


// --- reading metadata track samples

/**
 * Get the "sample entry type" of the first sample sample cluster in the track.
 * In the case of metadata tracks, this will usually be "urim" for "URI Meta Sample Entry".
 * The exact URI can then be obtained with 'heif_track_get_urim_sample_entry_uri_of_first_cluster'.
 */
LIBHEIF_API
uint32_t heif_track_get_sample_entry_type_of_first_cluster(struct heif_track*);

/**
 * Get the URI of the first sample cluster in an 'urim' track.
 * Only call this for tracks with 'urim' sample entry types. It will return an error otherwise.
 *
 * @param out_uri A string with the URI will be returned. Free this string with `heif_string_release()`.
 */
LIBHEIF_API
heif_error heif_track_get_urim_sample_entry_uri_of_first_cluster(struct heif_track* track, const char** out_uri);

/**
 * Free a string returned by libheif in various API functions.
 * You may pass NULL.
 */
LIBHEIF_API
void heif_string_release(const char*);


/** Sequence sample object that can hold any raw byte data.
 * Use this to store and read raw metadata samples.
 */
struct heif_raw_sequence_sample;

/**
 * Get the next raw sample from the (metadata) sequence track.
 * You have to free the returned sample with heif_raw_sequence_sample_release().
 */
LIBHEIF_API
struct heif_error heif_track_get_next_raw_sequence_sample(struct heif_track*,
                                                          heif_raw_sequence_sample** out_sample);

/**
 * Release a heif_raw_sequence_sample object.
 * You may pass NULL.
 */
LIBHEIF_API
void heif_raw_sequence_sample_release(const heif_raw_sequence_sample*);

/**
 * Get a pointer to the data of the (metadata) sample.
 * The data pointer stays valid until the heif_raw_sequence_sample object is released.
 *
 * @param out_array_size Size of the returned array (may be NULL).
 */
LIBHEIF_API
const uint8_t* heif_raw_sequence_sample_get_data(const heif_raw_sequence_sample*, size_t* out_array_size);

/**
 * Return the size of the raw data contained in the sample.
 * This is the same as returned through the 'out_array_size' parameter of 'heif_raw_sequence_sample_get_data()'.
 */
LIBHEIF_API
size_t heif_raw_sequence_sample_get_data_size(const heif_raw_sequence_sample*);

/**
 * Get the sample duration in clock ticks of this track.
 * Make sure to use the timescale of the track and not the timescale of the total sequence.
 */
LIBHEIF_API
uint32_t heif_raw_sequence_sample_get_duration(const heif_raw_sequence_sample*);


// --- writing sequences

/**
 * Set an independent global timescale for the sequence.
 * If no timescale is set with this function, the timescale of the first track will be used.
 */
LIBHEIF_API
void heif_context_set_sequence_timescale(heif_context*, uint32_t);


/**
 * Specifies whether a 'sample auxiliary info' is stored with the samples.
 * The difference between `heif_sample_aux_info_presence_optional` and `heif_sample_aux_info_presence_mandatory`
 * is that `heif_sample_aux_info_presence_mandatory` will throw an error if the data is missing when writing a sample.
 */
enum heif_sample_aux_info_presence {
  heif_sample_aux_info_presence_none = 0,
  heif_sample_aux_info_presence_optional = 1,
  heif_sample_aux_info_presence_mandatory = 2
};


/**
 * This structure specifies what will be written in a track and how it will be laid out in the file.
 */
struct heif_track_info
{
  uint8_t version;

  // --- version 1

  // Timescale (clock ticks per second) for this track.
  uint32_t track_timescale;

  // If 'true', the aux_info data blocks will be interleaved with the compressed image.
  // This has the advantage that the aux_info is localized near the image data.
  //
  // If 'false', all aux_info will be written as one block after the compressed image data.
  // This has the advantage that no aux_info offsets have to be written.
  uint8_t write_aux_info_interleaved; // bool


  // --- TAI timestamps for samples
  enum heif_sample_aux_info_presence with_tai_timestamps;
  struct heif_tai_clock_info* tai_clock_info;

  // --- GIMI content IDs for samples

  // TODO: should this be in an extension API as it is not in the HEIF standard?
  enum heif_sample_aux_info_presence with_sample_content_ids;

  // --- GIMI content ID for the track

  // TODO: should this be in an extension API as it is not in the HEIF standard?
  uint8_t with_gimi_track_content_id;
  const char* gimi_track_content_id;
};

/**
 * Allocate a heif_track_info structure and initialize it with the default values.
 */
LIBHEIF_API
struct heif_track_info* heif_track_info_alloc();

/**
 * Release heif_track_info structure. You may pass NULL.
 */
LIBHEIF_API
void heif_track_info_release(struct heif_track_info*);


// --- writing visual tracks

/**
 * Add a visual track to the sequence.
 * The track ID is assigned automatically.
 *
 * @param width Image resolution width
 * @param height Image resolution height
 * @param track_info
 * @param track_type Has to be heif_track_type_video or heif_track_type_image_sequence
 * @param out_track Output parameter to receive the track object for the just created track.
 * @return
 */
LIBHEIF_API
struct heif_error heif_context_add_visual_sequence_track(heif_context*,
                                                         uint16_t width, uint16_t height,
                                                         struct heif_track_info* info,
                                                         heif_track_type track_type,
                                                         heif_track** out_track);

/**
 * Set the image display duration in the track's timescale units.
 */
LIBHEIF_API
void heif_image_set_duration(heif_image*, uint32_t duration);

/**
 * Encode the image into a visual track.
 * If the passed track is no visual track, an error will be returned.
 */
LIBHEIF_API
struct heif_error heif_track_encode_sequence_image(struct heif_track*,
                                                   const struct heif_image* image,
                                                   struct heif_encoder* encoder,
                                                   const struct heif_encoding_options* options);

// --- metadata tracks

/**
 * Add a metadata track.
 * The track content type is specified by the 'uri' parameter.
 * This will be created as a 'urim' "URI Meta Sample Entry".
 */
LIBHEIF_API
struct heif_error heif_context_add_uri_metadata_sequence_track(heif_context*,
                                                               struct heif_track_info* info,
                                                               const char* uri,
                                                               heif_track** out_track);

/**
 * Allocate a new heif_raw_sequence_sample object.
 * Free with heif_raw_sequence_sample_release().
 */
LIBHEIF_API
heif_raw_sequence_sample* heif_raw_sequence_sample_alloc();

/**
 * Set the raw sequence sample data.
 */
LIBHEIF_API
heif_error heif_raw_sequence_sample_set_data(heif_raw_sequence_sample*, const uint8_t* data, size_t size);

/**
 * Set the sample duration in track timescale units.
 */
LIBHEIF_API
void heif_raw_sequence_sample_set_duration(heif_raw_sequence_sample*, uint32_t duration);

/**
 * Add a raw sequence sample (usually a metadata sample) to the (metadata) track.
 */
LIBHEIF_API
struct heif_error heif_track_add_raw_sequence_sample(struct heif_track*,
                                                     const heif_raw_sequence_sample*);


// --- sample auxiliary data

/**
 * Contains the type of sample auxiliary data assigned to the track samples.
 */
struct heif_sample_aux_info_type
{
  uint32_t type;
  uint32_t parameter;
};

/**
 * Returns how many different types of sample auxiliary data units are assigned to this track's samples.
 */
LIBHEIF_API
int heif_track_get_number_of_sample_aux_infos(struct heif_track*);

/**
 * Get get the list of sample auxiliary data types used in the track.
 * The passed array has to have heif_track_get_number_of_sample_aux_infos() entries.
 */
LIBHEIF_API
void heif_track_get_sample_aux_info_types(struct heif_track*, struct heif_sample_aux_info_type out_types[]);


// --- GIMI content IDs

/**
 * Get the GIMI content ID for the track (as a whole).
 * If there is no content ID, nullptr is returned.
 *
 * @return The returned string has to be released with `heif_string_release()`.
 */
LIBHEIF_API
const char* heif_track_get_gimi_track_content_id(const struct heif_track*);

/**
 * Get the GIMI content ID stored in the image sample.
 * If there is no content ID, NULL is returned.
 * @return
 */
LIBHEIF_API
const char* heif_image_get_gimi_sample_content_id(const heif_image*);

/**
 * Get the GIMI content ID stored in the metadata sample.
 * If there is no content ID, NULL is returned.
 * @return
 */
LIBHEIF_API
const char* heif_raw_sequence_sample_get_gimi_sample_content_id(const heif_raw_sequence_sample*);

/**
 * Set the GIMI content ID for an image sample. It will be stored as SAI.
 * When passing NULL, a previously set ID will be removed.
 */
LIBHEIF_API
void heif_image_set_gimi_sample_content_id(heif_image*, const char* contentID);

/**
 * Set the GIMI content ID for a (metadata) sample. It will be stored as SAI.
 * When passing NULL, a previously set ID will be removed.
 */
LIBHEIF_API
void heif_raw_sequence_sample_set_gimi_sample_content_id(heif_raw_sequence_sample*, const char* contentID);


// --- TAI timestamps

/**
 * Returns whether the raw (metadata) sample has a TAI timestamp attached to it (stored as SAI).
 *
 * @return boolean flag whether a TAI exists for this sample.
 */
LIBHEIF_API
int heif_raw_sequence_sample_has_tai_timestamp(const struct heif_raw_sequence_sample*);

/**
 * Get the TAI timestamp of the (metadata) sample.
 * If there is no timestamp assigned to it, NULL will be returned.
 *
 * @note You should NOT free the returned timestamp with 'heif_tai_timestamp_packet_release()'.
 *       The returned struct stays valid until the heif_raw_sequence_sample is released.
 */
LIBHEIF_API
const struct heif_tai_timestamp_packet* heif_raw_sequence_sample_get_tai_timestamp(const struct heif_raw_sequence_sample*);

/**
 * Set the TAI timestamp for a raw sequence sample.
 * The timestamp will be copied, you can release it after calling this function.
 */
LIBHEIF_API
void heif_raw_sequence_sample_set_tai_timestamp(struct heif_raw_sequence_sample* sample,
                                                const struct heif_tai_timestamp_packet* timestamp);

/**
 * Returns the TAI clock info of the track.
 * If there is no TAI clock info, NULL is returned.
 * You should NOT free the returned heif_tai_clock_info.
 * The structure stays valid until the heif_track object is released.
 */
LIBHEIF_API
const struct heif_tai_clock_info* heif_track_get_tai_clock_info_of_first_cluster(struct heif_track*);


// --- track references

enum heif_track_reference_type {
  heif_track_reference_type_description_of = heif_fourcc('c','d','s','c') // track_description
};

/**
 * Add a reference between tracks.
 * 'reference_type' can be one of the four-cc codes listed in heif_track_reference_type or any other type.
 */
LIBHEIF_API
void heif_track_add_reference_to_track(heif_track*, uint32_t reference_type, heif_track* to_track);

/**
 * Return the number of different reference types used in this track's tref box.
 */
LIBHEIF_API
size_t heif_track_get_number_of_track_reference_types(heif_track*);

/**
 * List the reference types used in this track.
 * The passed array must have heif_track_get_number_of_track_reference_types() entries.
 */
LIBHEIF_API
void heif_track_get_track_reference_types(heif_track*, uint32_t out_reference_types[]);

/**
 * Get the number of references of the passed type.
 */
LIBHEIF_API
size_t heif_track_get_number_of_track_reference_of_type(heif_track*, uint32_t reference_type);

/**
 * List the track ids this track points to with the passed reference type.
 * The passed array must have heif_track_get_number_of_track_reference_of_type() entries.
 */
LIBHEIF_API
size_t heif_track_get_references_from_track(heif_track*, uint32_t reference_type, uint32_t out_to_track_id[]);


#ifdef __cplusplus
}
#endif

#endif
