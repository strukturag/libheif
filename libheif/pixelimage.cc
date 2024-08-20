/*
 * HEIF codec.
 * Copyright (c) 2017 Dirk Farin <dirk.farin@gmail.com>
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


#include "pixelimage.h"
#include "common_utils.h"

#include <cassert>
#include <cstring>
#include <utility>
#include <limits>


heif_chroma chroma_from_subsampling(int h, int v)
{
  if (h == 2 && v == 2) {
    return heif_chroma_420;
  }
  else if (h == 2 && v == 1) {
    return heif_chroma_422;
  }
  else if (h == 1 && v == 1) {
    return heif_chroma_444;
  }
  else {
    assert(false);
    return heif_chroma_undefined;
  }
}


HeifPixelImage::~HeifPixelImage()
{
  for (auto& iter : m_planes) {
    delete[] iter.second.allocated_mem;
  }
}


int num_interleaved_pixels_per_plane(heif_chroma chroma)
{
  switch (chroma) {
    case heif_chroma_undefined:
    case heif_chroma_monochrome:
    case heif_chroma_420:
    case heif_chroma_422:
    case heif_chroma_444:
      return 1;

    case heif_chroma_interleaved_RGB:
    case heif_chroma_interleaved_RRGGBB_BE:
    case heif_chroma_interleaved_RRGGBB_LE:
      return 3;

    case heif_chroma_interleaved_RGBA:
    case heif_chroma_interleaved_RRGGBBAA_BE:
    case heif_chroma_interleaved_RRGGBBAA_LE:
      return 4;
  }

  assert(false);
  return 0;
}


bool is_integer_multiple_of_chroma_size(int width,
                                        int height,
                                        heif_chroma chroma)
{
  switch (chroma) {
    case heif_chroma_444:
    case heif_chroma_monochrome:
      return true;
    case heif_chroma_422:
      return (width & 1) == 0;
    case heif_chroma_420:
      return (width & 1) == 0 && (height & 1) == 0;
    default:
      assert(false);
      return false;
  }
}


std::vector<heif_chroma> get_valid_chroma_values_for_colorspace(heif_colorspace colorspace)
{
  switch (colorspace) {
    case heif_colorspace_YCbCr:
      return {heif_chroma_420, heif_chroma_422, heif_chroma_444};

    case heif_colorspace_RGB:
      return {heif_chroma_444,
              heif_chroma_interleaved_RGB,
              heif_chroma_interleaved_RGBA,
              heif_chroma_interleaved_RRGGBB_BE,
              heif_chroma_interleaved_RRGGBBAA_BE,
              heif_chroma_interleaved_RRGGBB_LE,
              heif_chroma_interleaved_RRGGBBAA_LE};

    case heif_colorspace_monochrome:
      return {heif_chroma_monochrome};

    default:
      return {};
  }
}


void HeifPixelImage::create(int width, int height, heif_colorspace colorspace, heif_chroma chroma)
{
  m_width = width;
  m_height = height;
  m_colorspace = colorspace;
  m_chroma = chroma;
}

static uint32_t rounded_size(uint32_t s)
{
  s = (s + 1U) & ~1U;

  if (s < 64) {
    s = 64;
  }

  return s;
}

bool HeifPixelImage::add_plane(heif_channel channel, int width, int height, int bit_depth)
{
  ImagePlane plane;
  int num_interleaved_pixels = num_interleaved_pixels_per_plane(m_chroma);

  // for backwards compatibility, allow for 24/32 bits for RGB/RGBA interleaved chromas

  if (m_chroma == heif_chroma_interleaved_RGB && bit_depth == 24) {
    bit_depth = 8;
  }

  if (m_chroma == heif_chroma_interleaved_RGBA && bit_depth == 32) {
    bit_depth = 8;
  }

  if (plane.alloc(width, height, heif_channel_datatype_unsigned_integer, bit_depth, num_interleaved_pixels)) {
    m_planes.insert(std::make_pair(channel, plane));
    return true;
  }
  else {
    return false;
  }
}


bool HeifPixelImage::add_channel(heif_channel channel, int width, int height, heif_channel_datatype datatype, int bit_depth)
{
  ImagePlane plane;
  if (plane.alloc(width, height, datatype, bit_depth, 1)) {
    m_planes.insert(std::make_pair(channel, plane));
    return true;
  }
  else {
    return false;
  }
}


bool HeifPixelImage::ImagePlane::alloc(int width, int height, heif_channel_datatype datatype, int bit_depth,
                                       int num_interleaved_components) // heif_chroma chroma)
{
  assert(width >= 0);
  assert(height >= 0);
  assert(bit_depth >= 1);
  assert(bit_depth <= 32);

  // use 16 byte alignment (enough for 128 bit data-types). Every row is an integer number of data-elements.
  uint16_t alignment = 16; // must be power of two

  m_width = width;
  m_height = height;

  m_mem_width = rounded_size(width);
  m_mem_height = rounded_size(height);

  m_bit_depth = static_cast<uint8_t>(bit_depth);
  m_datatype = datatype;


  int bytes_per_component = get_bytes_per_pixel();
  int bytes_per_pixel = num_interleaved_components * bytes_per_component;

  stride = m_mem_width * bytes_per_pixel;
  stride = (stride + alignment - 1U) & ~(alignment - 1U);

  try {
    allocated_mem = new uint8_t[m_mem_height * stride + alignment - 1];
    uint8_t* mem_8 = allocated_mem;

    // shift beginning of image data to aligned memory position

    auto mem_start_addr = (uint64_t) mem_8;
    auto mem_start_offset = (mem_start_addr & (alignment - 1U));
    if (mem_start_offset != 0) {
      mem_8 += alignment - mem_start_offset;
    }

    mem = mem_8;

    return true;
  }
  catch (const std::bad_alloc& excpt) {
    return false;
  }
}


bool HeifPixelImage::extend_padding_to_size(int width, int height)
{
  for (auto& planeIter : m_planes) {
    auto* plane = &planeIter.second;

    int subsampled_width, subsampled_height;
    get_subsampled_size(width, height, planeIter.first, m_chroma,
                        &subsampled_width, &subsampled_height);

    int old_width = plane->m_width;
    int old_height = plane->m_height;

    if (plane->m_mem_width < subsampled_width ||
        plane->m_mem_height < subsampled_height) {

      ImagePlane newPlane;
      if (!newPlane.alloc(subsampled_width, subsampled_height, plane->m_datatype, plane->m_bit_depth, num_interleaved_pixels_per_plane(m_chroma))) {
        return false;
      }

      // copy the visible part of the old plane into the new plane

      for (int y = 0; y < plane->m_height; y++) {
        memcpy(static_cast<uint8_t*>(newPlane.mem) + y * newPlane.stride,
               static_cast<uint8_t*>(plane->mem) + y * plane->stride,
               plane->m_width);
      }

      planeIter.second = newPlane;
      plane = &planeIter.second;
    }

    // extend plane size

    int bytes_per_pixel = (plane->m_bit_depth + 7) / 8;

    for (int y = 0; y < old_height; y++) {
      for (int x = old_width; x < subsampled_width; x++) {
        memcpy(static_cast<uint8_t*>(plane->mem) + y * plane->stride + x * bytes_per_pixel,
               static_cast<uint8_t*>(plane->mem) + y * plane->stride + (plane->m_width - 1) * bytes_per_pixel,
               bytes_per_pixel);
      }
    }

    for (int y = old_height; y < subsampled_height; y++) {
      memcpy(static_cast<uint8_t*>(plane->mem) + y * plane->stride,
             static_cast<uint8_t*>(plane->mem) + (plane->m_height - 1) * plane->stride,
             subsampled_width * bytes_per_pixel);
    }
  }

  // don't modify the logical image size

  return true;
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

  return iter->second.m_width;
}


int HeifPixelImage::get_height(enum heif_channel channel) const
{
  auto iter = m_planes.find(channel);
  if (iter == m_planes.end()) {
    return -1;
  }

  return iter->second.m_height;
}


std::set<heif_channel> HeifPixelImage::get_channel_set() const
{
  std::set<heif_channel> channels;

  for (const auto& plane : m_planes) {
    channels.insert(plane.first);
  }

  return channels;
}


uint8_t HeifPixelImage::get_storage_bits_per_pixel(enum heif_channel channel) const
{
  if (channel == heif_channel_interleaved) {
    auto chroma = get_chroma_format();
    switch (chroma) {
      case heif_chroma_interleaved_RGB:
        return 24;
      case heif_chroma_interleaved_RGBA:
        return 32;
      case heif_chroma_interleaved_RRGGBB_BE:
      case heif_chroma_interleaved_RRGGBB_LE:
        return 48;
      case heif_chroma_interleaved_RRGGBBAA_BE:
      case heif_chroma_interleaved_RRGGBBAA_LE:
        return 64;
      default:
        return -1; // invalid channel/chroma specification
    }
  }
  else {
    uint32_t bpp = (get_bits_per_pixel(channel) + 7U) & ~7U;
    assert(bpp <= 255);
    return static_cast<uint8_t>(bpp);
  }
}


uint8_t HeifPixelImage::get_bits_per_pixel(enum heif_channel channel) const
{
  auto iter = m_planes.find(channel);
  if (iter == m_planes.end()) {
    return -1;
  }

  return iter->second.m_bit_depth;
}


void HeifPixelImage::copy_new_plane_from(const std::shared_ptr<const HeifPixelImage>& src_image,
                                         heif_channel src_channel,
                                         heif_channel dst_channel)
{
  int width = src_image->get_width(src_channel);
  int height = src_image->get_height(src_channel);

  assert(!has_channel(dst_channel));

  const auto& src_plane = src_image->m_planes.find(src_channel)->second;

  add_channel(dst_channel, width, height,
              src_plane.m_datatype,
              src_image->get_bits_per_pixel(src_channel));

  uint8_t* dst;
  int dst_stride = 0;

  const uint8_t* src;
  int src_stride = 0;

  src = src_image->get_plane(src_channel, &src_stride);
  dst = get_plane(dst_channel, &dst_stride);

  int bpl = width * (src_image->get_storage_bits_per_pixel(src_channel) / 8);

  for (int y = 0; y < height; y++) {
    memcpy(dst + y * dst_stride, src + y * src_stride, bpl);
  }
}

void HeifPixelImage::fill_new_plane(heif_channel dst_channel, uint16_t value, int width, int height, int bpp)
{
  add_plane(dst_channel, width, height, bpp);

  int num_interleaved = num_interleaved_pixels_per_plane(m_chroma);

  if (bpp <= 8) {
    uint8_t* dst;
    int dst_stride = 0;
    dst = get_plane(dst_channel, &dst_stride);
    int width_bytes = width * num_interleaved;

    for (int y = 0; y < height; y++) {
      memset(dst + y * dst_stride, value, width_bytes);
    }
  }
  else {
    uint16_t* dst;
    int dst_stride = 0;
    dst = (uint16_t*) get_plane(dst_channel, &dst_stride);

    dst_stride /= 2;

    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width * num_interleaved; x++) {
        dst[y * dst_stride + x] = value;
      }
    }
  }
}


void HeifPixelImage::transfer_plane_from_image_as(const std::shared_ptr<HeifPixelImage>& source,
                                                  heif_channel src_channel,
                                                  heif_channel dst_channel)
{
  // TODO: check that dst_channel does not exist yet

  ImagePlane plane = source->m_planes[src_channel];
  source->m_planes.erase(src_channel);

  m_planes.insert(std::make_pair(dst_channel, plane));
}


bool is_chroma_with_alpha(heif_chroma chroma)
{
  switch (chroma) {
    case heif_chroma_undefined:
    case heif_chroma_monochrome:
    case heif_chroma_420:
    case heif_chroma_422:
    case heif_chroma_444:
    case heif_chroma_interleaved_RGB:
    case heif_chroma_interleaved_RRGGBB_BE:
    case heif_chroma_interleaved_RRGGBB_LE:
      return false;

    case heif_chroma_interleaved_RGBA:
    case heif_chroma_interleaved_RRGGBBAA_BE:
    case heif_chroma_interleaved_RRGGBBAA_LE:
      return true;
  }

  assert(false);
  return false;
}


inline void assign1(uint8_t* dst, const uint8_t* src)
{
  *dst = *src;
}

inline void assign2(uint8_t* dst, const uint8_t* src)
{
  dst[0] = src[0];
  dst[1] = src[1];
}

Error HeifPixelImage::rotate_ccw(int angle_degrees,
                                 std::shared_ptr<HeifPixelImage> &out_img) {
  // --- create output image (or simply reuse existing image)

  if (angle_degrees == 0) {
    out_img = shared_from_this();
    return Error::Ok;
  }

  int out_width = m_width;
  int out_height = m_height;

  if (angle_degrees == 90 || angle_degrees == 270) {
    std::swap(out_width, out_height);
  }

  out_img = std::make_shared<HeifPixelImage>();
  out_img->create(out_width, out_height, m_colorspace, m_chroma);


  // --- rotate all channels

  for (const auto &plane_pair: m_planes) {
    heif_channel channel = plane_pair.first;
    const ImagePlane &plane = plane_pair.second;

    /*
    if (plane.bit_depth != 8) {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unspecified,
                   "Can currently only rotate images with 8 bits per pixel");
    }
    */

    int out_plane_width = plane.m_width;
    int out_plane_height = plane.m_height;

    if (angle_degrees == 90 || angle_degrees == 270) {
      std::swap(out_plane_width, out_plane_height);
    }

    out_img->add_channel(channel, out_plane_width, out_plane_height, plane.m_datatype, plane.m_bit_depth);

    ImagePlane& out_plane = out_img->m_planes.find(channel)->second;

    if (plane.m_bit_depth <= 8) {
      plane.rotate_ccw<uint8_t>(angle_degrees, out_plane);
    }
    else if (plane.m_bit_depth <= 16) {
      plane.rotate_ccw<uint16_t>(angle_degrees, out_plane);
    }
    else if (plane.m_bit_depth <= 32) {
      plane.rotate_ccw<uint32_t>(angle_degrees, out_plane);
    }
    else if (plane.m_bit_depth <= 64) {
      plane.rotate_ccw<uint64_t>(angle_degrees, out_plane);
    }
    else if (plane.m_bit_depth <= 128) {
      plane.rotate_ccw<complex64>(angle_degrees, out_plane);
    }
  }
  // --- pass the color profiles to the new image

  out_img->set_color_profile_nclx(get_color_profile_nclx());
  out_img->set_color_profile_icc(get_color_profile_icc());

  return Error::Ok;
}

template<typename T>
void HeifPixelImage::ImagePlane::rotate_ccw(int angle_degrees,
                                            ImagePlane& out_plane) const
{
  int w = m_width;
  int h = m_height;

  uint32_t in_stride = stride;
  const T* in_data = static_cast<const T*>(mem);

  uint32_t out_stride = out_plane.stride;
  T* out_data = static_cast<T*>(out_plane.mem);

  if (angle_degrees == 270) {
    for (int x = 0; x < h; x++)
      for (int y = 0; y < w; y++) {
        out_data[y * out_stride + x] = in_data[(h - 1 - x) * in_stride + y];
      }
  } else if (angle_degrees == 180) {
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        out_data[y * out_stride + x] = in_data[(h - 1 - y) * in_stride + (w - 1 - x)];
      }
  } else if (angle_degrees == 90) {
    for (int x = 0; x < h; x++)
      for (int y = 0; y < w; y++) {
        out_data[y * out_stride + x] = in_data[x * in_stride + (w - 1 - y)];
      }
  }
}


template<typename T>
void HeifPixelImage::ImagePlane::mirror_inplace(heif_transform_mirror_direction direction)
{
  int w = m_width;
  int h = m_height;

  T* data = static_cast<T*>(mem);

  if (direction == heif_transform_mirror_direction_horizontal) {
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w / 2; x++)
        std::swap(data[y * stride + x], data[y * stride + w - 1 - x]);
    }
  } else {
    for (int y = 0; y < h / 2; y++) {
      for (int x = 0; x < w; x++)
        std::swap(data[y * stride + x], data[(h - 1 - y) * stride + x]);
    }
  }
}


Error HeifPixelImage::mirror_inplace(heif_transform_mirror_direction direction)
{
  for (auto& plane_pair : m_planes) {
    ImagePlane& plane = plane_pair.second;

    if (plane.m_bit_depth != 8) {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unspecified,
                   "Can currently only mirror images with 8 bits per pixel");
    }

    if (plane.m_bit_depth <= 8) {
      plane.mirror_inplace<uint8_t>(direction);
    }
    else if (plane.m_bit_depth <= 16) {
      plane.mirror_inplace<uint16_t>(direction);
    }
    else if (plane.m_bit_depth <= 32) {
      plane.mirror_inplace<uint32_t>(direction);
    }
    else if (plane.m_bit_depth <= 64) {
      plane.mirror_inplace<uint64_t>(direction);
    }
    else if (plane.m_bit_depth <= 128) {
      plane.mirror_inplace<complex64>(direction);
    }
  }

  return Error::Ok;
}


int HeifPixelImage::ImagePlane::get_bytes_per_pixel() const
{
  if (m_bit_depth <= 8) {
    return 1;
  }
  else if (m_bit_depth <= 16) {
    return 2;
  }
  else if (m_bit_depth <= 32) {
    return 4;
  }
  else if (m_bit_depth <= 64) {
    return 8;
  }
  else {
    assert(m_bit_depth <= 128);
    return 16;
  }
}


Error HeifPixelImage::crop(int left, int right, int top, int bottom,
                           std::shared_ptr<HeifPixelImage>& out_img) const
{
  out_img = std::make_shared<HeifPixelImage>();
  out_img->create(right - left + 1, bottom - top + 1, m_colorspace, m_chroma);


  // --- crop all channels

  for (const auto& plane_pair : m_planes) {
    heif_channel channel = plane_pair.first;
    const ImagePlane& plane = plane_pair.second;

    int w = plane.m_width;
    int h = plane.m_height;

    int plane_left = left * w / m_width;
    int plane_right = right * w / m_width;
    int plane_top = top * h / m_height;
    int plane_bottom = bottom * h / m_height;

    out_img->add_channel(channel,
                         plane_right - plane_left + 1,
                         plane_bottom - plane_top + 1,
                         plane.m_datatype,
                         plane.m_bit_depth);

    ImagePlane& out_plane = out_img->m_planes.find(channel)->second;

    int bytes_per_pixel = plane.get_bytes_per_pixel();
    plane.crop(plane_left, plane_right, plane_top, plane_bottom, bytes_per_pixel, out_plane);
  }

  // --- pass the color profiles to the new image

  out_img->set_color_profile_nclx(get_color_profile_nclx());
  out_img->set_color_profile_icc(get_color_profile_icc());

  return Error::Ok;
}


void HeifPixelImage::ImagePlane::crop(int left, int right, int top, int bottom,
                                      int bytes_per_pixel, ImagePlane& out_plane) const
{
  int in_stride = stride;
  const uint8_t* in_data = static_cast<const uint8_t*>(mem);

  int out_stride = out_plane.stride;
  uint8_t* out_data = static_cast<uint8_t*>(out_plane.mem);

  for (int y = top; y <= bottom; y++) {
    memcpy(&out_data[(y - top) * out_stride],
           &in_data[y * in_stride + left * bytes_per_pixel],
           (right - left + 1) * bytes_per_pixel);
  }
}


Error HeifPixelImage::fill_RGB_16bit(uint16_t r, uint16_t g, uint16_t b, uint16_t a)
{
  for (const auto& channel : {heif_channel_R, heif_channel_G, heif_channel_B, heif_channel_Alpha}) {

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

    if (plane.m_bit_depth != 8) {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unspecified,
                   "Can currently only fill images with 8 bits per pixel");
    }

    size_t h = plane.m_height;

    size_t stride = plane.stride;
    uint8_t* data = static_cast<uint8_t*>(plane.mem);

    uint16_t val16;
    switch (channel) {
      case heif_channel_R:
        val16 = r;
        break;
      case heif_channel_G:
        val16 = g;
        break;
      case heif_channel_B:
        val16 = b;
        break;
      case heif_channel_Alpha:
        val16 = a;
        break;
      default:
        // initialization only to avoid warning of uninitialized variable.
        val16 = 0;
        // Should already be detected by the check above ("m_planes.find").
        assert(false);
    }

    auto val8 = static_cast<uint8_t>(val16 >> 8U);


    // memset() even when h * stride > sizeof(size_t)

    if (std::numeric_limits<size_t>::max() / stride > h) {
      // can fill in one step
      memset(data, val8, stride * h);
    }
    else {
      // fill line by line
      auto* p = data;

      for (size_t y=0;y<h;y++) {
        memset(p, val8, stride);
        p += stride;
      }
    }
  }

  return Error::Ok;
}


uint32_t negate_negative_int32(int32_t x)
{
  assert(x <= 0);

  if (x == INT32_MIN) {
    return static_cast<uint32_t>(INT32_MAX) + 1;
  }
  else {
    return static_cast<uint32_t>(-x);
  }
}


Error HeifPixelImage::overlay(std::shared_ptr<HeifPixelImage>& overlay, int32_t dx, int32_t dy)
{
  std::set<enum heif_channel> channels = overlay->get_channel_set();

  bool has_alpha = overlay->has_channel(heif_channel_Alpha);
  //bool has_alpha_me = has_channel(heif_channel_Alpha);

  int alpha_stride = 0;
  uint8_t* alpha_p;
  alpha_p = overlay->get_plane(heif_channel_Alpha, &alpha_stride);

  for (heif_channel channel : channels) {
    if (!has_channel(channel)) {
      continue;
    }

    int in_stride = 0;
    const uint8_t* in_p;

    int out_stride = 0;
    uint8_t* out_p;

    in_p = overlay->get_plane(channel, &in_stride);
    out_p = get_plane(channel, &out_stride);

    uint32_t in_w = overlay->get_width(channel);
    uint32_t in_h = overlay->get_height(channel);

    uint32_t out_w = get_width(channel);
    uint32_t out_h = get_height(channel);

    // top-left points where to start copying in source and destination
    uint32_t in_x0;
    uint32_t in_y0;
    uint32_t out_x0;
    uint32_t out_y0;

    if (dx > 0 && static_cast<uint32_t>(dx) >= out_w) {
      // the overlay image is completely outside the right border -> skip overlaying
      return Error::Ok;
    }
    else if (dx < 0 && in_w <= negate_negative_int32(dx)) {
      // the overlay image is completely outside the left border -> skip overlaying
      return Error::Ok;
    }

    if (dx < 0) {
      // overlay image started partially outside of left border

      in_x0 = negate_negative_int32(dx);
      out_x0 = 0;
      in_w = in_w - in_x0; // in_x0 < in_w because in_w > -dx = in_x0
    }
    else {
      in_x0 = 0;
      out_x0 = static_cast<uint32_t>(dx);
    }

    // we know that dx >= 0 && dx < out_w

    if (static_cast<uint32_t>(dx) > UINT32_MAX - in_w ||
        dx + in_w > out_w) {
      // overlay image extends partially outside of right border

      in_w = out_w - static_cast<uint32_t>(dx); // we know that dx < out_w from first condition
    }


    if (dy > 0 && static_cast<uint32_t>(dy) >= out_h) {
      // the overlay image is completely outside the bottom border -> skip overlaying
      return Error::Ok;
    }
    else if (dy < 0 && in_h <= negate_negative_int32(dy)) {
      // the overlay image is completely outside the top border -> skip overlaying
      return Error::Ok;
    }

    if (dy < 0) {
      // overlay image started partially outside of top border

      in_y0 = negate_negative_int32(dy);
      out_y0 = 0;
      in_h = in_h - in_y0; // in_y0 < in_h because in_h > -dy = in_y0
    }
    else {
      in_y0 = 0;
      out_y0 = static_cast<uint32_t>(dy);
    }

    // we know that dy >= 0 && dy < out_h

    if (static_cast<uint32_t>(dy) > UINT32_MAX - in_h ||
        dy + in_h > out_h) {
      // overlay image extends partially outside of bottom border

      in_h = out_h - static_cast<uint32_t>(dy); // we know that dy < out_h from first condition
    }


    for (uint32_t y = in_y0; y < in_h; y++) {
      if (!has_alpha) {
        memcpy(out_p + out_x0 + (out_y0 + y - in_y0) * out_stride,
               in_p + in_x0 + y * in_stride,
               in_w - in_x0);
      }
      else {
        for (uint32_t x = in_x0; x < in_w; x++) {
          uint8_t* outptr = &out_p[out_x0 + (out_y0 + y - in_y0) * out_stride + x];
          uint8_t in_val = in_p[in_x0 + y * in_stride + x];
          uint8_t alpha_val = alpha_p[in_x0 + y * in_stride + x];

          *outptr = (uint8_t) ((in_val * alpha_val + *outptr * (255 - alpha_val)) / 255);
        }
      }
    }
  }

  return Error::Ok;
}


Error HeifPixelImage::scale_nearest_neighbor(std::shared_ptr<HeifPixelImage>& out_img,
                                             int width, int height) const
{
  out_img = std::make_shared<HeifPixelImage>();
  out_img->create(width, height, m_colorspace, m_chroma);


  // --- create output image with scaled planes

  if (has_channel(heif_channel_interleaved)) {
    out_img->add_plane(heif_channel_interleaved, width, height, get_bits_per_pixel(heif_channel_interleaved));
  }
  else {
    if (get_colorspace() == heif_colorspace_RGB) {
      if (!has_channel(heif_channel_R) ||
          !has_channel(heif_channel_G) ||
          !has_channel(heif_channel_B)) {
        return Error(heif_error_Invalid_input, heif_suberror_Unspecified, "RGB input without R,G,B, planes");
      }

      out_img->add_plane(heif_channel_R, width, height, get_bits_per_pixel(heif_channel_R));
      out_img->add_plane(heif_channel_G, width, height, get_bits_per_pixel(heif_channel_G));
      out_img->add_plane(heif_channel_B, width, height, get_bits_per_pixel(heif_channel_B));
    }
    else if (get_colorspace() == heif_colorspace_monochrome) {
      if (!has_channel(heif_channel_Y)) {
        return Error(heif_error_Invalid_input, heif_suberror_Unspecified, "monochrome input with no Y plane");
      }

      out_img->add_plane(heif_channel_Y, width, height, get_bits_per_pixel(heif_channel_Y));
    }
    else if (get_colorspace() == heif_colorspace_YCbCr) {
      if (!has_channel(heif_channel_Y) ||
          !has_channel(heif_channel_Cb) ||
          !has_channel(heif_channel_Cr)) {
        return Error(heif_error_Invalid_input, heif_suberror_Unspecified, "YCbCr image without Y,Cb,Cr planes");
      }

      int cw, ch;
      get_subsampled_size(width, height, heif_channel_Cb, get_chroma_format(), &cw, &ch);
      out_img->add_plane(heif_channel_Y, width, height, get_bits_per_pixel(heif_channel_Y));
      out_img->add_plane(heif_channel_Cb, cw, ch, get_bits_per_pixel(heif_channel_Cb));
      out_img->add_plane(heif_channel_Cr, cw, ch, get_bits_per_pixel(heif_channel_Cr));
    }
    else {
      return Error(heif_error_Invalid_input, heif_suberror_Unspecified, "unknown color configuration");
    }

    if (has_channel(heif_channel_Alpha)) {
      out_img->add_plane(heif_channel_Alpha, width, height, get_bits_per_pixel(heif_channel_Alpha));
    }
  }


  // --- scale all channels

  for (const auto& plane_pair : m_planes) {
    heif_channel channel = plane_pair.first;
    const ImagePlane& plane = plane_pair.second;

    const int bpp = get_storage_bits_per_pixel(channel) / 8;

    if (!out_img->has_channel(channel)) {
      return Error(heif_error_Invalid_input, heif_suberror_Unspecified, "scaling input has extra color plane");
    }


    if (plane.m_bit_depth != 8) {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unspecified,
                   "Can currently only crop images with 8 bits per pixel");
    }

    int out_w = out_img->get_width(channel);
    int out_h = out_img->get_height(channel);

    int in_stride = plane.stride;
    const uint8_t* in_data = static_cast<const uint8_t*>(plane.mem);

    int out_stride = 0;
    uint8_t* out_data = static_cast<uint8_t*>(out_img->get_plane(channel, &out_stride));


    for (int y = 0; y < out_h; y++) {
      int iy = y * m_height / height;

      if (bpp == 1) {
        for (int x = 0; x < out_w; x++) {
          int ix = x * m_width / width;

          out_data[y * out_stride + x] = in_data[iy * in_stride + ix];
        }
      }
      else {
        for (int x = 0; x < out_w; x++) {
          int ix = x * m_width / width;

          for (int b = 0; b < bpp; b++) {
            out_data[y * out_stride + bpp * x + b] = in_data[iy * in_stride + bpp * ix + b];
          }
        }
      }
    }
  }

  return Error::Ok;
}


void HeifPixelImage::debug_dump() const
{
  auto channels = get_channel_set();
  for (auto c : channels) {
    int stride = 0;
    const uint8_t* p = get_plane(c, &stride);

    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        printf("%02x ", p[y * stride + x]);
      }
      printf("\n");
    }
  }
}
