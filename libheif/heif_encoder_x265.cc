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

#include "heif.h"
#include "heif_plugin.h"

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <memory>
#include <string>
#include <string.h>
#include <stdio.h>
#include <assert.h>

extern "C" {
#include <x265.h>
}


struct x265_encoder_struct
{
  x265_encoder* encoder;

  x265_nal* nals;
  uint32_t num_nals;
  uint32_t nal_output_counter;

  // --- parameters

  int quality;
  bool lossless;
  std::string preset;
  std::string tune;
  int tu_intra_depth;
  int logLevel = X265_LOG_NONE;
  int bit_depth;
};


static const char* kParam_preset = "preset";
static const char* kParam_tune = "tune";
static const char* kParam_TU_intra_depth = "tu-intra-depth";

static const char*const kParam_preset_valid_values[] = {
  "ultrafast", "superfast", "veryfast", "faster", "fast", "medium",
  "slow", "slower", "veryslow", "placebo", nullptr
};

static const char*const kParam_tune_valid_values[] = {
  "psnr", "ssim", "grain", "fastdecode", nullptr
  // note: zerolatency is missing, because we do not need it for single images
};


static const int X265_PLUGIN_PRIORITY = 100;

#define MAX_PLUGIN_NAME_LENGTH 80

static char plugin_name[MAX_PLUGIN_NAME_LENGTH];


static void x265_set_default_parameters(void* encoder);


const char* x265_plugin_name()
{
  strcpy(plugin_name, "x265 HEVC encoder");

  if (strlen(x265_version_str) + strlen(plugin_name) + 4 < MAX_PLUGIN_NAME_LENGTH) {
    strcat(plugin_name," (");
    strcat(plugin_name,x265_version_str);
    strcat(plugin_name,")");
  }

  return plugin_name;
}


#define MAX_NPARAMETERS 10

static struct heif_encoder_parameter x265_encoder_params[MAX_NPARAMETERS];
static const struct heif_encoder_parameter* x265_encoder_parameter_ptrs[MAX_NPARAMETERS+1];

static void x265_init_parameters()
{
  struct heif_encoder_parameter* p = x265_encoder_params;
  const struct heif_encoder_parameter** d = x265_encoder_parameter_ptrs;
  int i=0;

  assert(i < MAX_NPARAMETERS);
  p->version = 1;
  p->name = heif_encoder_parameter_name_quality;
  p->type = heif_encoder_parameter_type_integer;
  p->integer.default_value = 50;
  p->integer.have_minimum_maximum = true;
  p->integer.minimum = 0;
  p->integer.maximum = 100;
  p->integer.valid_values = NULL;
  p->integer.num_valid_values = 0;
  d[i++] = p++;

  assert(i < MAX_NPARAMETERS);
  p->version = 1;
  p->name = heif_encoder_parameter_name_lossless;
  p->type = heif_encoder_parameter_type_boolean;
  p->boolean.default_value = false;
  d[i++] = p++;

  assert(i < MAX_NPARAMETERS);
  p->version = 1;
  p->name = kParam_preset;
  p->type = heif_encoder_parameter_type_string;
  p->string.default_value = "veryslow";
  p->string.valid_values = kParam_preset_valid_values;
  d[i++] = p++;

  assert(i < MAX_NPARAMETERS);
  p->version = 1;
  p->name = kParam_tune;
  p->type = heif_encoder_parameter_type_string;
  p->string.default_value = "ssim";
  p->string.valid_values = kParam_tune_valid_values;
  d[i++] = p++;

  assert(i < MAX_NPARAMETERS);
  p->version = 1;
  p->name = kParam_TU_intra_depth;
  p->type = heif_encoder_parameter_type_integer;
  p->integer.default_value = 4;
  p->integer.have_minimum_maximum = true;
  p->integer.minimum = 1;
  p->integer.maximum = 4;
  p->integer.valid_values = NULL;
  p->integer.num_valid_values = 0;
  d[i++] = p++;

  d[i++] = nullptr;
}


const struct heif_encoder_parameter** x265_list_parameters(void* encoder)
{
  return x265_encoder_parameter_ptrs;
}


void x265_init_plugin()
{
  x265_init_parameters();
}


void x265_cleanup_plugin()
{
}


struct heif_error x265_new_encoder(void** enc)
{
  struct x265_encoder_struct* encoder = new x265_encoder_struct();
  struct heif_error err = heif_error_ok;


  // encoder has to be allocated in x265_encode_image, because it needs to know the image size
  encoder->encoder = nullptr;

  encoder->nals = nullptr;
  encoder->num_nals = 0;
  encoder->nal_output_counter = 0;
  encoder->bit_depth = 8;

  *enc = encoder;


  // set default parameters

  x265_set_default_parameters(encoder);

  return err;
}

void x265_free_encoder(void* encoder_raw)
{
  struct x265_encoder_struct* encoder = (struct x265_encoder_struct*)encoder_raw;

  if (encoder->encoder) {
    const x265_api* api = x265_api_get(encoder->bit_depth);
    api->encoder_close(encoder->encoder);
  }

  delete encoder;
}

struct heif_error x265_set_parameter_quality(void* encoder_raw, int quality)
{
  struct x265_encoder_struct* encoder = (struct x265_encoder_struct*)encoder_raw;

  if (quality<0 || quality>100) {
    return heif_error_invalid_parameter_value;
  }

  // quality=0   -> crf=50
  // quality=50  -> crf=25
  // quality=100 -> crf=0
  encoder->quality = quality;

  return heif_error_ok;
}

struct heif_error x265_get_parameter_quality(void* encoder_raw, int* quality)
{
  struct x265_encoder_struct* encoder = (struct x265_encoder_struct*)encoder_raw;

  *quality = encoder->quality;

  return heif_error_ok;
}

struct heif_error x265_set_parameter_lossless(void* encoder_raw, int enable)
{
  struct x265_encoder_struct* encoder = (struct x265_encoder_struct*)encoder_raw;

  encoder->lossless = enable;

  return heif_error_ok;
}

struct heif_error x265_get_parameter_lossless(void* encoder_raw, int* enable)
{
  struct x265_encoder_struct* encoder = (struct x265_encoder_struct*)encoder_raw;

  *enable = encoder->lossless;

  return heif_error_ok;
}

struct heif_error x265_set_parameter_logging_level(void* encoder_raw, int logging)
{
  struct x265_encoder_struct* encoder = (struct x265_encoder_struct*)encoder_raw;

  if (logging<0 || logging>4) {
    return heif_error_invalid_parameter_value;
  }

  encoder->logLevel = logging;

  return heif_error_ok;
}

struct heif_error x265_get_parameter_logging_level(void* encoder_raw, int* loglevel)
{
  struct x265_encoder_struct* encoder = (struct x265_encoder_struct*)encoder_raw;

  *loglevel = encoder->logLevel;

  return heif_error_ok;
}


struct heif_error x265_set_parameter_integer(void* encoder_raw, const char* name, int value)
{
  struct x265_encoder_struct* encoder = (struct x265_encoder_struct*)encoder_raw;

  if (strcmp(name, heif_encoder_parameter_name_quality)==0) {
    return x265_set_parameter_quality(encoder,value);
  }
  else if (strcmp(name, heif_encoder_parameter_name_lossless)==0) {
    return x265_set_parameter_lossless(encoder,value);
  }
  else if (strcmp(name, kParam_TU_intra_depth)==0) {
    if (value < 1 || value > 4) {
      return heif_error_invalid_parameter_value;
    }

    encoder->tu_intra_depth = value;
    return heif_error_ok;
  }

  return heif_error_unsupported_parameter;
}

struct heif_error x265_get_parameter_integer(void* encoder_raw, const char* name, int* value)
{
  struct x265_encoder_struct* encoder = (struct x265_encoder_struct*)encoder_raw;

  if (strcmp(name, heif_encoder_parameter_name_quality)==0) {
    return x265_get_parameter_quality(encoder,value);
  }
  else if (strcmp(name, heif_encoder_parameter_name_lossless)==0) {
    return x265_get_parameter_lossless(encoder,value);
  }
  else if (strcmp(name, kParam_TU_intra_depth)==0) {
    *value = encoder->tu_intra_depth;
    return heif_error_ok;
  }

  return heif_error_unsupported_parameter;
}


struct heif_error x265_set_parameter_boolean(void* encoder, const char* name, int value)
{
  if (strcmp(name, heif_encoder_parameter_name_lossless)==0) {
    return x265_set_parameter_lossless(encoder,value);
  }

  return heif_error_unsupported_parameter;
}

struct heif_error x265_get_parameter_boolean(void* encoder, const char* name, int* value)
{
  if (strcmp(name, heif_encoder_parameter_name_lossless)==0) {
    return x265_get_parameter_lossless(encoder,value);
  }

  return heif_error_unsupported_parameter;
}


bool string_list_contains(const char*const* values_list, const char* value)
{
  for (int i=0; values_list[i]; i++) {
    if (strcmp(values_list[i], value)==0) {
      return true;
    }
  }

  return false;
}


struct heif_error x265_set_parameter_string(void* encoder_raw, const char* name, const char* value)
{
  struct x265_encoder_struct* encoder = (struct x265_encoder_struct*)encoder_raw;

  if (strcmp(name, kParam_preset)==0) {
    if (!string_list_contains(kParam_preset_valid_values, value)) {
      return heif_error_invalid_parameter_value;
    }

    encoder->preset = value;
    return heif_error_ok;
  }
  else if (strcmp(name, kParam_tune)==0) {
    if (!string_list_contains(kParam_tune_valid_values, value)) {
      return heif_error_invalid_parameter_value;
    }

    encoder->tune = value;
    return heif_error_ok;
  }

  return heif_error_unsupported_parameter;
}

void save_strcpy(char* dst, int dst_size, const char* src)
{
  strncpy(dst, src, dst_size-1);
  dst[dst_size-1] = 0;
}

struct heif_error x265_get_parameter_string(void* encoder_raw, const char* name,
                                            char* value, int value_size)
{
  struct x265_encoder_struct* encoder = (struct x265_encoder_struct*)encoder_raw;

  if (strcmp(name, kParam_preset)==0) {
    save_strcpy(value, value_size, encoder->preset.c_str());
    return heif_error_ok;
  }
  else if (strcmp(name, kParam_tune)==0) {
    save_strcpy(value, value_size, encoder->tune.c_str());
    return heif_error_ok;
  }

  return heif_error_unsupported_parameter;
}


static void x265_set_default_parameters(void* encoder)
{
  for (const struct heif_encoder_parameter** p = x265_encoder_parameter_ptrs; *p; p++) {
    const struct heif_encoder_parameter* param = *p;

    switch (param->type) {
    case heif_encoder_parameter_type_integer:
      x265_set_parameter_integer(encoder, param->name, param->integer.default_value);
      break;
    case heif_encoder_parameter_type_boolean:
      x265_set_parameter_boolean(encoder, param->name, param->boolean.default_value);
      break;
    case heif_encoder_parameter_type_string:
      x265_set_parameter_string(encoder, param->name, param->string.default_value);
      break;
    }
  }
}


void x265_query_input_colorspace(heif_colorspace* colorspace, heif_chroma* chroma)
{
  *colorspace = heif_colorspace_YCbCr;
  *chroma = heif_chroma_420;
}


struct heif_error x265_encode_image(void* encoder_raw, const struct heif_image* image,
                                    heif_image_input_class input_class)
{
  struct x265_encoder_struct* encoder = (struct x265_encoder_struct*)encoder_raw;

  int bit_depth = heif_image_get_bits_per_pixel(image, heif_channel_Y);

  const x265_api* api = x265_api_get(bit_depth);

  x265_param* param = api->param_alloc();
  api->param_default_preset(param, encoder->preset.c_str(), encoder->tune.c_str());

  if (bit_depth == 8) api->param_apply_profile(param, "mainstillpicture");
  else if (bit_depth == 10) api->param_apply_profile(param, "main10-intra");
  else if (bit_depth == 12) api->param_apply_profile(param, "main12-intra");
  else return heif_error_unsupported_parameter;


  param->fpsNum = 1;
  param->fpsDenom = 1;

  param->rc.rfConstant = (100 - encoder->quality)/2;

// BPG uses CQP. It does not seem to be better though.
//  param->rc.rateControlMode = X265_RC_CQP;
//  param->rc.qp = (100 - encoder->quality)/2;
  param->totalFrames = 1;
  param->internalCsp = X265_CSP_I420;
  api->param_parse(param, "info", "0");
  api->param_parse(param, "limit-modes", "0");
  api->param_parse(param, "limit-refs", "0");
  api->param_parse(param, "wpp", "0");
  api->param_parse(param, "rd", "6");
  api->param_parse(param, "ctu", "64");
  api->param_parse(param, "rskip", "0");
  api->param_parse(param, "rect", "1");
  api->param_parse(param, "amp", "1");
  api->param_parse(param, "cu-lossless", "1");

  api->param_parse(param, "aq-mode", "1");
  api->param_parse(param, "rd-refine", "1");
  api->param_parse(param, "psy-rd", "1.0");
  api->param_parse(param, "psy-rdoq", "1.0");


  param->bLossless = encoder->lossless;
  param->logLevel = encoder->logLevel;
  api->param_parse(param, "range", "full");

  char buf[100];
  sprintf(buf, "%d", encoder->tu_intra_depth);
  api->param_parse(param, "tu-intra-depth", buf);

  param->sourceWidth  = heif_image_get_width(image, heif_channel_Y) & ~1;
  param->sourceHeight = heif_image_get_height(image, heif_channel_Y) & ~1;
  param->internalBitDepth = bit_depth;



  x265_picture* pic = api->picture_alloc();
  api->picture_init(param, pic);

  pic->planes[0] = (void*)heif_image_get_plane_readonly(image, heif_channel_Y,  &pic->stride[0]);
  pic->planes[1] = (void*)heif_image_get_plane_readonly(image, heif_channel_Cb, &pic->stride[1]);
  pic->planes[2] = (void*)heif_image_get_plane_readonly(image, heif_channel_Cr, &pic->stride[2]);
  pic->bitDepth = bit_depth;


  // close previous encoder if there is still one hanging around
  if (encoder->encoder) {
    const x265_api* api = x265_api_get(encoder->bit_depth);
    api->encoder_close(encoder->encoder);
  }

  encoder->bit_depth = bit_depth;

  encoder->encoder = api->encoder_open(param);

  api->encoder_encode(encoder->encoder,
                                   &encoder->nals,
                                   &encoder->num_nals,
                                   pic,
                                   NULL);

  api->picture_free(pic);
  api->param_free(param);

  encoder->nal_output_counter = 0;

  return heif_error_ok;
}


struct heif_error x265_get_compressed_data(void* encoder_raw, uint8_t** data, int* size,
                                           enum heif_encoded_data_type* type)
{
  struct x265_encoder_struct* encoder = (struct x265_encoder_struct*)encoder_raw;


  if (encoder->encoder == nullptr) {
    *data = nullptr;
    *size = 0;

    return heif_error_ok;
  }

  const x265_api* api = x265_api_get(encoder->bit_depth);

  for (;;) {
    while (encoder->nal_output_counter < encoder->num_nals) {
      *data = encoder->nals[encoder->nal_output_counter].payload;
      *size = encoder->nals[encoder->nal_output_counter].sizeBytes;
      encoder->nal_output_counter++;

      // --- skip start code ---

      // skip '0' bytes
      while (**data==0 && *size>0) {
        (*data)++;
        (*size)--;
      }

      // skip '1' byte
      (*data)++;
      (*size)--;


      // --- skip NALs with irrelevant data ---

      if (*size >= 3 && (*data)[0]==0x4e && (*data)[2]==5) {
        // skip "unregistered user data SEI"

      }
      else {
        // output NAL

        return heif_error_ok;
      }
    }


    encoder->nal_output_counter = 0;


    int result = api->encoder_encode(encoder->encoder,
                                     &encoder->nals,
                                     &encoder->num_nals,
                                     NULL,
                                     NULL);
    if (result <= 0) {
      *data = nullptr;
      *size = 0;

      return heif_error_ok;
    }
  }
}


static const struct heif_encoder_plugin encoder_plugin_x265
{
  /* plugin_api_version */ 1,
  /* compression_format */ heif_compression_HEVC,
  /* id_name */ "x265",
  /* priority */ X265_PLUGIN_PRIORITY,
  /* supports_lossy_compression */ true,
  /* supports_lossless_compression */ true,
  /* get_plugin_name */ x265_plugin_name,
  /* init_plugin */ x265_init_plugin,
  /* cleanup_plugin */ x265_cleanup_plugin,
  /* new_encoder */ x265_new_encoder,
  /* free_encoder */ x265_free_encoder,
  /* set_parameter_quality */ x265_set_parameter_quality,
  /* get_parameter_quality */ x265_get_parameter_quality,
  /* set_parameter_lossless */ x265_set_parameter_lossless,
  /* get_parameter_lossless */ x265_get_parameter_lossless,
  /* set_parameter_logging_level */ x265_set_parameter_logging_level,
  /* get_parameter_logging_level */ x265_get_parameter_logging_level,
  /* list_parameters */ x265_list_parameters,
  /* set_parameter_integer */ x265_set_parameter_integer,
  /* get_parameter_integer */ x265_get_parameter_integer,
  /* set_parameter_boolean */ x265_set_parameter_integer, // boolean also maps to integer function
  /* get_parameter_boolean */ x265_get_parameter_integer, // boolean also maps to integer function
  /* set_parameter_string */ x265_set_parameter_string,
  /* get_parameter_string */ x265_get_parameter_string,
  /* query_input_colorspace */ x265_query_input_colorspace,
  /* encode_image */ x265_encode_image,
  /* get_compressed_data */ x265_get_compressed_data
};

const struct heif_encoder_plugin* get_encoder_plugin_x265()
{
  return &encoder_plugin_x265;
}
