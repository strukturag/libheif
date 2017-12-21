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


#ifndef LIBHEIF_HEIF_IMAGE_H
#define LIBHEIF_HEIF_IMAGE_H

#include "heif.h"

#include <vector>
#include <map>
#include <set>


class HeifPixelImage
{
 public:
  ~HeifPixelImage();

  void create(int width,int height, heif_colorspace colorspace, heif_chroma chroma);

  void add_plane(heif_channel channel, int width, int height, int bit_depth);


  int get_width() const { return m_width; }

  int get_height() const { return m_height; }

  int get_width(enum heif_channel channel) const;

  int get_height(enum heif_channel channel) const;

  heif_chroma get_chroma_format() const;

  std::set<enum heif_channel> get_channel_set() const;

  int get_bits_per_pixel(enum heif_channel channel) const;

  uint8_t* get_plane(enum heif_channel channel, int* out_stride);
  const uint8_t* get_plane(enum heif_channel channel, int* out_stride) const;

 private:
  struct ImagePlane {
    int width;
    int height;
    int bit_depth;
    std::vector<uint8_t> mem;
  };

  int m_width = 0;
  int m_height = 0;
  heif_colorspace m_colorspace = heif_colorspace_undefined;
  heif_chroma m_chroma = heif_chroma_undefined;

  std::map<heif_channel, ImagePlane> m_planes;
};


class HeifImage
{
 public:
  ~HeifImage();
};

#endif
