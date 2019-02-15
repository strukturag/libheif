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


int heif::chroma_h_subsampling(heif_chroma c)
{
  switch (c) {
  case heif_chroma_monochrome:
  case heif_chroma_444:
    return 1;

  case heif_chroma_420:
  case heif_chroma_422:
    return 2;

  case heif_chroma_interleaved_RGB:
  case heif_chroma_interleaved_RGBA:
  default:
    assert(false);
    return 0;
  }
}

int heif::chroma_v_subsampling(heif_chroma c)
{
  switch (c) {
  case heif_chroma_monochrome:
  case heif_chroma_444:
  case heif_chroma_422:
    return 1;

  case heif_chroma_420:
    return 2;

  case heif_chroma_interleaved_RGB:
  case heif_chroma_interleaved_RGBA:
  default:
    assert(false);
    return 0;
  }
}


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


bool HeifPixelImage::has_alpha() const
{
  return has_channel(heif_channel_Alpha) ||
    get_chroma_format() == heif_chroma_interleaved_RGBA ||
    get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_BE ||
    get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE;
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


void HeifPixelImage::copy_new_plane_from(const std::shared_ptr<HeifPixelImage> src_image,
                                         heif_channel src_channel,
                                         heif_channel dst_channel)
{
  int width  = src_image->get_width(src_channel);
  int height = src_image->get_height(src_channel);

  add_plane(dst_channel, width, height, src_image->get_bits_per_pixel(src_channel));

  uint8_t* dst;
  int dst_stride=0;

  const uint8_t* src;
  int src_stride=0;

  src = src_image->get_plane(src_channel, &src_stride);
  dst = get_plane(dst_channel, &dst_stride);

  int bpl = width * ((src_image->get_bits_per_pixel(src_channel)+7)/8);

  for (int y=0;y<height;y++) {
    memcpy(dst+y*dst_stride, src+y*src_stride, bpl);
  }
}

void HeifPixelImage::fill_new_plane(heif_channel dst_channel, uint8_t value, int width, int height)
{
  add_plane(dst_channel, width, height, 8);

  uint8_t* dst;
  int dst_stride=0;
  dst = get_plane(dst_channel, &dst_stride);

  for (int y=0;y<height;y++) {
    memset(dst+y*dst_stride, value, width);
  }
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

    // --- YCbCr -> RGB

    if (get_colorspace() == heif_colorspace_YCbCr &&
        target_colorspace == heif_colorspace_RGB) {

      // 4:2:0 input -> 4:4:4 planes

      if (get_chroma_format() == heif_chroma_420 &&
          target_chroma == heif_chroma_444) {
        out_img = convert_YCbCr420_to_RGB();
      }

      // 4:2:0 input -> RGB 24bit

      if (get_chroma_format() == heif_chroma_420 &&
          target_chroma == heif_chroma_interleaved_RGB) {
        out_img = convert_YCbCr420_to_RGB24();
      }

      // 4:2:0 input -> RGBA 32bit

      if (get_chroma_format() == heif_chroma_420 &&
          target_chroma == heif_chroma_interleaved_RGBA) {
        out_img = convert_YCbCr420_to_RGB32();
      }

      // 4:2:0 input -> HDR RGB, big endian

      if (get_chroma_format() == heif_chroma_420 &&
          (target_chroma == heif_chroma_interleaved_RRGGBB_BE ||
           target_chroma == heif_chroma_interleaved_RRGGBBAA_BE)) {
        std::shared_ptr<HeifPixelImage> tmpImg = convert_YCbCr420_to_RGB_HDR();
        out_img = tmpImg->convert_RGB_to_RRGGBBaa_BE();
      }


      // greyscale -> RGB 24bit

      if (get_chroma_format() == heif_chroma_monochrome &&
          target_chroma == heif_chroma_interleaved_RGB) {
        out_img = convert_mono_to_RGB(3);
      }

      // greyscale -> RGB 32bit

      if (get_chroma_format() == heif_chroma_monochrome &&
          target_chroma == heif_chroma_interleaved_RGBA) {
        out_img = convert_mono_to_RGB(4);
      }
    }
    else if (get_colorspace() == heif_colorspace_RGB &&
	     target_colorspace == heif_colorspace_YCbCr) {

      // --- RGB -> YCbCr

      if (get_chroma_format() == heif_chroma_444) {
	if (get_bits_per_pixel(heif_channel_G) != get_bits_per_pixel(heif_channel_R) ||
	    get_bits_per_pixel(heif_channel_B) != get_bits_per_pixel(heif_channel_R)) {
	  assert(false); // TODO: different bit depths for each channel
	}
      
	if (has_alpha()) {
	  if (get_bits_per_pixel(heif_channel_Alpha) != get_bits_per_pixel(heif_channel_R)) {
	    assert(false); // TODO: different bit depths for each channel
	  }
	}
      }

      if ((get_chroma_format() == heif_chroma_interleaved_RGB ||
	   get_chroma_format() == heif_chroma_interleaved_RGBA) &&
	  target_chroma == heif_chroma_420) {
	out_img = convert_RGB24_32_to_YCbCr420();
      }

      if (get_chroma_format() == heif_chroma_444) {
	std::shared_ptr<HeifPixelImage> img_rgb = convert_RGB_to_RGB24_32();
	out_img = img_rgb->convert_RGB24_32_to_YCbCr420();
      }
    }

    if (get_chroma_format() == heif_chroma_444 &&
	get_bits_per_pixel(heif_channel_R) > 8 &&
	target_chroma == heif_chroma_420) {
      out_img = convert_RGB_to_YCbCr420_HDR();
    }
  }
  else { // same colorspace
    if (target_colorspace == heif_colorspace_RGB) {
      if (get_chroma_format() == heif_chroma_444 &&
          target_chroma == heif_chroma_interleaved_RGB) {
        out_img = convert_RGB_to_RGB24_32();
      }

      if (get_chroma_format() == heif_chroma_444 &&
          (target_chroma == heif_chroma_interleaved_RRGGBB_BE ||
           target_chroma == heif_chroma_interleaved_RRGGBBAA_BE)) {
        out_img = convert_RGB_to_RRGGBBaa_BE();
      }
    }


    if (target_colorspace == heif_colorspace_YCbCr) {
      if (get_chroma_format() == heif_chroma_monochrome &&
          target_chroma == heif_chroma_420) {
        out_img = convert_mono_to_YCbCr420();
      }
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


static inline uint16_t clip(float fx,int32_t maxi)
{
  int x = static_cast<int>(fx);
  if (x<0) return 0;
  if (x>maxi) return (uint16_t)maxi;
  return static_cast<uint16_t>(x);
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

  bool has_alpha = has_channel(heif_channel_Alpha);
  if (has_alpha) {
    outimg->add_plane(heif_channel_Alpha, m_width, m_height, bpp);
  }

  const uint8_t *in_y,*in_cb,*in_cr,*in_a;
  int in_y_stride=0, in_cb_stride=0, in_cr_stride=0, in_a_stride=0;

  uint8_t *out_r,*out_g,*out_b,*out_a;
  int out_r_stride=0, out_g_stride=0, out_b_stride=0, out_a_stride=0;

  in_y  = get_plane(heif_channel_Y,  &in_y_stride);
  in_cb = get_plane(heif_channel_Cb, &in_cb_stride);
  in_cr = get_plane(heif_channel_Cr, &in_cr_stride);
  out_r = outimg->get_plane(heif_channel_R, &out_r_stride);
  out_g = outimg->get_plane(heif_channel_G, &out_g_stride);
  out_b = outimg->get_plane(heif_channel_B, &out_b_stride);

  if (has_alpha) {
    in_a = get_plane(heif_channel_Alpha, &in_a_stride);
    out_a = outimg->get_plane(heif_channel_Alpha, &out_a_stride);
  }
  else {
    in_a = nullptr;
    out_a = nullptr;
  }

  int x,y;
  for (y=0;y<m_height;y++) {
    for (x=0;x<m_width;x++) {
      float yv = static_cast<float>(in_y [y  *in_y_stride  + x] );
      float cb = static_cast<float>(in_cb[y/2*in_cb_stride + x/2]-128);
      float cr = static_cast<float>(in_cr[y/2*in_cr_stride + x/2]-128);

      out_r[y*out_r_stride + x] = clip(yv + 1.402f*cr);
      out_g[y*out_g_stride + x] = clip(yv - 0.344136f*cb - 0.714136f*cr);
      out_b[y*out_b_stride + x] = clip(yv + 1.772f*cb);
    }

    if (has_alpha) {
      memcpy(&out_a[y*out_a_stride], &in_a[y*in_a_stride], m_width);
    }
  }

  return outimg;
}



std::shared_ptr<HeifPixelImage> HeifPixelImage::convert_YCbCr420_to_RGB_HDR() const
{
  if (get_bits_per_pixel(heif_channel_Y) == 8 ||
      get_bits_per_pixel(heif_channel_Cb) == 8 ||
      get_bits_per_pixel(heif_channel_Cr) == 8) {
    return nullptr;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  // TODO: how do we specify the output BPPs ?
  int bpp = get_bits_per_pixel(heif_channel_Y);

  outimg->create(m_width, m_height, heif_colorspace_RGB, heif_chroma_444);

  outimg->add_plane(heif_channel_R, m_width, m_height, bpp);
  outimg->add_plane(heif_channel_G, m_width, m_height, bpp);
  outimg->add_plane(heif_channel_B, m_width, m_height, bpp);

  bool has_alpha = has_channel(heif_channel_Alpha);
  if (has_alpha) {
    outimg->add_plane(heif_channel_Alpha, m_width, m_height, bpp);
  }

  const uint16_t *in_y,*in_cb,*in_cr,*in_a;
  int in_y_stride=0, in_cb_stride=0, in_cr_stride=0, in_a_stride=0;

  uint16_t *out_r,*out_g,*out_b,*out_a;
  int out_r_stride=0, out_g_stride=0, out_b_stride=0, out_a_stride=0;

  in_y  = (const uint16_t*)get_plane(heif_channel_Y,  &in_y_stride);
  in_cb = (const uint16_t*)get_plane(heif_channel_Cb, &in_cb_stride);
  in_cr = (const uint16_t*)get_plane(heif_channel_Cr, &in_cr_stride);
  out_r = (uint16_t*)outimg->get_plane(heif_channel_R, &out_r_stride);
  out_g = (uint16_t*)outimg->get_plane(heif_channel_G, &out_g_stride);
  out_b = (uint16_t*)outimg->get_plane(heif_channel_B, &out_b_stride);

  if (has_alpha) {
    in_a = (const uint16_t*)get_plane(heif_channel_Alpha, &in_a_stride);
    out_a = (uint16_t*)outimg->get_plane(heif_channel_Alpha, &out_a_stride);
  }
  else {
    in_a = nullptr;
    out_a = nullptr;
  }

  in_y_stride /= 2;
  in_cb_stride /= 2;
  in_cr_stride /= 2;
  in_a_stride /= 2;
  out_r_stride /= 2;
  out_g_stride /= 2;
  out_b_stride /= 2;
  out_a_stride /= 2;

  uint16_t halfRange = (uint16_t)(1<<(bpp-1));
  int32_t fullRange = (1<<bpp)-1;

  int bdShift = 16-bpp;

  int x,y;
  for (y=0;y<m_height;y++) {
    for (x=0;x<m_width;x++) {
      float yv = static_cast<float>(in_y [y  *in_y_stride  + x] );
      float cb = static_cast<float>(in_cb[y/2*in_cb_stride + x/2]-halfRange);
      float cr = static_cast<float>(in_cr[y/2*in_cr_stride + x/2]-halfRange);

      out_r[y*out_r_stride + x] = (uint16_t)(clip(yv + 1.402f*cr, fullRange) << bdShift);
      out_g[y*out_g_stride + x] = (uint16_t)(clip(yv - 0.344136f*cb - 0.714136f*cr, fullRange) << bdShift);
      out_b[y*out_b_stride + x] = (uint16_t)(clip(yv + 1.772f*cb, fullRange) << bdShift);
    }

    if (has_alpha) {
      for (int x=0;x<m_width;x++) {
        out_a[y*out_a_stride+x] = (uint16_t)(in_a[y*in_a_stride+x] << bdShift);
      }
      //memcpy(&out_a[y*out_a_stride], &in_a[y*in_a_stride], m_width *2);
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
  int in_y_stride=0, in_cb_stride=0, in_cr_stride=0;

  uint8_t *out_p;
  int out_p_stride=0;

  in_y  = get_plane(heif_channel_Y,  &in_y_stride);
  in_cb = get_plane(heif_channel_Cb, &in_cb_stride);
  in_cr = get_plane(heif_channel_Cr, &in_cr_stride);
  out_p = outimg->get_plane(heif_channel_interleaved, &out_p_stride);

  int x,y;
  for (y=0;y<m_height;y++) {
    for (x=0;x<m_width;x++) {
      float yv = static_cast<float>(in_y [y  *in_y_stride  + x] );
      float cb = static_cast<float>(in_cb[y/2*in_cb_stride + x/2]-128);
      float cr = static_cast<float>(in_cr[y/2*in_cr_stride + x/2]-128);

      out_p[y*out_p_stride + 3*x + 0] = clip(yv + 1.402f*cr);
      out_p[y*out_p_stride + 3*x + 1] = clip(yv - 0.344136f*cb - 0.714136f*cr);
      out_p[y*out_p_stride + 3*x + 2] = clip(yv + 1.772f*cb);
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
  int in_y_stride=0, in_cb_stride=0, in_cr_stride=0, in_a_stride=0;

  uint8_t *out_p;
  int out_p_stride=0;

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
      float yv = static_cast<float>(in_y [y  *in_y_stride  + x]);
      float cb = static_cast<float>(in_cb[y/2*in_cb_stride + x/2]-128);
      float cr = static_cast<float>(in_cr[y/2*in_cr_stride + x/2]-128);

      out_p[y*out_p_stride + 4*x + 0] = clip(yv + 1.402f*cr);
      out_p[y*out_p_stride + 4*x + 1] = clip(yv - 0.344136f*cb - 0.714136f*cr);
      out_p[y*out_p_stride + 4*x + 2] = clip(yv + 1.772f*cb);

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


std::shared_ptr<HeifPixelImage> HeifPixelImage::convert_RGB_to_RGB24_32() const
{
  bool has_alpha = has_channel(heif_channel_Alpha);

  if (get_bits_per_pixel(heif_channel_R) != 8 ||
      get_bits_per_pixel(heif_channel_G) != 8 ||
      get_bits_per_pixel(heif_channel_B) != 8) {
    return nullptr;
  }

  if (has_alpha && get_bits_per_pixel(heif_channel_Alpha) != 8) {
    return nullptr;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(m_width, m_height, heif_colorspace_RGB,
                 has_alpha ? heif_chroma_interleaved_32bit : heif_chroma_interleaved_24bit);

  outimg->add_plane(heif_channel_interleaved, m_width, m_height, has_alpha ? 32 : 24);

  const uint8_t *in_r,*in_g,*in_b,*in_a=nullptr;
  int in_r_stride=0, in_g_stride=0, in_b_stride=0, in_a_stride=0;

  uint8_t *out_p;
  int out_p_stride=0;

  in_r = get_plane(heif_channel_R, &in_r_stride);
  in_g = get_plane(heif_channel_G, &in_g_stride);
  in_b = get_plane(heif_channel_B, &in_b_stride);
  out_p = outimg->get_plane(heif_channel_interleaved, &out_p_stride);

  if (has_alpha) {
    in_a = get_plane(heif_channel_Alpha, &in_a_stride);
  }

  int x,y;
  for (y=0;y<m_height;y++) {

    if (has_alpha) {
      for (x=0;x<m_width;x++) {
        out_p[y*out_p_stride + 4*x + 0] = in_r[x + y*in_r_stride];
        out_p[y*out_p_stride + 4*x + 1] = in_g[x + y*in_g_stride];
        out_p[y*out_p_stride + 4*x + 2] = in_b[x + y*in_b_stride];
        out_p[y*out_p_stride + 4*x + 3] = in_a[x + y*in_a_stride];
      }
    }
    else {
      for (x=0;x<m_width;x++) {
        out_p[y*out_p_stride + 3*x + 0] = in_r[x + y*in_r_stride];
        out_p[y*out_p_stride + 3*x + 1] = in_g[x + y*in_g_stride];
        out_p[y*out_p_stride + 3*x + 2] = in_b[x + y*in_b_stride];
      }
    }
  }

  return outimg;
}



std::shared_ptr<HeifPixelImage> HeifPixelImage::convert_RGB_to_RRGGBBaa_BE() const
{
  bool has_alpha = has_channel(heif_channel_Alpha);

  if (get_bits_per_pixel(heif_channel_R) == 8 ||
      get_bits_per_pixel(heif_channel_G) == 8 ||
      get_bits_per_pixel(heif_channel_B) == 8) {
    return nullptr;
  }

  if (has_alpha && get_bits_per_pixel(heif_channel_Alpha) == 8) {
    return nullptr;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(m_width, m_height, heif_colorspace_RGB,
                 has_alpha ? heif_chroma_interleaved_RRGGBBAA_BE : heif_chroma_interleaved_RRGGBB_BE);

  outimg->add_plane(heif_channel_interleaved, m_width, m_height, has_alpha ? 64 : 48);

  const uint16_t *in_r,*in_g,*in_b,*in_a=nullptr;
  int in_r_stride=0, in_g_stride=0, in_b_stride=0, in_a_stride=0;

  uint8_t *out_p;
  int out_p_stride=0;

  in_r = (uint16_t*)get_plane(heif_channel_R, &in_r_stride);
  in_g = (uint16_t*)get_plane(heif_channel_G, &in_g_stride);
  in_b = (uint16_t*)get_plane(heif_channel_B, &in_b_stride);
  out_p = outimg->get_plane(heif_channel_interleaved, &out_p_stride);

  if (has_alpha) {
    in_a = (uint16_t*)get_plane(heif_channel_Alpha, &in_a_stride);
  }

  in_r_stride /= 2;
  in_g_stride /= 2;
  in_b_stride /= 2;
  in_a_stride /= 2;

  int x,y;
  for (y=0;y<m_height;y++) {

    if (has_alpha) {
      for (x=0;x<m_width;x++) {
        uint16_t r = in_r[x + y*in_r_stride];
        uint16_t g = in_g[x + y*in_g_stride];
        uint16_t b = in_b[x + y*in_b_stride];
        uint16_t a = in_a[x + y*in_a_stride];
        out_p[y*out_p_stride + 8*x + 0] = (uint8_t)(r>>8);
        out_p[y*out_p_stride + 8*x + 1] = (uint8_t)(r & 0xFF);
        out_p[y*out_p_stride + 8*x + 2] = (uint8_t)(g>>8);
        out_p[y*out_p_stride + 8*x + 3] = (uint8_t)(g & 0xFF);
        out_p[y*out_p_stride + 8*x + 4] = (uint8_t)(b>>8);
        out_p[y*out_p_stride + 8*x + 5] = (uint8_t)(b & 0xFF);
        out_p[y*out_p_stride + 8*x + 6] = (uint8_t)(a>>8);
        out_p[y*out_p_stride + 8*x + 7] = (uint8_t)(a & 0xFF);
      }
    }
    else {
      for (x=0;x<m_width;x++) {
        uint16_t r = in_r[x + y*in_r_stride];
        uint16_t g = in_g[x + y*in_g_stride];
        uint16_t b = in_b[x + y*in_b_stride];
        out_p[y*out_p_stride + 6*x + 0] = (uint8_t)(r>>8);
        out_p[y*out_p_stride + 6*x + 1] = (uint8_t)(r & 0xFF);
        out_p[y*out_p_stride + 6*x + 2] = (uint8_t)(g>>8);
        out_p[y*out_p_stride + 6*x + 3] = (uint8_t)(g & 0xFF);
        out_p[y*out_p_stride + 6*x + 4] = (uint8_t)(b>>8);
        out_p[y*out_p_stride + 6*x + 5] = (uint8_t)(b & 0xFF);
      }
    }
  }

  return outimg;
}


std::shared_ptr<HeifPixelImage> HeifPixelImage::convert_mono_to_YCbCr420() const
{
  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(m_width, m_height, heif_colorspace_YCbCr, heif_chroma_420);

  int chroma_width  = (m_width+1)/2;
  int chroma_height = (m_height+1)/2;

  outimg->add_plane(heif_channel_Y,  m_width, m_height, 8);
  outimg->add_plane(heif_channel_Cb, chroma_width, chroma_height, 8);
  outimg->add_plane(heif_channel_Cr, chroma_width, chroma_height, 8);
  //outimg->transfer_plane_from_image_as(shared_from_this(), heif_channel_Y, heif_channel_Y);

  uint8_t *out_cb,*out_cr,*out_y;
  int out_cb_stride=0, out_cr_stride=0, out_y_stride=0;

  const uint8_t *in_y;
  int in_y_stride=0;

  in_y  = get_plane(heif_channel_Y,  &in_y_stride);

  out_y  = outimg->get_plane(heif_channel_Y,  &out_y_stride);
  out_cb = outimg->get_plane(heif_channel_Cb, &out_cb_stride);
  out_cr = outimg->get_plane(heif_channel_Cr, &out_cr_stride);

  memset(out_cb, 128, out_cb_stride*chroma_height);
  memset(out_cr, 128, out_cr_stride*chroma_height);

  for (int y=0;y<m_height;y++) {
    memcpy(out_y + y*out_y_stride,
           in_y + y*in_y_stride,
           m_width);
  }

  return outimg;
}


std::shared_ptr<HeifPixelImage> HeifPixelImage::convert_mono_to_RGB(int bpp) const
{
  if (get_bits_per_pixel(heif_channel_Y) != 8) {
    return nullptr;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  if (bpp==3) {
    outimg->create(m_width, m_height, heif_colorspace_RGB, heif_chroma_interleaved_24bit);
  } else {
    outimg->create(m_width, m_height, heif_colorspace_RGB, heif_chroma_interleaved_32bit);
  }

  outimg->add_plane(heif_channel_interleaved, m_width, m_height, bpp*8);

  const uint8_t *in_y;
  int in_y_stride=0;

  uint8_t *out_p;
  int out_p_stride=0;

  in_y = get_plane(heif_channel_Y, &in_y_stride);
  out_p = outimg->get_plane(heif_channel_interleaved, &out_p_stride);

  int x,y;
  for (y=0;y<m_height;y++) {
    if (bpp==3) {
      for (x=0;x<m_width;x++) {
        uint8_t v = in_y[x + y*in_y_stride];
        out_p[y*out_p_stride + 3*x + 0] = v;
        out_p[y*out_p_stride + 3*x + 1] = v;
        out_p[y*out_p_stride + 3*x + 2] = v;
      }
    }
    else {
      // TODO: monochrome with alpha channel

      for (x=0;x<m_width;x++) {
        uint8_t v = in_y[x + y*in_y_stride];
        out_p[y*out_p_stride + 4*x + 0] = v;
        out_p[y*out_p_stride + 4*x + 1] = v;
        out_p[y*out_p_stride + 4*x + 2] = v;
        out_p[y*out_p_stride + 4*x + 3] = 0xFF;
      }
    }
  }

  return outimg;
}


std::shared_ptr<HeifPixelImage> HeifPixelImage::convert_RGB24_32_to_YCbCr420() const
{
  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(m_width, m_height, heif_colorspace_YCbCr, heif_chroma_420);

  int chroma_width  = (m_width+1)/2;
  int chroma_height = (m_height+1)/2;

  const bool has_alpha = (get_chroma_format() == heif_chroma_interleaved_32bit);

  outimg->add_plane(heif_channel_Y,  m_width, m_height, 8);
  outimg->add_plane(heif_channel_Cb, chroma_width, chroma_height, 8);
  outimg->add_plane(heif_channel_Cr, chroma_width, chroma_height, 8);

  if (has_alpha) {
    outimg->add_plane(heif_channel_Alpha, m_width, m_height, 8);
  }

  uint8_t *out_cb,*out_cr,*out_y, *out_a;
  int out_cb_stride=0, out_cr_stride=0, out_y_stride=0, out_a_stride=0;

  const uint8_t *in_p;
  int in_stride=0;

  in_p  = get_plane(heif_channel_interleaved,  &in_stride);

  out_y  = outimg->get_plane(heif_channel_Y,  &out_y_stride);
  out_cb = outimg->get_plane(heif_channel_Cb, &out_cb_stride);
  out_cr = outimg->get_plane(heif_channel_Cr, &out_cr_stride);

  if (has_alpha) {
    out_a = outimg->get_plane(heif_channel_Alpha, &out_a_stride);
  }


  if (!has_alpha) {
    for (int y=0;y<m_height;y++) {
      for (int x=0;x<m_width;x++) {
        uint8_t r = in_p[y*in_stride + x*3 +0];
        uint8_t g = in_p[y*in_stride + x*3 +1];
        uint8_t b = in_p[y*in_stride + x*3 +2];
        out_y[y*out_y_stride + x] = clip(r*0.299f + g*0.587f + b*0.114f);
      }
    }

    for (int y=0;y<m_height;y+=2) {
      for (int x=0;x<m_width;x+=2) {
        uint8_t r = in_p[y*in_stride + x*3 +0];
        uint8_t g = in_p[y*in_stride + x*3 +1];
        uint8_t b = in_p[y*in_stride + x*3 +2];
        out_cb[(y/2)*out_cb_stride + (x/2)] = clip(128 - r*0.168736f - g*0.331264f + b*0.5f);
        out_cr[(y/2)*out_cb_stride + (x/2)] = clip(128 + r*0.5f - g*0.418688f - b*0.081312f);
      }
    }
  }
  else {
    for (int y=0;y<m_height;y++) {
      for (int x=0;x<m_width;x++) {
        uint8_t r = in_p[y*in_stride + x*4 +0];
        uint8_t g = in_p[y*in_stride + x*4 +1];
        uint8_t b = in_p[y*in_stride + x*4 +2];
        uint8_t a = in_p[y*in_stride + x*4 +3];
        out_y[y*out_y_stride + x] = clip(r*0.299f + g*0.587f + b*0.114f);

        // alpha
        out_a[y*out_a_stride + x] = a;
      }
    }

    for (int y=0;y<m_height;y+=2) {
      for (int x=0;x<m_width;x+=2) {
        uint8_t r = in_p[y*in_stride + x*4 +0];
        uint8_t g = in_p[y*in_stride + x*4 +1];
        uint8_t b = in_p[y*in_stride + x*4 +2];
        out_cb[(y/2)*out_cb_stride + (x/2)] = clip(128 - r*0.168736f - g*0.331264f + b*0.5f);
        out_cr[(y/2)*out_cb_stride + (x/2)] = clip(128 + r*0.5f - g*0.418688f - b*0.081312f);
      }
    }
  }

  return outimg;
}


std::shared_ptr<HeifPixelImage> HeifPixelImage::convert_RGB_to_YCbCr420_HDR() const
{
  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(m_width, m_height, heif_colorspace_YCbCr, heif_chroma_420);

  int chroma_width  = (m_width+1)/2;
  int chroma_height = (m_height+1)/2;

  // TODO: make sure that all color channels have the same bit depth
  // TODO: in HEVC, we could save with different luma/chroma bit depths
  int bit_depth = get_bits_per_pixel(heif_channel_R);

  outimg->add_plane(heif_channel_Y,  m_width, m_height, bit_depth);
  outimg->add_plane(heif_channel_Cb, chroma_width, chroma_height, bit_depth);
  outimg->add_plane(heif_channel_Cr, chroma_width, chroma_height, bit_depth);

  if (has_alpha()) {
    outimg->add_plane(heif_channel_Alpha, m_width, m_height, bit_depth);
  }

  uint16_t *out_cb,*out_cr,*out_y, *out_a = nullptr;
  int out_cb_stride=0, out_cr_stride=0, out_y_stride=0, out_a_stride=0;

  const uint16_t *in_r;
  const uint16_t *in_g;
  const uint16_t *in_b;
  const uint16_t *in_a;
  int in_stride_r=0;
  int in_stride_g=0;
  int in_stride_b=0;
  int in_stride_a=0;

  in_r = (const uint16_t*)get_plane(heif_channel_R,  &in_stride_r);
  in_g = (const uint16_t*)get_plane(heif_channel_G,  &in_stride_g);
  in_b = (const uint16_t*)get_plane(heif_channel_B,  &in_stride_b);
  in_a = (const uint16_t*)get_plane(heif_channel_Alpha,  &in_stride_a);

  out_y  = (uint16_t*)outimg->get_plane(heif_channel_Y,  &out_y_stride);
  out_cb = (uint16_t*)outimg->get_plane(heif_channel_Cb, &out_cb_stride);
  out_cr = (uint16_t*)outimg->get_plane(heif_channel_Cr, &out_cr_stride);

  if (has_alpha()) {
    out_a = (uint16_t*)outimg->get_plane(heif_channel_Alpha, &out_a_stride);
  }

  in_stride_r /= 2;
  in_stride_g /= 2;
  in_stride_b /= 2;
  in_stride_a /= 2;

  out_y_stride /= 2;
  out_cb_stride /= 2;
  out_cr_stride /= 2;
  out_a_stride /= 2;

  assert(bit_depth<=16);

  uint16_t halfRange = (uint16_t)(1<<(bit_depth-1));
  int32_t fullRange = (1<<bit_depth)-1;

  if (!has_alpha()) {
    for (int y=0;y<m_height;y++) {
      for (int x=0;x<m_width;x++) {
        uint16_t r = in_r[y*in_stride_r + x];
        uint16_t g = in_g[y*in_stride_g + x];
        uint16_t b = in_b[y*in_stride_b + x];
        out_y[y*out_y_stride + x] = clip(r*0.299f + g*0.587f + b*0.114f, fullRange);
      }
    }

    for (int y=0;y<m_height;y+=2) {
      for (int x=0;x<m_width;x+=2) {
        uint16_t r = in_r[y*in_stride_r + x];
        uint16_t g = in_g[y*in_stride_g + x];
        uint16_t b = in_b[y*in_stride_b + x];
        out_cb[(y/2)*out_cb_stride + (x/2)] = clip(halfRange - r*0.168736f - g*0.331264f + b*0.5f, fullRange);
        out_cr[(y/2)*out_cb_stride + (x/2)] = clip(halfRange + r*0.5f - g*0.418688f - b*0.081312f, fullRange);
      }
    }
  }
  else {
    for (int y=0;y<m_height;y++) {
      for (int x=0;x<m_width;x++) {
        uint16_t r = in_r[y*in_stride_r + x];
        uint16_t g = in_g[y*in_stride_g + x];
        uint16_t b = in_b[y*in_stride_b + x];
        uint16_t a = in_a[y*in_stride_a + x];
        out_y[y*out_y_stride + x] = clip(r*0.299f + g*0.587f + b*0.114f, fullRange);

        // alpha
        out_a[y*out_a_stride + x] = a;
      }
    }

    for (int y=0;y<m_height;y+=2) {
      for (int x=0;x<m_width;x+=2) {
        uint16_t r = in_r[y*in_stride_r + x];
        uint16_t g = in_g[y*in_stride_g + x];
        uint16_t b = in_b[y*in_stride_b + x];
        out_cb[(y/2)*out_cb_stride + (x/2)] = clip(halfRange - r*0.168736f - g*0.331264f + b*0.5f, fullRange);
        out_cr[(y/2)*out_cb_stride + (x/2)] = clip(halfRange + r*0.5f - g*0.418688f - b*0.081312f, fullRange);
      }
    }
  }

  return outimg;
}


Error HeifPixelImage::rotate_ccw(int angle_degrees,
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

    int out_stride = 0;
    uint8_t* out_data = out_img->get_plane(channel, &out_stride);

    if (plane.bit_depth==8) {
      if (angle_degrees==270) {
        for (int x=0;x<h;x++)
          for (int y=0;y<w;y++) {
            out_data[y*out_stride + x] = in_data[(h-1-x)*in_stride + y];
          }
      }
      else if (angle_degrees==180) {
        for (int y=0;y<h;y++)
          for (int x=0;x<w;x++) {
            out_data[y*out_stride + x] = in_data[(h-1-y)*in_stride + (w-1-x)];
          }
      }
      else if (angle_degrees==90) {
        for (int x=0;x<h;x++)
          for (int y=0;y<w;y++) {
            out_data[y*out_stride + x] = in_data[x*in_stride + (w-1-y)];
          }
      }
    }
    else { // 16 bit (TODO: unchecked code)
      if (angle_degrees==270) {
        for (int x=0;x<h;x++)
          for (int y=0;y<w;y++) {
            out_data[y*out_stride + x] = in_data[(h-1-x)*in_stride + y];
            out_data[y*out_stride + x+1] = in_data[(h-1-x)*in_stride + y+1];
          }
      }
      else if (angle_degrees==180) {
        for (int y=0;y<h;y++)
          for (int x=0;x<w;x++) {
            out_data[y*out_stride + x] = in_data[(h-1-y)*in_stride + (w-1-x)];
            out_data[y*out_stride + x+1] = in_data[(h-1-y)*in_stride + (w-1-x)+1];
          }
      }
      else if (angle_degrees==90) {
        for (int x=0;x<h;x++)
          for (int y=0;y<w;y++) {
            out_data[y*out_stride + x] = in_data[x*in_stride + (w-1-y)];
            out_data[y*out_stride + x+1] = in_data[x*in_stride + (w-1-y)+1];
          }
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
                   "Can currently only mirror images with 8 bits per pixel");
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

    if (false && plane.bit_depth != 8) {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unspecified,
                   "Can currently only crop images with 8 bits per pixel");
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

    int out_stride = 0;
    uint8_t* out_data = out_img->get_plane(channel, &out_stride);

    if (plane.bit_depth==8) {
      for (int y=plane_top;y<=plane_bottom;y++) {
        memcpy( &out_data[(y-plane_top)*out_stride],
                &in_data[y*in_stride + plane_left],
                plane_right - plane_left + 1 );
      }
    }
    else {
      for (int y=plane_top;y<=plane_bottom;y++) {
        memcpy( &out_data[(y-plane_top)*out_stride],
                &in_data[y*in_stride + plane_left*2],
                (plane_right - plane_left + 1)*2 );
      }
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
      // initialization only to avoid warning of uninitalized variable.
      val16 = 0;
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

  bool has_alpha = overlay->has_channel(heif_channel_Alpha);
  //bool has_alpha_me = has_channel(heif_channel_Alpha);

  int alpha_stride=0;
  uint8_t* alpha_p;
  alpha_p = overlay->get_plane(heif_channel_Alpha, &alpha_stride);

  for (heif_channel channel : channels) {
    if (!has_channel(channel)) {
      continue;
    }

    int in_stride=0;
    const uint8_t* in_p;

    int out_stride=0;
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
      if (!has_alpha) {
        memcpy(out_p + out_x0 + (out_y0 + y-in_y0)*out_stride,
               in_p + in_x0 + y*in_stride,
               in_w-in_x0);
      }
      else {
        for (int x=in_x0; x<in_w; x++) {
          uint8_t* outptr = &out_p[out_x0 + (out_y0 + y-in_y0)*out_stride +x];
          uint8_t in_val = in_p[in_x0 + y*in_stride +x];
          uint8_t alpha_val = alpha_p[in_x0 + y*in_stride +x];

          *outptr = (uint8_t)((in_val * alpha_val + *outptr * (255-alpha_val)) / 255);
        }
      }
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

    if (!width || !height) {
      continue;
    }

    int in_stride = plane.stride;
    const uint8_t* in_data = plane.mem.data();

    int out_stride = 0;
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
