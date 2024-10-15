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


#ifndef LIBHEIF_IMAGE_H
#define LIBHEIF_IMAGE_H

//#include "heif.h"
#include "error.h"
#include "nclx.h"
#include <libheif/heif_experimental.h>

#include <vector>
#include <memory>
#include <map>
#include <set>
#include <utility>
#include <cassert>


heif_chroma chroma_from_subsampling(int h, int v);

uint32_t chroma_width(uint32_t w, heif_chroma chroma);

uint32_t chroma_height(uint32_t h, heif_chroma chroma);

uint32_t channel_width(uint32_t w, heif_chroma chroma, heif_channel channel);

uint32_t channel_height(uint32_t h, heif_chroma chroma, heif_channel channel);

bool is_interleaved_with_alpha(heif_chroma chroma);

int num_interleaved_pixels_per_plane(heif_chroma chroma);

bool is_integer_multiple_of_chroma_size(uint32_t width,
                                        uint32_t height,
                                        heif_chroma chroma);

// Returns the list of valid heif_chroma values for a given colorspace.
std::vector<heif_chroma> get_valid_chroma_values_for_colorspace(heif_colorspace colorspace);

// TODO: move to public API when used
enum heif_chroma420_sample_position {
  // values 0-5 according to ISO 23091-2 / ITU-T H.273
  heif_chroma420_sample_position_00_05 = 0,
  heif_chroma420_sample_position_05_05 = 1,
  heif_chroma420_sample_position_00_00 = 2,
  heif_chroma420_sample_position_05_00 = 3,
  heif_chroma420_sample_position_00_10 = 4,
  heif_chroma420_sample_position_05_10 = 5,

  // values 6 according to ISO 23001-17
  heif_chroma420_sample_position_00_00_01_00 = 6
};


class HeifPixelImage : public std::enable_shared_from_this<HeifPixelImage>,
                       public ErrorBuffer
{
public:
  explicit HeifPixelImage() = default;

  ~HeifPixelImage();

  void create(uint32_t width, uint32_t height, heif_colorspace colorspace, heif_chroma chroma);

  void create_clone_image_at_new_size(const std::shared_ptr<const HeifPixelImage>& source, uint32_t w, uint32_t h);

  bool add_plane(heif_channel channel, uint32_t width, uint32_t height, int bit_depth);

  bool add_channel(heif_channel channel, uint32_t width, uint32_t height, heif_channel_datatype datatype, int bit_depth);

  bool has_channel(heif_channel channel) const;

  // Has alpha information either as a separate channel or in the interleaved format.
  bool has_alpha() const;

  bool is_premultiplied_alpha() const { return m_premultiplied_alpha; }

  void set_premultiplied_alpha(bool flag) { m_premultiplied_alpha = flag; }

  uint32_t get_width() const { return m_width; }

  uint32_t get_height() const { return m_height; }

  uint32_t get_width(enum heif_channel channel) const;

  uint32_t get_height(enum heif_channel channel) const;

  bool has_odd_width() const { return !!(m_width & 1); }

  bool has_odd_height() const { return !!(m_height & 1); }

  heif_chroma get_chroma_format() const { return m_chroma; }

  heif_colorspace get_colorspace() const { return m_colorspace; }

  std::set<enum heif_channel> get_channel_set() const;

  uint8_t get_storage_bits_per_pixel(enum heif_channel channel) const;

  uint8_t get_bits_per_pixel(enum heif_channel channel) const;

  heif_channel_datatype get_datatype(enum heif_channel channel) const;

  int get_number_of_interleaved_components(heif_channel channel) const;

  uint8_t* get_plane(enum heif_channel channel, uint32_t* out_stride) { return get_channel<uint8_t>(channel, out_stride); }

  const uint8_t* get_plane(enum heif_channel channel, uint32_t* out_stride) const { return get_channel<uint8_t>(channel, out_stride); }

  template <typename T>
  T* get_channel(enum heif_channel channel, uint32_t* out_stride)
  {
    auto iter = m_planes.find(channel);
    if (iter == m_planes.end()) {
      if (out_stride)
        *out_stride = 0;

      return nullptr;
    }

    if (out_stride) {
      *out_stride = static_cast<int>(iter->second.stride / sizeof(T));
    }

    //assert(sizeof(T) == iter->second.get_bytes_per_pixel());

    return static_cast<T*>(iter->second.mem);
  }

  template <typename T>
  const T* get_channel(enum heif_channel channel, uint32_t* out_stride) const
  {
    return const_cast<HeifPixelImage*>(this)->get_channel<T>(channel, out_stride);
  }

  void copy_new_plane_from(const std::shared_ptr<const HeifPixelImage>& src_image,
                           heif_channel src_channel,
                           heif_channel dst_channel);

  void extract_alpha_from_RGBA(const std::shared_ptr<const HeifPixelImage>& srcimage);

  void fill_plane(heif_channel dst_channel, uint16_t value);

  void fill_new_plane(heif_channel dst_channel, uint16_t value, int width, int height, int bpp);

  void transfer_plane_from_image_as(const std::shared_ptr<HeifPixelImage>& source,
                                    heif_channel src_channel,
                                    heif_channel dst_channel);

  Error copy_image_to(const std::shared_ptr<const HeifPixelImage>& source, uint32_t x0, uint32_t y0);

  Result<std::shared_ptr<HeifPixelImage>> rotate_ccw(int angle_degrees);

  Result<std::shared_ptr<HeifPixelImage>> mirror_inplace(heif_transform_mirror_direction);

  Result<std::shared_ptr<HeifPixelImage>> crop(uint32_t left, uint32_t right, uint32_t top, uint32_t bottom) const;

  Error fill_RGB_16bit(uint16_t r, uint16_t g, uint16_t b, uint16_t a);

  Error overlay(std::shared_ptr<HeifPixelImage>& overlay, int32_t dx, int32_t dy);

  Error scale_nearest_neighbor(std::shared_ptr<HeifPixelImage>& output, uint32_t width, uint32_t height) const;

  void set_color_profile_nclx(const std::shared_ptr<const color_profile_nclx>& profile) { m_color_profile_nclx = profile; }

  const std::shared_ptr<const color_profile_nclx>& get_color_profile_nclx() const { return m_color_profile_nclx; }

  void set_color_profile_icc(const std::shared_ptr<const color_profile_raw>& profile) { m_color_profile_icc = profile; }

  const std::shared_ptr<const color_profile_raw>& get_color_profile_icc() const { return m_color_profile_icc; }

  void debug_dump() const;

  bool extend_padding_to_size(uint32_t width, uint32_t height, bool adjust_size = false);

  bool extend_to_size_with_zero(uint32_t width, uint32_t height);

  // --- pixel aspect ratio

  bool has_nonsquare_pixel_ratio() const { return m_PixelAspectRatio_h != m_PixelAspectRatio_v; }

  void get_pixel_ratio(uint32_t* h, uint32_t* v) const
  {
    *h = m_PixelAspectRatio_h;
    *v = m_PixelAspectRatio_v;
  }

  void set_pixel_ratio(uint32_t h, uint32_t v)
  {
    m_PixelAspectRatio_h = h;
    m_PixelAspectRatio_v = v;
  }

  // --- clli

  bool has_clli() const { return m_clli.max_content_light_level != 0 || m_clli.max_pic_average_light_level != 0; }

  heif_content_light_level get_clli() const { return m_clli; }

  void set_clli(const heif_content_light_level& clli) { m_clli = clli; }

  // --- mdcv

  bool has_mdcv() const { return m_mdcv_set; }

  heif_mastering_display_colour_volume get_mdcv() const { return m_mdcv; }

  void set_mdcv(const heif_mastering_display_colour_volume& mdcv)
  {
    m_mdcv = mdcv;
    m_mdcv_set = true;
  }

  void unset_mdcv() { m_mdcv_set = false; }

  // --- warnings

  void add_warning(Error warning) { m_warnings.emplace_back(std::move(warning)); }

  void add_warnings(const std::vector<Error>& warning) { for (const auto& err : warning) m_warnings.emplace_back(err); }

  const std::vector<Error>& get_warnings() const { return m_warnings; }

private:
  struct ImagePlane
  {
    bool alloc(uint32_t width, uint32_t height, heif_channel_datatype datatype, int bit_depth, int num_interleaved_components);

    heif_channel_datatype m_datatype = heif_channel_datatype_unsigned_integer;
    uint8_t m_bit_depth = 0;
    uint8_t m_num_interleaved_components = 1;

    // the "visible" area of the plane
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    // the allocated memory size
    uint32_t m_mem_width = 0;
    uint32_t m_mem_height = 0;

    void* mem = nullptr; // aligned memory start
    uint8_t* allocated_mem = nullptr; // unaligned memory we allocated
    uint32_t stride = 0; // bytes per line

    int get_bytes_per_pixel() const;

    template <typename T> void mirror_inplace(heif_transform_mirror_direction);

    template<typename T>
    void rotate_ccw(int angle_degrees, ImagePlane& out_plane) const;

    void crop(uint32_t left, uint32_t right, uint32_t top, uint32_t bottom, int bytes_per_pixel, ImagePlane& out_plane) const;
  };

  uint32_t m_width = 0;
  uint32_t m_height = 0;
  heif_colorspace m_colorspace = heif_colorspace_undefined;
  heif_chroma m_chroma = heif_chroma_undefined;
  bool m_premultiplied_alpha = false;
  std::shared_ptr<const color_profile_nclx> m_color_profile_nclx;
  std::shared_ptr<const color_profile_raw> m_color_profile_icc;

  std::map<heif_channel, ImagePlane> m_planes;

  uint32_t m_PixelAspectRatio_h = 1;
  uint32_t m_PixelAspectRatio_v = 1;
  heif_content_light_level m_clli{};
  heif_mastering_display_colour_volume m_mdcv{};
  bool m_mdcv_set = false; // replace with std::optional<> when we are on C*+17

  std::vector<Error> m_warnings;
};

#endif
