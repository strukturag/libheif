/*
 * HEIF codec.
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
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
#include "heif_image.h"

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <memory>
#include <string.h>

extern "C" {

#if HAVE_LIBDE265

#include <libde265/de265.h>
#include <stdio.h>

struct libde265_decoder
{
  de265_decoder_context* ctx;
};



heif_pixel_image* convert_libde265_image_to_heif_image(const struct de265_image* de265img)
{
  auto out_img = std::make_shared<HeifPixelImage>();

  out_img->create( de265_get_image_width(de265img, 0),
                   de265_get_image_height(de265img, 0),
                   heif_colorspace_YCbCr, // TODO
                   (heif_chroma)de265_get_chroma_format(de265img) );


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


    out_img->add_plane(channel2plane[c], w,h, bpp);

    int dst_stride;
    uint8_t* dst_mem = out_img->get_plane(channel2plane[c], &dst_stride);

    int bytes_per_pixel = (bpp+7)/8;

    for (int y=0;y<h;y++) {
      memcpy(dst_mem + y*dst_stride, data + y*stride, w * bytes_per_pixel);
    }
  }


  heif_pixel_image* out_C_img = (heif_pixel_image*)(new std::shared_ptr<HeifPixelImage>(out_img));

  return out_C_img;
}


void* libde265_new_decoder()
{
  struct libde265_decoder* decoder = new libde265_decoder();

  decoder->ctx = de265_new_decoder();
  de265_start_worker_threads(decoder->ctx,1);

  return decoder;
}

void libde265_free_decoder(void* decoder_raw)
{
  struct libde265_decoder* decoder = (struct libde265_decoder*)decoder_raw;

  de265_error err = de265_free_decoder(decoder->ctx);
  (void)err;

  delete decoder;
}


#if LIBDE265_NUMERIC_VERSION >= 0x02000000

void libde265_v2_push_data(void* decoder_raw, uint8_t* data,uint32_t size)
{
  struct libde265_decoder* decoder = (struct libde265_decoder*)decoder_raw;

  de265_push_data(decoder->ctx, data, size, 0, nullptr);
}


void libde265_v2_decode_image(void* decoder_raw, struct heif_pixel_image** out_img)
{
  struct libde265_decoder* decoder = (struct libde265_decoder*)decoder_raw;

  de265_push_end_of_stream(decoder->ctx);

  int action = de265_get_action(decoder->ctx, 1);
  printf("libde265 action: %d\n",action);

  if (action==de265_action_get_image) {
    printf("image decoded !\n");

    const de265_image* img = de265_get_next_picture(decoder->ctx);
    if (img) {
      *out_img = convert_libde265_image_to_heif_image(img);

      de265_release_picture(img);
    }
  }
}

#else

void libde265_v1_push_data(void* decoder_raw, uint8_t* data,uint32_t size)
{
  struct libde265_decoder* decoder = (struct libde265_decoder*)decoder_raw;

  de265_push_data(ctx, data, size, 0, nullptr);
}


void libde265_v1_decode_image(void* decoder_raw, struct heif_pixel_image** out_img)
{
  struct libde265_decoder* decoder = (struct libde265_decoder*)decoder_raw;

  de265_flush_data(ctx);

  int more;
  de265_error decode_err;
  do {
    more = 0;
    decode_err = de265_decode(ctx, &more);
    if (decode_err != DE265_OK) {
      printf("Error decoding: %s (%d)\n", de265_get_error_text(decode_err), decode_err);
      break;
    }

    const struct de265_image* image = de265_get_next_picture(ctx);
    if (image) {
      *out_img = convert_libde265_image_to_heif_image(image);

      printf("Decoded image: %d/%d\n", de265_get_image_width(image, 0),
             de265_get_image_height(image, 0));
      de265_release_next_picture(ctx);
    }
  } while (more);
}

#endif





#if LIBDE265_NUMERIC_VERSION >= 0x02000000

static const struct heif_decoder_plugin decoder_libde265
{
  .new_decoder = libde265_new_decoder,
  .free_decoder = libde265_free_decoder,
  .push_data = libde265_v2_push_data,
  .decode_image = libde265_v2_decode_image
};

#else

static const struct heif_decoder_plugin decoder_libde265
{
};

#endif

const struct heif_decoder_plugin* get_decoder_plugin_libde265() { return &decoder_libde265; }

#else

const struct heif_decoder_plugin* get_decoder_plugin_libde265() { return NULL; }

#endif

}
