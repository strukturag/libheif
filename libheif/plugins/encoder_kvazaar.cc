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
#include "encoder_kvazaar.h"
#include <memory>
#include <string>   // apparently, this is a false positive of cpplint
#include <cstring>
#include <cassert>
#include <vector>
#include <algorithm>

extern "C" {
#include <kvazaar.h>
}


static const char* kError_unspecified_error = "Unspecified encoder error";
static const char* kError_unsupported_bit_depth = "Bit depth not supported by kvazaar";
static const char* kError_unsupported_chroma = "Unsupported chroma type";
//static const char* kError_unsupported_image_size = "Images smaller than 16 pixels are not supported";


struct encoder_struct_kvazaar
{
  int quality = 75;
  bool lossless = false;

  std::vector<uint8_t> output_data;
  size_t output_idx = 0;
};

static const int kvazaar_PLUGIN_PRIORITY = 100;

#define MAX_PLUGIN_NAME_LENGTH 80

static char plugin_name[MAX_PLUGIN_NAME_LENGTH];


static void kvazaar_set_default_parameters(void* encoder);


static const char* kvazaar_plugin_name()
{
  strcpy(plugin_name, "kvazaar HEVC encoder");
  return plugin_name;
}


#define MAX_NPARAMETERS 10

static struct heif_encoder_parameter kvazaar_encoder_params[MAX_NPARAMETERS];
static const struct heif_encoder_parameter* kvazaar_encoder_parameter_ptrs[MAX_NPARAMETERS + 1];

static void kvazaar_init_parameters()
{
  struct heif_encoder_parameter* p = kvazaar_encoder_params;
  const struct heif_encoder_parameter** d = kvazaar_encoder_parameter_ptrs;
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


const struct heif_encoder_parameter** kvazaar_list_parameters(void* encoder)
{
  return kvazaar_encoder_parameter_ptrs;
}


static void kvazaar_init_plugin()
{
  kvazaar_init_parameters();
}


static void kvazaar_cleanup_plugin()
{
}


static struct heif_error kvazaar_new_encoder(void** enc)
{
  struct encoder_struct_kvazaar* encoder = new encoder_struct_kvazaar();
  struct heif_error err = heif_error_ok;

  *enc = encoder;

  // set default parameters

  kvazaar_set_default_parameters(encoder);

  return err;
}

static void kvazaar_free_encoder(void* encoder_raw)
{
  struct encoder_struct_kvazaar* encoder = (struct encoder_struct_kvazaar*) encoder_raw;

  delete encoder;
}

static struct heif_error kvazaar_set_parameter_quality(void* encoder_raw, int quality)
{
  struct encoder_struct_kvazaar* encoder = (struct encoder_struct_kvazaar*) encoder_raw;

  if (quality < 0 || quality > 100) {
    return heif_error_invalid_parameter_value;
  }

  encoder->quality = quality;

  return heif_error_ok;
}

static struct heif_error kvazaar_get_parameter_quality(void* encoder_raw, int* quality)
{
  struct encoder_struct_kvazaar* encoder = (struct encoder_struct_kvazaar*) encoder_raw;

  *quality = encoder->quality;

  return heif_error_ok;
}

static struct heif_error kvazaar_set_parameter_lossless(void* encoder_raw, int enable)
{
  struct encoder_struct_kvazaar* encoder = (struct encoder_struct_kvazaar*) encoder_raw;

  encoder->lossless = enable ? 1 : 0;

  return heif_error_ok;
}

static struct heif_error kvazaar_get_parameter_lossless(void* encoder_raw, int* enable)
{
  struct encoder_struct_kvazaar* encoder = (struct encoder_struct_kvazaar*) encoder_raw;

  *enable = encoder->lossless;

  return heif_error_ok;
}

static struct heif_error kvazaar_set_parameter_logging_level(void* encoder_raw, int logging)
{
//  struct encoder_struct_kvazaar* encoder = (struct encoder_struct_kvazaar*) encoder_raw;

// return heif_error_invalid_parameter_value;

  return heif_error_ok;
}

static struct heif_error kvazaar_get_parameter_logging_level(void* encoder_raw, int* loglevel)
{
//  struct encoder_struct_kvazaar* encoder = (struct encoder_struct_kvazaar*) encoder_raw;

  *loglevel = 0;

  return heif_error_ok;
}


static struct heif_error kvazaar_set_parameter_integer(void* encoder_raw, const char* name, int value)
{
  struct encoder_struct_kvazaar* encoder = (struct encoder_struct_kvazaar*) encoder_raw;

  if (strcmp(name, heif_encoder_parameter_name_quality) == 0) {
    return kvazaar_set_parameter_quality(encoder, value);
  }
  else if (strcmp(name, heif_encoder_parameter_name_lossless) == 0) {
    return kvazaar_set_parameter_lossless(encoder, value);
  }

  return heif_error_unsupported_parameter;
}

static struct heif_error kvazaar_get_parameter_integer(void* encoder_raw, const char* name, int* value)
{
  struct encoder_struct_kvazaar* encoder = (struct encoder_struct_kvazaar*) encoder_raw;

  if (strcmp(name, heif_encoder_parameter_name_quality) == 0) {
    return kvazaar_get_parameter_quality(encoder, value);
  }
  else if (strcmp(name, heif_encoder_parameter_name_lossless) == 0) {
    return kvazaar_get_parameter_lossless(encoder, value);
  }

  return heif_error_unsupported_parameter;
}


static struct heif_error kvazaar_set_parameter_boolean(void* encoder, const char* name, int value)
{
  if (strcmp(name, heif_encoder_parameter_name_lossless) == 0) {
    return kvazaar_set_parameter_lossless(encoder, value);
  }

  return heif_error_unsupported_parameter;
}

// Unused, will use "kvazaar_get_parameter_integer" instead.
/*
static struct heif_error kvazaar_get_parameter_boolean(void* encoder, const char* name, int* value)
{
  if (strcmp(name, heif_encoder_parameter_name_lossless)==0) {
    return kvazaar_get_parameter_lossless(encoder,value);
  }

  return heif_error_unsupported_parameter;
}
*/


static struct heif_error kvazaar_set_parameter_string(void* encoder_raw, const char* name, const char* value)
{
  return heif_error_unsupported_parameter;
}

static struct heif_error kvazaar_get_parameter_string(void* encoder_raw, const char* name,
                                                      char* value, int value_size)
{
  return heif_error_unsupported_parameter;
}


static void kvazaar_set_default_parameters(void* encoder)
{
  for (const struct heif_encoder_parameter** p = kvazaar_encoder_parameter_ptrs; *p; p++) {
    const struct heif_encoder_parameter* param = *p;

    if (param->has_default) {
      switch (param->type) {
        case heif_encoder_parameter_type_integer:
          kvazaar_set_parameter_integer(encoder, param->name, param->integer.default_value);
          break;
        case heif_encoder_parameter_type_boolean:
          kvazaar_set_parameter_boolean(encoder, param->name, param->boolean.default_value);
          break;
        case heif_encoder_parameter_type_string:
          kvazaar_set_parameter_string(encoder, param->name, param->string.default_value);
          break;
      }
    }
  }
}


static void kvazaar_query_input_colorspace(heif_colorspace* colorspace, heif_chroma* chroma)
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


static void kvazaar_query_input_colorspace2(void* encoder_raw, heif_colorspace* colorspace, heif_chroma* chroma)
{
  if (*colorspace == heif_colorspace_monochrome) {
    *colorspace = heif_colorspace_monochrome;
    *chroma = heif_chroma_monochrome;
  }
  else {
    *colorspace = heif_colorspace_YCbCr;
    if (*chroma != heif_chroma_420) {
      // Encoding to 4:2:2 and 4:4:4 currently does not work with Kvazaar (https://github.com/ultravideo/kvazaar/issues/418).
      *chroma = heif_chroma_420;
    }
  }
}

void kvazaar_query_encoded_size(void* encoder_raw, uint32_t input_width, uint32_t input_height,
                                uint32_t* encoded_width, uint32_t* encoded_height)
{
  *encoded_width = (input_width + 7) & ~0x7;
  *encoded_height = (input_height + 7) & ~0x7;
}


#if 0
static int rounded_size(int s)
{
  s = (s + 1) & ~1;

  if (s < 64) {
    s = 64;
  }

  return s;
}
#endif

static void append_chunk_data(kvz_data_chunk* data, std::vector<uint8_t>& out)
{
  if (!data || data->len == 0) {
    return;
  }

  size_t old_size = out.size();
  out.resize(old_size + data->len);
  memcpy(out.data() + old_size, data->data, data->len);

  if (data->next) {
    append_chunk_data(data->next, out);
  }
}


static void copy_plane(kvz_pixel* out_p, size_t out_stride, const uint8_t* in_p, size_t in_stride, int w, int h, int padded_width, int padded_height,
                       int bit_depth)
{
  int bpp = (bit_depth > 8) ? 2 : 1;

  for (int y = 0; y < padded_height; y++) {
    int sy = std::min(y, h - 1); // source y
    memcpy(out_p + y * out_stride, in_p + sy * in_stride, w * bpp);

    if (padded_width > w) {
      memset(out_p + y * out_stride + w, *(in_p + sy * in_stride + w - 1), (padded_width - w) * bpp);
    }
  }
}


template<typename T, typename D>
std::unique_ptr<T, D> make_guard(T* ptr, D&& deleter) {
    return std::unique_ptr<T, D>(ptr, deleter);
}

static struct heif_error kvazaar_encode_image(void* encoder_raw, const struct heif_image* image,
                                              heif_image_input_class input_class)
{
  struct encoder_struct_kvazaar* encoder = (struct encoder_struct_kvazaar*) encoder_raw;

  int bit_depth = heif_image_get_bits_per_pixel_range(image, heif_channel_Y);
  int bit_depth_chroma = 0;
  bool isGreyscale = (heif_image_get_colorspace(image) == heif_colorspace_monochrome);
  heif_chroma chroma = heif_image_get_chroma_format(image);

  if (!isGreyscale) {
    bit_depth_chroma = heif_image_get_bits_per_pixel_range(image, heif_channel_Cb);
    if (bit_depth != bit_depth_chroma) {
      struct heif_error err = {
          heif_error_Encoder_plugin_error,
          heif_suberror_Unsupported_bit_depth,
          "Luma bit depth must equal the chroma bit depth"
      };
      return err;
    }
  }

  // Kvazaar uses a hard-coded bit depth (https://github.com/ultravideo/kvazaar/issues/399).
  // Check whether this matches to the image bit depth.
  // TODO: we should check the bit depth supported by the encoder (like query_input_colorspace2()) and output a warning
  //       if we have to encode at a different bit depth than requested.
  if (bit_depth != KVZ_BIT_DEPTH) {
    struct heif_error err = {
        heif_error_Encoder_plugin_error,
        heif_suberror_Unsupported_bit_depth,
        kError_unsupported_bit_depth
    };
    return err;
  }

  const kvz_api* api = kvz_api_get(bit_depth);
  if (api == nullptr) {
    struct heif_error err = {
        heif_error_Encoder_plugin_error,
        heif_suberror_Unspecified,
        "Could not initialize Kvazaar API"
    };
    return err;
  }

  auto uconfig = make_guard(api->config_alloc(), [api](kvz_config* cfg) { api->config_destroy(cfg); });
  kvz_config* config = uconfig.get();
  api->config_init(config); // param, encoder->preset.c_str(), encoder->tune.c_str());

#if HAVE_KVAZAAR_ENABLE_LOGGING
  config->enable_logging_output = 0;
#endif

#if !ENABLE_MULTITHREADING_SUPPORT
  // 0: Process everything with main thread
  // -1 (default): Select automatically.
  config->threads = 0;
#endif

#if 1
#if 0
  while (ctuSize > 16 &&
         (heif_image_get_width(image, heif_channel_Y) < ctuSize ||
          heif_image_get_height(image, heif_channel_Y) < ctuSize)) {
    ctuSize /= 2;
  }

  if (ctuSize < 16) {
    api->config_destroy(config);
    struct heif_error err = {
        heif_error_Encoder_plugin_error,
        heif_suberror_Invalid_parameter_value,
        kError_unsupported_image_size
    };
    return err;
  }
#endif
#else
  // TODO: There seems to be a bug in kvazaar where increasing the CTU size between
  // multiple encoding jobs causes a segmentation fault. E.g. encoding multiple
  // times with a CTU of 16 works, the next encoding with a CTU of 32 crashes.
  // Use hardcoded value of 64 and reject images that are too small.

  if (heif_image_get_width(image, heif_channel_Y) < ctuSize ||
      heif_image_get_height(image, heif_channel_Y) < ctuSize) {
    api->param_free(param);
    struct heif_error err = {
      heif_error_Encoder_plugin_error,
      heif_suberror_Invalid_parameter_value,
      kError_unsupported_image_size
    };
    return err;
  }
#endif

#if 0
  // ctuSize should be a power of 2 in [16;64]
  switch (ctuSize) {
    case 64:
      ctu = "64";
      break;
    case 32:
      ctu = "32";
      break;
    case 16:
      ctu = "16";
      break;
    default:
      struct heif_error err = {
          heif_error_Encoder_plugin_error,
          heif_suberror_Invalid_parameter_value,
          kError_unsupported_image_size
      };
      return err;
  }
  (void) ctu;
#endif

  int input_width = heif_image_get_width(image, heif_channel_Y);
  int input_height = heif_image_get_height(image, heif_channel_Y);
  int input_chroma_width = 0;
  int input_chroma_height = 0;

  uint32_t encoded_width, encoded_height;
  kvazaar_query_encoded_size(encoder_raw, input_width, input_height, &encoded_width, &encoded_height);

  kvz_chroma_format kvzChroma;
  int chroma_stride_shift = 0;
  int chroma_height_shift = 0;

  if (isGreyscale) {
    config->input_format = KVZ_FORMAT_P400;
    kvzChroma = KVZ_CSP_400;
  }
  else if (chroma == heif_chroma_420) {
    config->input_format = KVZ_FORMAT_P420;
    kvzChroma = KVZ_CSP_420;
    chroma_stride_shift = 1;
    chroma_height_shift = 1;
    input_chroma_width = (input_width + 1) / 2;
    input_chroma_height = (input_height + 1) / 2;
  }
  else if (chroma == heif_chroma_422) {
    config->input_format = KVZ_FORMAT_P422;
    kvzChroma = KVZ_CSP_422;
    chroma_stride_shift = 1;
    chroma_height_shift = 0;
    input_chroma_width = (input_width + 1) / 2;
    input_chroma_height = input_height;
  }
  else if (chroma == heif_chroma_444) {
    config->input_format = KVZ_FORMAT_P444;
    kvzChroma = KVZ_CSP_444;
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

  struct heif_color_profile_nclx* nclx = nullptr;
  heif_error err = heif_image_get_nclx_color_profile(image, &nclx);
  if (err.code != heif_error_Ok) {
    nclx = nullptr;
  }

  // make sure NCLX profile is deleted at end of function
  auto nclx_deleter = std::unique_ptr<heif_color_profile_nclx, void (*)(heif_color_profile_nclx*)>(nclx, heif_nclx_color_profile_free);

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

  auto upic = make_guard(api->picture_alloc_csp(kvzChroma, encoded_width, encoded_height), [api](kvz_picture* pic) { api->picture_free(pic); });
  kvz_picture* pic = upic.get();
  if (!pic) {
    return heif_error{
        heif_error_Encoder_plugin_error,
        heif_suberror_Encoder_encoding,
        kError_unspecified_error
    };
  }

  if (isGreyscale) {
    size_t stride;
    const uint8_t* data = heif_image_get_plane_readonly2(image, heif_channel_Y, &stride);

    copy_plane(pic->y, pic->stride, data, stride, input_width, input_height, encoded_width, encoded_height, bit_depth);
  }
  else {
    size_t stride;
    const uint8_t* data;

    data = heif_image_get_plane_readonly2(image, heif_channel_Y, &stride);
    copy_plane(pic->y, pic->stride, data, stride, input_width, input_height, encoded_width, encoded_height, bit_depth);

    data = heif_image_get_plane_readonly2(image, heif_channel_Cb, &stride);
    copy_plane(pic->u, pic->stride >> chroma_stride_shift, data, stride, input_chroma_width, input_chroma_height,
               encoded_width >> chroma_stride_shift, encoded_height >> chroma_height_shift, bit_depth_chroma);

    data = heif_image_get_plane_readonly2(image, heif_channel_Cr, &stride);
    copy_plane(pic->v, pic->stride >> chroma_stride_shift, data, stride, input_chroma_width, input_chroma_height,
               encoded_width >> chroma_stride_shift, encoded_height >> chroma_height_shift, bit_depth_chroma);
  }

  auto uencoder = make_guard(api->encoder_open(config), [api](kvz_encoder* e) { api->encoder_close(e); });
  kvz_encoder* kvzencoder = uencoder.get();
  if (!kvzencoder) {
    return heif_error{
        heif_error_Encoder_plugin_error,
        heif_suberror_Encoder_encoding,
        kError_unspecified_error
    };
  }

  kvz_data_chunk* data = nullptr;
  auto free_data = [api](kvz_data_chunk** data){
    if(*data) {
        api->chunk_free(*data);
        *data = nullptr;
    }
  };
  auto data_deleter = std::unique_ptr<kvz_data_chunk*, decltype(free_data)>(&data, free_data);

  uint32_t data_len;
  int success;
  success = api->encoder_headers(kvzencoder, &data, &data_len);
  if (!success) {
    return heif_error{
        heif_error_Encoder_plugin_error,
        heif_suberror_Encoder_encoding,
        kError_unspecified_error
    };
  }

  append_chunk_data(data, encoder->output_data);
  free_data(&data);

  success = api->encoder_encode(kvzencoder,
                                pic,
                                &data, &data_len,
                                nullptr, nullptr, nullptr);
  if (!success) {
    return heif_error{
        heif_error_Encoder_plugin_error,
        heif_suberror_Encoder_encoding,
        kError_unspecified_error
    };
  }

  append_chunk_data(data, encoder->output_data);
  free_data(&data);

  for (;;) {
    success = api->encoder_encode(kvzencoder,
                                  nullptr,
                                  &data, &data_len,
                                  nullptr, nullptr, nullptr);
    if (!success) {
      return heif_error{
          heif_error_Encoder_plugin_error,
          heif_suberror_Encoder_encoding,
          kError_unspecified_error
      };
    }

    if (data == nullptr || data->len == 0) {
      break;
    }

    append_chunk_data(data, encoder->output_data);
    free_data(&data);
  }

  (void) success;
  return heif_error_ok;
}


static struct heif_error kvazaar_get_compressed_data(void* encoder_raw, uint8_t** data, int* size,
                                                     enum heif_encoded_data_type* type)
{
  struct encoder_struct_kvazaar* encoder = (struct encoder_struct_kvazaar*) encoder_raw;

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


static const struct heif_encoder_plugin encoder_plugin_kvazaar
    {
        /* plugin_api_version */ 3,
        /* compression_format */ heif_compression_HEVC,
        /* id_name */ "kvazaar",
        /* priority */ kvazaar_PLUGIN_PRIORITY,
        /* supports_lossy_compression */ true,
        /* supports_lossless_compression */ true,
        /* get_plugin_name */ kvazaar_plugin_name,
        /* init_plugin */ kvazaar_init_plugin,
        /* cleanup_plugin */ kvazaar_cleanup_plugin,
        /* new_encoder */ kvazaar_new_encoder,
        /* free_encoder */ kvazaar_free_encoder,
        /* set_parameter_quality */ kvazaar_set_parameter_quality,
        /* get_parameter_quality */ kvazaar_get_parameter_quality,
        /* set_parameter_lossless */ kvazaar_set_parameter_lossless,
        /* get_parameter_lossless */ kvazaar_get_parameter_lossless,
        /* set_parameter_logging_level */ kvazaar_set_parameter_logging_level,
        /* get_parameter_logging_level */ kvazaar_get_parameter_logging_level,
        /* list_parameters */ kvazaar_list_parameters,
        /* set_parameter_integer */ kvazaar_set_parameter_integer,
        /* get_parameter_integer */ kvazaar_get_parameter_integer,
        /* set_parameter_boolean */ kvazaar_set_parameter_integer, // boolean also maps to integer function
        /* get_parameter_boolean */ kvazaar_get_parameter_integer, // boolean also maps to integer function
        /* set_parameter_string */ kvazaar_set_parameter_string,
        /* get_parameter_string */ kvazaar_get_parameter_string,
        /* query_input_colorspace */ kvazaar_query_input_colorspace,
        /* encode_image */ kvazaar_encode_image,
        /* get_compressed_data */ kvazaar_get_compressed_data,
        /* query_input_colorspace (v2) */ kvazaar_query_input_colorspace2,
        /* query_encoded_size (v3) */ kvazaar_query_encoded_size
    };

const struct heif_encoder_plugin* get_encoder_plugin_kvazaar()
{
  return &encoder_plugin_kvazaar;
}


#if PLUGIN_KVAZAAR
heif_plugin_info plugin_info {
  1,
  heif_plugin_type_encoder,
  &encoder_plugin_kvazaar
};
#endif
