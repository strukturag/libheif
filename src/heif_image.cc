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
#include "heif_context.h"

#include <assert.h>


using namespace heif;


HeifPixelImage::HeifPixelImage(std::shared_ptr<HeifContext> context) : m_context(context)
{
}

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
  assert(bit_depth >= 1);

  ImagePlane plane;
  plane.width = width;
  plane.height = height;
  plane.bit_depth = bit_depth;

  int bytes_per_pixel = (bit_depth+7)/8;
  plane.stride = width * bytes_per_pixel;

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


int HeifPixelImage::get_bits_per_pixel(enum heif_channel channel) const
{
  auto iter = m_planes.find(channel);
  assert(iter != m_planes.end());

  return iter->second.bit_depth;
}


uint8_t* HeifPixelImage::get_plane(enum heif_channel channel, int* out_stride)
{
  auto iter = m_planes.find(channel);
  assert(iter != m_planes.end());

  if (out_stride) {
    *out_stride = iter->second.stride;
  }

  return iter->second.mem.data();
}


const uint8_t* HeifPixelImage::get_plane(enum heif_channel channel, int* out_stride) const
{
  auto iter = m_planes.find(channel);
  assert(iter != m_planes.end());

  if (out_stride) {
    *out_stride = iter->second.stride;
  }

  return iter->second.mem.data();
}


std::shared_ptr<HeifPixelImage> HeifPixelImage::convert_colorspace(heif_colorspace target_colorspace,
                                                                   heif_chroma target_chroma) const
{
  std::shared_ptr<HeifPixelImage> out_img;

  if (target_colorspace != get_colorspace()) {

    // YCbCr -> RGB

    if (get_colorspace() == heif_colorspace_YCbCr &&
        target_colorspace == heif_colorspace_RGB) {

      // 4:2:0 input -> 4:4:4 planes

      if (get_chroma_format() == heif_chroma_420 &&
          target_chroma == heif_chroma_444) {
        out_img = convert_YCbCr420_to_RGB();
      }

      // 4:2:0 input -> RGB 24bit

      if (get_chroma_format() == heif_chroma_420 &&
          target_chroma == heif_chroma_interleaved_24bit) {
        out_img = convert_YCbCr420_to_RGB24();
      }
    }
  }

  if (!out_img) {
    // TODO: unsupported conversion
  }

  return out_img;
}


static inline uint8_t clip(int x)
{
  if (x<0) return 0;
  if (x>255) return 255;
  return static_cast<uint8_t>(x);
}


std::shared_ptr<HeifPixelImage> HeifPixelImage::convert_YCbCr420_to_RGB() const
{
  if (get_bits_per_pixel(heif_channel_Y) != 8 ||
      get_bits_per_pixel(heif_channel_Cb) != 8 ||
      get_bits_per_pixel(heif_channel_Cr) != 8) {
    return nullptr;
  }

  auto outimg = std::make_shared<HeifPixelImage>(m_context);

  int bpp = 8; // TODO: how do we specify the output BPPs ?

  outimg->create(m_width, m_height, heif_colorspace_RGB, heif_chroma_444);

  outimg->add_plane(heif_channel_R, m_width, m_height, bpp);
  outimg->add_plane(heif_channel_G, m_width, m_height, bpp);
  outimg->add_plane(heif_channel_B, m_width, m_height, bpp);

  const uint8_t *in_y,*in_cb,*in_cr;
  int in_y_stride, in_cb_stride, in_cr_stride;

  uint8_t *out_r,*out_g,*out_b;
  int out_r_stride, out_g_stride, out_b_stride;

  in_y  = get_plane(heif_channel_Y,  &in_y_stride);
  in_cb = get_plane(heif_channel_Cb, &in_cb_stride);
  in_cr = get_plane(heif_channel_Cr, &in_cr_stride);
  out_r = outimg->get_plane(heif_channel_R, &out_r_stride);
  out_g = outimg->get_plane(heif_channel_G, &out_g_stride);
  out_b = outimg->get_plane(heif_channel_B, &out_b_stride);

  int x,y;
  for (y=0;y<m_height;y++) {
    for (x=0;x<m_width;x++) {
      int yv = in_y [y  *in_y_stride  + x] - 16;
      int uv = in_cb[y/2*in_cb_stride + x/2] - 128;
      int vv = in_cr[y/2*in_cr_stride + x/2] - 128;

      float y_val = 1.164 * yv;
      out_r[y*out_r_stride + x] = clip(y_val + 1.596 * vv);
      out_g[y*out_g_stride + x] = clip(y_val - 0.813 * vv - 0.391 * uv);
      out_b[y*out_b_stride + x] = clip(y_val + 2.018 * uv);
    }
  }

  return outimg;
}



std::shared_ptr<HeifPixelImage> HeifPixelImage::convert_YCbCr420_to_RGB24() const
{
  if (get_bits_per_pixel(heif_channel_Y) != 8 ||
      get_bits_per_pixel(heif_channel_Cb) != 8 ||
      get_bits_per_pixel(heif_channel_Cr) != 8) {
    return nullptr;
  }

  auto outimg = std::make_shared<HeifPixelImage>(m_context);

  outimg->create(m_width, m_height, heif_colorspace_RGB, heif_chroma_interleaved_24bit);

  outimg->add_plane(heif_channel_interleaved, m_width, m_height, 24);

  const uint8_t *in_y,*in_cb,*in_cr;
  int in_y_stride, in_cb_stride, in_cr_stride;

  uint8_t *out_p;
  int out_p_stride;

  in_y  = get_plane(heif_channel_Y,  &in_y_stride);
  in_cb = get_plane(heif_channel_Cb, &in_cb_stride);
  in_cr = get_plane(heif_channel_Cr, &in_cr_stride);
  out_p = outimg->get_plane(heif_channel_interleaved, &out_p_stride);

  int x,y;
  for (y=0;y<m_height;y++) {
    for (x=0;x<m_width;x++) {
      int yv = in_y [y  *in_y_stride  + x] - 16;
      int uv = in_cb[y/2*in_cb_stride + x/2] - 128;
      int vv = in_cr[y/2*in_cr_stride + x/2] - 128;

      float y_val = 1.164 * yv;
      out_p[y*out_p_stride + 3*x + 0] = clip(y_val + 1.596 * vv);
      out_p[y*out_p_stride + 3*x + 1] = clip(y_val - 0.813 * vv - 0.391 * uv);
      out_p[y*out_p_stride + 3*x + 2] = clip(y_val + 2.018 * uv);
    }
  }

  return outimg;
}


Error HeifPixelImage::rotate(int angle_degrees,
                             std::shared_ptr<HeifPixelImage>& out_img)
{
  // --- create output image (or simply reuse existing image)

  if (angle_degrees==0) {
    out_img = shared_from_this();
    return Error::Ok;
  }

  int out_width = m_width;
  int out_height = m_height;

  if (angle_degrees==90 || angle_degrees==270) {
    std::swap(out_width, out_height);
  }

  out_img = std::make_shared<HeifPixelImage>(m_context);
  out_img->create(out_width, out_height, m_colorspace, m_chroma);


  // --- rotate all channels

  for (const auto& plane_pair : m_planes) {
    heif_channel channel = plane_pair.first;
    const ImagePlane& plane = plane_pair.second;

    if (plane.bit_depth != 8) {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unspecified,
                   "Can currently only rotate images with 8 bits per pixel");
    }


    int out_plane_width = plane.width;
    int out_plane_height = plane.height;

    if (angle_degrees==90 || angle_degrees==270) {
      std::swap(out_plane_width, out_plane_height);
    }

    out_img->add_plane(channel, out_plane_width, out_plane_height, plane.bit_depth);


    int w = plane.width;
    int h = plane.height;

    int in_stride = plane.stride;
    const uint8_t* in_data = plane.mem.data();

    int out_stride;
    uint8_t* out_data = out_img->get_plane(channel, &out_stride);

    if (angle_degrees==90) {
      for (int x=0;x<w;x++)
        for (int y=0;y<h;y++) {
          out_data[x*out_stride + y] = in_data[(h-1-y)*in_stride + (w-1-x)];
        }
    }
    else if (angle_degrees==180) {
      for (int y=0;y<h;y++)
        for (int x=0;x<w;x++) {
          out_data[y*out_stride + x] = in_data[(h-1-y)*in_stride + (w-1-x)];
        }
    }
    else if (angle_degrees==270) {
      for (int x=0;x<w;x++)
        for (int y=0;y<h;y++) {
          out_data[x*out_stride + y] = in_data[y*in_stride + x];
        }
    }
  }

  return Error::Ok;
}
