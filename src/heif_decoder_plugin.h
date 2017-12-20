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
  void* (*new_decoder)();

  void (*free_decoder)(void*);

  void (*push_data)(void* decoder, uint8_t* data,uint32_t size);

  heif_image* (*decode_all)(void* decoder);

  heif_image* (*decode_partial)(void* decoder,
                                int x_left, int y_top,
                                int width, int height);
};

void heif_register_decoder(uint32_t type, const heif_decoder_plugin*);


#endif
