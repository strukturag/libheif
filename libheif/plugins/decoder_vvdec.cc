/*
 * AVIF codec.
 * Copyright (c) 2019 Dirk Farin <dirk.farin@gmail.com>
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
#include "decoder_vvdec.h"
#include "logging.h"
#include <memory>
#include <cstring>
#include <cassert>

#include <vvdec/vvdec.h>


struct vvdec_decoder
{
  vvdecDecoder* decoder = nullptr;
  vvdecAccessUnit* au = nullptr;
  size_t au_maxsize = 0;

  bool strict_decoding = false;

  std::vector<std::vector<uint8_t>> nalus;
};

static const char kSuccess[] = "Success";
static const char kEmptyString[] = "";

static const int VVDEC_PLUGIN_PRIORITY = 100;

#define MAX_PLUGIN_NAME_LENGTH 80

static char plugin_name[MAX_PLUGIN_NAME_LENGTH];


static const char* vvdec_plugin_name()
{
  const char* version = vvdec_get_version();

  if (strlen(version) < 60) {
    sprintf(plugin_name, "VVCDEC decoder (%s)", version);
  }
  else
  {
    strcpy(plugin_name, "VVDEC decoder");
  }

  return plugin_name;
}


static void vvdec_init_plugin()
{
}


static void vvdec_deinit_plugin()
{
}


static int vvdec_does_support_format(enum heif_compression_format format)
{
  if (format == heif_compression_VVC) {
    return VVDEC_PLUGIN_PRIORITY;
  }
  else {
    return 0;
  }
}


struct heif_error vvdec_new_decoder(void** dec)
{
  struct vvdec_decoder* decoder = new vvdec_decoder();

  vvdecParams params;
  vvdec_params_default(&params);
  params.logLevel = VVDEC_INFO;
  decoder->decoder = vvdec_decoder_open(&params);

  const int MaxNaluSize = 256 * 1024;
  decoder->au = vvdec_accessUnit_alloc();
  vvdec_accessUnit_default(decoder->au);
  vvdec_accessUnit_alloc_payload(decoder->au, MaxNaluSize);
  decoder->au_maxsize = MaxNaluSize;

  *dec = decoder;

  struct heif_error err = {heif_error_Ok, heif_suberror_Unspecified, kSuccess};
  return err;
}


void vvdec_free_decoder(void* decoder_raw)
{
  struct vvdec_decoder* decoder = (vvdec_decoder*) decoder_raw;

  if (!decoder) {
    return;
  }

  if (decoder->au) {
    vvdec_accessUnit_free(decoder->au);
    decoder->au = nullptr;
    decoder->au_maxsize = 0;
  }

  if (decoder->decoder) {
    vvdec_decoder_close(decoder->decoder);
    decoder->decoder = nullptr;
  }

  delete decoder;
}


void vvdec_set_strict_decoding(void* decoder_raw, int flag)
{
  struct vvdec_decoder* decoder = (vvdec_decoder*) decoder_raw;

  decoder->strict_decoding = flag;
}


struct heif_error vvdec_push_data(void* decoder_raw, const void* frame_data, size_t frame_size)
{
  struct vvdec_decoder* decoder = (struct vvdec_decoder*) decoder_raw;

  uint8_t* data = (uint8_t*) frame_data;

  for (;;) {
    uint32_t size = ((((uint32_t) data[0]) << 24) |
                     (((uint32_t) data[1]) << 16) |
                     (((uint32_t) data[2]) << 8) |
                     (data[3]));

    data += 4;

    std::vector<uint8_t> nalu;
    nalu.push_back(0);
    nalu.push_back(0);
    nalu.push_back(1);
    nalu.insert(nalu.end(), data, data + size);

    decoder->nalus.push_back(nalu);
    data += size;
    frame_size -= 4 + size;
    if (frame_size == 0) {
      break;
    }
  }

  struct heif_error err = {heif_error_Ok, heif_suberror_Unspecified, kSuccess};
  return err;
}


struct heif_error vvdec_decode_image(void* decoder_raw, struct heif_image** out_img)
{
  struct vvdec_decoder* decoder = (struct vvdec_decoder*) decoder_raw;

  vvdecFrame* frame = nullptr;

  for (int i = 0;; i++) {
    int ret;

    if (i < (int)decoder->nalus.size()) {
      const auto& nalu = decoder->nalus[i];

      if (nalu.size() > decoder->au_maxsize) {
        vvdec_accessUnit_free(decoder->au);

        decoder->au = vvdec_accessUnit_alloc();
        vvdec_accessUnit_default(decoder->au);
        vvdec_accessUnit_alloc_payload(decoder->au, nalu.size());
        decoder->au_maxsize = nalu.size();
      }

      memcpy(decoder->au->payload, nalu.data(), nalu.size());
      decoder->au->payloadUsedSize = nalu.size();

      ret = vvdec_decode(decoder->decoder, decoder->au, &frame);
    }
    else {
      ret = vvdec_flush(decoder->decoder, &frame);
    }

    if (ret != VVDEC_OK && ret != VVDEC_EOF && ret != VVDEC_TRY_AGAIN) {
      return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "vvdec decoding error"};
    }

    if (frame) {
      break;
    }
  }


  decoder->nalus.clear();

  heif_chroma chroma;
  heif_colorspace colorspace;

  if (frame->colorFormat == VVDEC_CF_YUV400_PLANAR) {
    chroma = heif_chroma_monochrome;
    colorspace = heif_colorspace_monochrome;
  }
  else {
    if (frame->colorFormat == VVDEC_CF_YUV444_PLANAR) {
      chroma = heif_chroma_444;
    }
    else if (frame->colorFormat == VVDEC_CF_YUV422_PLANAR) {
      chroma = heif_chroma_422;
    }
    else {
      chroma = heif_chroma_420;
    }
    colorspace = heif_colorspace_YCbCr;
  }

  struct heif_image* heif_img = nullptr;
  struct heif_error err = heif_image_create(frame->width, frame->height,
                                            colorspace,
                                            chroma,
                                            &heif_img);
  if (err.code != heif_error_Ok) {
    assert(heif_img == nullptr);
    return err;
  }


  // --- read nclx parameters from decoded AV1 bitstream

#if 0
  heif_color_profile_nclx nclx;
  nclx.version = 1;
  HEIF_WARN_OR_FAIL(decoder->strict_decoding, heif_img, heif_nclx_color_profile_set_color_primaries(&nclx, static_cast<uint16_t>(img->cp)), { heif_image_release(heif_img); });
  HEIF_WARN_OR_FAIL(decoder->strict_decoding, heif_img, heif_nclx_color_profile_set_transfer_characteristics(&nclx, static_cast<uint16_t>(img->tc)), { heif_image_release(heif_img); });
  HEIF_WARN_OR_FAIL(decoder->strict_decoding, heif_img, heif_nclx_color_profile_set_matrix_coefficients(&nclx, static_cast<uint16_t>(img->mc)), { heif_image_release(heif_img); });
  nclx.full_range_flag = (img->range == AOM_CR_FULL_RANGE);
  heif_image_set_nclx_color_profile(heif_img, &nclx);
#endif

  // --- transfer data from vvdecFrame to HeifPixelImage

  heif_channel channel2plane[3] = {
      heif_channel_Y,
      heif_channel_Cb,
      heif_channel_Cr
  };


  int num_planes = (chroma == heif_chroma_monochrome ? 1 : 3);

  for (int c = 0; c < num_planes; c++) {
    int bpp = frame->bitDepth;

    const auto& plane = frame->planes[c];
    const uint8_t* data = plane.ptr;
    int stride = plane.stride;

    int w = plane.width;
    int h = plane.height;

    err = heif_image_add_plane(heif_img, channel2plane[c], w, h, bpp);
    if (err.code != heif_error_Ok) {
      heif_image_release(heif_img);
      return err;
    }

    int dst_stride;
    uint8_t* dst_mem = heif_image_get_plane(heif_img, channel2plane[c], &dst_stride);

    int bytes_per_pixel = (bpp + 7) / 8;

    for (int y = 0; y < h; y++) {
      memcpy(dst_mem + y * dst_stride, data + y * stride, w * bytes_per_pixel);
    }
  }

  *out_img = heif_img;

  vvdec_frame_unref(decoder->decoder, frame);

  return err;
}


static const struct heif_decoder_plugin decoder_vvdec
    {
        3,
        vvdec_plugin_name,
        vvdec_init_plugin,
        vvdec_deinit_plugin,
        vvdec_does_support_format,
        vvdec_new_decoder,
        vvdec_free_decoder,
        vvdec_push_data,
        vvdec_decode_image,
        vvdec_set_strict_decoding,
        "vvdec"
    };


const struct heif_decoder_plugin* get_decoder_plugin_vvdec()
{
  return &decoder_vvdec;
}


#if PLUGIN_VVDEC
heif_plugin_info plugin_info {
  1,
  heif_plugin_type_decoder,
  &decoder_vvdec
};
#endif
