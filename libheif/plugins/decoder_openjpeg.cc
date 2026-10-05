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
#include "common_utils.h"
#include <openjpeg.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

#include <vector>
#include <cassert>
#include <memory>
#include <string>

static const int OPENJPEG_PLUGIN_PRIORITY = 100;
static const int OPENJPEG_PLUGIN_PRIORITY_HTJ2K = 90;

struct openjpeg_decoder
{
  std::vector<uint8_t> encoded_data;
  uintptr_t user_data;

  size_t read_position = 0;
  std::string error_message;
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


static int openjpeg_does_support_format(heif_compression_format format)
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

static int openjpeg_does_support_format2(const heif_decoder_plugin_compressed_format_description* format)
{
  return openjpeg_does_support_format(format->format);
}

heif_error openjpeg_new_decoder2(void** dec, const heif_decoder_plugin_options* options)
{
  openjpeg_decoder* decoder = new openjpeg_decoder();

  *dec = decoder;

  return heif_error_ok;
}

heif_error openjpeg_new_decoder(void** dec)
{
  heif_decoder_plugin_options options{};
  options.format = heif_compression_JPEG2000;
  options.num_threads = 0;
  options.strict_decoding = false;

  return openjpeg_new_decoder2(dec, &options);
}

void openjpeg_free_decoder(void* decoder_raw)
{
  openjpeg_decoder* decoder = (openjpeg_decoder*) decoder_raw;

  if (!decoder) {
    return;
  }

  delete decoder;
}


void openjpeg_set_strict_decoding(void* decoder_raw, int flag)
{

}


heif_error openjpeg_push_data2(void* decoder_raw, const void* frame_data, size_t frame_size,
                               uintptr_t user_data)
{
  openjpeg_decoder* decoder = (openjpeg_decoder*) decoder_raw;
  const uint8_t* frame_data_src = (const uint8_t*) frame_data;

  decoder->encoded_data.insert(decoder->encoded_data.end(), frame_data_src, frame_data_src + frame_size);
  decoder->user_data = user_data;

  return heif_error_ok;
}

heif_error openjpeg_push_data(void* decoder_raw, const void* frame_data, size_t frame_size)
{
  return openjpeg_push_data2(decoder_raw, frame_data, frame_size, 0);
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


// Memory that OpenJPEG allocates while it reads the SIZ marker segment: the coding
// parameters (opj_tcp_t) and the codestream index of each tile, and the coding parameters
// of each component of each tile (opj_tccp_t). These are the sizes of OpenJPEG 2.5 on a
// 64-bit platform, rounded up.
static const uint64_t OPENJPEG_HEADER_BYTES_PER_TILE = 8192;
static const uint64_t OPENJPEG_HEADER_BYTES_PER_TILE_COMPONENT = 1088;

// Each tile has at least one tile-part, which consists at least of an SOT marker segment
// (12 bytes) and an SOD marker (2 bytes).
static const uint64_t JPEG2000_MIN_BYTES_PER_TILE = 14;


static uint32_t read_uint32_be(const uint8_t* p)
{
  return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | uint32_t{p[3]};
}


// OpenJPEG allocates memory for every tile and for every component of every tile already
// when it reads the SIZ marker segment in opj_read_header(), and there is no way to limit
// that. A SIZ marker segment of less than 100 bytes can declare 65535 tiles of one pixel
// with up to 16384 components each, for which OpenJPEG allocates many gigabytes before we
// get to see the image header. Hence, we read the SIZ marker segment ourselves and check
// the number of tiles and components before we call OpenJPEG (GHSA-h4h8-qgvc-m7r2).
static heif_error openjpeg_check_siz_marker_segment(const std::vector<uint8_t>& data,
                                                    const heif_security_limits* limits)
{
  // SOC marker, SIZ marker, Lsiz, Rsiz, eight 32-bit sizes and offsets, Csiz
  const size_t fixed_part_size = 2 + 2 + 2 + 2 + 8 * 4 + 2;

  // The SIZ marker segment has to follow the SOC marker directly. OpenJPEG does not insist
  // on this, it skips over other data until it finds an SIZ marker. We do insist, because
  // this is the only way to know which SIZ marker segment OpenJPEG is going to use.
  if (data.size() < fixed_part_size ||
      data[0] != 0xFF || data[1] != 0x4F ||
      data[2] != 0xFF || data[3] != 0x51) {
    return {heif_error_Invalid_input, heif_suberror_Invalid_J2K_codestream,
            "JPEG 2000 codestream does not start with an SOC marker and an SIZ marker segment"};
  }

  const uint64_t xsiz = read_uint32_be(&data[8]);
  const uint64_t ysiz = read_uint32_be(&data[12]);
  const uint64_t xtsiz = read_uint32_be(&data[24]);
  const uint64_t ytsiz = read_uint32_be(&data[28]);
  const uint64_t xtosiz = read_uint32_be(&data[32]);
  const uint64_t ytosiz = read_uint32_be(&data[36]);
  const uint32_t csiz = (uint32_t{data[40]} << 8) | uint32_t{data[41]};

  if (xtsiz == 0 || ytsiz == 0 || xtosiz >= xsiz || ytosiz >= ysiz) {
    return {heif_error_Invalid_input, heif_suberror_Invalid_J2K_codestream,
            "Invalid tile geometry in JPEG 2000 codestream"};
  }

  // This plugin handles the image size as 'int'. Moreover, OpenJPEG versions before 2.5.1
  // compute the number of tiles with signed 32-bit integers. With a reference grid of
  // more than INT32_MAX in one direction, they can get to 65535 tiles where we compute
  // a single one here.
  if (xsiz > INT32_MAX || ysiz > INT32_MAX) {
    return {heif_error_Unsupported_feature, heif_suberror_Unsupported_data_version,
            "JPEG 2000 reference grid is too large"};
  }

  if (limits->max_components > 0 && csiz > limits->max_components) {
    return {heif_error_Memory_allocation_error, heif_suberror_Security_limit_exceeded,
            "JPEG 2000 image exceeds the maximum number of components"};
  }

  if (csiz != 3 && csiz != 1) {
    //TODO - Handle other numbers of components
    return {heif_error_Unsupported_feature, heif_suberror_Unsupported_data_version, "Number of components must be 3 or 1"};
  }

  // Both factors are below 2^31, thus there is no overflow in the product.
  const uint64_t num_tiles = ((xsiz - xtosiz + xtsiz - 1) / xtsiz) * ((ysiz - ytosiz + ytsiz - 1) / ytsiz);

  if (limits->max_number_of_tiles > 0 && num_tiles > limits->max_number_of_tiles) {
    return {heif_error_Memory_allocation_error, heif_suberror_Security_limit_exceeded,
            "JPEG 2000 image exceeds the maximum number of tiles"};
  }

  // A codestream that is shorter than the minimum size of its tiles cannot be complete.
  // This check ties the memory that OpenJPEG allocates for the tiles to the input size.
  if (num_tiles > data.size() / JPEG2000_MIN_BYTES_PER_TILE) {
    return {heif_error_Invalid_input, heif_suberror_Invalid_J2K_codestream,
            "JPEG 2000 codestream is too short for its number of tiles"};
  }

  // num_tiles is bounded by the input size here and csiz is at most 3, no overflow.
  const uint64_t header_memory = num_tiles * (OPENJPEG_HEADER_BYTES_PER_TILE +
                                              csiz * OPENJPEG_HEADER_BYTES_PER_TILE_COMPONENT);
  if (limits->max_memory_block_size > 0 && header_memory > limits->max_memory_block_size) {
    return {heif_error_Memory_allocation_error, heif_suberror_Security_limit_exceeded,
            "JPEG 2000 image would require too much memory for its tiles"};
  }

  return heif_error_ok;
}


// Conservative upper bound on bytes OpenJPEG will allocate to decode this
// codestream. Saturates to UINT64_MAX on overflow. OpenJPEG stores each sample
// internally as OPJ_INT32 regardless of the codestream bit depth; the 3x
// multiplier covers the final image planes plus in-flight tile and DWT
// working buffers.
static uint64_t openjpeg_estimate_decode_memory_bytes(const opj_image_t* image)
{
  auto sat_mul = [](uint64_t a, uint64_t b) -> uint64_t {
    if (a == 0 || b == 0) return 0;
    if (a > UINT64_MAX / b) return UINT64_MAX;
    return a * b;
  };

  auto sat_add = [](uint64_t a, uint64_t b) -> uint64_t {
    uint64_t s = a + b;
    return (s < a) ? UINT64_MAX : s;
  };

  uint64_t total = 0;
  for (uint32_t c = 0; c < image->numcomps; c++) {
    const opj_image_comp_t& comp = image->comps[c];
    uint64_t plane = sat_mul(uint64_t(comp.w), uint64_t(comp.h));
    plane = sat_mul(plane, sizeof(OPJ_INT32));
    total = sat_add(total, plane);
  }

  return sat_mul(total, 3);
}


heif_error openjpeg_decode_next_image2(void* decoder_raw, heif_image** out_img,
                                       uintptr_t* out_user_data,
                                       const heif_security_limits* limits)
{
  auto* decoder = (struct openjpeg_decoder*) decoder_raw;

  if (decoder->encoded_data.empty()) {
    *out_img = nullptr;
    return heif_error_ok;
  }


  heif_error siz_error = openjpeg_check_siz_marker_segment(decoder->encoded_data, limits);
  if (siz_error.code) {
    return siz_error;
  }

  // OpenJPEG has to read the data from its start, as we did in the check above.
  decoder->read_position = 0;


  OPJ_BOOL success;
  opj_dparameters_t decompression_parameters;
  std::unique_ptr<opj_codec_t, void (OPJ_CALLCONV *)(opj_codec_t*)> l_codec(opj_create_decompress(OPJ_CODEC_J2K),
                                                               opj_destroy_codec);

  // Initialize Decoder
  opj_set_default_decoder_parameters(&decompression_parameters);
  success = opj_setup_decoder(l_codec.get(), &decompression_parameters);
  if (!success) {
    return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "opj_setup_decoder()"};
  }


  // Create Input Stream

  OPJ_BOOL is_read_stream = true;
  std::unique_ptr<opj_stream_t, void (OPJ_CALLCONV *)(opj_stream_t*)> stream(opj_stream_create_default_memory_stream(decoder, is_read_stream),
                                                                opj_stream_destroy);


  // Read Codestream Header
  opj_image_t* image_ptr = nullptr;
  success = opj_read_header(stream.get(), l_codec.get(), &image_ptr);
  if (!success) {
    return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "opj_read_header()"};
  }

  std::unique_ptr<opj_image_t, void (OPJ_CALLCONV *)(opj_image_t*)> image(image_ptr, opj_image_destroy);

  // Reject obvious memory bombs before letting OpenJPEG allocate decode buffers.
  // OpenJPEG has no built-in resource limit API, so we enforce libheif's limits here.
  if (image->x1 < image->x0 || image->y1 < image->y0) {
    return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified,
            "Invalid JPEG 2000 image bounding box"};
  }

  uint64_t img_w = image->x1 - image->x0;
  uint64_t img_h = image->y1 - image->y0;

  // image->x0,x1,y0,y1 are uint32, thus no overflow here
  uint64_t pixels = img_w * img_h;
  if (limits->max_image_size_pixels > 0 && pixels > limits->max_image_size_pixels) {
    return {heif_error_Memory_allocation_error, heif_suberror_Security_limit_exceeded,
            "JPEG 2000 image exceeds maximum allowed image size"};
  }

  // The visible image is only the window (x1-x0, y1-y0), but OpenJPEG performs
  // its tile and coefficient arithmetic over the full JPEG 2000 reference grid
  // (Xsiz=x1, Ysiz=y1). A tiny window placed at very large absolute coordinates
  // therefore reaches internal decode paths with pathological geometry, which
  // has caused out-of-bounds writes inside OpenJPEG (see GHSA-q492-cfcm-895h,
  // CVE-2020-6851). Bound the reference-grid area, not just the window span.
  // (x1,y1 are uint32, so the product fits in uint64 without overflow.)
  uint64_t grid_pixels = uint64_t(image->x1) * uint64_t(image->y1);
  if (limits->max_image_size_pixels > 0 && grid_pixels > limits->max_image_size_pixels) {
    return {heif_error_Memory_allocation_error, heif_suberror_Security_limit_exceeded,
            "JPEG 2000 reference grid exceeds maximum allowed image size"};
  }

  uint64_t estimated_memory = openjpeg_estimate_decode_memory_bytes(image.get());
  if (limits->max_memory_block_size > 0 && estimated_memory > limits->max_memory_block_size) {
    return {heif_error_Memory_allocation_error, heif_suberror_Security_limit_exceeded,
            "JPEG 2000 image would require too much memory to decode"};
  }

  if (image->numcomps != 3 && image->numcomps != 1) {
    //TODO - Handle other numbers of components
    return {heif_error_Unsupported_feature, heif_suberror_Unsupported_data_version, "Number of components must be 3 or 1"};
  }
  else if ((image->color_space != OPJ_CLRSPC_UNSPECIFIED) && (image->color_space != OPJ_CLRSPC_SRGB)) {
    //TODO - Handle other colorspaces
    return {heif_error_Unsupported_feature, heif_suberror_Unsupported_data_version, "Colorspace must be SRGB"};
  }

  const int width = (image->x1 - image->x0);
  const int height = (image->y1 - image->y0);


  /* Get the decoded image */
  success = opj_decode(l_codec.get(), stream.get(), image.get());
  if (!success) {
    return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "opj_decode()"};
  }


  success = opj_end_decompress(l_codec.get(), stream.get());
  if (!success) {
    return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "opj_end_decompress()"};
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
    return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "unsupported image format"};
  }


  // Validate per-component sizes against the chroma format derived above. A malformed
  // JPEG 2000 stream may set comp[1].dx/dy consistently with a chroma format yet declare
  // comp[c].w/h that do not match the subsampled dimensions; using such planes downstream
  // causes out-of-bounds reads in color conversion (issue #1796).
  for (size_t c = 0; c < image->numcomps; c++) {
    uint32_t expected_w, expected_h;
    get_subsampled_size(static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                        channels[c], chroma, &expected_w, &expected_h);
    if (image->comps[c].w != expected_w || image->comps[c].h != expected_h) {
      return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified,
              "JPEG 2000 component size does not match the image's chroma subsampling"};
    }
  }

  // A JPEG 2000 codestream can declare its components as signed. OpenJPEG then returns
  // negative sample values, but the planes created below hold unsigned samples. The cast
  // in the copy loop turned a negative value into a sample far above the bit depth of the
  // plane (e.g. -1 into 65535 in a 10-bit plane), and code that relies on the bit depth
  // then read out of bounds (GHSA-q7mw-2fmm-5q94).
  for (size_t c = 0; c < image->numcomps; c++) {
    if (image->comps[c].sgnd) {
      return {heif_error_Unsupported_feature, heif_suberror_Unsupported_data_version,
              "JPEG 2000 images with signed components are not supported"};
    }
  }

  heif_error error = heif_image_create(width, height, colorspace, chroma, out_img);
  if (error.code) {
    return error;
  }

  for (size_t c = 0; c < image->numcomps; c++) {
    const opj_image_comp_t& opj_comp = image->comps[c];

    int bit_depth = opj_comp.prec;
    int cwidth = opj_comp.w;
    int cheight = opj_comp.h;

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


    // TODO: a SIMD implementation to convert int32 to uint8 would speed this up
    // https://stackoverflow.com/questions/63774643/how-to-convert-uint32-to-uint8-using-simd-but-not-avx512

    // OpenJPEG clamps the decoded values of an unsigned component to the range of its
    // precision. We clamp them nevertheless: a sample outside of the range of the plane's
    // bit depth must not leave the plugin, whatever the library does.
    const OPJ_INT32 max_value = (bit_depth < 31) ? ((OPJ_INT32{1} << bit_depth) - 1) : INT32_MAX;

    if (bit_depth <= 8) {
      for (int y = 0; y < cheight; y++) {
        for (int x = 0; x < cwidth; x++) {
          p[y * stride + x] = (uint8_t) std::clamp(opj_comp.data[y * cwidth + x], OPJ_INT32{0}, max_value);
        }
      }
    }
    else {
      uint16_t* p16 = (uint16_t*)p;
      for (int y = 0; y < cheight; y++) {
        for (int x = 0; x < cwidth; x++) {
          p16[y * stride/2 + x] = (uint16_t) std::clamp(opj_comp.data[y * cwidth + x], OPJ_INT32{0}, max_value);
        }
      }
    }
  }

  if (out_user_data) {
    *out_user_data = decoder->user_data;
  }

  decoder->encoded_data.clear();
  decoder->read_position = 0;

  return heif_error_ok;
}

heif_error openjpeg_decode_next_image(void* decoder_raw, heif_image** out_img,
                                      const heif_security_limits* limits)
{
  return openjpeg_decode_next_image2(decoder_raw, out_img, nullptr, limits);
}

heif_error openjpeg_decode_image(void* decoder_raw, heif_image** out_img)
{
  auto* limits = heif_get_global_security_limits();
  return openjpeg_decode_next_image(decoder_raw, out_img, limits);
}

heif_error openjpeg_flush_data(void* decoder)
{
  return heif_error_ok;
}


static const heif_decoder_plugin decoder_openjpeg{
    5,
    openjpeg_plugin_name,
    openjpeg_init_plugin,
    openjpeg_deinit_plugin,
    openjpeg_does_support_format,
    openjpeg_new_decoder,
    openjpeg_free_decoder,
    openjpeg_push_data,
    openjpeg_decode_image,
    openjpeg_set_strict_decoding,
    "openjpeg",
    openjpeg_decode_next_image,
    /* minimum_required_libheif_version */ LIBHEIF_MAKE_VERSION(1,21,0),
    openjpeg_does_support_format2,
    openjpeg_new_decoder2,
    openjpeg_push_data2,
    openjpeg_flush_data,
    openjpeg_decode_next_image2
};

const heif_decoder_plugin* get_decoder_plugin_openjpeg()
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
