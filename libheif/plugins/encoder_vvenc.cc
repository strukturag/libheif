/*
 * HEIF codec.
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
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
#include "encoder_vvenc.h"
#include <memory>
#include <string>   // apparently, this is a false positive of cpplint
#include <cstring>
#include <cassert>
#include <vector>
#include <algorithm>

extern "C" {
#include <vvenc/vvenc.h>
}


// TODO: it seems that the encoder does not support monochrome input. This affects also images with alpha channels.

static const char* kError_unspecified_error = "Unspecified encoder error";
static const char* kError_unsupported_bit_depth = "Bit depth not supported by vvenc";
static const char* kError_unsupported_chroma = "Unsupported chroma type";
//static const char* kError_unsupported_image_size = "Images smaller than 16 pixels are not supported";


struct encoder_struct_vvenc
{
  int quality = 32;
  bool lossless = false;

  std::vector<uint8_t> output_data;
  size_t output_idx = 0;
};

static const int vvenc_PLUGIN_PRIORITY = 100;

#define MAX_PLUGIN_NAME_LENGTH 80

static char plugin_name[MAX_PLUGIN_NAME_LENGTH];


static void vvenc_set_default_parameters(void* encoder);


static const char* vvenc_plugin_name()
{
  strcpy(plugin_name, "vvenc VVC encoder");
  return plugin_name;
}


#define MAX_NPARAMETERS 10

static struct heif_encoder_parameter vvenc_encoder_params[MAX_NPARAMETERS];
static const struct heif_encoder_parameter* vvenc_encoder_parameter_ptrs[MAX_NPARAMETERS + 1];

static void vvenc_init_parameters()
{
  struct heif_encoder_parameter* p = vvenc_encoder_params;
  const struct heif_encoder_parameter** d = vvenc_encoder_parameter_ptrs;
  int i = 0;

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

  d[i++] = nullptr;
}


const struct heif_encoder_parameter** vvenc_list_parameters(void* encoder)
{
  return vvenc_encoder_parameter_ptrs;
}


static void vvenc_init_plugin()
{
  vvenc_init_parameters();
}


static void vvenc_cleanup_plugin()
{
}


static struct heif_error vvenc_new_encoder(void** enc)
{
  struct encoder_struct_vvenc* encoder = new encoder_struct_vvenc();
  struct heif_error err = heif_error_ok;

  *enc = encoder;

  // set default parameters

  vvenc_set_default_parameters(encoder);

  return err;
}

static void vvenc_free_encoder(void* encoder_raw)
{
  struct encoder_struct_vvenc* encoder = (struct encoder_struct_vvenc*) encoder_raw;

  delete encoder;
}

static struct heif_error vvenc_set_parameter_quality(void* encoder_raw, int quality)
{
  struct encoder_struct_vvenc* encoder = (struct encoder_struct_vvenc*) encoder_raw;

  if (quality < 0 || quality > 100) {
    return heif_error_invalid_parameter_value;
  }

  encoder->quality = quality;

  return heif_error_ok;
}

static struct heif_error vvenc_get_parameter_quality(void* encoder_raw, int* quality)
{
  struct encoder_struct_vvenc* encoder = (struct encoder_struct_vvenc*) encoder_raw;

  *quality = encoder->quality;

  return heif_error_ok;
}

static struct heif_error vvenc_set_parameter_lossless(void* encoder_raw, int enable)
{
  struct encoder_struct_vvenc* encoder = (struct encoder_struct_vvenc*) encoder_raw;

  encoder->lossless = enable ? 1 : 0;

  return heif_error_ok;
}

static struct heif_error vvenc_get_parameter_lossless(void* encoder_raw, int* enable)
{
  struct encoder_struct_vvenc* encoder = (struct encoder_struct_vvenc*) encoder_raw;

  *enable = encoder->lossless;

  return heif_error_ok;
}

static struct heif_error vvenc_set_parameter_logging_level(void* encoder_raw, int logging)
{
//  struct encoder_struct_vvenc* encoder = (struct encoder_struct_vvenc*) encoder_raw;

// return heif_error_invalid_parameter_value;

  return heif_error_ok;
}

static struct heif_error vvenc_get_parameter_logging_level(void* encoder_raw, int* loglevel)
{
//  struct encoder_struct_vvenc* encoder = (struct encoder_struct_vvenc*) encoder_raw;

  *loglevel = 0;

  return heif_error_ok;
}


static struct heif_error vvenc_set_parameter_integer(void* encoder_raw, const char* name, int value)
{
  struct encoder_struct_vvenc* encoder = (struct encoder_struct_vvenc*) encoder_raw;

  if (strcmp(name, heif_encoder_parameter_name_quality) == 0) {
    return vvenc_set_parameter_quality(encoder, value);
  }
  else if (strcmp(name, heif_encoder_parameter_name_lossless) == 0) {
    return vvenc_set_parameter_lossless(encoder, value);
  }

  return heif_error_unsupported_parameter;
}

static struct heif_error vvenc_get_parameter_integer(void* encoder_raw, const char* name, int* value)
{
  struct encoder_struct_vvenc* encoder = (struct encoder_struct_vvenc*) encoder_raw;

  if (strcmp(name, heif_encoder_parameter_name_quality) == 0) {
    return vvenc_get_parameter_quality(encoder, value);
  }
  else if (strcmp(name, heif_encoder_parameter_name_lossless) == 0) {
    return vvenc_get_parameter_lossless(encoder, value);
  }

  return heif_error_unsupported_parameter;
}


static struct heif_error vvenc_set_parameter_boolean(void* encoder, const char* name, int value)
{
  if (strcmp(name, heif_encoder_parameter_name_lossless) == 0) {
    return vvenc_set_parameter_lossless(encoder, value);
  }

  return heif_error_unsupported_parameter;
}

// Unused, will use "vvenc_get_parameter_integer" instead.
/*
static struct heif_error vvenc_get_parameter_boolean(void* encoder, const char* name, int* value)
{
  if (strcmp(name, heif_encoder_parameter_name_lossless)==0) {
    return vvenc_get_parameter_lossless(encoder,value);
  }

  return heif_error_unsupported_parameter;
}
*/


static struct heif_error vvenc_set_parameter_string(void* encoder_raw, const char* name, const char* value)
{
  return heif_error_unsupported_parameter;
}

static struct heif_error vvenc_get_parameter_string(void* encoder_raw, const char* name,
                                                    char* value, int value_size)
{
  return heif_error_unsupported_parameter;
}


static void vvenc_set_default_parameters(void* encoder)
{
  for (const struct heif_encoder_parameter** p = vvenc_encoder_parameter_ptrs; *p; p++) {
    const struct heif_encoder_parameter* param = *p;

    if (param->has_default) {
      switch (param->type) {
        case heif_encoder_parameter_type_integer:
          vvenc_set_parameter_integer(encoder, param->name, param->integer.default_value);
          break;
        case heif_encoder_parameter_type_boolean:
          vvenc_set_parameter_boolean(encoder, param->name, param->boolean.default_value);
          break;
        case heif_encoder_parameter_type_string:
          vvenc_set_parameter_string(encoder, param->name, param->string.default_value);
          break;
      }
    }
  }
}


static void vvenc_query_input_colorspace(heif_colorspace* colorspace, heif_chroma* chroma)
{
  if (*colorspace == heif_colorspace_monochrome) {
    *colorspace = heif_colorspace_monochrome;
    *chroma = heif_chroma_monochrome;
  }
  else {
    *colorspace = heif_colorspace_YCbCr;
    *chroma = heif_chroma_420;
  }
}


static void vvenc_query_input_colorspace2(void* encoder_raw, heif_colorspace* colorspace, heif_chroma* chroma)
{
  if (*colorspace == heif_colorspace_monochrome) {
    *colorspace = heif_colorspace_monochrome;
    *chroma = heif_chroma_monochrome;
  }
  else {
    *colorspace = heif_colorspace_YCbCr;
    if (*chroma != heif_chroma_420 &&
        *chroma != heif_chroma_422 &&
        *chroma != heif_chroma_444) {
      *chroma = heif_chroma_420;
    }
  }
}

void vvenc_query_encoded_size(void* encoder_raw, uint32_t input_width, uint32_t input_height,
                              uint32_t* encoded_width, uint32_t* encoded_height)
{
  *encoded_width = (input_width + 7) & ~0x7;
  *encoded_height = (input_height + 7) & ~0x7;
}


#include <iostream>
#include <logging.h>

static void append_chunk_data(struct encoder_struct_vvenc* encoder, vvencAccessUnit* au)
{
#if 0
  std::cout << "DATA\n";
  std::cout << write_raw_data_as_hex(au->payload, au->payloadUsedSize, {}, {});
  std::cout << "---\n";
#endif

  size_t old_size = encoder->output_data.size();
  encoder->output_data.resize(old_size + au->payloadUsedSize);
  memcpy(encoder->output_data.data() + old_size, au->payload, au->payloadUsedSize);
}


static void copy_plane(int16_t*& out_p, size_t& out_stride, const uint8_t* in_p, size_t in_stride, int w, int h, int padded_width, int padded_height)
{
  out_stride = padded_width;
  out_p = new int16_t[out_stride * w * h];

  for (int y = 0; y < padded_height; y++) {
    int sy = std::min(y, h - 1); // source y

    for (int x = 0; x < w; x++) {
      out_p[y * out_stride + x] = in_p[sy * in_stride + x];
    }

    for (int x = w; x < padded_width; x++) {
      out_p[y * out_stride + x] = in_p[sy * in_stride + w - 1];
    }
  }
}


static struct heif_error vvenc_encode_image(void* encoder_raw, const struct heif_image* image,
                                            heif_image_input_class input_class)
{
  struct encoder_struct_vvenc* encoder = (struct encoder_struct_vvenc*) encoder_raw;

  int bit_depth = heif_image_get_bits_per_pixel_range(image, heif_channel_Y);
  bool isGreyscale = (heif_image_get_colorspace(image) == heif_colorspace_monochrome);
  heif_chroma chroma = heif_image_get_chroma_format(image);

  if (bit_depth != 8) {
    return heif_error{
        heif_error_Encoder_plugin_error,
        heif_suberror_Unsupported_image_type,
        kError_unsupported_bit_depth
    };
  }


  int input_width = heif_image_get_width(image, heif_channel_Y);
  int input_height = heif_image_get_height(image, heif_channel_Y);

  int input_chroma_width = 0;
  int input_chroma_height = 0;

  uint32_t encoded_width, encoded_height;
  vvenc_query_encoded_size(encoder_raw, input_width, input_height, &encoded_width, &encoded_height);

  vvencChromaFormat vvencChroma;
  int chroma_stride_shift = 0;
  int chroma_height_shift = 0;

  if (isGreyscale) {
    vvencChroma = VVENC_CHROMA_400;
  }
  else if (chroma == heif_chroma_420) {
    vvencChroma = VVENC_CHROMA_420;
    chroma_stride_shift = 1;
    chroma_height_shift = 1;
    input_chroma_width = (input_width + 1) / 2;
    input_chroma_height = (input_height + 1) / 2;
  }
  else if (chroma == heif_chroma_422) {
    vvencChroma = VVENC_CHROMA_422;
    chroma_stride_shift = 1;
    chroma_height_shift = 0;
    input_chroma_width = (input_width + 1) / 2;
    input_chroma_height = input_height;
  }
  else if (chroma == heif_chroma_444) {
    vvencChroma = VVENC_CHROMA_444;
    chroma_stride_shift = 0;
    chroma_height_shift = 0;
    input_chroma_width = input_width;
    input_chroma_height = input_height;
  }
  else {
    return heif_error{
        heif_error_Encoder_plugin_error,
        heif_suberror_Unsupported_image_type,
        kError_unsupported_chroma
    };
  }

  if (chroma != heif_chroma_monochrome) {
    int w = heif_image_get_width(image, heif_channel_Y);
    int h = heif_image_get_height(image, heif_channel_Y);
    if (chroma != heif_chroma_444) { w = (w + 1) / 2; }
    if (chroma == heif_chroma_420) { h = (h + 1) / 2; }

    assert(heif_image_get_width(image, heif_channel_Cb) == w);
    assert(heif_image_get_width(image, heif_channel_Cr) == w);
    assert(heif_image_get_height(image, heif_channel_Cb) == h);
    assert(heif_image_get_height(image, heif_channel_Cr) == h);
    (void) w;
    (void) h;
  }


  vvenc_config params;

  // invert encoder quality range and scale to 0-63
  int encoder_quality = 63 - encoder->quality*63/100;

  int ret = vvenc_init_default(&params, encoded_width, encoded_height, 25, 0,
                               encoder_quality,
                               VVENC_MEDIUM);
  if (ret != VVENC_OK) {
    // TODO: cleanup memory

    return heif_error{
        heif_error_Encoder_plugin_error,
        heif_suberror_Encoder_encoding,
        kError_unspecified_error
    };
  }

  params.m_inputBitDepth[0] = bit_depth;
  params.m_inputBitDepth[1] = bit_depth;
  params.m_outputBitDepth[0] = bit_depth;
  params.m_outputBitDepth[1] = bit_depth;
  params.m_internalBitDepth[0] = bit_depth;
  params.m_internalBitDepth[1] = bit_depth;

  vvencEncoder* vvencoder = vvenc_encoder_create();
  ret = vvenc_encoder_open(vvencoder, &params);
  if (ret != VVENC_OK) {
    // TODO: cleanup memory

    return heif_error{
        heif_error_Encoder_plugin_error,
        heif_suberror_Encoder_encoding,
        kError_unspecified_error
    };
  }


  struct heif_color_profile_nclx* nclx = nullptr;
  heif_error err = heif_image_get_nclx_color_profile(image, &nclx);
  if (err.code != heif_error_Ok) {
    nclx = nullptr;
  }

  // make sure NCLX profile is deleted at end of function
  auto nclx_deleter = std::unique_ptr<heif_color_profile_nclx, void (*)(heif_color_profile_nclx*)>(nclx, heif_nclx_color_profile_free);

#if 0
  if (nclx) {
    config->vui.fullrange = nclx->full_range_flag;
  }
  else {
    config->vui.fullrange = 1;
  }

  if (nclx &&
      (input_class == heif_image_input_class_normal ||
       input_class == heif_image_input_class_thumbnail)) {
    config->vui.colorprim = nclx->color_primaries;
    config->vui.transfer = nclx->transfer_characteristics;
    config->vui.colormatrix = nclx->matrix_coefficients;
  }

  config->qp = ((100 - encoder->quality) * 51 + 50) / 100;
  config->lossless = encoder->lossless ? 1 : 0;

  config->width = encoded_width;
  config->height = encoded_height;
#endif

  // Note: it is ok to cast away the const, as the image content is not changed.
  // However, we have to guarantee that there are no plane pointers or stride values kept over calling the svt_encode_image() function.
  /*
  err = heif_image_extend_padding_to_size(const_cast<struct heif_image*>(image),
                                          param->sourceWidth,
                                          param->sourceHeight);
  if (err.code) {
    return err;
  }
*/

  vvencYUVBuffer* yuvbuf = vvenc_YUVBuffer_alloc();
  vvenc_YUVBuffer_alloc_buffer(yuvbuf, vvencChroma, encoded_width, encoded_height);

  vvencAccessUnit* au = vvenc_accessUnit_alloc();

  const int auSizeScale = (vvencChroma <= VVENC_CHROMA_420 ? 2 : 3);
  vvenc_accessUnit_alloc_payload(au, auSizeScale * encoded_width * encoded_height + 1024);

  // vvenc_init_pass( encoder, pass, statsfilename );

  int16_t* yptr = nullptr;
  int16_t* cbptr = nullptr;
  int16_t* crptr = nullptr;
  size_t ystride = 0;
  size_t cbstride = 0;
  size_t crstride = 0;

  if (isGreyscale) {
    size_t stride;
    const uint8_t* data = heif_image_get_plane_readonly2(image, heif_channel_Y, &stride);

    copy_plane(yptr, ystride, data, stride, input_width, input_height, encoded_width, encoded_height);

    yuvbuf->planes[0].ptr = yptr;
    yuvbuf->planes[0].width = encoded_width;
    yuvbuf->planes[0].height = encoded_height;
    yuvbuf->planes[0].stride = (int)ystride;
  }
  else {
    size_t stride;
    const uint8_t* data;

    data = heif_image_get_plane_readonly2(image, heif_channel_Y, &stride);
    copy_plane(yptr, ystride, data, stride, input_width, input_height, encoded_width, encoded_height);

    data = heif_image_get_plane_readonly2(image, heif_channel_Cb, &stride);
    copy_plane(cbptr, cbstride, data, stride, input_chroma_width, input_chroma_height,
               encoded_width >> chroma_stride_shift, encoded_height >> chroma_height_shift);

    data = heif_image_get_plane_readonly2(image, heif_channel_Cr, &stride);
    copy_plane(crptr, crstride, data, stride, input_chroma_width, input_chroma_height,
               encoded_width >> chroma_stride_shift, encoded_height >> chroma_height_shift);

    yuvbuf->planes[0].ptr = yptr;
    yuvbuf->planes[0].width = encoded_width;
    yuvbuf->planes[0].height = encoded_height;
    yuvbuf->planes[0].stride = (int)ystride;

    yuvbuf->planes[1].ptr = cbptr;
    yuvbuf->planes[1].width = encoded_width >> chroma_stride_shift;
    yuvbuf->planes[1].height = encoded_height >> chroma_height_shift;
    yuvbuf->planes[1].stride = (int)cbstride;

    yuvbuf->planes[2].ptr = crptr;
    yuvbuf->planes[2].width = encoded_width >> chroma_stride_shift;
    yuvbuf->planes[2].height = encoded_height >> chroma_height_shift;
    yuvbuf->planes[2].stride = (int)crstride;
  }

  //yuvbuf->cts     = frame->pts;
  //yuvbuf->ctsValid = true;


  bool encDone;

  ret = vvenc_encode(vvencoder, yuvbuf, au, &encDone);
  if (ret != VVENC_OK) {
    vvenc_encoder_close(vvencoder);
    vvenc_YUVBuffer_free(yuvbuf, true); // release storage and payload memory
    vvenc_accessUnit_free(au, true); // release storage and payload memory

    return heif_error{
        heif_error_Encoder_plugin_error,
        heif_suberror_Encoder_encoding,
        kError_unspecified_error
    };
  }

  if (au->payloadUsedSize > 0) {
    append_chunk_data(encoder, au);
  }

  while (!encDone) {
    ret = vvenc_encode(vvencoder, nullptr, au, &encDone);
    if (ret != VVENC_OK) {
      vvenc_encoder_close(vvencoder);
      vvenc_YUVBuffer_free(yuvbuf, true); // release storage and payload memory
      vvenc_accessUnit_free(au, true); // release storage and payload memory

      return heif_error{
          heif_error_Encoder_plugin_error,
          heif_suberror_Encoder_encoding,
          kError_unspecified_error
      };
    }

    if (au->payloadUsedSize > 0) {
      append_chunk_data(encoder, au);
    }
  }

  vvenc_encoder_close(vvencoder);
  vvenc_YUVBuffer_free(yuvbuf, true); // release storage and payload memory
  vvenc_accessUnit_free(au, true); // release storage and payload memory

  /*
  delete[] yptr;
  delete[] cbptr;
  delete[] crptr;
*/

  return heif_error_ok;
}


static struct heif_error vvenc_get_compressed_data(void* encoder_raw, uint8_t** data, int* size,
                                                   enum heif_encoded_data_type* type)
{
  struct encoder_struct_vvenc* encoder = (struct encoder_struct_vvenc*) encoder_raw;

  if (encoder->output_idx == encoder->output_data.size()) {
    *data = nullptr;
    *size = 0;

    return heif_error_ok;
  }

  size_t start_idx = encoder->output_idx;

  while (start_idx < encoder->output_data.size() - 3 &&
         (encoder->output_data[start_idx] != 0 ||
          encoder->output_data[start_idx + 1] != 0 ||
          encoder->output_data[start_idx + 2] != 1)) {
    start_idx++;
  }

  size_t end_idx = start_idx + 1;

  while (end_idx < encoder->output_data.size() - 3 &&
         (encoder->output_data[end_idx] != 0 ||
          encoder->output_data[end_idx + 1] != 0 ||
          encoder->output_data[end_idx + 2] != 1)) {
    end_idx++;
  }

  if (end_idx == encoder->output_data.size() - 3) {
    end_idx = encoder->output_data.size();
  }

  *data = encoder->output_data.data() + start_idx + 3;
  *size = (int) (end_idx - start_idx - 3);

  encoder->output_idx = end_idx;

  return heif_error_ok;
}


static const struct heif_encoder_plugin encoder_plugin_vvenc
    {
        /* plugin_api_version */ 3,
        /* compression_format */ heif_compression_VVC,
        /* id_name */ "vvenc",
        /* priority */ vvenc_PLUGIN_PRIORITY,
        /* supports_lossy_compression */ true,
        /* supports_lossless_compression */ true,
        /* get_plugin_name */ vvenc_plugin_name,
        /* init_plugin */ vvenc_init_plugin,
        /* cleanup_plugin */ vvenc_cleanup_plugin,
        /* new_encoder */ vvenc_new_encoder,
        /* free_encoder */ vvenc_free_encoder,
        /* set_parameter_quality */ vvenc_set_parameter_quality,
        /* get_parameter_quality */ vvenc_get_parameter_quality,
        /* set_parameter_lossless */ vvenc_set_parameter_lossless,
        /* get_parameter_lossless */ vvenc_get_parameter_lossless,
        /* set_parameter_logging_level */ vvenc_set_parameter_logging_level,
        /* get_parameter_logging_level */ vvenc_get_parameter_logging_level,
        /* list_parameters */ vvenc_list_parameters,
        /* set_parameter_integer */ vvenc_set_parameter_integer,
        /* get_parameter_integer */ vvenc_get_parameter_integer,
        /* set_parameter_boolean */ vvenc_set_parameter_integer, // boolean also maps to integer function
        /* get_parameter_boolean */ vvenc_get_parameter_integer, // boolean also maps to integer function
        /* set_parameter_string */ vvenc_set_parameter_string,
        /* get_parameter_string */ vvenc_get_parameter_string,
        /* query_input_colorspace */ vvenc_query_input_colorspace,
        /* encode_image */ vvenc_encode_image,
        /* get_compressed_data */ vvenc_get_compressed_data,
        /* query_input_colorspace (v2) */ vvenc_query_input_colorspace2,
        /* query_encoded_size (v3) */ vvenc_query_encoded_size
    };

const struct heif_encoder_plugin* get_encoder_plugin_vvenc()
{
  return &encoder_plugin_vvenc;
}


#if PLUGIN_VVENC
heif_plugin_info plugin_info {
  1,
  heif_plugin_type_encoder,
  &encoder_plugin_vvenc
};
#endif
