/*
 * JasPer codec.
 * Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>
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
#include "decoder_jasper.h"
#include <jasper/jasper.h>
#include <cstring>

#include <vector>
#include <memory>
#include <string>

static const int JASPER_PLUGIN_PRIORITY = 100;
static const int JASPER_JPG_PLUGIN_PRIORITY = 70;

struct jasper_decoder
{
  std::vector<uint8_t> encoded_data;
  uintptr_t user_data;

  std::string error_message;
};


#define MAX_PLUGIN_NAME_LENGTH 80
static char plugin_name[MAX_PLUGIN_NAME_LENGTH];

static const char* jasper_plugin_name()
{
  snprintf(plugin_name, MAX_PLUGIN_NAME_LENGTH, "JasPer %s", jas_getversion());
  plugin_name[MAX_PLUGIN_NAME_LENGTH - 1] = 0;

  return plugin_name;
}


static void jasper_init_plugin()
{
  jas_conf_clear();
  // Assume ENABLE_PARALLEL_TILE_DECODING as plugins are dynamically linked, enable multithreading.
  jas_conf_set_multithread(1);
  jas_init_library();
  jas_init_thread();
}


static void jasper_deinit_plugin()
{
  jas_cleanup_thread();
  jas_cleanup_library();
}


static int jasper_does_support_format(heif_compression_format format)
{
#ifdef JAS_INCLUDE_JPC_CODEC
  if (format == heif_compression_JPEG2000) {
    return JASPER_PLUGIN_PRIORITY;
  }
#endif
#ifdef JAS_INCLUDE_JPG_CODEC
  if (format == heif_compression_JPEG) {
    return JASPER_JPG_PLUGIN_PRIORITY;
  }
#endif
  return 0;
}

static int jasper_does_support_format2(const heif_decoder_plugin_compressed_format_description* format)
{
  return jasper_does_support_format(format->format);
}

heif_error jasper_new_decoder2(void** dec, const heif_decoder_plugin_options* options)
{
  jasper_decoder* decoder = new jasper_decoder();

  *dec = decoder;

  return heif_error_ok;
}

heif_error jasper_new_decoder(void** dec)
{
  heif_decoder_plugin_options options{};
  options.format = heif_compression_JPEG2000;
  options.num_threads = 0;
  options.strict_decoding = false;

  return jasper_new_decoder2(dec, &options);
}

void jasper_free_decoder(void* decoder_raw)
{
  jasper_decoder* decoder = (jasper_decoder*) decoder_raw;

  if (!decoder) {
    return;
  }

  delete decoder;
}


void jasper_set_strict_decoding(void* decoder_raw, int flag)
{

}


heif_error jasper_push_data2(void* decoder_raw, const void* frame_data, size_t frame_size,
                               uintptr_t user_data)
{
  jasper_decoder* decoder = (jasper_decoder*) decoder_raw;
  const uint8_t* frame_data_src = (const uint8_t*) frame_data;

  decoder->encoded_data.insert(decoder->encoded_data.end(), frame_data_src, frame_data_src + frame_size);
  decoder->user_data = user_data;

  return heif_error_ok;
}

heif_error jasper_push_data(void* decoder_raw, const void* frame_data, size_t frame_size)
{
  return jasper_push_data2(decoder_raw, frame_data, frame_size, 0);
}


heif_error jasper_decode_next_image2(void* decoder_raw, heif_image** out_img,
                                       uintptr_t* out_user_data,
                                       const heif_security_limits* limits)
{
  auto* decoder = (struct jasper_decoder*) decoder_raw;

  if (decoder->encoded_data.empty()) {
    *out_img = nullptr;
    return heif_error_ok;
  }

  std::ostringstream opts;
  if (limits) {
    // JasPer max_samples value is components * pixels.
    auto size = jas_safeui64_mul({ true, limits->max_image_size_pixels }, { true, 3 });
    if (size.valid)
      opts << "max_samples=" << size.value;
    else
      return { heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "jas_stream_memopen()" };
  }

  // Create Input Stream
  std::unique_ptr<jas_stream_t, int(*)(jas_stream_t*)> stream(jas_stream_memopen((char*)decoder->encoded_data.data(), decoder->encoded_data.size()), jas_stream_close);
  if (!stream) {
    return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "jas_stream_memopen()"};
  }

  /* Get the decoded image */
  std::unique_ptr<jas_image_t, void(*)(jas_image_t*)> image(jas_image_decode(stream.get(), -1, opts.str().c_str()), jas_image_destroy);
  if (!image) {
    return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "jas_image_decode()"};
  }

  auto numcomps = jas_image_numcmpts(image);
  heif_colorspace colorspace = heif_colorspace_YCbCr;
  heif_chroma chroma = heif_chroma_444; //heif_chroma_interleaved_RGB;

  std::vector<heif_channel> channels;

  if (numcomps == 1) {
    colorspace = heif_colorspace_monochrome;
    chroma = heif_chroma_monochrome;
    channels = {heif_channel_Y};
  }
  else if (numcomps == 3 &&
           jas_image_cmpthstep(image, 1) == 1 &&
           jas_image_cmptvstep(image, 1) == 1) {
    colorspace = heif_colorspace_YCbCr;
    chroma = heif_chroma_444;
    channels = {heif_channel_Y, heif_channel_Cb, heif_channel_Cr};
  }
  else if (numcomps == 3 &&
           jas_image_cmpthstep(image, 1) == 2 &&
           jas_image_cmptvstep(image, 1) == 1) {
    colorspace = heif_colorspace_YCbCr;
    chroma = heif_chroma_422;
    channels = {heif_channel_Y, heif_channel_Cb, heif_channel_Cr};
  }
  else if (numcomps == 3 &&
           jas_image_cmpthstep(image, 1) == 2 &&
           jas_image_cmptvstep(image, 1) == 2) {
    colorspace = heif_colorspace_YCbCr;
    chroma = heif_chroma_420;
    channels = {heif_channel_Y, heif_channel_Cb, heif_channel_Cr};
  }
  else {
    return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "unsupported image format"};
  }

  heif_error error = heif_image_create(jas_image_width(image), jas_image_height(image), colorspace, chroma, out_img);
  if (error.code) {
    return error;
  }

  for (size_t c = 0; c < jas_image_numcmpts(image); c++) {
    auto bit_depth = jas_image_cmptprec(image, c);
    auto cwidth = jas_image_cmptwidth(image, c);
    auto cheight = jas_image_cmptheight(image, c);
    if (bit_depth > 16) {
      return { heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "unsupported image format" };
    }
    std::unique_ptr<jas_matrix_t, void(*)(jas_matrix_t*)> matrix(jas_matrix_create(cwidth, cheight), jas_matrix_destroy);

    error = heif_image_add_plane_safe(*out_img, channels[c], cwidth, cheight, bit_depth, limits);
    if (error.code) {
      // copy error message to decoder object because heif_image will be released
      decoder->error_message = error.message;
      error.message = decoder->error_message.c_str();

      heif_image_release(*out_img);
      *out_img = nullptr;
      return error;
    }

    size_t stride = 0;
    uint8_t* p = heif_image_get_plane2(*out_img, channels[c], &stride);
    if (jas_image_readcmpt(image.get(), c, 0, 0, cwidth, cheight, matrix.get()) != 0) {
      heif_image_release(*out_img);
      return { heif_error_Decoder_plugin_error, heif_suberror_Unspecified,
              "jas_image_readcmpt" };
    }

    for (int y = 0; y < cheight; y++) {
      jas_seqent_t* row = jas_matrix_getvref(matrix.get(), y);

      if (bit_depth > 8 && bit_depth <= 16) {
        uint16_t* p16 = (uint16_t*)p;
        for (int x = 0; x < cwidth; x++) {
          p16[y * stride / 2 + x] = (uint16_t)row[x];
        }
      }
      else {
        for (int x = 0; x < cwidth; x++) {
          p[y * stride + x] = (uint8_t)row[x];
        }
      }
    }
  }

  if (out_user_data) {
    *out_user_data = decoder->user_data;
  }

  decoder->encoded_data.clear();

  return heif_error_ok;
}

heif_error jasper_decode_next_image(void* decoder_raw, heif_image** out_img,
                                      const heif_security_limits* limits)
{
  return jasper_decode_next_image2(decoder_raw, out_img, nullptr, limits);
}

heif_error jasper_decode_image(void* decoder_raw, heif_image** out_img)
{
  auto* limits = heif_get_global_security_limits();
  return jasper_decode_next_image(decoder_raw, out_img, limits);
}

heif_error jasper_flush_data(void* decoder)
{
  return heif_error_ok;
}


static const heif_decoder_plugin decoder_jasper{
    5,
    jasper_plugin_name,
    jasper_init_plugin,
    jasper_deinit_plugin,
    jasper_does_support_format,
    jasper_new_decoder,
    jasper_free_decoder,
    jasper_push_data,
    jasper_decode_image,
    jasper_set_strict_decoding,
    "jasper",
    jasper_decode_next_image,
    /* minimum_required_libheif_version */ LIBHEIF_MAKE_VERSION(1,21,0),
    jasper_does_support_format2,
    jasper_new_decoder2,
    jasper_push_data2,
    jasper_flush_data,
    jasper_decode_next_image2
};

const heif_decoder_plugin* get_decoder_plugin_jasper()
{
  return &decoder_jasper;
}


#if PLUGIN_JASPER_DECODER
heif_plugin_info plugin_info {
  1,
  heif_plugin_type_decoder,
  &decoder_jasper
};
#endif
