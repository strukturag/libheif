/*
 * OpenJPEG codec.
 * Copyright (c) 2023 Devon Sookhoo
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
#include "decoder_openjpeg.h"
#include <openjpeg.h>
#include <cstring>

#include <vector>
#include <cassert>
#include <memory>

static const int OPENJPEG_PLUGIN_PRIORITY = 100;
static const int OPENJPEG_PLUGIN_PRIORITY_HTJ2K = 90;

struct openjpeg_decoder
{
  std::vector<uint8_t> encoded_data;
  size_t read_position = 0;
};


#define MAX_PLUGIN_NAME_LENGTH 80
static char plugin_name[MAX_PLUGIN_NAME_LENGTH];

static const char* openjpeg_plugin_name()
{
  snprintf(plugin_name, MAX_PLUGIN_NAME_LENGTH, "OpenJPEG %s", opj_version());
  plugin_name[MAX_PLUGIN_NAME_LENGTH - 1] = 0;

  return plugin_name;
}


static void openjpeg_init_plugin()
{
}


static void openjpeg_deinit_plugin()
{
}


static int openjpeg_does_support_format(enum heif_compression_format format)
{
  if (format == heif_compression_JPEG2000) {
    return OPENJPEG_PLUGIN_PRIORITY;
  }
  else if (format == heif_compression_HTJ2K) {
    return OPENJPEG_PLUGIN_PRIORITY_HTJ2K;
  }
  else {
    return 0;
  }
}


struct heif_error openjpeg_new_decoder(void** dec)
{
  struct openjpeg_decoder* decoder = new openjpeg_decoder();

  *dec = decoder;

  return heif_error_ok;
}


void openjpeg_free_decoder(void* decoder_raw)
{
  struct openjpeg_decoder* decoder = (openjpeg_decoder*) decoder_raw;

  if (!decoder) {
    return;
  }

  delete decoder;
}


void openjpeg_set_strict_decoding(void* decoder_raw, int flag)
{

}


struct heif_error openjpeg_push_data(void* decoder_raw, const void* frame_data, size_t frame_size)
{
  struct openjpeg_decoder* decoder = (struct openjpeg_decoder*) decoder_raw;
  const uint8_t* frame_data_src = (const uint8_t*) frame_data;

  decoder->encoded_data.insert(decoder->encoded_data.end(), frame_data_src, frame_data_src + frame_size);

  return heif_error_ok;
}


//**************************************************************************

//  This will read from our memory to the buffer.

static OPJ_SIZE_T opj_memory_stream_read(void* p_buffer, OPJ_SIZE_T p_nb_bytes, void* p_user_data)
{
  openjpeg_decoder* decoder = (openjpeg_decoder*) p_user_data; // Our data.
  size_t data_size = decoder->encoded_data.size();

  OPJ_SIZE_T l_nb_bytes_read = p_nb_bytes; // Amount to move to buffer.

  // Check if the current offset is outside our data buffer.

  if (decoder->read_position >= data_size) {
    return (OPJ_SIZE_T) -1;
  }

  // Check if we are reading more than we have.

  if (p_nb_bytes > (data_size - decoder->read_position)) {
    //Read all we have.
    l_nb_bytes_read = data_size - decoder->read_position;
  }

  // Copy the data to the internal buffer.

  memcpy(p_buffer, &(decoder->encoded_data[decoder->read_position]), l_nb_bytes_read);

  decoder->read_position += l_nb_bytes_read; // Update the pointer to the new location.

  return l_nb_bytes_read;
}


// This will write from the buffer to our memory.

static OPJ_SIZE_T opj_memory_stream_write(void* p_buffer, OPJ_SIZE_T p_nb_bytes, void* p_user_data)
{
  assert(false); // We should never need to write to the buffer.
  return 0;
}


// Moves the pointer forward, but never more than we have.

static OPJ_OFF_T opj_memory_stream_skip(OPJ_OFF_T p_nb_bytes, void* p_user_data)
{
  openjpeg_decoder* decoder = (openjpeg_decoder*) p_user_data; // Our data.
  size_t data_size = decoder->encoded_data.size();

  OPJ_SIZE_T l_nb_bytes;


  if (p_nb_bytes < 0) {
    //No skipping backwards.
    return -1;
  }

  l_nb_bytes = (OPJ_SIZE_T) p_nb_bytes; // Allowed because it is positive.

  // Do not allow jumping past the end.

  if (l_nb_bytes > data_size - decoder->read_position) {
    l_nb_bytes = data_size - decoder->read_position;//Jump the max.
  }

  // Make the jump.

  decoder->read_position += l_nb_bytes;

  // Return how far we jumped.

  return l_nb_bytes;
}


// Sets the pointer to anywhere in the memory.

static OPJ_BOOL opj_memory_stream_seek(OPJ_OFF_T p_nb_bytes, void* p_user_data)
{
  openjpeg_decoder* decoder = (openjpeg_decoder*) p_user_data; // Our data.
  size_t data_size = decoder->encoded_data.size();

  // No before the buffer.
  if (p_nb_bytes < 0)
    return OPJ_FALSE;

  // No after the buffer.
  if (p_nb_bytes > (OPJ_OFF_T) data_size)
    return OPJ_FALSE;

  // Move to new position.
  decoder->read_position = (OPJ_SIZE_T) p_nb_bytes;

  return OPJ_TRUE;
}

//The system needs a routine to do when finished, the name tells you what I want it to do.

static void opj_memory_stream_do_nothing(void* p_user_data)
{
  OPJ_ARG_NOT_USED(p_user_data);
}


// Create a stream to use memory as the input or output.

opj_stream_t* opj_stream_create_default_memory_stream(openjpeg_decoder* p_decoder, OPJ_BOOL p_is_read_stream)
{
  opj_stream_t* stream;

  if (!(stream = opj_stream_default_create(p_is_read_stream))) {
    return nullptr;
  }

  // Set how to work with the frame buffer.

  if (p_is_read_stream) {
    opj_stream_set_read_function(stream, opj_memory_stream_read);
  }
  else {
    opj_stream_set_write_function(stream, opj_memory_stream_write);
  }

  opj_stream_set_seek_function(stream, opj_memory_stream_seek);

  opj_stream_set_skip_function(stream, opj_memory_stream_skip);

  opj_stream_set_user_data(stream, p_decoder, opj_memory_stream_do_nothing);

  opj_stream_set_user_data_length(stream, p_decoder->encoded_data.size());

  return stream;
}


//**************************************************************************


struct heif_error openjpeg_decode_image(void* decoder_raw, struct heif_image** out_img)
{
  auto* decoder = (struct openjpeg_decoder*) decoder_raw;

  OPJ_BOOL success;
  opj_dparameters_t decompression_parameters;
  std::unique_ptr<opj_codec_t, void (OPJ_CALLCONV *)(opj_codec_t*)> l_codec(opj_create_decompress(OPJ_CODEC_J2K),
                                                               opj_destroy_codec);

  // Initialize Decoder
  opj_set_default_decoder_parameters(&decompression_parameters);
  success = opj_setup_decoder(l_codec.get(), &decompression_parameters);
  if (!success) {
    struct heif_error err = {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "opj_setup_decoder()"};
    return err;
  }


  // Create Input Stream

  OPJ_BOOL is_read_stream = true;
  std::unique_ptr<opj_stream_t, void (OPJ_CALLCONV *)(opj_stream_t*)> stream(opj_stream_create_default_memory_stream(decoder, is_read_stream),
                                                                opj_stream_destroy);


  // Read Codestream Header
  opj_image_t* image_ptr = nullptr;
  success = opj_read_header(stream.get(), l_codec.get(), &image_ptr);
  if (!success) {
    struct heif_error err = {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "opj_read_header()"};
    return err;
  }

  std::unique_ptr<opj_image_t, void (OPJ_CALLCONV *)(opj_image_t*)> image(image_ptr, opj_image_destroy);

  if (image->numcomps != 3 && image->numcomps != 1) {
    //TODO - Handle other numbers of components
    struct heif_error err = {heif_error_Unsupported_feature, heif_suberror_Unsupported_data_version, "Number of components must be 3 or 1"};
    return err;
  }
  else if ((image->color_space != OPJ_CLRSPC_UNSPECIFIED) && (image->color_space != OPJ_CLRSPC_SRGB)) {
    //TODO - Handle other colorspaces
    struct heif_error err = {heif_error_Unsupported_feature, heif_suberror_Unsupported_data_version, "Colorspace must be SRGB"};
    return err;
  }

  const int width = (image->x1 - image->x0);
  const int height = (image->y1 - image->y0);


  /* Get the decoded image */
  success = opj_decode(l_codec.get(), stream.get(), image.get());
  if (!success) {
    struct heif_error err = {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "opj_decode()"};
    return err;
  }


  success = opj_end_decompress(l_codec.get(), stream.get());
  if (!success) {
    struct heif_error err = {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "opj_end_decompress()"};
    return err;
  }


  heif_colorspace colorspace = heif_colorspace_YCbCr;
  heif_chroma chroma = heif_chroma_444; //heif_chroma_interleaved_RGB;

  std::vector<heif_channel> channels;

  if (image->numcomps == 1) {
    colorspace = heif_colorspace_monochrome;
    chroma = heif_chroma_monochrome;
    channels = {heif_channel_Y};
  }
  else if (image->numcomps == 3 &&
           image->comps[1].dx == 1 &&
           image->comps[1].dy == 1) {
    colorspace = heif_colorspace_YCbCr;
    chroma = heif_chroma_444;
    channels = {heif_channel_Y, heif_channel_Cb, heif_channel_Cr};
  }
  else if (image->numcomps == 3 &&
           image->comps[1].dx == 2 &&
           image->comps[1].dy == 1) {
    colorspace = heif_colorspace_YCbCr;
    chroma = heif_chroma_422;
    channels = {heif_channel_Y, heif_channel_Cb, heif_channel_Cr};
  }
  else if (image->numcomps == 3 &&
           image->comps[1].dx == 2 &&
           image->comps[1].dy == 2) {
    colorspace = heif_colorspace_YCbCr;
    chroma = heif_chroma_420;
    channels = {heif_channel_Y, heif_channel_Cb, heif_channel_Cr};
  }
  else {
    struct heif_error err = {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "unsupported image format"};
    return err;
  }


  struct heif_error error = heif_image_create(width, height, colorspace, chroma, out_img);
  if (error.code) {
    return error;
  }

  for (size_t c = 0; c < image->numcomps; c++) {
    const opj_image_comp_t& opj_comp = image->comps[c];

    int bit_depth = opj_comp.prec;
    int cwidth = opj_comp.w;
    int cheight = opj_comp.h;

    error = heif_image_add_plane(*out_img, channels[c], cwidth, cheight, bit_depth);

    int stride = -1;
    uint8_t* p = heif_image_get_plane(*out_img, channels[c], &stride);


    // TODO: a SIMD implementation to convert int32 to uint8 would speed this up
    // https://stackoverflow.com/questions/63774643/how-to-convert-uint32-to-uint8-using-simd-but-not-avx512

    if (stride == cwidth) {
      for (int i = 0; i < cwidth * cheight; i++) {
        p[i] = (uint8_t) opj_comp.data[i];
      }
    }
    else {
      for (int y = 0; y < cheight; y++) {
        for (int x = 0; x < cwidth; x++) {
          p[y * stride + x] = (uint8_t) opj_comp.data[y * cwidth + x];
        }
      }
    }
  }

  return heif_error_ok;
}


static const struct heif_decoder_plugin decoder_openjpeg{
    3,
    openjpeg_plugin_name,
    openjpeg_init_plugin,
    openjpeg_deinit_plugin,
    openjpeg_does_support_format,
    openjpeg_new_decoder,
    openjpeg_free_decoder,
    openjpeg_push_data,
    openjpeg_decode_image,
    openjpeg_set_strict_decoding,
    "openjpeg"
};

const struct heif_decoder_plugin* get_decoder_plugin_openjpeg()
{
  return &decoder_openjpeg;
}


#if PLUGIN_OPENJPEG_DECODER
heif_plugin_info plugin_info {
  1,
  heif_plugin_type_decoder,
  &decoder_openjpeg
};
#endif
