/*
 * AVIF codec.
 * Copyright (c) 2019 struktur AG, Dirk Farin <farin@struktur.de>
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
#include "heif_plugin.h"

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <memory>
#include <string.h>
#include <stdio.h>

#include <aom/aom_decoder.h>
#include <aom/aomdx.h>


struct aom_decoder
{
  aom_codec_ctx_t codec;
  aom_codec_iface_t* iface;
  //const AvxInterface *decoder = NULL;
};

static const char kSuccess[] = "Success";
static const char kEmptyString[] = "";

static const int AOM_PLUGIN_PRIORITY = 100;

#define MAX_PLUGIN_NAME_LENGTH 80

static char plugin_name[MAX_PLUGIN_NAME_LENGTH];


static const char* aom_plugin_name()
{
  if (strlen(aom_codec_iface_name(aom_codec_av1_dx())) < MAX_PLUGIN_NAME_LENGTH) {
    strcpy(plugin_name, aom_codec_iface_name(aom_codec_av1_dx()));
  }
  else {
    strcpy(plugin_name, "AOMedia AV1 decoder");
  }

  return plugin_name;
}


static void aom_init_plugin()
{
  //de265_init();
}


static void aom_deinit_plugin()
{
  //de265_free();
}


static int aom_does_support_format(enum heif_compression_format format)
{
  if (format == heif_compression_AV1) {
    return AOM_PLUGIN_PRIORITY;
  }
  else {
    return 0;
  }
}


/*
struct heif_error convert_libde265_image_to_heif_image(struct libde265_decoder* decoder,
                                                       const struct de265_image* de265img, struct heif_image** image)
{
  struct heif_image* out_img;
  struct heif_error err = heif_image_create(
    de265_get_image_width(de265img, 0),
    de265_get_image_height(de265img, 0),
    heif_colorspace_YCbCr, // TODO
    (heif_chroma)de265_get_chroma_format(de265img),
    &out_img);
  if (err.code != heif_error_Ok) {
    return err;
  }

  // --- transfer data from de265_image to HeifPixelImage

  heif_channel channel2plane[3] = {
    heif_channel_Y,
    heif_channel_Cb,
    heif_channel_Cr
  };


  for (int c=0;c<3;c++) {
    int bpp = de265_get_bits_per_pixel(de265img, c);

    int stride;
    const uint8_t* data = de265_get_image_plane(de265img, c, &stride);

    int w = de265_get_image_width(de265img, c);
    int h = de265_get_image_height(de265img, c);

    err = heif_image_add_plane(out_img, channel2plane[c], w,h, bpp);
    if (err.code != heif_error_Ok) {
      heif_image_release(out_img);
      return err;
    }

    int dst_stride;
    uint8_t* dst_mem = heif_image_get_plane(out_img, channel2plane[c], &dst_stride);

    int bytes_per_pixel = (bpp+7)/8;

    for (int y=0;y<h;y++) {
      memcpy(dst_mem + y*dst_stride, data + y*stride, w * bytes_per_pixel);
    }
  }

  *image = out_img;
  return err;
}
*/

struct heif_error aom_new_decoder(void** dec)
{
  struct aom_decoder* decoder = new aom_decoder();

  //decoder->decoder = get_aom_decoder_by_fourcc(AV1_FOURCC);

  decoder->iface = aom_codec_av1_dx();

  aom_codec_err_t aomerr = aom_codec_dec_init(&decoder->codec, decoder->iface, NULL, 0);
  if (aomerr) {
    //die_codec(&codec, "Failed to initialize decoder.");
    printf("failed to initialize decoder %d\n", aomerr);
    exit(10);
  }

  *dec = decoder;

  struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, kSuccess };
  return err;
}

void aom_free_decoder(void* decoder_raw)
{
  //struct libde265_decoder* decoder = (struct libde265_decoder*)decoder_raw;

  //de265_error err = de265_free_decoder(decoder->ctx);
  //(void)err;

  //delete decoder;
}


struct heif_error aom_push_data(void* decoder_raw, const void* frame_data, size_t frame_size)
{
  struct aom_decoder* decoder = (struct aom_decoder*)decoder_raw;

  printf("in: %d bytes\n", (int)frame_size);

  for (int i=0;i<64;i++) {
    printf("%02x ", ((uint8_t*)frame_data)[i]);
    if ((i%16)==15) printf("\n");
  }

  aom_codec_err_t aomerr;
  aomerr = aom_codec_decode(&decoder->codec, (const uint8_t*)frame_data, frame_size, NULL);
  if (aomerr) {
    printf("ierr: %d\n", aomerr);

    struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, kSuccess };
    return err;
  }

  printf("i0\n");

  /*
  const uint8_t* cdata = (const uint8_t*)data;

  size_t ptr=0;
  while (ptr < size) {
    if (ptr+4 > size) {
      struct heif_error err = { heif_error_Decoder_plugin_error,
                                heif_suberror_End_of_data,
                                kEmptyString };
      return err;
    }

    // TODO: the size of the NAL unit length variable is defined in the hvcC header.
    // We should not assume that it is always 4 bytes.
    uint32_t nal_size = (cdata[ptr]<<24) | (cdata[ptr+1]<<16) | (cdata[ptr+2]<<8) | (cdata[ptr+3]);
    ptr+=4;

    if (ptr+nal_size > size) {
      //sstr << "NAL size (" << size32 << ") exceeds available data in file ("
      //<< data_bytes_left_to_read << ")";

      struct heif_error err = { heif_error_Decoder_plugin_error,
                                heif_suberror_End_of_data,
                                kEmptyString };
      return err;
    }

    de265_push_NAL(decoder->ctx, cdata+ptr, nal_size, 0, nullptr);
    ptr += nal_size;
  }
  */

  struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, kSuccess };
  return err;
}


struct heif_error aom_decode_image(void* decoder_raw, struct heif_image** out_img)
{
  struct aom_decoder* decoder = (struct aom_decoder*)decoder_raw;

  aom_codec_iter_t iter = NULL;
  aom_image_t* img = NULL;

  img = aom_codec_get_frame(&decoder->codec, &iter);

  if (img==NULL) {
    struct heif_error err = { heif_error_Decoder_plugin_error,
                              heif_suberror_Unspecified,
                              kEmptyString };
    return err;
  }



  struct heif_image* heif_img;
  struct heif_error err = heif_image_create(img->w, img->h,
                                            heif_colorspace_YCbCr,
                                            heif_chroma_420, // TODO
                                            &heif_img);
  if (err.code != heif_error_Ok) {
    return err;
  }



  // --- transfer data from de265_image to HeifPixelImage

  heif_channel channel2plane[3] = {
    heif_channel_Y,
    heif_channel_Cb,
    heif_channel_Cr
  };


  printf("wh: %d %d\n",img->w, img->h);

  for (int c=0;c<3;c++) {
    int bpp = img->bit_depth;

    const uint8_t* data = img->planes[c];
    int stride = img->stride[c];

    int w = img->w;
    int h = img->h;

    err = heif_image_add_plane(heif_img, channel2plane[c], w,h, bpp);
    if (err.code != heif_error_Ok) {
      heif_image_release(heif_img);
      return err;
    }

    int dst_stride;
    uint8_t* dst_mem = heif_image_get_plane(heif_img, channel2plane[c], &dst_stride);

    int bytes_per_pixel = (bpp+7)/8;

    for (int y=0;y<h;y++) {
      memcpy(dst_mem + y*dst_stride, data + y*stride, w * bytes_per_pixel);
    }
  }

  printf("i1\n");

  *out_img = heif_img;
  return err;
}



static const struct heif_decoder_plugin decoder_aom
{
  1,
  aom_plugin_name,
  aom_init_plugin,
  aom_deinit_plugin,
  aom_does_support_format,
  aom_new_decoder,
  aom_free_decoder,
  aom_push_data,
  aom_decode_image
};

const struct heif_decoder_plugin* get_decoder_plugin_aom() {
  return &decoder_aom;
}
