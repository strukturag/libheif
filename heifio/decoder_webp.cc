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

#include <memory>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <cassert>
#include <algorithm>
#include <array>
#include "decoder_webp.h"
#include "exif.h"

extern "C" {
#include <webp/mux.h>
#include <webp/decode.h>
}

static struct heif_error heif_error_ok = { heif_error_Ok, heif_suberror_Unspecified, "Success" };

heif_error loadWEBP(const char* filename, InputImage* input_image)
{
  struct heif_image* image = nullptr;
  struct heif_error err = heif_error_ok;

  // to store embedded icc profile
  uint32_t iccLen;
  uint8_t* iccBuffer = NULL;

  std::vector<uint8_t> xmpData;
  std::vector<uint8_t> exifData;

  // open input file

  FILE* infile;
  if ((infile = fopen(filename, "rb")) == NULL) {
    struct heif_error err = {
      .code = heif_error_Invalid_input,
      .subcode = heif_suberror_Unspecified,
      .message = "Cannot open WEBP file" };
    return err;
  }
  std::vector<uint8_t> bitstream;
  fseek(infile, 0, SEEK_END);
  auto fsize = _ftelli64(infile);
  if (fsize >= INT_MAX) {
    fclose(infile);
    struct heif_error err = {
      .code = heif_error_Invalid_input,
      .subcode = heif_suberror_Unspecified,
      .message = "WEBP file is too large" };
    return err;
  }
  fseek(infile, 0, SEEK_SET);
  bitstream.resize(fsize);
  fread(bitstream.data(), 1, fsize, infile);
  fclose(infile);

  // initialize decompressor
  int copy_data = 0;
  WebPData webpdata;
  WebPDataInit(&webpdata);
  webpdata.bytes = bitstream.data();
  webpdata.size = bitstream.size();

  WebPMux* mux = WebPMuxCreate(&webpdata, copy_data);
  if (!mux) {
    struct heif_error err = {
      .code = heif_error_Invalid_input,
      .subcode = heif_suberror_Unspecified,
      .message = "WebPMuxCreate failed" };
    return err;
  }
  std::unique_ptr<WebPMux, void(*)(WebPMux*)> mux_deleter(mux, &WebPMuxDelete);
  WebPMuxFrameInfo frame;
  if (WebPMuxGetFrame(mux, 1, &frame) != WEBP_MUX_OK) {
    struct heif_error err = {
      .code = heif_error_Invalid_input,
      .subcode = heif_suberror_Unspecified,
      .message = "WebPMuxGetFrame failed" };
    return err;
  }
  std::unique_ptr<WebPData, void(*)(WebPData*)> frame_deleter(&frame.bitstream, &WebPDataClear);
  // Getting ICC, XMP and EXIF
  WebPData icc_chunk;
  bool has_icc = false;;
  WebPDataInit(&icc_chunk);
  if (WebPMuxGetChunk(mux, "ICCP", &icc_chunk) == WEBP_MUX_OK) {
    has_icc = true;
  }
  WebPData exif_chunk;
  bool has_exif = false;
  WebPDataInit(&exif_chunk);
  if (WebPMuxGetChunk(mux, "EXIF", &exif_chunk) == WEBP_MUX_OK) {
    has_exif = true;
  }
  WebPData xmp_chunk;
  bool has_xmp = false;
  WebPDataInit(&xmp_chunk);
  if (WebPMuxGetChunk(mux, "XMP ", &xmp_chunk) == WEBP_MUX_OK) {
    has_xmp = true;
  }

  WebPDecoderConfig config;
  if (!WebPInitDecoderConfig(&config)) {
    struct heif_error err = {
      .code = heif_error_Invalid_input,
      .subcode = heif_suberror_Unspecified,
      .message = "Cannot open WEBP file" };
    return err;
  }

  // retrieve the bitstream's features.
  if (WebPGetFeatures(frame.bitstream.bytes, frame.bitstream.size, &config.input) != VP8_STATUS_OK) {
    struct heif_error err = {
      .code = heif_error_Invalid_input,
      .subcode = heif_suberror_Unspecified,
      .message = "Cannot open WEBP file" };
    return err;
  }

  if (config.input.format == 2) {
    // Lossless: RGB(A)
    if (config.input.has_alpha) {
      config.output.colorspace = MODE_RGBA;
    }
    else {
      config.output.colorspace = MODE_RGB;
    }

    // initialize heif image
    const int width = config.input.width;
    const int height = config.input.height;
    if (config.input.has_alpha) {
      err = heif_image_create((int)width, (int)height,
        heif_colorspace_RGB,
        heif_chroma_interleaved_RGBA,
        &image);
    }
    else {
      err = heif_image_create((int)width, (int)height,
        heif_colorspace_RGB,
        heif_chroma_interleaved_RGB,
        &image);
    }
    if (err.code)
      return err;

    err = heif_image_add_plane(image, heif_channel_interleaved, (int)width, (int)height, 8);
    if (err.code)
      return err;
    size_t stride;
    uint8_t* ptr = heif_image_get_plane2(image, heif_channel_interleaved, &stride);
    // decode into heif image
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = ptr;
    config.output.u.RGBA.size = stride * height;
    config.output.u.RGBA.stride = stride;
    config.output.width = width;
    config.output.height = height;
    // D) Decode!
    if (WebPDecode(frame.bitstream.bytes, frame.bitstream.size, &config) != VP8_STATUS_OK)
    {
      struct heif_error err = {
        .code = heif_error_Invalid_input,
        .subcode = heif_suberror_Unspecified,
        .message = "WEBP file decoding error!" };
      return err;
    }
  }
  else if (config.input.format == 1) {
    // Lossy: YUV420
    if (config.input.has_alpha) {
      config.output.colorspace = MODE_YUVA;
    }
    else {
      config.output.colorspace = MODE_YUV;
    }

    const int width = config.input.width;
    const int height = config.input.height;
    err = heif_image_create((int)width, (int)height,
      heif_colorspace_YCbCr,
      heif_chroma_420,
      &image);
    if (err.code)
      return err;
    size_t stride[4];
    uint8_t* ptr[4];
    const int uv_width = (width + 1) / 2;
    const int uv_height = (height + 1) / 2;
    err = heif_image_add_plane(image, heif_channel_Y, (int)width, (int)height, 8);
    if (err.code)
      return err;
    err = heif_image_add_plane(image, heif_channel_Cb, uv_width, uv_height, 8);
    if (err.code)
      return err;
    err = heif_image_add_plane(image, heif_channel_Cr, uv_width, uv_height, 8);
    if (err.code)
      return err;
    if (config.input.has_alpha) {
      err = heif_image_add_plane(image, heif_channel_Alpha, (int)width, (int)height, 8);
      if (err.code)
        return err;
    }
    ptr[0] = heif_image_get_plane2(image, heif_channel_Y, &stride[0]);
    ptr[1] = heif_image_get_plane2(image, heif_channel_Cb, &stride[1]);
    ptr[2] = heif_image_get_plane2(image, heif_channel_Cr, &stride[2]);
    if (config.input.has_alpha) {
      ptr[3] = heif_image_get_plane2(image, heif_channel_Alpha, &stride[3]);
    }
    config.output.is_external_memory = 1;
    config.output.u.YUVA.y = ptr[0];
    config.output.u.YUVA.y_stride = stride[0];
    config.output.u.YUVA.y_size = height * stride[0];
    config.output.u.YUVA.u = ptr[1];
    config.output.u.YUVA.u_stride = stride[1];
    config.output.u.YUVA.u_size = uv_height * stride[1];
    config.output.u.YUVA.v = ptr[2];
    config.output.u.YUVA.v_stride = stride[2];
    config.output.u.YUVA.v_size = uv_height * stride[2];
    if (config.input.has_alpha) {
      config.output.u.YUVA.a = ptr[3];
      config.output.u.YUVA.a_stride = stride[3];
      config.output.u.YUVA.a_size = height * stride[3];
    }
    else {
      config.output.u.YUVA.a = NULL;
      config.output.u.YUVA.a_stride = 0;
      config.output.u.YUVA.a_size = 0;
    }
    // D) Decode!
    if (WebPDecode(frame.bitstream.bytes, frame.bitstream.size, &config) != VP8_STATUS_OK) {
      struct heif_error err = {
        .code = heif_error_Invalid_input,
        .subcode = heif_suberror_Unspecified,
        .message = "WEBP file decoding error!" };
      return err;
    }
  }
  else {
    struct heif_error err = {
      .code = heif_error_Invalid_input,
      .subcode = heif_suberror_Unspecified,
      .message = "WEBP file is too strange" };
    return err;
  }

  if (config.input.has_alpha) {
    heif_image_set_premultiplied_alpha(image, 0);
  }

  if (has_exif) {
    input_image->exif.resize(exif_chunk.size);
    memcpy(input_image->exif.data(), exif_chunk.bytes, exif_chunk.size);
  }
  if (has_icc) {
    heif_image_set_raw_color_profile(image, "prof", icc_chunk.bytes, icc_chunk.size);
  }
  if (has_xmp) {
    input_image->xmp.resize(xmp_chunk.size);
    memcpy(input_image->xmp.data(), xmp_chunk.bytes, xmp_chunk.size);
  }


  // F) Reclaim memory allocated in config's object. It's safe to call
  // this function even if the memory is external and wasn't allocated
  // by WebPDecode().
  WebPFreeDecBuffer(&config.output);

  input_image->image = std::shared_ptr<heif_image>(image,
    [](heif_image* img) { heif_image_release(img); });
  return err;
}
