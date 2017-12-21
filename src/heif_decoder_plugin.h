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

#ifndef LIBHEIF_HEIF_DECODER_PLUGIN_H
#define LIBHEIF_HEIF_DECODER_PLUGIN_H


struct heif_decoder_plugin
{
  // Create a new decoder context for decoding an image
  void* (*new_decoder)();

  // Free the decoder context (heif_image can still be used after destruction)
  void (*free_decoder)(void*);

  // Push more data into the decoder. This can be called multiple times.
  // This may not be called after any decode_*() function has been called.
  void (*push_data)(void* decoder, uint8_t* data,uint32_t size);


  // --- After pushing the data into the decoder, exactly one of the decode functions may be called once.

  // Decode data into a full image. All data has to be pushed into the decoder before calling this.
  heif_image* (*decode_image)(void* decoder);

  // Decode only part of the image.
  // May be useful if the input image is tiled and we only need part of it.
  /*
  heif_image* (*decode_partial)(void* decoder,
                                int x_left, int y_top,
                                int width, int height);
  */

  // Reset decoder, such that we can feed in new data for another image.
  // void (*reset_image)(void* decoder);
};


void heif_register_decoder(heif_file* heif, uint32_t type, const heif_decoder_plugin*);

// TODO void heif_register_encoder(heif_file* heif, uint32_t type, const heif_encoder_plugin*);


#endif
