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

#include "heif_experimental.h"
#include "context.h"
#include "api_structs.h"
#include "file.h"
#include "sequences/track.h"
#include "sequences/track_visual.h"
#include "sequences/track_metadata.h"

#include <array>
#include <cstring>
#include <memory>
#include <vector>
#include <string>


struct heif_property_camera_intrinsic_matrix
{
  Box_cmin::RelativeIntrinsicMatrix matrix;
};

struct heif_error heif_item_get_property_camera_intrinsic_matrix(const struct heif_context* context,
                                                                 heif_item_id itemId,
                                                                 heif_property_id propertyId,
                                                                 struct heif_property_camera_intrinsic_matrix** out_matrix)
{
  if (!out_matrix || !context) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed"};
  }

  auto file = context->context->get_heif_file();

  std::vector<std::shared_ptr<Box>> properties;
  Error err = file->get_properties(itemId, properties);
  if (err) {
    return err.error_struct(context->context.get());
  }

  if (propertyId < 1 || propertyId - 1 >= properties.size()) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "property index out of range"};
  }

  auto cmin = std::dynamic_pointer_cast<Box_cmin>(properties[propertyId - 1]);
  if (!cmin) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "wrong property type"};
  }

  *out_matrix = new heif_property_camera_intrinsic_matrix;
  (*out_matrix)->matrix = cmin->get_intrinsic_matrix();

  return heif_error_success;
}


void heif_property_camera_intrinsic_matrix_release(struct heif_property_camera_intrinsic_matrix* matrix)
{
  delete matrix;
}

struct heif_error heif_property_camera_intrinsic_matrix_get_focal_length(const struct heif_property_camera_intrinsic_matrix* matrix,
                                                                int image_width, int image_height,
                                                                double* out_focal_length_x,
                                                                double* out_focal_length_y)
{
  if (!matrix) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed as matrix"};
  }

  double fx, fy;
  matrix->matrix.compute_focal_length(image_width, image_height, fx, fy);

  if (out_focal_length_x) *out_focal_length_x = fx;
  if (out_focal_length_y) *out_focal_length_y = fy;

  return heif_error_success;
}


struct heif_error heif_property_camera_intrinsic_matrix_get_principal_point(const struct heif_property_camera_intrinsic_matrix* matrix,
                                                                   int image_width, int image_height,
                                                                   double* out_principal_point_x,
                                                                   double* out_principal_point_y)
{
  if (!matrix) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed as matrix"};
  }

  double px, py;
  matrix->matrix.compute_principal_point(image_width, image_height, px, py);

  if (out_principal_point_x) *out_principal_point_x = px;
  if (out_principal_point_y) *out_principal_point_y = py;

  return heif_error_success;
}


struct heif_error heif_property_camera_intrinsic_matrix_get_skew(const struct heif_property_camera_intrinsic_matrix* matrix,
                                                        double* out_skew)
{
  if (!matrix || !out_skew) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed"};
  }

  *out_skew = matrix->matrix.skew;

  return heif_error_success;
}


struct heif_property_camera_intrinsic_matrix* heif_property_camera_intrinsic_matrix_alloc()
{
  return new heif_property_camera_intrinsic_matrix;
}

void heif_property_camera_intrinsic_matrix_set_simple(struct heif_property_camera_intrinsic_matrix* matrix,
                                             int image_width, int image_height,
                                             double focal_length, double principal_point_x, double principal_point_y)
{
  if (!matrix) {
    return;
  }

  matrix->matrix.is_anisotropic = false;
  matrix->matrix.focal_length_x = focal_length / image_width;
  matrix->matrix.principal_point_x = principal_point_x / image_width;
  matrix->matrix.principal_point_y = principal_point_y / image_height;
}

void heif_property_camera_intrinsic_matrix_set_full(struct heif_property_camera_intrinsic_matrix* matrix,
                                           int image_width, int image_height,
                                           double focal_length_x,
                                           double focal_length_y,
                                           double principal_point_x, double principal_point_y,
                                           double skew)
{
  if (!matrix) {
    return;
  }

  if (focal_length_x == focal_length_y && skew == 0) {
    heif_property_camera_intrinsic_matrix_set_simple(matrix, image_width, image_height, focal_length_x, principal_point_x, principal_point_y);
    return;
  }

  matrix->matrix.is_anisotropic = true;
  matrix->matrix.focal_length_x = focal_length_x / image_width;
  matrix->matrix.focal_length_y = focal_length_y / image_width;
  matrix->matrix.principal_point_x = principal_point_x / image_width;
  matrix->matrix.principal_point_y = principal_point_y / image_height;
  matrix->matrix.skew = skew;
}


struct heif_error heif_item_add_property_camera_intrinsic_matrix(const struct heif_context* context,
                                                                 heif_item_id itemId,
                                                                 const struct heif_property_camera_intrinsic_matrix* matrix,
                                                                 heif_property_id* out_propertyId)
{
  if (!context || !matrix) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "NULL passed"};
  }

  auto cmin = std::make_shared<Box_cmin>();
  cmin->set_intrinsic_matrix(matrix->matrix);

  heif_property_id id = context->context->add_property(itemId, cmin, false);

  if (out_propertyId) {
    *out_propertyId = id;
  }

  return heif_error_success;
}


struct heif_property_camera_extrinsic_matrix
{
  Box_cmex::ExtrinsicMatrix matrix;
};


struct heif_error heif_item_get_property_camera_extrinsic_matrix(const struct heif_context* context,
                                                                 heif_item_id itemId,
                                                                 heif_property_id propertyId,
                                                                 struct heif_property_camera_extrinsic_matrix** out_matrix)
{
  if (!out_matrix || !context) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed"};
  }

  auto file = context->context->get_heif_file();

  std::vector<std::shared_ptr<Box>> properties;
  Error err = file->get_properties(itemId, properties);
  if (err) {
    return err.error_struct(context->context.get());
  }

  if (propertyId < 1 || propertyId - 1 >= properties.size()) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "property index out of range"};
  }

  auto cmex = std::dynamic_pointer_cast<Box_cmex>(properties[propertyId - 1]);
  if (!cmex) {
    return {heif_error_Usage_error, heif_suberror_Invalid_property, "wrong property type"};
  }

  *out_matrix = new heif_property_camera_extrinsic_matrix;
  (*out_matrix)->matrix = cmex->get_extrinsic_matrix();

  return heif_error_success;
}


void heif_property_camera_extrinsic_matrix_release(struct heif_property_camera_extrinsic_matrix* matrix)
{
  delete matrix;
}


struct heif_error heif_property_camera_extrinsic_matrix_get_rotation_matrix(const struct heif_property_camera_extrinsic_matrix* matrix,
                                                                   double* out_matrix)
{
  if (!matrix || !out_matrix) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed"};
  }

  auto rot_matrix = matrix->matrix.calculate_rotation_matrix();
  for (int i = 0; i < 9; i++) {
    out_matrix[i] = rot_matrix[i];
  }

  return heif_error_success;
}


struct heif_error heif_property_camera_extrinsic_matrix_get_position_vector(const struct heif_property_camera_extrinsic_matrix* matrix,
                                                                   int32_t* out_vector)
{
  if (!matrix || !out_vector) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed"};
  }

  out_vector[0] = matrix->matrix.pos_x;
  out_vector[1] = matrix->matrix.pos_y;
  out_vector[2] = matrix->matrix.pos_z;

  return heif_error_success;
}


struct heif_error heif_property_camera_extrinsic_matrix_get_world_coordinate_system_id(const struct heif_property_camera_extrinsic_matrix* matrix,
                                                                              uint32_t* out_wcs_id)
{
  if (!matrix || !out_wcs_id) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "NULL passed"};
  }

  *out_wcs_id = matrix->matrix.world_coordinate_system_id;

  return heif_error_success;
}


int heif_context_has_sequence(heif_context* ctx)
{
  return ctx->context->has_sequence();
}


extern void fill_default_decoding_options(heif_decoding_options& options);


struct heif_error heif_track_decode_next_image(struct heif_track* track_ptr,
                                               struct heif_image** out_img,
                                               enum heif_colorspace colorspace,
                                               enum heif_chroma chroma,
                                               const struct heif_decoding_options* options)
{
  if (out_img == nullptr) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "Output image pointer is NULL."};
  }

  // --- get the visual track

  auto track = track_ptr->track;

  // --- reached end of sequence ?

  if (track->end_of_sequence_reached()) {
    *out_img = nullptr;
    return {heif_error_End_of_sequence, heif_suberror_Unspecified, "End of sequence"};
  }

  // --- decode next sequence image

  const heif_decoding_options* opts = options;
  heif_decoding_options default_options;

  if (!opts) {
    fill_default_decoding_options(default_options);
    opts = &default_options;
  }

  auto visual_track = std::dynamic_pointer_cast<Track_Visual>(track);
  if (!visual_track) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "Cannot get image from non-visual track."};
  }

  auto decodingResult = visual_track->decode_next_image_sample(*opts);
  if (!decodingResult) {
    return decodingResult.error.error_struct(track_ptr->context.get());
  }

  std::shared_ptr<HeifPixelImage> img = *decodingResult;


  // --- convert to output colorspace

  auto conversion_result = track_ptr->context->convert_to_output_colorspace(img, colorspace, chroma, *opts);
  if (conversion_result.error) {
    return conversion_result.error.error_struct(track_ptr->context.get());
  }
  else {
    img = *conversion_result;
  }

  *out_img = new heif_image();
  (*out_img)->image = std::move(img);

  return {};
}


struct heif_error heif_track_get_raw_sequence_sample(struct heif_track* track_ptr,
                                                     heif_raw_sequence_sample** out_sample)
{
  auto track = track_ptr->track;

  // --- reached end of sequence ?

  if (track->end_of_sequence_reached()) {
    return {heif_error_End_of_sequence, heif_suberror_Unspecified, "End of sequence"};
  }

  // --- get next raw sample

  auto decodingResult = track->get_next_sample_raw_data();
  if (!decodingResult) {
    return decodingResult.error.error_struct(track_ptr->context.get());
  }

  *out_sample = decodingResult.value;

  return heif_error_success;
}


void heif_raw_sequence_sample_release(const heif_raw_sequence_sample* sample)
{
  delete sample;
}

const uint8_t* heif_raw_sequence_sample_get_data(const heif_raw_sequence_sample* sample)
{
  return sample->data.data();
}

size_t heif_raw_sequence_sample_get_data_size(const heif_raw_sequence_sample* sample)
{
  return sample->data.size();
}

uint32_t heif_raw_sequence_sample_get_duration(const heif_raw_sequence_sample* sample)
{
  return sample->duration;
}

const char* heif_raw_sequence_sample_get_gimi_sample_content_id(const heif_raw_sequence_sample* sample)
{
  char* s = new char[sample->gimi_sample_content_id.size() + 1];
  strcpy(s, sample->gimi_sample_content_id.c_str());
  return s;
}

int heif_raw_sequence_sample_has_tai_timestamp(const struct heif_raw_sequence_sample* sample)
{
  return sample->timestamp ? 1 : 0;
}

struct heif_error heif_raw_sequence_sample_get_tai_timestamp(const struct heif_raw_sequence_sample* sample,
                                                             struct heif_tai_timestamp_packet* out_timestamp)
{
  if (!sample->timestamp) {
    return {
      heif_error_Usage_error,
      heif_suberror_Unspecified,
      "sample has no TAI timestamp"
    };
  }

  if (out_timestamp) {
    heif_tai_timestamp_packet_copy(out_timestamp, sample->timestamp);
  }

  return heif_error_ok;
}


uint32_t heif_image_get_sample_duration(heif_image* img)
{
  return img->image->get_sample_duration();
}


uint64_t heif_context_get_sequence_timescale(heif_context* ctx)
{
  return ctx->context->get_sequence_timescale();
}

void heif_context_set_sequence_timescale(heif_context* ctx, uint32_t timescale)
{
  ctx->context->set_sequence_timescale(timescale);
}


uint64_t heif_context_get_sequence_duration(heif_context* ctx)
{
  return ctx->context->get_sequence_duration();
}

struct heif_error heif_track_get_image_resolution(heif_track* track_ptr, uint16_t* out_width, uint16_t* out_height)
{
  auto track = track_ptr->track;

  auto visual_track = std::dynamic_pointer_cast<Track_Visual>(track);
  if (!visual_track) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "Cannot get resolution of non-visual track."};
  }

  if (out_width) *out_width = visual_track->get_width();
  if (out_height) *out_height = visual_track->get_height();

  return heif_error_ok;
}


struct heif_error heif_context_add_visual_sequence_track(heif_context* ctx, uint16_t width, uint16_t height,
                                                         struct heif_track_info* info,
                                                         enum heif_track_type track_type,
                                                         heif_track** out_track)
{
  if (track_type != heif_track_type_video &&
      track_type != heif_track_type_image_sequence) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "visual track has to be of type video or image sequence"};
  }

  uint32_t handler_type = static_cast<uint32_t>(track_type);

  Result<std::shared_ptr<Track_Visual>> addResult = ctx->context->add_visual_sequence_track(info, handler_type, width,height);
  if (addResult.error) {
    return addResult.error.error_struct(ctx->context.get());
  }

  if (out_track) {
    auto* track = new heif_track;
    track->track = *addResult;
    track->context = ctx->context;

    *out_track = track;
  }

  return heif_error_ok;
}


struct heif_error heif_context_add_uri_metadata_sequence_track(heif_context* ctx, struct heif_track_info* info,
                                                               const char* uri,
                                                               heif_track** out_track)
{
  Result<std::shared_ptr<Track_Metadata>> addResult = ctx->context->add_uri_metadata_sequence_track(info, uri);
  if (addResult.error) {
    return addResult.error.error_struct(ctx->context.get());
  }

  if (out_track) {
    auto* track = new heif_track;
    track->track = *addResult;
    track->context = ctx->context;

    *out_track = track;
  }

  return heif_error_ok;
}


void heif_track_add_reference_to_track(heif_track* track, uint32_t reference_type, heif_track* to_track)
{
  track->track->add_reference_to_track(reference_type, to_track->track->get_id());
}


void heif_track_release(heif_track* track)
{
  delete track;
}


uint32_t heif_image_get_duration(const heif_image* img)
{
  return img->image->get_sample_duration();
}


void heif_image_set_duration(heif_image* img, uint32_t duration)
{
  img->image->set_sample_duration(duration);
}


void heif_image_set_gimi_sample_content_id(heif_image* img, const char* contentID)
{
  img->image->set_gimi_sample_content_id(contentID);
}


const char* heif_image_get_gimi_sample_content_id(const heif_image* img)
{
  if (!img->image->has_gimi_sample_content_id()) {
    return nullptr;
  }

  auto id_string = img->image->get_gimi_sample_content_id();
  char* id = new char[id_string.length() + 1];
  strcpy(id, id_string.c_str());

  return id;
}


const char* heif_track_get_gimi_track_content_id(const heif_track* track)
{
  const char* contentId = track->track->get_track_info()->gimi_track_content_id;
  if (!contentId) {
    return nullptr;
  }

  char* id = new char[strlen(contentId) + 1];
  strcpy(id, contentId);

  return id;
}

/*
void heif_gimi_content_id_release(const char* id)
{
  delete[] id;
}
*/

extern void set_default_encoding_options(heif_encoding_options& options);
extern void copy_options(heif_encoding_options& options, const heif_encoding_options& input_options);


struct heif_error heif_track_encode_sequence_image(struct heif_track* track,
                                                   const struct heif_image* input_image,
                                                   struct heif_encoder* encoder,
                                                   const struct heif_encoding_options* input_options)
{
  heif_encoding_options options;
  heif_color_profile_nclx nclx;
  set_default_encoding_options(options);
  if (input_options) {
    copy_options(options, *input_options);

    if (options.output_nclx_profile == nullptr) {
      auto input_nclx = input_image->image->get_color_profile_nclx();
      if (input_nclx) {
        options.output_nclx_profile = &nclx;
        nclx.version = 1;
        nclx.color_primaries = (enum heif_color_primaries) input_nclx->get_colour_primaries();
        nclx.transfer_characteristics = (enum heif_transfer_characteristics) input_nclx->get_transfer_characteristics();
        nclx.matrix_coefficients = (enum heif_matrix_coefficients) input_nclx->get_matrix_coefficients();
        nclx.full_range_flag = input_nclx->get_full_range_flag();
      }
    }
  }

  auto visual_track = std::dynamic_pointer_cast<Track_Visual>(track->track);
  if (!visual_track) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "Cannot encode image for non-visual track."};
  }

  auto error = visual_track->encode_image(input_image->image,
                                          encoder,
                                          options,
                                          heif_image_input_class_normal);
  if (error.error_code) {
    return error.error_struct(track->context.get());
  }

  return heif_error_ok;
}


struct heif_error heif_track_add_metadata(struct heif_track* track,
                                          const uint8_t* data, uint32_t length,
                                          uint32_t duration,
                                          const heif_tai_timestamp_packet* timestamp,
                                          const char* gimi_track_content_id)
{
  auto metadata_track = std::dynamic_pointer_cast<Track_Metadata>(track->track);
  if (!metadata_track) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "Cannot save metadata in a non-metadata track."};
  }

  Track_Metadata::Metadata metadata;
  metadata.raw_metadata.resize(length);
  memcpy(metadata.raw_metadata.data(), data, length);
  metadata.duration = duration;
  metadata.timestamp = timestamp;
  if (gimi_track_content_id) {
    metadata.gimi_contentID = gimi_track_content_id;
  }

  auto error = metadata_track->write_raw_metadata(metadata);
  if (error.error_code) {
    return error.error_struct(track->context.get());
  }

  return heif_error_ok;
}


int heif_context_number_of_sequence_tracks(const struct heif_context* ctx)
{
  return ctx->context->get_number_of_tracks();
}

void heif_context_get_track_ids(const struct heif_context* ctx, uint32_t* out_track_id_array)
{
  std::vector<uint32_t> IDs;
  IDs = ctx->context->get_track_IDs();

  for (uint32_t id : IDs) {
    *out_track_id_array++ = id;
  }
}

// Use id=0 for the first visual track.
struct heif_track* heif_context_get_track(const struct heif_context* ctx, int32_t track_id)
{
  auto trackResult = ctx->context->get_track(track_id);
  if (trackResult.error) {
    return nullptr;
  }

  auto* track = new heif_track;
  track->track = trackResult.value;
  track->context = ctx->context;

  return track;
}

uint32_t heif_track_get_handler_type(struct heif_track* track)
{
  return track->track->get_handler();
}

enum heif_track_type heif_track_get_track_type(struct heif_track* track)
{
  uint32_t handler_type = track->track->get_handler();

  switch (handler_type) {
    case heif_track_type_video:
    case heif_track_type_image_sequence:
    case heif_track_type_metadata:
      return static_cast<heif_track_type>(handler_type);

    default:
      return heif_track_type_unknown;
  }
}


uint32_t heif_track_get_sample_entry_type_of_first_cluster(struct heif_track* track)
{
  return track->track->get_first_cluster_sample_entry_type();
}


const char* heif_track_get_urim_sample_entry_uri_of_first_cluster(struct heif_track* track)
{
  std::string uri = track->track->get_first_cluster_urim_uri();

  char* s = new char[uri.size() + 1];
  strncpy(s, uri.c_str(), uri.size());
  s[uri.size()] = '\0';

  return s;
}

void heif_string_release(const char* str)
{
  delete[] str;
}


int heif_track_get_tai_clock_info_of_first_cluster(struct heif_track* track, struct heif_tai_clock_info* taic)
{
  auto first_taic = track->track->get_first_cluster_taic();
  if (!first_taic) {
    return 0;
  }

  first_taic->get_tai_clock_info(taic);
  return 1;
}


int heif_track_get_number_of_sample_aux_infos(struct heif_track* track)
{
  std::vector<heif_sample_aux_info_type> aux_info_types = track->track->get_sample_aux_info_types();
  return (int)aux_info_types.size();
}


void heif_track_get_sample_aux_info_types(struct heif_track* track, struct heif_sample_aux_info_type* out_types)
{
  std::vector<heif_sample_aux_info_type> aux_info_types = track->track->get_sample_aux_info_types();
  for (size_t i=0;i<aux_info_types.size();i++) {
    out_types[i] = aux_info_types[i];
  }
}


struct heif_error heif_image_extract_area(const heif_image* srcimg,
                                          uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                                          const heif_security_limits* limits,
                                          struct heif_image** out_image)
{
  auto extractResult = srcimg->image->extract_image_area(x0,y0,w,h, limits);
  if (extractResult.error) {
    return extractResult.error.error_struct(srcimg->image.get());
  }

  heif_image* area = new heif_image;
  area->image = extractResult.value;

  *out_image = area;

  return heif_error_success;
}

