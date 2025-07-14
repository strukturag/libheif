/*
 * openh264 codec.
 * Copyright (c) 2024 Dirk Farin <dirk.farin@gmail.com>
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
#include "decoder_openh264.h"
#include <memory>
#include <cstring>
#include <cassert>
#include <vector>
#include <cstdio>

#include <wels/codec_api.h>
#include <string>


struct openh264_decoder
{
  std::vector<uint8_t> data;
  std::string error_message;
};

static const char kSuccess[] = "Success";

static const int OpenH264_PLUGIN_PRIORITY = 100;

#define MAX_PLUGIN_NAME_LENGTH 80

static char plugin_name[MAX_PLUGIN_NAME_LENGTH];

static heif_error kError_EOF = {heif_error_Decoder_plugin_error, heif_suberror_End_of_data, "Insufficient input data"};


static const char* openh264_plugin_name()
{
  OpenH264Version version = WelsGetCodecVersion();

  sprintf(plugin_name, "OpenH264 %d.%d.%d", version.uMajor, version.uMinor, version.uRevision);

  return plugin_name;
}


static void openh264_init_plugin()
{
}


static void openh264_deinit_plugin()
{
}


static int openh264_does_support_format(enum heif_compression_format format)
{
  if (format == heif_compression_AVC) {
    return OpenH264_PLUGIN_PRIORITY;
  }
  else {
    return 0;
  }
}


struct heif_error openh264_new_decoder(void** dec)
{
  auto* decoder = new openh264_decoder();
  *dec = decoder;

  struct heif_error err = {heif_error_Ok, heif_suberror_Unspecified, kSuccess};
  return err;
}


void openh264_free_decoder(void* decoder_raw)
{
  auto* decoder = (openh264_decoder*) decoder_raw;

  if (!decoder) {
    return;
  }

  delete decoder;
}


void openh264_set_strict_decoding(void* decoder_raw, int flag)
{
//  auto* decoder = (openh264_decoder*) decoder_raw;
}


struct heif_error openh264_push_data(void* decoder_raw, const void* frame_data, size_t frame_size)
{
  auto* decoder = (struct openh264_decoder*) decoder_raw;

  const auto* input_data = (const uint8_t*) frame_data;

  decoder->data.insert(decoder->data.end(), input_data, input_data + frame_size);

  struct heif_error err = {heif_error_Ok, heif_suberror_Unspecified, kSuccess};
  return err;
}


struct heif_error openh264_decode_next_image(void* decoder_raw, struct heif_image** out_img,
                                             const heif_security_limits* limits)
{
  auto* decoder = (struct openh264_decoder*) decoder_raw;

  if (decoder->data.size() < 4) {
    return kError_EOF;
  }

  const std::vector<uint8_t>& indata = decoder->data;
  std::vector<uint8_t> scdata;

  size_t idx = 0;
  while (idx < indata.size()) {
    if (indata.size() - 4 < idx) {
      return kError_EOF;
    }

    uint32_t size = ((indata[idx] << 24) | (indata[idx + 1] << 16) | (indata[idx + 2] << 8) | indata[idx + 3]);
    idx += 4;

    if (indata.size() < size || indata.size() - size < idx) {
      return kError_EOF;
    }

    scdata.push_back(0);
    scdata.push_back(0);
    scdata.push_back(1);

    // check for need of start code emulation prevention

    bool do_start_code_emulation_check = true;

    while (do_start_code_emulation_check && size >= 3) {

      bool found_start_code_emulation = false;

      for (size_t i = 0; i < size - 3; i++) {
        if (indata[idx + 0] == 0 &&
            indata[idx + 1] == 0 &&
            (indata[idx + 2] >= 0 && indata[idx + 2] <= 3)) {
          scdata.push_back(0);
          scdata.push_back(0);
          scdata.push_back(3);

          scdata.insert(scdata.end(), &indata[idx + 2], indata.data() + idx + i + 2);
          idx += i + 2;
          size -= (uint32_t)(i + 2);
          found_start_code_emulation = true;
          break;
        }
      }

      do_start_code_emulation_check = found_start_code_emulation;
    }

    assert(size > 0);
    // Note: we cannot write &indata[idx + size] since that would use the operator[] on an element beyond the vector range.
    scdata.insert(scdata.end(), &indata[idx], indata.data() + idx + size);

    idx += size;
  }

  if (idx != indata.size()) {
    return kError_EOF;
  }

  // input: encoded bitstream start position; should include start code prefix
  unsigned char* pBuf = scdata.data();

  // input: encoded bit stream length; should include the size of start code prefix
  int iSize = static_cast<int>(scdata.size());

  //output: [0~2] for Y,U,V buffer for Decoding only
  unsigned char* pData[3] = {nullptr, nullptr, nullptr};

  // in-out: for Decoding only: declare and initialize the output buffer info, this should never co-exist with Parsing only

  SBufferInfo sDstBufInfo;
  memset(&sDstBufInfo, 0, sizeof(SBufferInfo));

  // Step 2:decoder creation
  ISVCDecoder* pSvcDecoder;
  WelsCreateDecoder(&pSvcDecoder);
  if (!pSvcDecoder) {
    return {heif_error_Decoder_plugin_error,
            heif_suberror_Unspecified,
            "Cannot create OpenH264 decoder"};
  }

  std::unique_ptr<ISVCDecoder, void (*)(ISVCDecoder*)> dummy_h264_decoder_ptr(pSvcDecoder, WelsDestroyDecoder);


  // Step 3:declare required parameter, used to differentiate Decoding only and Parsing only
  SDecodingParam sDecParam{};
  sDecParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;

  //for Parsing only, the assignment is mandatory
  // sDecParam.bParseOnly = true;

  // Step 4:initialize the parameter and decoder context, allocate memory
  pSvcDecoder->Initialize(&sDecParam);

  // Step 5:do actual decoding process in slice level; this can be done in a loop until data ends

  //for Decoding only
  int iRet = pSvcDecoder->DecodeFrameNoDelay(pBuf, iSize, pData, &sDstBufInfo);

  if (iRet != 0) {
    return {heif_error_Decoder_plugin_error,
            heif_suberror_Unspecified,
            "OpenH264 decoder error"};
  }

  /*
  // TODO: I receive an iBufferStatus==0, but the output image is still decoded
  if (sDstBufInfo.iBufferStatus == 0) {
    return {heif_error_Decoder_plugin_error,
            heif_suberror_Unspecified,
            "OpenH264 decoder did not output any image"};
  }
  */

  uint32_t width = sDstBufInfo.UsrData.sSystemBuffer.iWidth;
  uint32_t height = sDstBufInfo.UsrData.sSystemBuffer.iHeight;

  struct heif_image* heif_img;
  struct heif_error err{};

  uint32_t cwidth, cheight;

  if (sDstBufInfo.UsrData.sSystemBuffer.iFormat == videoFormatI420) {
    cwidth = (width + 1) / 2;
    cheight = (height + 1) / 2;

    err = heif_image_create(width, height,
                            heif_colorspace_YCbCr,
                            heif_chroma_420,
                            &heif_img);
    if (err.code != heif_error_Ok) {
      assert(heif_img == nullptr);
      return err;
    }

    *out_img = heif_img;

    err = heif_image_add_plane_safe(heif_img, heif_channel_Y, width, height, 8, limits);
    if (err.code != heif_error_Ok) {
      // copy error message to decoder object because heif_image will be released
      decoder->error_message = err.message;
      err.message = decoder->error_message.c_str();

      heif_image_release(heif_img);
      return err;
    }

    err = heif_image_add_plane_safe(heif_img, heif_channel_Cb, cwidth, cheight, 8, limits);
    if (err.code != heif_error_Ok) {
      // copy error message to decoder object because heif_image will be released
      decoder->error_message = err.message;
      err.message = decoder->error_message.c_str();

      heif_image_release(heif_img);
      return err;
    }

    err = heif_image_add_plane_safe(heif_img, heif_channel_Cr, cwidth, cheight, 8, limits);
    if (err.code != heif_error_Ok) {
      // copy error message to decoder object because heif_image will be released
      decoder->error_message = err.message;
      err.message = decoder->error_message.c_str();

      heif_image_release(heif_img);
      return err;
    }

    size_t y_stride;
    size_t cb_stride;
    size_t cr_stride;
    uint8_t* py = heif_image_get_plane2(heif_img, heif_channel_Y, &y_stride);
    uint8_t* pcb = heif_image_get_plane2(heif_img, heif_channel_Cb, &cb_stride);
    uint8_t* pcr = heif_image_get_plane2(heif_img, heif_channel_Cr, &cr_stride);

    int ystride = sDstBufInfo.UsrData.sSystemBuffer.iStride[0];
    int cstride = sDstBufInfo.UsrData.sSystemBuffer.iStride[1];

    for (uint32_t y = 0; y < height; y++) {
      memcpy(py + y * y_stride, sDstBufInfo.pDst[0] + y * ystride, width);
    }

    for (uint32_t y = 0; y < (height + 1) / 2; y++) {
      memcpy(pcb + y * cb_stride, sDstBufInfo.pDst[1] + y * cstride, (width + 1) / 2);
      memcpy(pcr + y * cr_stride, sDstBufInfo.pDst[2] + y * cstride, (width + 1) / 2);
    }
  }
  else {
    return {heif_error_Decoder_plugin_error,
            heif_suberror_Unspecified,
            "Unsupported image pixel format"};
  }

  // Step 6:uninitialize the decoder and memory free

  pSvcDecoder->Uninitialize(); // TODO: do we have to Uninitialize when an error is returned?

  decoder->data.clear();

  return heif_error_ok;
}

struct heif_error openh264_decode_image(void* decoder_raw, struct heif_image** out_img)
{
  auto* limits = heif_get_global_security_limits();
  return openh264_decode_next_image(decoder_raw, out_img, limits);
}


static const struct heif_decoder_plugin decoder_openh264{
        4,
        openh264_plugin_name,
        openh264_init_plugin,
        openh264_deinit_plugin,
        openh264_does_support_format,
        openh264_new_decoder,
        openh264_free_decoder,
        openh264_push_data,
        openh264_decode_image,
        openh264_set_strict_decoding,
        "openh264",
        openh264_decode_next_image
};


const struct heif_decoder_plugin* get_decoder_plugin_openh264()
{
  return &decoder_openh264;
}


#if PLUGIN_OpenH264_DECODER
heif_plugin_info plugin_info {
  1,
  heif_plugin_type_decoder,
  &decoder_openh264
};
#endif
