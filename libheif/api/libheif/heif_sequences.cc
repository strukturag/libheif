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

#include "heif_sequences.h"
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
#include <utility>


int heif_context_has_sequence(const heif_context* ctx)
{
  return ctx->context->has_sequence();
}


uint32_t heif_context_get_sequence_timescale(const heif_context* ctx)
{
  return ctx->context->get_sequence_timescale();
}


uint64_t heif_context_get_sequence_duration(const heif_context* ctx)
{
  return ctx->context->get_sequence_duration();
}


void heif_track_release(heif_track* track)
{
  delete track;
}


int heif_context_number_of_sequence_tracks(const struct heif_context* ctx)
{
  return ctx->context->get_number_of_tracks();
}


void heif_context_get_track_ids(const struct heif_context* ctx, uint32_t out_track_id_array[])
{
  std::vector<uint32_t> IDs;
  IDs = ctx->context->get_track_IDs();

  for (uint32_t id : IDs) {
    *out_track_id_array++ = id;
  }
}


uint32_t heif_track_get_id(const struct heif_track* track)
{
  return track->track->get_id();
}


// Use id=0 for the first visual track.
struct heif_track* heif_context_get_track(const struct heif_context* ctx, uint32_t track_id)
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


uint32_t heif_track_get_track_handler_type(const struct heif_track* track)
{
  return track->track->get_handler();
}


uint32_t heif_track_get_timescale(const struct heif_track* track)
{
  return track->track->get_timescale();
}


struct heif_error heif_track_get_image_resolution(const struct heif_track* track_ptr, uint16_t* out_width, uint16_t* out_height)
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

  std::unique_ptr<heif_decoding_options, void(*)(heif_decoding_options*)> opts(heif_decoding_options_alloc(), heif_decoding_options_free);
  heif_decoding_options_copy(opts.get(), options);


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


uint32_t heif_image_get_duration(const heif_image* img)
{
  return img->image->get_sample_duration();
}


uint32_t heif_track_get_sample_entry_type_of_first_cluster(const struct heif_track* track)
{
  return track->track->get_first_cluster_sample_entry_type();
}


heif_error heif_track_get_urim_sample_entry_uri_of_first_cluster(const struct heif_track* track, const char** out_uri)
{
  Result<std::string> uriResult = track->track->get_first_cluster_urim_uri();

  if (uriResult.error.error_code) {
    return uriResult.error.error_struct(track->context.get());
  }

  if (out_uri) {
    const std::string& uri = uriResult.value;

    char* s = new char[uri.size() + 1];
    strncpy(s, uri.c_str(), uri.size());
    s[uri.size()] = '\0';

    *out_uri = s;
  }

  return heif_error_ok;
}


struct heif_error heif_track_get_next_raw_sequence_sample(struct heif_track* track_ptr,
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


void heif_raw_sequence_sample_release(struct heif_raw_sequence_sample* sample)
{
  delete sample;
}


const uint8_t* heif_raw_sequence_sample_get_data(const heif_raw_sequence_sample* sample, size_t* out_array_size)
{
  if (out_array_size) { *out_array_size = sample->data.size(); }

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


// --- writing sequences


void heif_context_set_sequence_timescale(heif_context* ctx, uint32_t timescale)
{
  ctx->context->set_sequence_timescale(timescale);
}


struct heif_track_options {
  TrackOptions options;
};


heif_track_options* heif_track_options_alloc()
{
  return new heif_track_options;
}


void heif_track_options_release(struct heif_track_options* options)
{
  delete options;
}


void heif_track_options_set_timescale(struct heif_track_options* options, uint32_t timescale)
{
  options->options.track_timescale = timescale;
}


void heif_track_options_set_interleaved_sample_aux_infos(struct heif_track_options* options, int interleaved_flag)
{
  options->options.write_sample_aux_infos_interleaved = (interleaved_flag != 0);
}


struct heif_error heif_track_options_enable_sample_tai_timestamps(struct heif_track_options* options,
                                                                  const struct heif_tai_clock_info* tai_info,
                                                                  enum heif_sample_aux_info_presence presence)
{
  if (presence != heif_sample_aux_info_presence_none &&
      tai_info == nullptr) {
    return {heif_error_Usage_error,
            heif_suberror_Unspecified,
            "NULL tai clock info passed for track with TAI timestamps"};
  }

  options->options.with_sample_tai_timestamps = presence;

  // delete old object in case we are calling heif_track_options_enable_sample_tai_timestamps() multiple times
  delete options->options.tai_clock_info;

  if (tai_info != nullptr) {
    options->options.tai_clock_info = heif_tai_clock_info_alloc();
    heif_tai_clock_info_copy(options->options.tai_clock_info, tai_info);
  }
  else {
    options->options.tai_clock_info = nullptr;
  }

  return heif_error_ok;
}


void heif_track_options_enable_sample_gimi_content_ids(struct heif_track_options* options,
                                                       enum heif_sample_aux_info_presence presence)
{
  options->options.with_sample_content_ids = presence;
}


void heif_track_options_set_gimi_track_id(struct heif_track_options* options,
                                          const char* track_id)
{
  if (track_id == nullptr) {
    options->options.gimi_track_content_id.clear();
    return;
  }

  options->options.gimi_track_content_id = track_id;
}


heif_sequence_encoding_options* heif_sequence_encoding_options_alloc()
{
  heif_sequence_encoding_options* options = new heif_sequence_encoding_options();

  options->version = 1;
  options->output_nclx_profile = nullptr;

  options->color_conversion_options.version = 1;
  options->color_conversion_options.preferred_chroma_downsampling_algorithm = heif_chroma_downsampling_average;
  options->color_conversion_options.preferred_chroma_upsampling_algorithm = heif_chroma_upsampling_bilinear;
  options->color_conversion_options.only_use_preferred_chroma_algorithm = false;

  return options;
}


void heif_sequence_encoding_options_release(heif_sequence_encoding_options* options)
{
  delete options;
}


struct heif_error heif_context_add_visual_sequence_track(heif_context* ctx,
                                                         uint16_t width, uint16_t height,
                                                         heif_track_type track_type,
                                                         const struct heif_track_options* track_options,
                                                         const struct heif_sequence_encoding_options* encoding_options,
                                                         heif_track** out_track)
{
  if (track_type != heif_track_type_video &&
      track_type != heif_track_type_image_sequence) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "visual track has to be of type video or image sequence"};
  }

  TrackOptions default_track_info;
  const TrackOptions* track_info = &default_track_info;
  if (track_options != nullptr) {
    track_info = &track_options->options;
  }

  Result<std::shared_ptr<Track_Visual>> addResult = ctx->context->add_visual_sequence_track(track_info, track_type, width,height);
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


void heif_image_set_duration(heif_image* img, uint32_t duration)
{
  img->image->set_sample_duration(duration);
}


struct heif_error heif_track_encode_sequence_image(struct heif_track* track,
                                                   const struct heif_image* input_image,
                                                   struct heif_encoder* encoder,
                                                   const struct heif_sequence_encoding_options* sequence_encoding_options)
{
  // the input track must be a visual track

  auto visual_track = std::dynamic_pointer_cast<Track_Visual>(track->track);
  if (!visual_track) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "Cannot encode image for non-visual track."};
  }


  // convert heif_sequence_encoding_options to heif_encoding_options that is used by track->encode_image()

  heif_encoding_options* encoding_options = heif_encoding_options_alloc();
  heif_color_profile_nclx nclx;
  if (sequence_encoding_options) {
    // the const_cast<> is ok, because output_nclx_profile will not be changed. It should actually be const, but we cannot change that.
    encoding_options->output_nclx_profile = const_cast<heif_color_profile_nclx*>(sequence_encoding_options->output_nclx_profile);
    encoding_options->color_conversion_options = sequence_encoding_options->color_conversion_options;

    if (encoding_options->output_nclx_profile == nullptr) {
      auto input_nclx = input_image->image->get_color_profile_nclx();
      if (input_nclx) {
        encoding_options->output_nclx_profile = &nclx;
        nclx.version = 1;
        nclx.color_primaries = (enum heif_color_primaries) input_nclx->get_colour_primaries();
        nclx.transfer_characteristics = (enum heif_transfer_characteristics) input_nclx->get_transfer_characteristics();
        nclx.matrix_coefficients = (enum heif_matrix_coefficients) input_nclx->get_matrix_coefficients();
        nclx.full_range_flag = input_nclx->get_full_range_flag();
      }
    }
  }

  // encode the image

  auto error = visual_track->encode_image(input_image->image,
                                          encoder,
                                          *encoding_options,
                                          heif_image_input_class_normal);
  heif_encoding_options_free(encoding_options);

  if (error.error_code) {
    return error.error_struct(track->context.get());
  }

  return heif_error_ok;
}


struct heif_error heif_context_add_uri_metadata_sequence_track(heif_context* ctx,
                                                               const char* uri,
                                                               const struct heif_track_options* track_options,
                                                               heif_track** out_track)
{
  struct TrackOptions default_track_info;
  const struct TrackOptions* track_info = &default_track_info;
  if (track_options != nullptr) {
    track_info = &track_options->options;
  }

  Result<std::shared_ptr<Track_Metadata>> addResult = ctx->context->add_uri_metadata_sequence_track(track_info, uri);
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


heif_raw_sequence_sample* heif_raw_sequence_sample_alloc()
{
  return new heif_raw_sequence_sample();
}


heif_error heif_raw_sequence_sample_set_data(heif_raw_sequence_sample* sample, const uint8_t* data, size_t size)
{
  // TODO: do we have to check the vector memory allocation?

  sample->data.clear();
  sample->data.insert(sample->data.begin(), data, data + size);

  return heif_error_ok;
}


void heif_raw_sequence_sample_set_duration(heif_raw_sequence_sample* sample, uint32_t duration)
{
  sample->duration = duration;
}


struct heif_error heif_track_add_raw_sequence_sample(struct heif_track* track,
                                                     const struct heif_raw_sequence_sample* sample)
{
  auto metadata_track = std::dynamic_pointer_cast<Track_Metadata>(track->track);
  if (!metadata_track) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "Cannot save metadata in a non-metadata track."};
  }

  auto error = metadata_track->write_raw_metadata(sample);
  if (error.error_code) {
    return error.error_struct(track->context.get());
  }

  return heif_error_ok;
}


int heif_track_get_number_of_sample_aux_infos(const struct heif_track* track)
{
  std::vector<heif_sample_aux_info_type> aux_info_types = track->track->get_sample_aux_info_types();
  return (int)aux_info_types.size();
}


void heif_track_get_sample_aux_info_types(const struct heif_track* track, struct heif_sample_aux_info_type out_types[])
{
  std::vector<heif_sample_aux_info_type> aux_info_types = track->track->get_sample_aux_info_types();
  for (size_t i=0;i<aux_info_types.size();i++) {
    out_types[i] = aux_info_types[i];
  }
}


const char* heif_track_get_gimi_track_content_id(const heif_track* track)
{
  std::string contentId = track->track->get_track_info().gimi_track_content_id;
  if (contentId.empty()) {
    return nullptr;
  }

  char* id = new char[contentId.length() + 1];
  strcpy(id, contentId.c_str());

  return id;
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


const char* heif_raw_sequence_sample_get_gimi_sample_content_id(const heif_raw_sequence_sample* sample)
{
  char* s = new char[sample->gimi_sample_content_id.size() + 1];
  strcpy(s, sample->gimi_sample_content_id.c_str());
  return s;
}


void heif_image_set_gimi_sample_content_id(heif_image* img, const char* contentID)
{
  if (contentID) {
    img->image->set_gimi_sample_content_id(contentID);
  }
  else {
    img->image->set_gimi_sample_content_id({});
  }
}


void heif_raw_sequence_sample_set_gimi_sample_content_id(heif_raw_sequence_sample* sample, const char* contentID)
{
  if (contentID) {
    sample->gimi_sample_content_id = contentID;
  }
  else {
    sample->gimi_sample_content_id.clear();
  }
}


int heif_raw_sequence_sample_has_tai_timestamp(const struct heif_raw_sequence_sample* sample)
{
  return sample->timestamp ? 1 : 0;
}


const struct heif_tai_timestamp_packet* heif_raw_sequence_sample_get_tai_timestamp(const struct heif_raw_sequence_sample* sample)
{
  if (!sample->timestamp) {
    return nullptr;
  }

  return sample->timestamp;
}


void heif_raw_sequence_sample_set_tai_timestamp(struct heif_raw_sequence_sample* sample,
                                                const struct heif_tai_timestamp_packet* timestamp)
{
  // release of timestamp in case we overwrite it
  heif_tai_timestamp_packet_release(sample->timestamp);

  sample->timestamp = heif_tai_timestamp_packet_alloc();
  heif_tai_timestamp_packet_copy(sample->timestamp, timestamp);
}


const struct heif_tai_clock_info* heif_track_get_tai_clock_info_of_first_cluster(struct heif_track* track)
{
  auto first_taic = track->track->get_first_cluster_taic();
  if (!first_taic) {
    return nullptr;
  }

  return first_taic->get_tai_clock_info();
}


void heif_track_add_reference_to_track(heif_track* track, uint32_t reference_type, const heif_track* to_track)
{
  track->track->add_reference_to_track(reference_type, to_track->track->get_id());
}


size_t heif_track_get_number_of_track_reference_types(const heif_track* track)
{
  auto tref = track->track->get_tref_box();
  if (!tref) {
    return 0;
  }

  return tref->get_number_of_reference_types();
}


void heif_track_get_track_reference_types(const heif_track* track, uint32_t out_reference_types[])
{
  auto tref = track->track->get_tref_box();
  if (!tref) {
    return;
  }

  auto refTypes = tref->get_reference_types();
  for (size_t i = 0; i < refTypes.size(); i++) {
    out_reference_types[i] = refTypes[i];
  }
}


size_t heif_track_get_number_of_track_reference_of_type(const heif_track* track, uint32_t reference_type)
{
  auto tref = track->track->get_tref_box();
  if (!tref) {
    return 0;
  }

  return tref->get_number_of_references_of_type(reference_type);
}


size_t heif_track_get_references_from_track(const heif_track* track, uint32_t reference_type, uint32_t out_to_track_id[])
{
  auto tref = track->track->get_tref_box();
  if (!tref) {
    return 0;
  }

  auto refs = tref->get_references(reference_type);
  for (size_t i = 0; i < refs.size(); i++) {
    out_to_track_id[i] = refs[i];
  }

  return refs.size();
}


size_t heif_track_find_referring_tracks(const heif_track* track, uint32_t reference_type, uint32_t out_track_id[], size_t array_size)
{
  size_t nFound = 0;

  // iterate through all tracks

  auto trackIDs = track->context->get_track_IDs();
  for (auto id : trackIDs) {
    // a track should never reference itself
    if (id == track->track->get_id()) {
      continue;
    }

    // get the other track object

    auto other_trackResult = track->context->get_track(id);
    if (other_trackResult.error) {
      continue; // TODO: should we return an error in this case?
    }

    auto other_track = other_trackResult.value;

    // get the references of the other track

    auto tref = other_track->get_tref_box();
    if (!tref) {
      continue;
    }

    // if the other track has a reference that points to the current track, add the other track to the list

    std::vector<uint32_t> refs = tref->get_references(reference_type);
    for (uint32_t to_track : refs) {
      if (to_track == track->track->get_id() && nFound < array_size) {
        out_track_id[nFound++] = other_track->get_id();
        break;
      }
    }

    // quick exit path
    if (nFound == array_size)
      break;
  }

  return nFound;
}
