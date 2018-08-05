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
#include "error.h"

#include <vector>
#include <memory>
#include <map>
#include <set>


namespace heif {

class HeifPixelImage : public std::enable_shared_from_this<HeifPixelImage>,
                       public ErrorBuffer
{
 public:
  explicit HeifPixelImage();
  ~HeifPixelImage();

  void create(int width,int height, heif_colorspace colorspace, heif_chroma chroma);

  void add_plane(heif_channel channel, int width, int height, int bit_depth);

  bool has_channel(heif_channel channel) const;

  // Has alpha information either as a separate channel or in the interleaved format.
  bool has_alpha() const;

  int get_width() const { return m_width; }

  int get_height() const { return m_height; }

  int get_width(enum heif_channel channel) const;

  int get_height(enum heif_channel channel) const;

  heif_chroma get_chroma_format() const { return m_chroma; }

  heif_colorspace get_colorspace() const { return m_colorspace; }

  std::set<enum heif_channel> get_channel_set() const;

  int get_bits_per_pixel(enum heif_channel channel) const;

  uint8_t* get_plane(enum heif_channel channel, int* out_stride);
  const uint8_t* get_plane(enum heif_channel channel, int* out_stride) const;

  void copy_new_plane_from(const std::shared_ptr<HeifPixelImage> src_image,
                           heif_channel src_channel,
                           heif_channel dst_channel);
  void fill_new_plane(heif_channel dst_channel, uint8_t value, int width, int height);

  void transfer_plane_from_image_as(std::shared_ptr<HeifPixelImage> source,
                                    heif_channel src_channel,
                                    heif_channel dst_channel);

  std::shared_ptr<HeifPixelImage> convert_colorspace(heif_colorspace colorspace,
                                                     heif_chroma chroma) const;

  Error rotate_ccw(int angle_degrees,
                   std::shared_ptr<HeifPixelImage>& out_img);

  Error mirror_inplace(bool horizontal);

  Error crop(int left,int right,int top,int bottom,
             std::shared_ptr<HeifPixelImage>& out_img) const;

  Error fill_RGB_16bit(uint16_t r, uint16_t g, uint16_t b, uint16_t a);

  Error overlay(std::shared_ptr<HeifPixelImage>& overlay, int dx,int dy);

  Error scale_nearest_neighbor(std::shared_ptr<HeifPixelImage>& output, int width,int height) const;

  void copy_color_profile_from(const std::vector<uint8_t> color_profile){ m_color_profile = color_profile; };
  
  std::vector<uint8_t> get_color_profile(){ return m_color_profile; };

 private:
  struct ImagePlane {
    int width;
    int height;
    int bit_depth;

    std::vector<uint8_t> mem;
    int stride;
  };

  int m_width = 0;
  int m_height = 0;
  heif_colorspace m_colorspace = heif_colorspace_undefined;
  heif_chroma m_chroma = heif_chroma_undefined;
  std::vector<uint8_t> m_color_profile;

  std::map<heif_channel, ImagePlane> m_planes;

  std::shared_ptr<HeifPixelImage> convert_YCbCr420_to_RGB() const;
  std::shared_ptr<HeifPixelImage> convert_YCbCr420_to_RGB24() const;
  std::shared_ptr<HeifPixelImage> convert_YCbCr420_to_RGB32() const;
  std::shared_ptr<HeifPixelImage> convert_RGB_to_RGB24_32() const;
  std::shared_ptr<HeifPixelImage> convert_mono_to_RGB(int bpp) const;
  std::shared_ptr<HeifPixelImage> convert_mono_to_YCbCr420() const;
  std::shared_ptr<HeifPixelImage> convert_RGB24_32_to_YCbCr420() const;
};


}

#endif
