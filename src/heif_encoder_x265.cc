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

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <memory>
#include <string.h>
#include <stdio.h>

extern "C" {
#include <x265.h>
}


struct x265_encoder_struct
{
  x265_encoder* encoder;

  x265_nal* nals;
  uint32_t num_nals;
  uint32_t nal_output_counter;
};

struct x265_options_struct
{
  x265_param* params;
};

static const char kSuccess[] = "Success";

static const int X265_PLUGIN_PRIORITY = 100;

#define MAX_PLUGIN_NAME_LENGTH 80

static char plugin_name[MAX_PLUGIN_NAME_LENGTH];


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


void x265_init_plugin()
{
}


void x265_deinit_plugin()
{
}


struct heif_error x265_new_encoder(void** enc)
{
  struct x265_encoder_struct* encoder = new x265_encoder_struct();
  struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, kSuccess };

  // encoder has to be allocated in x265_encode_image, because it needs to know the image size
  encoder->encoder = nullptr;

  encoder->nals = nullptr;
  encoder->num_nals = 0;
  encoder->nal_output_counter = 0;

  *enc = encoder;

  return err;
}

void x265_free_encoder(void* encoder_raw)
{
  struct x265_encoder_struct* encoder = (struct x265_encoder_struct*)encoder_raw;

  if (encoder->encoder) {
    x265_encoder_close(encoder->encoder);
  }

  delete encoder;
}

struct heif_encoder_options* x265_alloc_options(void* encoder_raw) {
  struct x265_options_struct* options = new x265_options_struct();

  x265_param* params = x265_param_alloc();
  x265_param_default_preset(params, "slow", "ssim");
  x265_param_apply_profile(params, "mainstillpicture");
  params->fpsNum = 1;
  params->fpsDenom = 1;
  params->sourceWidth = 0;
  params->sourceHeight = 0;
  params->logLevel = X265_LOG_NONE;

  options->params = params;
  return reinterpret_cast<struct heif_encoder_options*>(options);
}

void x265_free_options(void* encoder_raw, struct heif_encoder_options* options_raw) {
  struct x265_options_struct* options = reinterpret_cast<struct x265_options_struct*>(options_raw);

  x265_param_free(options->params);
  delete options;
}

struct heif_error x265_set_option_int(void* encoder_raw,
    struct heif_encoder_options* options_raw, const char* name, int value) {
  struct x265_options_struct* options = reinterpret_cast<struct x265_options_struct*>(options_raw);

  if (strcmp(name, HEIF_OPTION_QUALITY) == 0) {
    // quality=0   -> crf=50
    // quality=50  -> crf=25
    // quality=100 -> crf=0
    options->params->rc.rfConstant = (100-value)/2;
  } else if (strcmp(name, HEIF_OPTION_LOSSLESS) == 0) {
    options->params->bLossless = value;
  } else if (strcmp(name, HEIF_OPTION_LOGLEVEL) == 0) {
    if (value<0) value=0;
    if (value>4) value=4;

    options->params->logLevel = value;
  }

  struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, kSuccess };
  return err;
}

void x265_query_input_colorspace(heif_colorspace* colorspace, heif_chroma* chroma)
{
  *colorspace = heif_colorspace_YCbCr;
  *chroma = heif_chroma_420;
}


struct heif_error x265_encode_image(void* encoder_raw, struct heif_encoder_options* options_raw, const struct heif_image* image)
{
  struct x265_encoder_struct* encoder = (struct x265_encoder_struct*)encoder_raw;
  struct x265_options_struct* options = reinterpret_cast<struct x265_options_struct*>(options_raw);

  x265_picture* pic = x265_picture_alloc();
  x265_picture_init(options->params, pic);

  pic->planes[0] = (void*)heif_image_get_plane_readonly(image, heif_channel_Y,  &pic->stride[0]);
  pic->planes[1] = (void*)heif_image_get_plane_readonly(image, heif_channel_Cb, &pic->stride[1]);
  pic->planes[2] = (void*)heif_image_get_plane_readonly(image, heif_channel_Cr, &pic->stride[2]);
  pic->bitDepth = 8;

  options->params->sourceWidth  = heif_image_get_width(image, heif_channel_Y) & ~1;
  options->params->sourceHeight = heif_image_get_height(image, heif_channel_Y) & ~1;


  // close encoder after all data has been extracted

  if (encoder->encoder) {
    x265_encoder_close(encoder->encoder);
  }

  encoder->encoder = x265_encoder_open(options->params);

  int result = x265_encoder_encode(encoder->encoder,
                                   &encoder->nals,
                                   &encoder->num_nals,
                                   pic,
                                   NULL);
  (void)result;

  x265_picture_free(pic);

  encoder->nal_output_counter = 0;

  struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, kSuccess };
  return err;
}


struct heif_error x265_get_compressed_data(void* encoder_raw, uint8_t** data, int* size,
                                           enum heif_encoded_data_type* type)
{
  struct x265_encoder_struct* encoder = (struct x265_encoder_struct*)encoder_raw;


  if (encoder->encoder == nullptr) {
    *data = nullptr;
    *size = 0;

    struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, kSuccess };
    return err;
  }


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

        struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, kSuccess };
        return err;
      }
    }


    int result = x265_encoder_encode(encoder->encoder,
                                     &encoder->nals,
                                     &encoder->num_nals,
                                     NULL,
                                     NULL);
    if (result <= 0) {
      *data = nullptr;
      *size = 0;

      struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, kSuccess };
      return err;
    }
  }
}


static const struct heif_encoder_plugin encoder_plugin_x265
{
  .plugin_api_version = 1,
  .compression_format = heif_compression_HEVC,
  .id_name = "x265",
  .priority = X265_PLUGIN_PRIORITY,
  .get_plugin_name = x265_plugin_name,
  .init_plugin = x265_init_plugin,
  .deinit_plugin = x265_deinit_plugin,
  .new_encoder = x265_new_encoder,
  .free_encoder = x265_free_encoder,
  .alloc_options = x265_alloc_options,
  .free_options = x265_free_options,
  .set_option_int = x265_set_option_int,
  .query_input_colorspace = x265_query_input_colorspace,
  .encode_image = x265_encode_image,
  .get_compressed_data = x265_get_compressed_data
};

const struct heif_encoder_plugin* get_encoder_plugin_x265()
{
  return &encoder_plugin_x265;
}
