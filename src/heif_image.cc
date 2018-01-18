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
#include <string.h>

#include <utility>

using namespace heif;


HeifPixelImage::HeifPixelImage()
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


bool HeifPixelImage::has_channel(heif_channel channel) const
{
  return (m_planes.find(channel) != m_planes.end());
}


int HeifPixelImage::get_width(enum heif_channel channel) const
{
  auto iter = m_planes.find(channel);
  if (iter == m_planes.end()) {
    return -1;
  }

  return iter->second.width;
}


int HeifPixelImage::get_height(enum heif_channel channel) const
{
  auto iter = m_planes.find(channel);
  if (iter == m_planes.end()) {
    return -1;
  }

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
  if (iter == m_planes.end()) {
    return -1;
  }

  return iter->second.bit_depth;
}


uint8_t* HeifPixelImage::get_plane(enum heif_channel channel, int* out_stride)
{
  auto iter = m_planes.find(channel);
  if (iter == m_planes.end()) {
    return nullptr;
  }

  if (out_stride) {
    *out_stride = iter->second.stride;
  }

  return iter->second.mem.data();
}


const uint8_t* HeifPixelImage::get_plane(enum heif_channel channel, int* out_stride) const
{
  auto iter = m_planes.find(channel);
  if (iter == m_planes.end()) {
    return nullptr;
  }

  if (out_stride) {
    *out_stride = iter->second.stride;
  }

  return iter->second.mem.data();
}


void HeifPixelImage::transfer_plane_from_image_as(std::shared_ptr<HeifPixelImage> source,
                                                  heif_channel src_channel,
                                                  heif_channel dst_channel)
{
  // TODO: check that dst_channel does not exist yet

  ImagePlane plane = source->m_planes[src_channel];
  source->m_planes.erase(src_channel);

  m_planes.insert( std::make_pair(dst_channel, plane) );
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

      // 4:2:0 input -> RGBA 32bit

      if (get_chroma_format() == heif_chroma_420 &&
          target_chroma == heif_chroma_interleaved_32bit) {
        out_img = convert_YCbCr420_to_RGB32();
      }
    }
  }


  if (target_colorspace == get_colorspace() &&
      target_colorspace == heif_colorspace_RGB) {
    if (get_chroma_format() == heif_chroma_444 &&
        target_chroma == heif_chroma_interleaved_24bit) {
      out_img = convert_RGB_to_RGB24();
    }
  }


  if (!out_img) {
    // TODO: unsupported conversion
  }

  return out_img;
}


static inline uint8_t clip(float fx)
{
  int x = static_cast<int>(fx);
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

  auto outimg = std::make_shared<HeifPixelImage>();

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
      float yv = static_cast<float>(in_y [y  *in_y_stride  + x] - 16);
      float uv = static_cast<float>(in_cb[y/2*in_cb_stride + x/2] - 128);
      float vv = static_cast<float>(in_cr[y/2*in_cr_stride + x/2] - 128);

      float y_val = 1.164f * yv;
      out_r[y*out_r_stride + x] = clip(y_val + 1.596f * vv);
      out_g[y*out_g_stride + x] = clip(y_val - 0.813f * vv - 0.391f * uv);
      out_b[y*out_b_stride + x] = clip(y_val + 2.018f * uv);
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

  auto outimg = std::make_shared<HeifPixelImage>();

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
      float yv = static_cast<float>(in_y [y  *in_y_stride  + x] - 16);
      float uv = static_cast<float>(in_cb[y/2*in_cb_stride + x/2] - 128);
      float vv = static_cast<float>(in_cr[y/2*in_cr_stride + x/2] - 128);

      float y_val = 1.164f * yv;
      out_p[y*out_p_stride + 3*x + 0] = clip(y_val + 1.596f * vv);
      out_p[y*out_p_stride + 3*x + 1] = clip(y_val - 0.813f * vv - 0.391f * uv);
      out_p[y*out_p_stride + 3*x + 2] = clip(y_val + 2.018f * uv);
    }
  }

  return outimg;
}


std::shared_ptr<HeifPixelImage> HeifPixelImage::convert_YCbCr420_to_RGB32() const
{
  if (get_bits_per_pixel(heif_channel_Y) != 8 ||
      get_bits_per_pixel(heif_channel_Cb) != 8 ||
      get_bits_per_pixel(heif_channel_Cr) != 8) {
    return nullptr;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(m_width, m_height, heif_colorspace_RGB, heif_chroma_interleaved_32bit);

  outimg->add_plane(heif_channel_interleaved, m_width, m_height, 32);

  const bool with_alpha = has_channel(heif_channel_Alpha);

  const uint8_t *in_y,*in_cb,*in_cr,*in_a = nullptr;
  int in_y_stride, in_cb_stride, in_cr_stride, in_a_stride;

  uint8_t *out_p;
  int out_p_stride;

  in_y  = get_plane(heif_channel_Y,  &in_y_stride);
  in_cb = get_plane(heif_channel_Cb, &in_cb_stride);
  in_cr = get_plane(heif_channel_Cr, &in_cr_stride);
  if (with_alpha) {
    in_a = get_plane(heif_channel_Alpha, &in_a_stride);
  }

  out_p = outimg->get_plane(heif_channel_interleaved, &out_p_stride);

  int x,y;
  for (y=0;y<m_height;y++) {
    for (x=0;x<m_width;x++) {
      float yv = static_cast<float>(in_y [y  *in_y_stride  + x] - 16);
      float uv = static_cast<float>(in_cb[y/2*in_cb_stride + x/2] - 128);
      float vv = static_cast<float>(in_cr[y/2*in_cr_stride + x/2] - 128);

      float y_val = 1.164f * yv;
      out_p[y*out_p_stride + 4*x + 0] = clip(y_val + 1.596f * vv);
      out_p[y*out_p_stride + 4*x + 1] = clip(y_val - 0.813f * vv - 0.391f * uv);
      out_p[y*out_p_stride + 4*x + 2] = clip(y_val + 2.018f * uv);

      if (with_alpha) {
        out_p[y*out_p_stride + 4*x + 3] = in_a[y*in_a_stride + x];
      }
      else {
        out_p[y*out_p_stride + 4*x + 3] = 0xFF;
      }
    }
  }

  return outimg;
}


std::shared_ptr<HeifPixelImage> HeifPixelImage::convert_RGB_to_RGB24() const
{
  if (get_bits_per_pixel(heif_channel_R) != 8 ||
      get_bits_per_pixel(heif_channel_G) != 8 ||
      get_bits_per_pixel(heif_channel_B) != 8) {
    return nullptr;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(m_width, m_height, heif_colorspace_RGB, heif_chroma_interleaved_24bit);

  outimg->add_plane(heif_channel_interleaved, m_width, m_height, 24);

  const uint8_t *in_r,*in_g,*in_b;
  int in_r_stride, in_g_stride, in_b_stride;

  uint8_t *out_p;
  int out_p_stride;

  in_r = get_plane(heif_channel_R, &in_r_stride);
  in_g = get_plane(heif_channel_G, &in_g_stride);
  in_b = get_plane(heif_channel_B, &in_b_stride);
  out_p = outimg->get_plane(heif_channel_interleaved, &out_p_stride);

  int x,y;
  for (y=0;y<m_height;y++) {
    for (x=0;x<m_width;x++) {
      out_p[y*out_p_stride + 3*x + 0] = in_r[x + y*in_r_stride];
      out_p[y*out_p_stride + 3*x + 1] = in_g[x + y*in_r_stride];
      out_p[y*out_p_stride + 3*x + 2] = in_b[x + y*in_r_stride];
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

  out_img = std::make_shared<HeifPixelImage>();
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


Error HeifPixelImage::mirror_inplace(bool horizontal)
{
  for (auto& plane_pair : m_planes) {
    ImagePlane& plane = plane_pair.second;

    if (plane.bit_depth != 8) {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unspecified,
                   "Can currently only rotate images with 8 bits per pixel");
    }


    int w = plane.width;
    int h = plane.height;

    int stride = plane.stride;
    uint8_t* data = plane.mem.data();

    if (horizontal) {
      for (int y=0;y<h;y++) {
        for (int x=0;x<w/2;x++)
          std::swap(data[y*stride + x], data[y*stride + w-1-x]);
        }
    }
    else {
      for (int y=0;y<h/2;y++) {
        for (int x=0;x<w;x++)
          std::swap(data[y*stride + x], data[(h-1-y)*stride + x]);
        }
    }
  }

  return Error::Ok;
}


Error HeifPixelImage::crop(int left,int right,int top,int bottom,
                           std::shared_ptr<HeifPixelImage>& out_img) const
{
  out_img = std::make_shared<HeifPixelImage>();
  out_img->create(right-left+1, bottom-top+1, m_colorspace, m_chroma);


  // --- crop all channels

  for (const auto& plane_pair : m_planes) {
    heif_channel channel = plane_pair.first;
    const ImagePlane& plane = plane_pair.second;

    if (plane.bit_depth != 8) {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unspecified,
                   "Can currently only rotate images with 8 bits per pixel");
    }


    int w = plane.width;
    int h = plane.height;

    int plane_left = left * w/m_width;
    int plane_right = right * w/m_width;
    int plane_top = top * h/m_height;
    int plane_bottom = bottom * h/m_height;

    out_img->add_plane(channel,
                       plane_right - plane_left + 1,
                       plane_bottom - plane_top + 1,
                       plane.bit_depth);

    int in_stride = plane.stride;
    const uint8_t* in_data = plane.mem.data();

    int out_stride;
    uint8_t* out_data = out_img->get_plane(channel, &out_stride);

    for (int y=plane_top;y<=plane_bottom;y++) {
      memcpy( &out_data[(y-plane_top)*out_stride],
              &in_data[y*in_stride + plane_left],
              plane_right - plane_left + 1 );
    }
  }

  return Error::Ok;
}


Error HeifPixelImage::fill_RGB_16bit(uint16_t r, uint16_t g, uint16_t b, uint16_t a)
{
  for (const auto& channel : { heif_channel_R, heif_channel_G, heif_channel_B, heif_channel_Alpha } ) {

    const auto plane_iter = m_planes.find(channel);
    if (plane_iter == m_planes.end()) {

      // alpha channel is optional, R,G,B is required
      if (channel == heif_channel_Alpha) {
        continue;
      }

      return Error(heif_error_Usage_error,
                   heif_suberror_Nonexisting_image_channel_referenced);

    }

    ImagePlane& plane = plane_iter->second;

    if (plane.bit_depth != 8) {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unspecified,
                   "Can currently only fill images with 8 bits per pixel");
    }

    int h = plane.height;

    int stride = plane.stride;
    uint8_t* data = plane.mem.data();

    uint16_t val16;
    switch (channel) {
    case heif_channel_R: val16=r; break;
    case heif_channel_G: val16=g; break;
    case heif_channel_B: val16=b; break;
    case heif_channel_Alpha: val16=a; break;
    default:
      // Should already be detected by the check above ("m_planes.find").
      assert(false);
    }

    uint8_t val8 = static_cast<uint8_t>(val16>>8);

    memset(data, val8, stride*h);
  }

  return Error::Ok;
}


Error HeifPixelImage::overlay(std::shared_ptr<HeifPixelImage>& overlay, int dx,int dy)
{
  std::set<enum heif_channel> channels = overlay->get_channel_set();

  for (heif_channel channel : channels) {
    int in_stride;
    const uint8_t* in_p;

    int out_stride;
    uint8_t* out_p;

    in_p = overlay->get_plane(channel, &in_stride);
    out_p = get_plane(channel, &out_stride);

    int in_w = overlay->get_width(channel);
    int in_h = overlay->get_height(channel);
    assert(in_w >= 0);
    assert(in_h >= 0);

    int out_w = get_width(channel);
    int out_h = get_height(channel);
    assert(out_w >= 0);
    assert(out_h >= 0);

    // overlay image extends past the right border -> cut width for copy
    if (dx+in_w > out_w) {
      in_w = out_w - dx;
    }

    // overlay image extends past the bottom border -> cut height for copy
    if (dy+in_h > out_h) {
      in_h = out_h - dy;
    }

    // overlay image completely outside right or bottom border -> do not copy
    if (in_w < 0 || in_h < 0) {
      return Error(heif_error_Invalid_input,
                   heif_suberror_Overlay_image_outside_of_canvas,
                   "Overlay image outside of right or bottom canvas border");
    }


    // calculate top-left point where to start copying in source and destination
    int in_x0 = 0;
    int in_y0 = 0;
    int out_x0 = dx;
    int out_y0 = dy;

    // overlay image started outside of left border
    // -> move start into the image and start at left output column
    if (dx<0) {
      in_x0 = -dx;
      out_x0=0;
    }

    // overlay image started outside of top border
    // -> move start into the image and start at top output row
    if (dy<0) {
      in_y0 = -dy;
      out_y0=0;
    }

    // if overlay image is completely outside at left border, do not copy anything.
    if (in_w <= in_x0 ||
        in_h <= in_y0) {
      return Error(heif_error_Invalid_input,
                   heif_suberror_Overlay_image_outside_of_canvas,
                   "Overlay image outside of left or top canvas border");
    }

    for (int y=in_y0; y<in_h; y++) {
      memcpy(out_p + out_x0 + (out_y0 + y-in_y0)*out_stride,
             in_p + in_x0 + y*in_stride,
             in_w-in_x0);
    }
  }

  return Error::Ok;
}


Error HeifPixelImage::scale_nearest_neighbor(std::shared_ptr<HeifPixelImage>& out_img,
                                             int width,int height) const
{
  out_img = std::make_shared<HeifPixelImage>();
  out_img->create(width, height, m_colorspace, m_chroma);


  // --- scale all channels

  for (const auto& plane_pair : m_planes) {
    heif_channel channel = plane_pair.first;
    const ImagePlane& plane = plane_pair.second;

    const int bpp = (plane.bit_depth + 7)/8;

    int in_w = plane.width;
    int in_h = plane.height;

    int out_w = in_w * width/m_width;
    int out_h = in_h * height/m_height;

    out_img->add_plane(channel,
                       out_w,
                       out_h,
                       plane.bit_depth);

    int in_stride = plane.stride;
    const uint8_t* in_data = plane.mem.data();

    int out_stride;
    uint8_t* out_data = out_img->get_plane(channel, &out_stride);


    for (int y=0;y<out_h;y++) {
      int iy = y * m_height / height;

      if (bpp==1) {
        for (int x=0;x<out_w;x++) {
          int ix = x * m_width / width;

          out_data[y*out_stride + x] = in_data[iy*in_stride + ix];
        }
      }
      else {
        for (int x=0;x<out_w;x++) {
          int ix = x * m_width / width;

          for (int b=0;b<bpp;b++) {
            out_data[y*out_stride + bpp*x + b] = in_data[iy*in_stride + bpp*ix + b];
          }
        }
      }
    }
  }

  return Error::Ok;
}
