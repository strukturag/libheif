/*
 * HEIF JPEG 2000 codec.
 * Copyright (c) 2023 Devon Sookhoo <devonsookhoo14gmail.com>
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

#include "libheif/heif.h"
#include "libheif/heif_plugin.h"
#include "encoder_openjpeg.h"

#include <openjpeg.h>
#include <string.h>

#include <vector>
#include <string>
using namespace std;


static const int OPJ_PLUGIN_PRIORITY = 80;
static struct heif_error error_Ok = {heif_error_Ok, heif_suberror_Unspecified, "Success"};

struct encoder_struct_opj {

  heif_chroma chroma;

  std::vector<uint8_t> codestream; //contains encoded pixel data
  bool data_read = false;

};


const char* opj_plugin_name() {
    return "OpenJPEG JPEG2000 Encoder"; // Human-readable name of the plugin
}

void opj_init_plugin() {
}

void opj_cleanup_plugin() {
}

struct heif_error opj_new_encoder(void** encoder_out) {
  struct encoder_struct_opj* encoder = new encoder_struct_opj();
  encoder->chroma = heif_chroma_interleaved_RGB; //default chroma

  *encoder_out = encoder;
  return error_Ok;
}

void opj_free_encoder(void* encoder_raw) {
  struct encoder_struct_opj* encoder = (struct encoder_struct_opj*) encoder_raw;
  delete encoder;
}

struct heif_error opj_set_parameter_quality(void* encoder, int quality) {
  return error_Ok;
}

struct heif_error opj_get_parameter_quality(void* encoder, int* quality) {
  return error_Ok;
}

struct heif_error opj_set_parameter_lossless(void* encoder, int lossless) {
  return error_Ok;
}

struct heif_error opj_get_parameter_lossless(void* encoder, int* lossless) {
  return error_Ok;
}

struct heif_error opj_set_parameter_logging_level(void* encoder, int logging) {
  return error_Ok;
}

struct heif_error opj_get_parameter_logging_level(void* encoder, int* logging) {
  return error_Ok;
}

const struct heif_encoder_parameter** opj_list_parameters(void* encoder) {
  return nullptr;
}

struct heif_error opj_set_parameter_integer(void* encoder, const char* name, int value) {
  return error_Ok;
}

struct heif_error opj_get_parameter_integer(void* encoder, const char* name, int* value) {
  return error_Ok;
}

struct heif_error opj_set_parameter_boolean(void* encoder, const char* name, int value) {
  return error_Ok;
}

struct heif_error opj_get_parameter_boolean(void* encoder, const char* name, int* value) {
  return error_Ok;
}

struct heif_error opj_set_parameter_string(void* encoder, const char* name, const char* value) {
  return error_Ok;
}

struct heif_error opj_get_parameter_string(void* encoder, const char* name, char* value, int value_size) {
  return error_Ok;
}

void opj_query_input_colorspace(enum heif_colorspace* inout_colorspace, enum heif_chroma* inout_chroma) {
}



struct heif_error opj_encode_image(void* encoder_raw, const struct heif_image* image, enum heif_image_input_class image_class) {
  return heif_error {heif_error_Unsupported_feature, heif_suberror_Unsupported_codec, "JPEG2000 Encoding has not been implemented yet"};

}

struct heif_error opj_get_compressed_data(void* encoder_raw, uint8_t** data, int* size, enum heif_encoded_data_type* type) {
  return heif_error {heif_error_Unsupported_feature, heif_suberror_Unsupported_codec, "JPEG2000 Encoding has not been implemented yet"};
}

void opj_query_input_colorspace2(void* encoder, enum heif_colorspace* inout_colorspace, enum heif_chroma* inout_chroma) {
  *inout_colorspace = heif_colorspace_RGB;
  *inout_chroma = heif_chroma_interleaved_RGB;
}

void opj_query_encoded_size(void* encoder, uint32_t input_width, uint32_t input_height, uint32_t* encoded_width, uint32_t* encoded_height) {
}



static const struct heif_encoder_plugin encoder_plugin_openjpeg
    {
        /* plugin_api_version */ 3,
        /* compression_format */ heif_compression_JPEG2000,
        /* id_name */ "OpenJPEG",
        /* priority */ OPJ_PLUGIN_PRIORITY,
        /* supports_lossy_compression */ false,
        /* supports_lossless_compression */ true,
        /* get_plugin_name */ opj_plugin_name,
        /* init_plugin */ opj_init_plugin,
        /* cleanup_plugin */ opj_cleanup_plugin,
        /* new_encoder */ opj_new_encoder,
        /* free_encoder */ opj_free_encoder,
        /* set_parameter_quality */ opj_set_parameter_quality,
        /* get_parameter_quality */ opj_get_parameter_quality,
        /* set_parameter_lossless */ opj_set_parameter_lossless,
        /* get_parameter_lossless */ opj_get_parameter_lossless,
        /* set_parameter_logging_level */ opj_set_parameter_logging_level,
        /* get_parameter_logging_level */ opj_get_parameter_logging_level,
        /* list_parameters */ opj_list_parameters,
        /* set_parameter_integer */ opj_set_parameter_integer,
        /* get_parameter_integer */ opj_get_parameter_integer,
        /* set_parameter_boolean */ opj_set_parameter_boolean,
        /* get_parameter_boolean */ opj_get_parameter_boolean,
        /* set_parameter_string */ opj_set_parameter_string,
        /* get_parameter_string */ opj_get_parameter_string,
        /* query_input_colorspace */ opj_query_input_colorspace,
        /* encode_image */ opj_encode_image,
        /* get_compressed_data */ opj_get_compressed_data,
        /* query_input_colorspace (v2) */ opj_query_input_colorspace2,
        /* query_encoded_size (v3) */ opj_query_encoded_size
    };

const struct heif_encoder_plugin* get_encoder_plugin_openjpeg() {
    return &encoder_plugin_openjpeg;
}