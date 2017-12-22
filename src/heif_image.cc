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


#include "heif_image.h"

#include <assert.h>


namespace heif {

HeifPixelImage::~HeifPixelImage()
{
}

void HeifPixelImage::create(int width,int height, heif_colorspace colorspace, heif_chroma chroma)
{
  m_width = width;
  m_height = height;
  m_colorspace = colorspace;
  m_chroma = chroma;
}

void HeifPixelImage::add_plane(heif_channel channel, int width, int height, int bit_depth)
{
  assert(bit_depth >= 8);
  assert(bit_depth <= 16);

  ImagePlane plane;
  plane.width = width;
  plane.height = height;
  plane.bit_depth = bit_depth;

  int bytes_per_pixel = (bit_depth+7)/8;

  plane.mem.resize(width * height * bytes_per_pixel);

  m_planes.insert(std::make_pair(channel, std::move(plane)));
}


int HeifPixelImage::get_width(enum heif_channel channel) const
{
  auto iter = m_planes.find(channel);
  assert(iter != m_planes.end());

  return iter->second.width;
}


int HeifPixelImage::get_height(enum heif_channel channel) const
{
  auto iter = m_planes.find(channel);
  assert(iter != m_planes.end());

  return iter->second.height;
}


std::set<heif_channel> HeifPixelImage::get_channel_set() const
{
  std::set<heif_channel> channels;

  for (const auto& plane : m_planes) {
    channels.insert(plane.first);
  }

  return channels;
}


uint8_t* HeifPixelImage::get_plane(enum heif_channel channel, int* out_stride)
{
  auto iter = m_planes.find(channel);
  assert(iter != m_planes.end());

  if (out_stride) {
    *out_stride = iter->second.width;
  }

  return iter->second.mem.data();
}


const uint8_t* HeifPixelImage::get_plane(enum heif_channel channel, int* out_stride) const
{
  auto iter = m_planes.find(channel);
  assert(iter != m_planes.end());

  if (out_stride) {
    *out_stride = iter->second.width;
  }

  return iter->second.mem.data();
}

}
