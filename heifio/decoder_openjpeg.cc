/*
  libheif example application "heif".

  MIT License

  Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include <cassert>
#include "decoder_openjpeg.h"
#include <openjpeg.h>

static struct heif_error heif_error_ok = { heif_error_Ok, heif_suberror_Unspecified, "Success" };

// Copy JPEG2000 image compnent into heif image channel
heif_error convert_JPEG2000_image(heif_image* image, heif_channel channel, opj_image_comp_t* comp, int output_bit_depth) {
  if (comp->prec < 1 || comp->prec > 16)
    return {
        heif_error_Invalid_input,
        heif_suberror_Unspecified,
        "JPEG2000 init failed" };
  int bit_depth = output_bit_depth >= comp->prec ? comp->prec : output_bit_depth;
  heif_error err = heif_image_add_plane_safe(image, channel, comp->w, comp->h, bit_depth, nullptr);
  if (err.code)
    return err;
  size_t stride;
  uint8_t* ptr = heif_image_get_plane2(image, channel, &stride);
  if (comp->prec <= 8) {
    for (OPJ_UINT32 h = 0; h < comp->h; h++) {
      for (OPJ_UINT32 w = 0; w < comp->w; w++) {
        ptr[h * stride + w] = comp->data[h * comp->w + w];
      }
    }
  }
  else {
    uint16_t* ptru = (uint16_t*)ptr;
    assert(!(stride & 1));
    assert(output_bit_depth >= 9 && output_bit_depth <= 16);
    stride /= 2;
    if (output_bit_depth == comp->prec) {
      for (OPJ_UINT32 h = 0; h < comp->h; h++) {
        for (OPJ_UINT32 w = 0; w < comp->w; w++) {
          ptru[h * stride + w] = comp->data[h * comp->w + w];
        }
      }
    }
    else if (output_bit_depth < comp->prec) {
      int shift = comp->prec - output_bit_depth;
      int mask = (1 << output_bit_depth) - 1;
      for (OPJ_UINT32 h = 0; h < comp->h; h++) {
        for (OPJ_UINT32 w = 0; w < comp->w; w++) {
          ptru[h * stride + w] = (comp->data[h * comp->w + w] >> shift) & mask;
        }
      }
    }
    else if (output_bit_depth > comp->prec) {
      int shift = output_bit_depth - comp->prec;
      int mask = (1 << output_bit_depth) - 1;
      for (OPJ_UINT32 h = 0; h < comp->h; h++) {
        for (OPJ_UINT32 w = 0; w < comp->w; w++) {
          ptru[h * stride + w] = (comp->data[h * comp->w + w] << shift) & mask;
        }
      }
    }
  }
  return heif_error_ok;
}

heif_error loadJPEG2000(const char* filename, int output_bit_depth, InputImage* input_image)
{
  struct heif_image* image = nullptr;
  struct heif_error err = heif_error_ok;
  std::shared_ptr<heif_image> image_shared;

  // initialize decompressor

  opj_dparameters_t parameters;
  opj_set_default_decoder_parameters(&parameters);

  std::unique_ptr<opj_codec_t, decltype(&opj_destroy_codec)> codec(opj_create_decompress(OPJ_CODEC_JP2), opj_destroy_codec);
  if (!codec) {
    return {
        heif_error_Invalid_input,
        heif_suberror_Unspecified,
        "JPEG2000 init failed" };
  }
  //opj_set_error_handler(codec, error_callback, nullptr);

  std::unique_ptr<opj_stream_t, decltype(&opj_stream_destroy)> stream(opj_stream_create_default_file_stream(filename, true), opj_stream_destroy);
  if (!stream) {
    return {
        heif_error_Invalid_input,
        heif_suberror_Unspecified,
        "read JPEG2000 file failed" };
  }

  if (!opj_setup_decoder(codec.get(), &parameters)) {
    return {
        heif_error_Invalid_input,
        heif_suberror_Unspecified,
        "setup JPEG2000 decoder failed" };
  }
  opj_image_t* opj_img = nullptr;
  if (!opj_read_header(stream.get(), codec.get(), &opj_img)) {
    return {
        heif_error_Invalid_input,
        heif_suberror_Unspecified,
        "read JPEG2000 file header failed" };
  }
  std::unique_ptr<opj_image_t, decltype(&opj_image_destroy)> image_deleter(opj_img, opj_image_destroy);
  if (!opj_decode(codec.get(), stream.get(), opj_img)) {
    return {
        heif_error_Invalid_input,
        heif_suberror_Unspecified,
        "JPEG2000 decode failed" };
  }
  opj_end_decompress(codec.get(), stream.get());

  heif_channel channels[4] = { heif_channel_unknown , heif_channel_unknown , heif_channel_unknown , heif_channel_unknown };

  switch (opj_img->color_space) {
  case OPJ_CLRSPC_SRGB:
    if (opj_img->numcomps == 3 || opj_img->numcomps == 4) {
      err = heif_image_create((int)opj_img->x1, (int)opj_img->y1,
        heif_colorspace_RGB,
        heif_chroma_444,
        &image);
      channels[0] = heif_channel_R;
      channels[1] = heif_channel_G;
      channels[2] = heif_channel_B;
      if (opj_img->numcomps == 4)
        channels[3] = heif_channel_Alpha;
    }
    else {
      return {
          heif_error_Unsupported_feature,
          heif_suberror_Unsupported_image_type,
          "JPEG2000 colorspace not supported" };
    }
    break;
  case OPJ_CLRSPC_GRAY:
    if (opj_img->numcomps == 1 || opj_img->numcomps == 2) {
      err = heif_image_create((int)opj_img->x1, (int)opj_img->y1,
        heif_colorspace_monochrome,
        heif_chroma_planar,
        &image);
      channels[0] = heif_channel_Y;
      if (opj_img->numcomps == 2)
        channels[1] = heif_channel_Alpha;
    }
    else {
      return {
          heif_error_Unsupported_feature,
          heif_suberror_Unsupported_image_type,
          "JPEG2000 colorspace not supported" };
    }
    break;
  case OPJ_CLRSPC_SYCC:
    // BT. 601
    if (opj_img->numcomps == 3 || opj_img->numcomps == 4) {
      heif_chroma chroma;
      if (opj_img->comps[1].dx == 1 && opj_img->comps[1].dy == 1)
        chroma = heif_chroma_444;
      else if (opj_img->comps[1].dx == 2 && opj_img->comps[1].dy == 1)
        chroma = heif_chroma_422;
      else if (opj_img->comps[1].dx == 2 && opj_img->comps[1].dy == 2)
        chroma = heif_chroma_420;
      else {
        return {
            heif_error_Unsupported_feature,
            heif_suberror_Unsupported_image_type,
            "JPEG2000 colorspace not supported" };
      }
      err = heif_image_create((int)opj_img->x1, (int)opj_img->y1,
        heif_colorspace_YCbCr,
        chroma,
        &image);
      channels[0] = heif_channel_Y;
      channels[1] = heif_channel_Cb;
      channels[2] = heif_channel_Cr;
      if (opj_img->numcomps == 4)
        channels[3] = heif_channel_Alpha;
    }
    else {
      return {
          heif_error_Unsupported_feature,
          heif_suberror_Unsupported_image_type,
          "JPEG2000 colorspace not supported" };
    }
    break;
  default:
    return {
        heif_error_Unsupported_feature,
        heif_suberror_Unsupported_image_type,
        "JPEG2000 colorspace not supported" };
  }
  if (err.code)
    return err;
  image_shared = std::shared_ptr<heif_image>(image, &heif_image_release);
  for (int i = 0; i < 4; i++) {
    if (channels[i] != heif_channel_unknown) {
      err = convert_JPEG2000_image(image, channels[i], &opj_img->comps[i], output_bit_depth);
      if (err.code)
        return err;
    }
    else
      break;
  }
  if (opj_img->icc_profile_len > 0) {
    heif_image_set_raw_color_profile(image, "prof", opj_img->icc_profile_buf, opj_img->icc_profile_len);
  }
  input_image->image = std::move(image_shared);
  return err;
}
