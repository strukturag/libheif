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
#include <vector>

#include <aom/aom_encoder.h>
#include <aom/aomcx.h>


#include <iostream>  // TODO: remove me


#if 0
const char* kError_unsuppoerted_bit_depth = "Bit depth not supported by AV1 encoder";


enum parameter_type { UndefinedType, Int, Bool, String };

struct parameter {


  parameter_type type = UndefinedType;
  std::string name;

  int value_int = 0; // also used for boolean
  std::string value_string;
};
#endif


struct encoder_struct_aom
{
  aom_codec_iface_t* iface;
  aom_codec_ctx_t codec;

  aom_codec_iter_t iter = NULL; // for extracting the compressed packets
  bool got_packets = false;
  bool flushed = false;

#if 0
  x265_nal* nals;
  uint32_t num_nals;
  uint32_t nal_output_counter;
  int bit_depth;

  // --- parameters

  std::vector<parameter> parameters;

  void add_param(const parameter&);
  void add_param(std::string name, int value);
  void add_param(std::string name, bool value);
  void add_param(std::string name, std::string value);
  parameter get_param(std::string name) const;

  std::string preset;
  std::string tune;

  int logLevel = X265_LOG_NONE;
#endif
};


#if 0
void encoder_struct_x265::add_param(const parameter& p)
{
  // if there is already a parameter of that name, remove it from list

  for (size_t i=0;i<parameters.size();i++) {
    if (parameters[i].name == p.name) {
      for (size_t k=i+1;k<parameters.size();k++) {
        parameters[k-1] = parameters[k];
      }
      parameters.pop_back();
      break;
    }
  }

  // and add the new parameter at the end of the list

  parameters.push_back(p);
}


void encoder_struct_x265::add_param(std::string name, int value)
{
  parameter p;
  p.type = Int;
  p.name = name;
  p.value_int = value;
  add_param(p);
}

void encoder_struct_x265::add_param(std::string name, bool value)
{
  parameter p;
  p.type = Bool;
  p.name = name;
  p.value_int = value;
  add_param(p);
}

void encoder_struct_x265::add_param(std::string name, std::string value)
{
  parameter p;
  p.type = String;
  p.name = name;
  p.value_string = value;
  add_param(p);
}


parameter encoder_struct_x265::get_param(std::string name) const
{
  for (size_t i=0;i<parameters.size();i++) {
    if (parameters[i].name == name) {
      return parameters[i];
    }
  }

  return parameter();
}


static const char* kParam_preset = "preset";
static const char* kParam_tune = "tune";
static const char* kParam_TU_intra_depth = "tu-intra-depth";
static const char* kParam_complexity = "complexity";

static const char*const kParam_preset_valid_values[] = {
  "ultrafast", "superfast", "veryfast", "faster", "fast", "medium",
  "slow", "slower", "veryslow", "placebo", nullptr
};

static const char*const kParam_tune_valid_values[] = {
  "psnr", "ssim", "grain", "fastdecode", nullptr
  // note: zerolatency is missing, because we do not need it for single images
};
#endif


static const int AOM_PLUGIN_PRIORITY = 80;

#define MAX_PLUGIN_NAME_LENGTH 80

static char plugin_name[MAX_PLUGIN_NAME_LENGTH];


static void aom_set_default_parameters(void* encoder);


static const char* aom_plugin_name()
{
  if (strlen(aom_codec_iface_name(aom_codec_av1_cx())) < MAX_PLUGIN_NAME_LENGTH) {
    strcpy(plugin_name, aom_codec_iface_name(aom_codec_av1_cx()));
  }
  else {
    strcpy(plugin_name, "AOMedia AV1 encoder");
  }

  return plugin_name;
}


#define MAX_NPARAMETERS 10

#if 0
static struct heif_encoder_parameter aom_encoder_params[MAX_NPARAMETERS];
#endif
static const struct heif_encoder_parameter* aom_encoder_parameter_ptrs[MAX_NPARAMETERS+1];

static void aom_init_parameters()
{
#if 0
  struct heif_encoder_parameter* p = aom_encoder_params;
#endif
  const struct heif_encoder_parameter** d = aom_encoder_parameter_ptrs;
  int i=0;

#if 0
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

  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = heif_encoder_parameter_name_lossless;
  p->type = heif_encoder_parameter_type_boolean;
  p->boolean.default_value = false;
  p->has_default = true;
  d[i++] = p++;

  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = kParam_preset;
  p->type = heif_encoder_parameter_type_string;
  p->string.default_value = "slow";  // increases computation time
  p->has_default = true;
  p->string.valid_values = kParam_preset_valid_values;
  d[i++] = p++;

  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = kParam_tune;
  p->type = heif_encoder_parameter_type_string;
  p->string.default_value = "ssim";
  p->has_default = true;
  p->string.valid_values = kParam_tune_valid_values;
  d[i++] = p++;

  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = kParam_TU_intra_depth;
  p->type = heif_encoder_parameter_type_integer;
  p->integer.default_value = 2;  // increases computation time
  p->has_default = true;
  p->integer.have_minimum_maximum = true;
  p->integer.minimum = 1;
  p->integer.maximum = 4;
  p->integer.valid_values = NULL;
  p->integer.num_valid_values = 0;
  d[i++] = p++;

  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = kParam_complexity;
  p->type = heif_encoder_parameter_type_integer;
  p->integer.default_value = 50;
  p->has_default = false;
  p->integer.have_minimum_maximum = true;
  p->integer.minimum = 0;
  p->integer.maximum = 100;
  p->integer.valid_values = NULL;
  p->integer.num_valid_values = 0;
  d[i++] = p++;
#endif

  d[i++] = nullptr;
}


const struct heif_encoder_parameter** aom_list_parameters(void* encoder)
{
  return aom_encoder_parameter_ptrs;
}

static void aom_init_plugin()
{
  aom_init_parameters();
}


static void aom_cleanup_plugin()
{
}

struct heif_error aom_new_encoder(void** enc)
{
  struct encoder_struct_aom* encoder = new encoder_struct_aom();
  struct heif_error err = heif_error_ok;

  encoder->iface = aom_codec_av1_cx();
  //encoder->encoder = get_aom_encoder_by_name("av1");
  if (!encoder->iface) {
    printf("Unsupported codec.");
    assert(false);
    // TODO
  }



#if 0
  // encoder has to be allocated in aom_encode_image, because it needs to know the image size
  encoder->encoder = nullptr;

  encoder->nals = nullptr;
  encoder->num_nals = 0;
  encoder->nal_output_counter = 0;
  encoder->bit_depth = 8;
#endif

  *enc = encoder;

  // set default parameters

  aom_set_default_parameters(encoder);

  return err;
}

void aom_free_encoder(void* encoder_raw)
{
  struct encoder_struct_aom* encoder = (struct encoder_struct_aom*)encoder_raw;

  if (aom_codec_destroy(&encoder->codec)) {
    printf("Failed to destroy codec.\n");
    assert(0);
    // TODO
  }

  delete encoder;
}


struct heif_error aom_set_parameter_quality(void* encoder_raw, int quality)
{
#if 0
  struct encoder_struct_x265* encoder = (struct encoder_struct_x265*)encoder_raw;

  if (quality<0 || quality>100) {
    return heif_error_invalid_parameter_value;
  }

  encoder->add_param(heif_encoder_parameter_name_quality, quality);
#endif
  return heif_error_ok;
}

struct heif_error aom_get_parameter_quality(void* encoder_raw, int* quality)
{
#if 0
  struct encoder_struct_x265* encoder = (struct encoder_struct_x265*)encoder_raw;

  parameter p = encoder->get_param(heif_encoder_parameter_name_quality);
  *quality = p.value_int;
#else
  *quality = 50;
#endif

  return heif_error_ok;
}

struct heif_error aom_set_parameter_lossless(void* encoder_raw, int enable)
{
#if 0
  struct encoder_struct_x265* encoder = (struct encoder_struct_x265*)encoder_raw;

  encoder->add_param(heif_encoder_parameter_name_lossless, (bool)enable);
#endif
  return heif_error_ok;
}

struct heif_error aom_get_parameter_lossless(void* encoder_raw, int* enable)
{
#if 0
  struct encoder_struct_x265* encoder = (struct encoder_struct_x265*)encoder_raw;

  parameter p = encoder->get_param(heif_encoder_parameter_name_lossless);
  *enable = p.value_int;
#else
  *enable = false;
#endif

  return heif_error_ok;
}

struct heif_error aom_set_parameter_logging_level(void* encoder_raw, int logging)
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

struct heif_error aom_get_parameter_logging_level(void* encoder_raw, int* loglevel)
{
#if 0
  struct encoder_struct_x265* encoder = (struct encoder_struct_x265*)encoder_raw;

  *loglevel = encoder->logLevel;
#else
  *loglevel = 0;
#endif

  return heif_error_ok;
}

struct heif_error aom_set_parameter_integer(void* encoder_raw, const char* name, int value)
{
  struct encoder_struct_aom* encoder = (struct encoder_struct_aom*)encoder_raw;

  if (strcmp(name, heif_encoder_parameter_name_quality)==0) {
    return aom_set_parameter_quality(encoder,value);
  }
  else if (strcmp(name, heif_encoder_parameter_name_lossless)==0) {
    return aom_set_parameter_lossless(encoder,value);
  }
#if 0
  else if (strcmp(name, kParam_TU_intra_depth)==0) {
    if (value < 1 || value > 4) {
      return heif_error_invalid_parameter_value;
    }

    encoder->add_param(name, value);
    return heif_error_ok;
  }
  else if (strcmp(name, kParam_complexity)==0) {
    if (value < 0 || value > 100) {
      return heif_error_invalid_parameter_value;
    }

    encoder->add_param(name, value);
    return heif_error_ok;
  }
#endif

  return heif_error_unsupported_parameter;
}

struct heif_error aom_get_parameter_integer(void* encoder_raw, const char* name, int* value)
{
  struct encoder_struct_aom* encoder = (struct encoder_struct_aom*)encoder_raw;

  if (strcmp(name, heif_encoder_parameter_name_quality)==0) {
    return aom_get_parameter_quality(encoder,value);
  }
  else if (strcmp(name, heif_encoder_parameter_name_lossless)==0) {
    return aom_get_parameter_lossless(encoder,value);
  }
#if 0
  else if (strcmp(name, kParam_TU_intra_depth)==0) {
    *value = encoder->get_param(name).value_int;
    return heif_error_ok;
  }
  else if (strcmp(name, kParam_complexity)==0) {
    *value = encoder->get_param(name).value_int;
    return heif_error_ok;
  }
#endif

  return heif_error_unsupported_parameter;
}


struct heif_error aom_set_parameter_boolean(void* encoder, const char* name, int value)
{
  if (strcmp(name, heif_encoder_parameter_name_lossless)==0) {
    return aom_set_parameter_lossless(encoder,value);
  }

  return heif_error_unsupported_parameter;
}

struct heif_error aom_get_parameter_boolean(void* encoder, const char* name, int* value)
{
  if (strcmp(name, heif_encoder_parameter_name_lossless)==0) {
    return aom_get_parameter_lossless(encoder,value);
  }

  return heif_error_unsupported_parameter;
}


struct heif_error aom_set_parameter_string(void* encoder_raw, const char* name, const char* value)
{
#if 0
  struct encoder_struct_aom* encoder = (struct encoder_struct_aom*)encoder_raw;

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
  else if (strncmp(name, "x265:", 5)==0) {
    encoder->add_param(name, std::string(value));
    return heif_error_ok;
  }
#endif

  return heif_error_unsupported_parameter;
}

#if 0
static void save_strcpy(char* dst, int dst_size, const char* src)
{
  strncpy(dst, src, dst_size-1);
  dst[dst_size-1] = 0;
}
#endif

struct heif_error aom_get_parameter_string(void* encoder_raw, const char* name,
                                           char* value, int value_size)
{
#if 0
  struct encoder_struct_aom* encoder = (struct encoder_struct_aom*)encoder_raw;

  if (strcmp(name, kParam_preset)==0) {
    save_strcpy(value, value_size, encoder->preset.c_str());
    return heif_error_ok;
  }
  else if (strcmp(name, kParam_tune)==0) {
    save_strcpy(value, value_size, encoder->tune.c_str());
    return heif_error_ok;
  }
#endif

  return heif_error_unsupported_parameter;
}


static void aom_set_default_parameters(void* encoder)
{
  for (const struct heif_encoder_parameter** p = aom_encoder_parameter_ptrs; *p; p++) {
    const struct heif_encoder_parameter* param = *p;

    if (param->has_default) {
      switch (param->type) {
      case heif_encoder_parameter_type_integer:
        aom_set_parameter_integer(encoder, param->name, param->integer.default_value);
        break;
      case heif_encoder_parameter_type_boolean:
        aom_set_parameter_boolean(encoder, param->name, param->boolean.default_value);
        break;
      case heif_encoder_parameter_type_string:
        aom_set_parameter_string(encoder, param->name, param->string.default_value);
        break;
      }
    }
  }
}


void aom_query_input_colorspace(heif_colorspace* colorspace, heif_chroma* chroma)
{
  *colorspace = heif_colorspace_YCbCr;
  *chroma = heif_chroma_420;
}


// TODO: encode as still frame (seq header)
static int encode_frame(aom_codec_ctx_t *codec, aom_image_t *img)
{
  int got_pkts = 0;
  //aom_codec_iter_t iter = NULL;
  int frame_index = 0; // only encoding a single frame
  int flags = 0; // no flags

  //const aom_codec_cx_pkt_t *pkt = NULL;
  const aom_codec_err_t res = aom_codec_encode(codec, img, frame_index, 1, flags);
  if (res != AOM_CODEC_OK) {
    printf("Failed to encode frame\n");
    assert(0);
  }

  return got_pkts;
}


struct heif_error aom_encode_image(void* encoder_raw, const struct heif_image* image,
                                   heif_image_input_class input_class)
{
  struct encoder_struct_aom* encoder = (struct encoder_struct_aom*)encoder_raw;

  const int source_width  = heif_image_get_width(image, heif_channel_Y) & ~1;
  const int source_height = heif_image_get_height(image, heif_channel_Y) & ~1;

  // --- copy libheif image to aom image

  aom_image_t input_image;

  if (!aom_img_alloc(&input_image, AOM_IMG_FMT_I420,
                     source_width, source_height, 1)) {
    printf("Failed to allocate image.\n");
    assert(false);
    // TODO
  }


  for (int plane=0; plane<3; plane++) {
    unsigned char *buf = input_image.planes[plane];
    const int stride = input_image.stride[plane];

    /*
    const int w = aom_img_plane_width(img, plane) *
                  ((img->fmt & AOM_IMG_FMT_HIGHBITDEPTH) ? 2 : 1);
    const int h = aom_img_plane_height(img, plane);
    */

    int in_stride=0;
    const uint8_t* in_p = heif_image_get_plane_readonly(image, (heif_channel)plane, &in_stride);

    int w = source_width;
    int h = source_height;

    if (plane != 0) {
      w/=2;
      h/=2;
    }

    for (int y=0; y<h; y++) {
      memcpy(buf, &in_p[y*in_stride], w);
      buf += stride;
    }
  }



  // --- configure codec

  unsigned int aomUsage = AOM_USAGE_GOOD_QUALITY;


  aom_codec_enc_cfg_t cfg;
  aom_codec_err_t res = aom_codec_enc_config_default(encoder->iface, &cfg, aomUsage);
  if (res) {
    printf("Failed to get default codec config.\n");
    assert(0);
    // TODO
  }

  int bitrate=200;
  cfg.g_w = source_width;
  cfg.g_h = source_height;
  //cfg.g_timebase.num = info.time_base.numerator;
  //cfg.g_timebase.den = info.time_base.denominator;
  cfg.rc_target_bitrate = bitrate;
  cfg.g_error_resilient = 0; // (aom_codec_er_flags_t)strtoul(argv[7], NULL, 0);



  // --- initialize codec

  if (aom_codec_enc_init(&encoder->codec, encoder->iface, &cfg, 0)) {
    printf("Failed to initialize encoder\n");
    assert(0);
    // TODO
  }


  int aomCpuUsed = 5;

  if (aomCpuUsed != -1) {
    aom_codec_control(&encoder->codec, AOME_SET_CPUUSED, aomCpuUsed);
  }


  // --- encode frame

  /*
    int flags = 0;
    if (keyframe_interval > 0 && frame_count % keyframe_interval == 0)
      flags |= AOM_EFLAG_FORCE_KF;
  */

  encode_frame(&encoder->codec, &input_image); //, frame_count++, flags, writer);
  //frames_encoded++;
  //if (max_frames > 0 && frames_encoded >= max_frames) break;


  // Flush encoder.
  //while (encode_frame(&encoder->codec, NULL)) continue; //, -1, 0, writer)) continue;

  /*
  printf("\n");
  fclose(infile);
  printf("Processed %d frames.\n", frame_count);
  */

  aom_img_free(&input_image);


#if 0
  struct encoder_struct_aom* encoder = (struct encoder_struct_aom*)encoder_raw;

  // close previous encoder if there is still one hanging around
  if (encoder->encoder) {
    const x265_api* api = x265_api_get(encoder->bit_depth);
    api->encoder_close(encoder->encoder);
    encoder->encoder = nullptr;
  }



  int bit_depth = heif_image_get_bits_per_pixel(image, heif_channel_Y);

  const x265_api* api = x265_api_get(bit_depth);
  if (api==nullptr) {
    struct heif_error err = {
      heif_error_Encoder_plugin_error,
      heif_suberror_Unsupported_bit_depth,
      kError_unsuppoerted_bit_depth
    };
    return err;
  }

  x265_param* param = api->param_alloc();
  api->param_default_preset(param, encoder->preset.c_str(), encoder->tune.c_str());

  if (bit_depth == 8) api->param_apply_profile(param, "mainstillpicture");
  else if (bit_depth == 10) api->param_apply_profile(param, "main10-intra");
  else if (bit_depth == 12) api->param_apply_profile(param, "main12-intra");
  else return heif_error_unsupported_parameter;


  param->fpsNum = 1;
  param->fpsDenom = 1;

  // BPG uses CQP. It does not seem to be better though.
  //  param->rc.rateControlMode = X265_RC_CQP;
  //  param->rc.qp = (100 - encoder->quality)/2;
  param->totalFrames = 1;
  param->internalCsp = X265_CSP_I420;
  api->param_parse(param, "info", "0");
  api->param_parse(param, "limit-modes", "0");
  api->param_parse(param, "limit-refs", "0");
  api->param_parse(param, "ctu", "64");
  api->param_parse(param, "rskip", "0");

  api->param_parse(param, "rect", "1");
  api->param_parse(param, "amp", "1");
  api->param_parse(param, "aq-mode", "1");
  api->param_parse(param, "psy-rd", "1.0");
  api->param_parse(param, "psy-rdoq", "1.0");

  api->param_parse(param, "range", "full");


  for (const auto& p : encoder->parameters) {
    if (p.name == heif_encoder_parameter_name_quality) {
      // quality=0   -> crf=50
      // quality=50  -> crf=25
      // quality=100 -> crf=0

      param->rc.rfConstant = (100 - p.value_int)/2;
    }
    else if (p.name == heif_encoder_parameter_name_lossless) {
      param->bLossless = p.value_int;
    }
    else if (p.name == kParam_TU_intra_depth) {
      char buf[100];
      sprintf(buf, "%d", p.value_int);
      api->param_parse(param, "tu-intra-depth", buf);
    }
    else if (p.name == kParam_complexity) {
      const int complexity = p.value_int;

      if (complexity >= 60) {
        api->param_parse(param, "rd-refine", "1"); // increases computation time
        api->param_parse(param, "rd", "6");
      }

      if (complexity >= 70) {
        api->param_parse(param, "cu-lossless", "1"); // increases computation time
      }

      if (complexity >= 90) {
        api->param_parse(param, "wpp", "0"); // setting to 0 significantly increases computation time
      }
    }
    else if (strncmp(p.name.c_str(), "x265:", 5)==0) {
      std::string x265p = p.name.substr(5);
      api->param_parse(param, x265p.c_str(), p.value_string.c_str());
    }
  }

  param->logLevel = encoder->logLevel;

  param->sourceWidth  = heif_image_get_width(image, heif_channel_Y) & ~1;
  param->sourceHeight = heif_image_get_height(image, heif_channel_Y) & ~1;
  param->internalBitDepth = bit_depth;



  x265_picture* pic = api->picture_alloc();
  api->picture_init(param, pic);

  pic->planes[0] = (void*)heif_image_get_plane_readonly(image, heif_channel_Y,  &pic->stride[0]);
  pic->planes[1] = (void*)heif_image_get_plane_readonly(image, heif_channel_Cb, &pic->stride[1]);
  pic->planes[2] = (void*)heif_image_get_plane_readonly(image, heif_channel_Cr, &pic->stride[2]);
  pic->bitDepth = bit_depth;


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
#endif

  return heif_error_ok;
}


struct heif_error aom_get_compressed_data(void* encoder_raw, uint8_t** data, int* size,
                                          enum heif_encoded_data_type* type)
{
  struct encoder_struct_aom* encoder = (struct encoder_struct_aom*)encoder_raw;

  const aom_codec_cx_pkt_t *pkt = NULL;

  for (;;) {
    if ((pkt = aom_codec_get_cx_data(&encoder->codec, &encoder->iter)) != NULL) {

      if (pkt->kind == AOM_CODEC_CX_FRAME_PKT) {
        //std::cerr.write((char*)pkt->data.frame.buf, pkt->data.frame.sz);

        printf("packet of size: %d\n",(int)pkt->data.frame.sz);


        // TODO: split the received data into separate OBUs
        // This allows the libheif to easily extract the sequence header for the av1C header

        *data = (uint8_t*)pkt->data.frame.buf;
        *size = (int)pkt->data.frame.sz;

        encoder->got_packets = true;

        return heif_error_ok;
      }
    }


    if (encoder->flushed && !encoder->got_packets) {
      *data = nullptr;
      *size = 0;

      return heif_error_ok;
    }


    int flags = 0;
    const aom_codec_err_t res = aom_codec_encode(&encoder->codec, NULL, -1, 0, flags);
    if (res != AOM_CODEC_OK) {
      printf("Failed to encode frame\n");
      assert(0);
    }

    encoder->iter = NULL;
    encoder->got_packets = false;
    encoder->flushed = true;
  }





#if 0
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
#endif
}


static const struct heif_encoder_plugin encoder_plugin_aom
{
  /* plugin_api_version */ 1,
  /* compression_format */ heif_compression_AV1,
  /* id_name */ "aom",
  /* priority */ AOM_PLUGIN_PRIORITY,
  /* supports_lossy_compression */ true,
  /* supports_lossless_compression */ true,
  /* get_plugin_name */ aom_plugin_name,
  /* init_plugin */ aom_init_plugin,
  /* cleanup_plugin */ aom_cleanup_plugin,
  /* new_encoder */ aom_new_encoder,
  /* free_encoder */ aom_free_encoder,
  /* set_parameter_quality */ aom_set_parameter_quality,
  /* get_parameter_quality */ aom_get_parameter_quality,
  /* set_parameter_lossless */ aom_set_parameter_lossless,
  /* get_parameter_lossless */ aom_get_parameter_lossless,
  /* set_parameter_logging_level */ aom_set_parameter_logging_level,
  /* get_parameter_logging_level */ aom_get_parameter_logging_level,
  /* list_parameters */ aom_list_parameters,
  /* set_parameter_integer */ aom_set_parameter_integer,
  /* get_parameter_integer */ aom_get_parameter_integer,
  /* set_parameter_boolean */ aom_set_parameter_integer, // boolean also maps to integer function
  /* get_parameter_boolean */ aom_get_parameter_integer, // boolean also maps to integer function
  /* set_parameter_string */ aom_set_parameter_string,
  /* get_parameter_string */ aom_get_parameter_string,
  /* query_input_colorspace */ aom_query_input_colorspace,
  /* encode_image */ aom_encode_image,
  /* get_compressed_data */ aom_get_compressed_data
};

const struct heif_encoder_plugin* get_encoder_plugin_aom()
{
  return &encoder_plugin_aom;
}
