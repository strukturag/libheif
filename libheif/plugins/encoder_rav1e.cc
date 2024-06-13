/*
 * HEIF codec.
 * Copyright (c) 2017 Dirk Farin <dirk.farin@gmail.com>
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
#include "encoder_rav1e.h"
#include <vector>
#include <memory>
#include <cstring>
#include <cassert>
#include <string>
#include <algorithm>

#include <iostream>  // TODO: remove me

#include "rav1e.h"


struct encoder_struct_rav1e
{
  int speed; // 0-10

  int quality; // TODO: not sure yet how to map quality to min/max q
  int min_q;
  int threads;

  int tile_rows = 1; // 1,2,4,8,16,32,64
  int tile_cols = 1; // 1,2,4,8,16,32,64

  heif_chroma chroma;

  // --- output

  std::vector<uint8_t> compressed_data;
  bool data_read = false;
};

//static const char* kError_out_of_memory = "Out of memory";

static const char* kParam_min_q = "min-q";
static const char* kParam_threads = "threads";
static const char* kParam_speed = "speed";

static const char* kParam_chroma = "chroma";
static const char* const kParam_chroma_valid_values[] = {
    "420", "422", "444", nullptr
};

static int valid_tile_num_values[] = {1, 2, 4, 8, 16, 32, 64};

static struct heif_error heif_error_codec_library_error = {heif_error_Encoder_plugin_error,
                                                           heif_suberror_Unspecified,
                                                           "rav1e error"};

static const int RAV1E_PLUGIN_PRIORITY = 20;

#define MAX_PLUGIN_NAME_LENGTH 80

static char plugin_name[MAX_PLUGIN_NAME_LENGTH];


static void rav1e_set_default_parameters(void* encoder);


static const char* rav1e_plugin_name()
{
  strcpy(plugin_name, "Rav1e encoder");

  return plugin_name;
}


#define MAX_NPARAMETERS 10

static struct heif_encoder_parameter rav1e_encoder_params[MAX_NPARAMETERS];
static const struct heif_encoder_parameter* rav1e_encoder_parameter_ptrs[MAX_NPARAMETERS + 1];

static void rav1e_init_parameters()
{
  struct heif_encoder_parameter* p = rav1e_encoder_params;
  const struct heif_encoder_parameter** d = rav1e_encoder_parameter_ptrs;
  int i = 0;

  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = kParam_speed;
  p->type = heif_encoder_parameter_type_integer;
  p->integer.default_value = 8;
  p->has_default = true;
  p->integer.have_minimum_maximum = true;
  p->integer.minimum = 0;
  p->integer.maximum = 10;
  p->integer.valid_values = NULL;
  p->integer.num_valid_values = 0;
  d[i++] = p++;

  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = kParam_threads;
  p->type = heif_encoder_parameter_type_integer;
  p->integer.default_value = 4;
  p->has_default = true;
  p->integer.have_minimum_maximum = true;
  p->integer.minimum = 1;
  p->integer.maximum = 16;
  p->integer.valid_values = NULL;
  p->integer.num_valid_values = 0;
  d[i++] = p++;

  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = "tile-rows";
  p->type = heif_encoder_parameter_type_integer;
  p->integer.default_value = 4;
  p->has_default = true;
  p->integer.have_minimum_maximum = false;
  p->integer.valid_values = valid_tile_num_values;
  p->integer.num_valid_values = 7;
  d[i++] = p++;

  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = "tile-cols";
  p->type = heif_encoder_parameter_type_integer;
  p->integer.default_value = 4;
  p->has_default = true;
  p->integer.have_minimum_maximum = false;
  p->integer.valid_values = valid_tile_num_values;
  p->integer.num_valid_values = 7;
  d[i++] = p++;


  /*
  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = heif_encoder_parameter_name_quality;
  p->type = heif_encoder_parameter_type_integer;
  p->integer.default_value = 50;
  p->has_default = true;
  p->integer.have_minimum_maximum = true;
  p->integer.minimum = 0;
  p->integer.maximum = 100;
  p->integer.valid_values = NULL;
  p->integer.num_valid_values = 0;
  d[i++] = p++;
*/

  /*
  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = heif_encoder_parameter_name_lossless;
  p->type = heif_encoder_parameter_type_boolean;
  p->boolean.default_value = false;
  p->has_default = true;
  d[i++] = p++;
*/

  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = kParam_chroma;
  p->type = heif_encoder_parameter_type_string;
  p->string.default_value = "420";
  p->has_default = true;
  p->string.valid_values = kParam_chroma_valid_values;
  d[i++] = p++;

  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = kParam_min_q;
  p->type = heif_encoder_parameter_type_integer;
  p->integer.default_value = 0;
  p->has_default = true;
  p->integer.have_minimum_maximum = true;
  p->integer.minimum = 0;
  p->integer.maximum = 255;
  p->integer.valid_values = NULL;
  p->integer.num_valid_values = 0;
  d[i++] = p++;

  d[i++] = nullptr;
}


const struct heif_encoder_parameter** rav1e_list_parameters(void* encoder)
{
  return rav1e_encoder_parameter_ptrs;
}

static void rav1e_init_plugin()
{
  rav1e_init_parameters();
}


static void rav1e_cleanup_plugin()
{
}

struct heif_error rav1e_new_encoder(void** enc)
{
  auto* encoder = new encoder_struct_rav1e();
  struct heif_error err = heif_error_ok;

  *enc = encoder;

  // set default parameters

  rav1e_set_default_parameters(encoder);

  return err;
}

void rav1e_free_encoder(void* encoder_raw)
{
  auto* encoder = (struct encoder_struct_rav1e*) encoder_raw;

  delete encoder;
}


struct heif_error rav1e_set_parameter_quality(void* encoder_raw, int quality)
{
  auto* encoder = (struct encoder_struct_rav1e*) encoder_raw;

  if (quality < 0 || quality > 100) {
    return heif_error_invalid_parameter_value;
  }

  encoder->quality = quality;

  return heif_error_ok;
}

struct heif_error rav1e_get_parameter_quality(void* encoder_raw, int* quality)
{
  auto* encoder = (struct encoder_struct_rav1e*) encoder_raw;

  *quality = encoder->quality;

  return heif_error_ok;
}

struct heif_error rav1e_set_parameter_lossless(void* encoder_raw, int enable)
{
  struct encoder_struct_rav1e* encoder = (struct encoder_struct_rav1e*) encoder_raw;

  if (enable) {
    encoder->min_q = 0;
  }

  return heif_error_ok;
}

struct heif_error rav1e_get_parameter_lossless(void* encoder_raw, int* enable)
{
  struct encoder_struct_rav1e* encoder = (struct encoder_struct_rav1e*) encoder_raw;

  *enable = (encoder->min_q == 0);

  return heif_error_ok;
}

struct heif_error rav1e_set_parameter_logging_level(void* encoder_raw, int logging)
{
#if 0
  struct encoder_struct_x265* encoder = (struct encoder_struct_x265*)encoder_raw;

  if (logging<0 || logging>4) {
    return heif_error_invalid_parameter_value;
  }

  encoder->logLevel = logging;
#endif

  return heif_error_ok;
}

struct heif_error rav1e_get_parameter_logging_level(void* encoder_raw, int* loglevel)
{
#if 0
  struct encoder_struct_x265* encoder = (struct encoder_struct_x265*)encoder_raw;

  *loglevel = encoder->logLevel;
#else
  *loglevel = 0;
#endif

  return heif_error_ok;
}

#define set_value(paramname, paramvar) if (strcmp(name, paramname)==0) { encoder->paramvar = value; return heif_error_ok; }
#define get_value(paramname, paramvar) if (strcmp(name, paramname)==0) { *value = encoder->paramvar; return heif_error_ok; }


struct heif_error rav1e_set_parameter_integer(void* encoder_raw, const char* name, int value)
{
  struct encoder_struct_rav1e* encoder = (struct encoder_struct_rav1e*) encoder_raw;

  if (strcmp(name, heif_encoder_parameter_name_quality) == 0) {
    return rav1e_set_parameter_quality(encoder, value);
  }
  else if (strcmp(name, heif_encoder_parameter_name_lossless) == 0) {
    return rav1e_set_parameter_lossless(encoder, value);
  }

  set_value(kParam_min_q, min_q);
  set_value(kParam_threads, threads);
  set_value(kParam_speed, speed);
  set_value("tile-rows", tile_rows);
  set_value("tile-cols", tile_cols);

  return heif_error_unsupported_parameter;
}

struct heif_error rav1e_get_parameter_integer(void* encoder_raw, const char* name, int* value)
{
  struct encoder_struct_rav1e* encoder = (struct encoder_struct_rav1e*) encoder_raw;

  if (strcmp(name, heif_encoder_parameter_name_quality) == 0) {
    return rav1e_get_parameter_quality(encoder, value);
  }
  else if (strcmp(name, heif_encoder_parameter_name_lossless) == 0) {
    return rav1e_get_parameter_lossless(encoder, value);
  }

  get_value(kParam_min_q, min_q);
  get_value(kParam_threads, threads);
  get_value(kParam_speed, speed);
  get_value("tile-rows", tile_rows);
  get_value("tile-cols", tile_cols);

  return heif_error_unsupported_parameter;
}


struct heif_error rav1e_set_parameter_boolean(void* encoder_raw, const char* name, int value)
{
  auto* encoder = (struct encoder_struct_rav1e*) encoder_raw;

  if (strcmp(name, heif_encoder_parameter_name_lossless) == 0) {
    return rav1e_set_parameter_lossless(encoder, value);
  }

  //set_value(kParam_realtime, realtime_mode);

  return heif_error_unsupported_parameter;
}

struct heif_error rav1e_get_parameter_boolean(void* encoder_raw, const char* name, int* value)
{
  auto* encoder = (struct encoder_struct_rav1e*) encoder_raw;

  if (strcmp(name, heif_encoder_parameter_name_lossless) == 0) {
    return rav1e_get_parameter_lossless(encoder, value);
  }

  //get_value(kParam_realtime, realtime_mode);

  return heif_error_unsupported_parameter;
}


struct heif_error rav1e_set_parameter_string(void* encoder_raw, const char* name, const char* value)
{
  auto* encoder = (struct encoder_struct_rav1e*) encoder_raw;

  if (strcmp(name, kParam_chroma) == 0) {
    if (strcmp(value, "420") == 0) {
      encoder->chroma = heif_chroma_420;
      return heif_error_ok;
    }
    else if (strcmp(value, "422") == 0) {
      encoder->chroma = heif_chroma_422;
      return heif_error_ok;
    }
    else if (strcmp(value, "444") == 0) {
      encoder->chroma = heif_chroma_444;
      return heif_error_ok;
    }
    else {
      return heif_error_invalid_parameter_value;
    }
  }

  return heif_error_unsupported_parameter;
}


static void save_strcpy(char* dst, int dst_size, const char* src)
{
  strncpy(dst, src, dst_size - 1);
  dst[dst_size - 1] = 0;
}


struct heif_error rav1e_get_parameter_string(void* encoder_raw, const char* name,
                                             char* value, int value_size)
{
  struct encoder_struct_rav1e* encoder = (struct encoder_struct_rav1e*) encoder_raw;

  if (strcmp(name, kParam_chroma) == 0) {
    switch (encoder->chroma) {
      case heif_chroma_420:
        save_strcpy(value, value_size, "420");
        break;
      case heif_chroma_422:
        save_strcpy(value, value_size, "422");
        break;
      case heif_chroma_444:
        save_strcpy(value, value_size, "444");
        break;
      default:
        assert(false);
        return heif_error_invalid_parameter_value;
    }
    return heif_error_ok;
  }

  return heif_error_unsupported_parameter;
}


static void rav1e_set_default_parameters(void* encoder)
{
  for (const struct heif_encoder_parameter** p = rav1e_encoder_parameter_ptrs; *p; p++) {
    const struct heif_encoder_parameter* param = *p;

    if (param->has_default) {
      switch (param->type) {
        case heif_encoder_parameter_type_integer:
          rav1e_set_parameter_integer(encoder, param->name, param->integer.default_value);
          break;
        case heif_encoder_parameter_type_boolean:
          rav1e_set_parameter_boolean(encoder, param->name, param->boolean.default_value);
          break;
        case heif_encoder_parameter_type_string:
          rav1e_set_parameter_string(encoder, param->name, param->string.default_value);
          break;
      }
    }
  }
}


void rav1e_query_input_colorspace(heif_colorspace* colorspace, heif_chroma* chroma)
{
  *colorspace = heif_colorspace_YCbCr;
  *chroma = heif_chroma_420;
}


void rav1e_query_input_colorspace2(void* encoder_raw, heif_colorspace* colorspace, heif_chroma* chroma)
{
  auto* encoder = (struct encoder_struct_rav1e*) encoder_raw;

  *colorspace = heif_colorspace_YCbCr;
  *chroma = encoder->chroma;
}


struct heif_error rav1e_encode_image(void* encoder_raw, const struct heif_image* image,
                                     heif_image_input_class input_class)
{
  auto* encoder = (struct encoder_struct_rav1e*) encoder_raw;

  const heif_chroma chroma = heif_image_get_chroma_format(image);

  uint8_t yShift = 0;
  RaChromaSampling chromaSampling;
  RaChromaSamplePosition chromaPosition;
  RaPixelRange rav1eRange;

  if (input_class == heif_image_input_class_alpha) {
    chromaSampling = RA_CHROMA_SAMPLING_CS420; // I can't seem to get RA_CHROMA_SAMPLING_CS400 to work right now, unfortunately
    chromaPosition = RA_CHROMA_SAMPLE_POSITION_UNKNOWN; // TODO: set to CENTER when AV1 and rav1e supports this
    yShift = 1;
  }
  else {
    switch (chroma) {
      case heif_chroma_444:
        chromaSampling = RA_CHROMA_SAMPLING_CS444;
        chromaPosition = RA_CHROMA_SAMPLE_POSITION_COLOCATED;
        break;
      case heif_chroma_422:
        chromaSampling = RA_CHROMA_SAMPLING_CS422;
        chromaPosition = RA_CHROMA_SAMPLE_POSITION_COLOCATED;
        break;
      case heif_chroma_420:
        chromaSampling = RA_CHROMA_SAMPLING_CS420;
        chromaPosition = RA_CHROMA_SAMPLE_POSITION_UNKNOWN; // TODO: set to CENTER when AV1 and rav1e supports this
        yShift = 1;
        break;
      default:
        return heif_error_codec_library_error;
    }
  }

  struct heif_color_profile_nclx* nclx = nullptr;
  heif_error err = heif_image_get_nclx_color_profile(image, &nclx);
  if (err.code != heif_error_Ok) {
    nclx = nullptr;
  }

  // make sure NCLX profile is deleted at end of function
  auto nclx_deleter = std::unique_ptr<heif_color_profile_nclx, void (*)(heif_color_profile_nclx*)>(nclx, heif_nclx_color_profile_free);

  rav1eRange = RA_PIXEL_RANGE_FULL;
  if (nclx) {
    rav1eRange = nclx->full_range_flag ? RA_PIXEL_RANGE_FULL : RA_PIXEL_RANGE_LIMITED;
  }

  int bitDepth = heif_image_get_bits_per_pixel(image, heif_channel_Y);

  auto rav1eConfigRaw = rav1e_config_default();
  auto rav1eConfig = std::shared_ptr<RaConfig>(rav1eConfigRaw, [](RaConfig* c) { rav1e_config_unref(c); });

  if (rav1e_config_set_pixel_format(rav1eConfig.get(), (uint8_t) bitDepth, chromaSampling, chromaPosition, rav1eRange) < 0) {
    return heif_error_codec_library_error;
  }

  if (rav1e_config_parse(rav1eConfig.get(), "still_picture", "true") == -1) {
    return heif_error_codec_library_error;
  }
  if (rav1e_config_parse_int(rav1eConfig.get(), "width", heif_image_get_width(image, heif_channel_Y)) == -1) {
    return heif_error_codec_library_error;
  }
  if (rav1e_config_parse_int(rav1eConfig.get(), "height", heif_image_get_height(image, heif_channel_Y)) == -1) {
    return heif_error_codec_library_error;
  }
  if (rav1e_config_parse_int(rav1eConfig.get(), "threads", encoder->threads) == -1) {
    return heif_error_codec_library_error;
  }

  if (nclx &&
      (input_class == heif_image_input_class_normal ||
#if WITH_EXPERIMENTAL_GAIN_MAP
       input_class == heif_image_input_class_thumbnail ||
       input_class == heif_image_input_class_gain_map)) {
#else
       input_class == heif_image_input_class_thumbnail)) {
#endif
    if (rav1e_config_set_color_description(rav1eConfig.get(),
                                           (RaMatrixCoefficients) nclx->matrix_coefficients,
                                           (RaColorPrimaries) nclx->color_primaries,
                                           (RaTransferCharacteristics) nclx->transfer_characteristics) == -1) {
      return heif_error_codec_library_error;
    }
  }

  if (rav1e_config_parse_int(rav1eConfig.get(), "min_quantizer", encoder->min_q) == -1) {
    return heif_error_codec_library_error;
  }

  int base_quantizer = ((100 - encoder->quality) * 255 + 50) / 100;

  if (rav1e_config_parse_int(rav1eConfig.get(), "quantizer", base_quantizer) == -1) {
    return heif_error_codec_library_error;
  }

  if (encoder->tile_rows != 1) {
    if (rav1e_config_parse_int(rav1eConfig.get(), "tile_rows", encoder->tile_rows) == -1) {
      return heif_error_codec_library_error;
    }
  }
  if (encoder->tile_cols != 1) {
    if (rav1e_config_parse_int(rav1eConfig.get(), "tile_cols", encoder->tile_cols) == -1) {
      return heif_error_codec_library_error;
    }
  }
  /*if (encoder->speed != -1)*/ {
    if (rav1e_config_parse_int(rav1eConfig.get(), "speed", encoder->speed) == -1) {
      return heif_error_codec_library_error;
    }
  }

  if (nclx) {
    rav1e_config_set_color_description(rav1eConfig.get(),
                                       (RaMatrixCoefficients) nclx->matrix_coefficients,
                                       (RaColorPrimaries) nclx->color_primaries,
                                       (RaTransferCharacteristics) nclx->transfer_characteristics);
  }

  RaContext* rav1eContextRaw = rav1e_context_new(rav1eConfig.get());
  if (!rav1eContextRaw) {
    return heif_error_codec_library_error;
  }
  auto rav1eContext = std::shared_ptr<RaContext>(rav1eContextRaw, [](RaContext* ctx) { rav1e_context_unref(ctx); });


  // --- copy libheif image to rav1e image

  auto rav1eFrameRaw = rav1e_frame_new(rav1eContext.get());
  auto rav1eFrame = std::shared_ptr<RaFrame>(rav1eFrameRaw, [](RaFrame* frm) { rav1e_frame_unref(frm); });

  int byteWidth = (bitDepth > 8) ? 2 : 1;
  // if (input_class == heif_image_input_class_alpha) {
  //} else
  {
    int strideY;
    const uint8_t* Y = heif_image_get_plane_readonly(image, heif_channel_Y, &strideY);
    int strideCb;
    const uint8_t* Cb = heif_image_get_plane_readonly(image, heif_channel_Cb, &strideCb);
    int strideCr;
    const uint8_t* Cr = heif_image_get_plane_readonly(image, heif_channel_Cr, &strideCr);


    uint32_t height = heif_image_get_height(image, heif_channel_Y);

    uint32_t uvHeight = (height + yShift) >> yShift;
    rav1e_frame_fill_plane(rav1eFrame.get(), 0, Y, strideY * height, strideY, byteWidth);
    rav1e_frame_fill_plane(rav1eFrame.get(), 1, Cb, strideCb * uvHeight, strideCb, byteWidth);
    rav1e_frame_fill_plane(rav1eFrame.get(), 2, Cr, strideCr * uvHeight, strideCr, byteWidth);
  }

  RaEncoderStatus encoderStatus = rav1e_send_frame(rav1eContext.get(), rav1eFrame.get());
  if (encoderStatus != 0) {
    return heif_error_codec_library_error;
  }

  // flush encoder
  encoderStatus = rav1e_send_frame(rav1eContext.get(), nullptr);
  if (encoderStatus != 0) {
    return heif_error_codec_library_error;
  }

  RaPacket* pkt = nullptr;
  encoderStatus = rav1e_receive_packet(rav1eContext.get(), &pkt);
  if (encoderStatus != 0) {
    return heif_error_codec_library_error;
  }

  if (pkt && pkt->data && (pkt->len > 0)) {
    encoder->compressed_data.resize(pkt->len);
    memcpy(encoder->compressed_data.data(), pkt->data, pkt->len);
    encoder->data_read = false;
  }

  if (pkt) {
    rav1e_packet_unref(pkt);
  }

  return heif_error_ok;
}

struct heif_error rav1e_get_compressed_data(void* encoder_raw, uint8_t** data, int* size,
                                            enum heif_encoded_data_type* type)
{
  auto* encoder = (struct encoder_struct_rav1e*) encoder_raw;

  if (encoder->data_read) {
    *data = nullptr;
    *size = 0;
  }
  else {
    *data = encoder->compressed_data.data();
    *size = (int) encoder->compressed_data.size();
    encoder->data_read = true;
  }

  return heif_error_ok;
}


static const struct heif_encoder_plugin encoder_plugin_rav1e
    {
        /* plugin_api_version */ 3,
        /* compression_format */ heif_compression_AV1,
        /* id_name */ "rav1e",
        /* priority */ RAV1E_PLUGIN_PRIORITY,
        /* supports_lossy_compression */ true,
        /* supports_lossless_compression */ false,
        /* get_plugin_name */ rav1e_plugin_name,
        /* init_plugin */ rav1e_init_plugin,
        /* cleanup_plugin */ rav1e_cleanup_plugin,
        /* new_encoder */ rav1e_new_encoder,
        /* free_encoder */ rav1e_free_encoder,
        /* set_parameter_quality */ rav1e_set_parameter_quality,
        /* get_parameter_quality */ rav1e_get_parameter_quality,
        /* set_parameter_lossless */ rav1e_set_parameter_lossless,
        /* get_parameter_lossless */ rav1e_get_parameter_lossless,
        /* set_parameter_logging_level */ rav1e_set_parameter_logging_level,
        /* get_parameter_logging_level */ rav1e_get_parameter_logging_level,
        /* list_parameters */ rav1e_list_parameters,
        /* set_parameter_integer */ rav1e_set_parameter_integer,
        /* get_parameter_integer */ rav1e_get_parameter_integer,
        /* set_parameter_boolean */ rav1e_set_parameter_boolean,
        /* get_parameter_boolean */ rav1e_get_parameter_boolean,
        /* set_parameter_string */ rav1e_set_parameter_string,
        /* get_parameter_string */ rav1e_get_parameter_string,
        /* query_input_colorspace */ rav1e_query_input_colorspace,
        /* encode_image */ rav1e_encode_image,
        /* get_compressed_data */ rav1e_get_compressed_data,
        /* query_input_colorspace (v2) */ rav1e_query_input_colorspace2,
        /* query_encoded_size (v3) */ nullptr
    };

const struct heif_encoder_plugin* get_encoder_plugin_rav1e()
{
  return &encoder_plugin_rav1e;
}


#if PLUGIN_RAV1E
heif_plugin_info plugin_info{
    1,
    heif_plugin_type_encoder,
    &encoder_plugin_rav1e
};
#endif
