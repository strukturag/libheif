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
#include <libheif/heif_uncompressed.h>
#if HEIF_WITH_OMAF
#include "omaf_boxes.h"
#endif
#include "security_limits.h"

#include <vector>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <cassert>
#include <string>

struct BayerPattern
{
  uint16_t pattern_width = 0;
  uint16_t pattern_height = 0;
  std::vector<heif_bayer_pattern_pixel> pixels;
};

struct PolarizationPattern
{
  std::vector<uint32_t> component_indices;  // empty = applies to all components
  uint16_t pattern_width = 0;
  uint16_t pattern_height = 0;
  std::vector<float> polarization_angles;   // pattern_width * pattern_height entries
                                            // 0xFFFFFFFF bit-pattern (NaN) = no polarization filter
};

struct SensorBadPixelsMap
{
  std::vector<uint32_t> component_indices;  // empty = applies to all components
  bool correction_applied = false;
  std::vector<uint32_t> bad_rows;
  std::vector<uint32_t> bad_columns;
  struct BadPixel { uint32_t row; uint32_t column; };
  std::vector<BadPixel> bad_pixels;
};

struct SensorNonUniformityCorrection
{
  std::vector<uint32_t> component_indices;  // empty = applies to all components
  bool nuc_is_applied = false;
  uint32_t image_width = 0;
  uint32_t image_height = 0;
  std::vector<float> nuc_gains;    // image_width * image_height entries
  std::vector<float> nuc_offsets;  // image_width * image_height entries
};

heif_chroma chroma_from_subsampling(int h, int v);

uint32_t chroma_width(uint32_t w, heif_chroma chroma);

uint32_t chroma_height(uint32_t h, heif_chroma chroma);

uint32_t channel_width(uint32_t w, heif_chroma chroma, heif_channel channel);

uint32_t channel_height(uint32_t h, heif_chroma chroma, heif_channel channel);

bool is_interleaved_with_alpha(heif_chroma chroma);

int num_interleaved_components_per_plane(heif_chroma chroma);

bool is_integer_multiple_of_chroma_size(uint32_t width,
                                        uint32_t height,
                                        heif_chroma chroma);

// Returns the list of valid heif_chroma values for a given colorspace.
std::vector<heif_chroma> get_valid_chroma_values_for_colorspace(heif_colorspace colorspace);

class ImageExtraData
{
public:
  virtual ~ImageExtraData();

  // TODO: Decide who is responsible for writing the colr boxes.
  //       Currently it is distributed over various places.
  //       Either here, in image_item.cc or in grid.cc.
  std::vector<std::shared_ptr<Box>> generate_property_boxes(bool generate_colr_boxes) const;


  // --- color profile

  bool has_nclx_color_profile() const;

  virtual void set_color_profile_nclx(const nclx_profile& profile) { m_color_profile_nclx = profile; }

  nclx_profile get_color_profile_nclx() const { return m_color_profile_nclx; }

  // get the stored nclx fallback or return the default nclx if none is stored
  nclx_profile get_color_profile_nclx_with_fallback() const;

  virtual void set_color_profile_icc(const std::shared_ptr<const color_profile_raw>& profile) { m_color_profile_icc = profile; }

  bool has_icc_color_profile() const { return m_color_profile_icc != nullptr; }

  const std::shared_ptr<const color_profile_raw>& get_color_profile_icc() const { return m_color_profile_icc; }

  void set_color_profile(const std::shared_ptr<const color_profile>& profile)
  {
    auto icc = std::dynamic_pointer_cast<const color_profile_raw>(profile);
    if (icc) {
      set_color_profile_icc(icc);
    }

    auto nclx = std::dynamic_pointer_cast<const color_profile_nclx>(profile);
    if (nclx) {
      set_color_profile_nclx(nclx->get_nclx_color_profile());
    }
  }


  // --- premultiplied alpha

  bool is_premultiplied_alpha() const { return m_premultiplied_alpha; }

  virtual void set_premultiplied_alpha(bool flag) { m_premultiplied_alpha = flag; }


  // --- pixel aspect ratio

  bool has_nonsquare_pixel_ratio() const { return m_PixelAspectRatio_h != m_PixelAspectRatio_v; }

  void get_pixel_ratio(uint32_t* h, uint32_t* v) const
  {
    *h = m_PixelAspectRatio_h;
    *v = m_PixelAspectRatio_v;
  }

  virtual void set_pixel_ratio(uint32_t h, uint32_t v)
  {
    m_PixelAspectRatio_h = h;
    m_PixelAspectRatio_v = v;
  }

  // --- clli

  bool has_clli() const { return m_clli.max_content_light_level != 0 || m_clli.max_pic_average_light_level != 0; }

  heif_content_light_level get_clli() const { return m_clli; }

  virtual void set_clli(const heif_content_light_level& clli) { m_clli = clli; }

  // --- mdcv

  bool has_mdcv() const { return m_mdcv.has_value(); }

  heif_mastering_display_colour_volume get_mdcv() const { return *m_mdcv; }

  virtual void set_mdcv(const heif_mastering_display_colour_volume& mdcv)
  {
    m_mdcv = mdcv;
  }

  void unset_mdcv() { m_mdcv.reset(); }

  virtual Error set_tai_timestamp(const heif_tai_timestamp_packet* tai) {
    delete m_tai_timestamp;

    m_tai_timestamp = heif_tai_timestamp_packet_alloc();
    heif_tai_timestamp_packet_copy(m_tai_timestamp, tai);
    return Error::Ok;
  }

  const heif_tai_timestamp_packet* get_tai_timestamp() const {
    return m_tai_timestamp;
  }


  virtual void set_gimi_sample_content_id(std::string id) { m_gimi_sample_content_id = id; }

  bool has_gimi_sample_content_id() const { return m_gimi_sample_content_id.has_value(); }

  std::string get_gimi_sample_content_id() const { assert(has_gimi_sample_content_id()); return *m_gimi_sample_content_id; }


  void set_component_content_ids(const std::vector<std::string>& ids) { m_component_content_ids = ids; }

  bool has_component_content_ids() const { return !m_component_content_ids.empty(); }

  const std::vector<std::string>& get_component_content_ids() const { return m_component_content_ids; }


  // --- bayer pattern

  bool has_bayer_pattern() const { return m_bayer_pattern.has_value(); }

  const BayerPattern& get_bayer_pattern() const { assert(has_bayer_pattern()); return *m_bayer_pattern; }

  virtual void set_bayer_pattern(const BayerPattern& pattern) { m_bayer_pattern = pattern; }


  // --- polarization pattern

  bool has_polarization_patterns() const { return !m_polarization_patterns.empty(); }

  const std::vector<PolarizationPattern>& get_polarization_patterns() const { return m_polarization_patterns; }

  virtual void set_polarization_patterns(const std::vector<PolarizationPattern>& p) { m_polarization_patterns = p; }

  virtual void add_polarization_pattern(const PolarizationPattern& p) { m_polarization_patterns.push_back(p); }


  // --- sensor bad pixels map

  bool has_sensor_bad_pixels_maps() const { return !m_sensor_bad_pixels_maps.empty(); }

  const std::vector<SensorBadPixelsMap>& get_sensor_bad_pixels_maps() const { return m_sensor_bad_pixels_maps; }

  virtual void set_sensor_bad_pixels_maps(const std::vector<SensorBadPixelsMap>& m) { m_sensor_bad_pixels_maps = m; }

  virtual void add_sensor_bad_pixels_map(const SensorBadPixelsMap& m) { m_sensor_bad_pixels_maps.push_back(m); }


  // --- sensor non-uniformity correction

  bool has_sensor_nuc() const { return !m_sensor_nuc.empty(); }

  const std::vector<SensorNonUniformityCorrection>& get_sensor_nuc() const { return m_sensor_nuc; }

  virtual void set_sensor_nuc(const std::vector<SensorNonUniformityCorrection>& n) { m_sensor_nuc = n; }

  virtual void add_sensor_nuc(const SensorNonUniformityCorrection& n) { m_sensor_nuc.push_back(n); }


  // --- chroma sample location (ISO 23001-17, Section 6.1.4)

  bool has_chroma_location() const { return m_chroma_location.has_value(); }

  uint8_t get_chroma_location() const { return m_chroma_location.value_or(0); }

  virtual void set_chroma_location(uint8_t loc) { m_chroma_location = loc; }


#if HEIF_WITH_OMAF
  bool has_omaf_image_projection() const {
    return (m_omaf_image_projection != heif_omaf_image_projection_flat);
  }

  const heif_omaf_image_projection get_omaf_image_projection() const {
    return m_omaf_image_projection;
  }

  virtual void set_omaf_image_projection(const heif_omaf_image_projection projection) {
    m_omaf_image_projection = projection;
  }
#endif

private:
  bool m_premultiplied_alpha = false;
  nclx_profile m_color_profile_nclx = nclx_profile::undefined();
  std::shared_ptr<const color_profile_raw> m_color_profile_icc;

  uint32_t m_PixelAspectRatio_h = 1;
  uint32_t m_PixelAspectRatio_v = 1;
  heif_content_light_level m_clli{};
  std::optional<heif_mastering_display_colour_volume> m_mdcv;

  heif_tai_timestamp_packet* m_tai_timestamp = nullptr;

  std::optional<std::string> m_gimi_sample_content_id;

  std::vector<std::string> m_component_content_ids;

  std::optional<BayerPattern> m_bayer_pattern;

  std::vector<PolarizationPattern> m_polarization_patterns;

  std::vector<SensorBadPixelsMap> m_sensor_bad_pixels_maps;

  std::vector<SensorNonUniformityCorrection> m_sensor_nuc;

  std::optional<uint8_t> m_chroma_location;

#if HEIF_WITH_OMAF
  heif_omaf_image_projection m_omaf_image_projection = heif_omaf_image_projection::heif_omaf_image_projection_flat;
#endif

protected:
  std::shared_ptr<Box_clli> get_clli_box() const;

  std::shared_ptr<Box_mdcv> get_mdcv_box() const;

  std::shared_ptr<Box_pasp> get_pasp_box() const;

  std::shared_ptr<Box_colr> get_colr_box_nclx() const;

  std::shared_ptr<Box_colr> get_colr_box_icc() const;

#if HEIF_WITH_OMAF
  std::shared_ptr<Box_prfr> get_prfr_box() const;
#endif
};


heif_channel map_uncompressed_component_to_channel(uint16_t component_type);


class HeifPixelImage : public std::enable_shared_from_this<HeifPixelImage>,
                       public ImageExtraData,
                       public ErrorBuffer
{
public:
  explicit HeifPixelImage() = default;

  ~HeifPixelImage() override;

  void create(uint32_t width, uint32_t height, heif_colorspace colorspace, heif_chroma chroma);

  Error create_clone_image_at_new_size(const std::shared_ptr<const HeifPixelImage>& source, uint32_t w, uint32_t h,
                                       const heif_security_limits* limits);

  Error add_plane(heif_channel channel, uint32_t width, uint32_t height, int bit_depth, const heif_security_limits* limits);

  Error add_channel(heif_channel channel, uint32_t width, uint32_t height, heif_channel_datatype datatype, int bit_depth,
                    const heif_security_limits* limits);

  bool has_channel(heif_channel channel) const;

  // Has alpha information either as a separate channel or in the interleaved format.
  bool has_alpha() const;

  uint32_t get_width() const { return m_width; }

  uint32_t get_height() const { return m_height; }

  uint32_t get_width(heif_channel channel) const;

  uint32_t get_height(heif_channel channel) const;

  uint32_t get_width(uint32_t component_idx) const;

  uint32_t get_height(uint32_t component_idx) const;

  bool has_odd_width() const { return !!(m_width & 1); }

  bool has_odd_height() const { return !!(m_height & 1); }

  // TODO: currently only defined for colorspace RGB, YCbCr, Monochrome
  //uint32_t get_primary_width() const;

  // TODO: currently only defined for colorspace RGB, YCbCr, Monochrome
  //uint32_t get_primary_height() const;

  uint32_t get_primary_component() const;

  heif_chroma get_chroma_format() const { return m_chroma; }

  heif_colorspace get_colorspace() const { return m_colorspace; }

  std::set<heif_channel> get_channel_set() const;

  uint8_t get_storage_bits_per_pixel(heif_channel channel) const;

  uint8_t get_bits_per_pixel(heif_channel channel) const;

  // Get the maximum bit depth of a visual channel (YCbCr or RGB).
  uint8_t get_visual_image_bits_per_pixel() const;

  heif_channel_datatype get_datatype(heif_channel channel) const;

  int get_number_of_interleaved_components(heif_channel channel) const;

  // Note: we are using size_t as stride type since the stride is usually involved in a multiplication with the line number.
  //       For very large images (e.g. >2 GB), this can result in an integer overflow and corresponding illegal memory access.
  //       (see https://github.com/strukturag/libheif/issues/1419)
  uint8_t* get_plane(heif_channel channel, size_t* out_stride) { return get_channel<uint8_t>(channel, out_stride); }

  const uint8_t* get_plane(heif_channel channel, size_t* out_stride) const { return get_channel<uint8_t>(channel, out_stride); }

  template <typename T>
  T* get_channel(heif_channel channel, size_t* out_stride)
  {
    auto* comp = find_component_for_channel(channel);
    if (!comp) {
      if (out_stride)
        *out_stride = 0;

      return nullptr;
    }

    if (out_stride) {
      *out_stride = static_cast<int>(comp->stride / sizeof(T));
    }

    return static_cast<T*>(comp->mem);
  }

  template <typename T>
  const T* get_channel(heif_channel channel, size_t* out_stride) const
  {
    return const_cast<HeifPixelImage*>(this)->get_channel<T>(channel, out_stride);
  }


  // --- index-based component access (for ISO 23001-17 multi-component images)

  uint32_t get_number_of_used_components() const { return static_cast<uint32_t>(m_planes.size()); }

  uint32_t get_total_number_of_cmpd_components() const { return static_cast<uint32_t>(m_cmpd_component_types.size()); }

  heif_channel get_component_channel(uint32_t component_idx) const;

  uint32_t get_component_width(uint32_t component_idx) const;
  uint32_t get_component_height(uint32_t component_idx) const;
  uint8_t get_component_bits_per_pixel(uint32_t component_idx) const;
  uint8_t get_component_storage_bits_per_pixel(uint32_t component_idx) const;
  heif_channel_datatype get_component_datatype(uint32_t component_idx) const;

  // Look up the component type from the cmpd table. Works for any cmpd index,
  // even those that have no image plane (e.g. bayer reference components).
  uint16_t get_component_type(uint32_t component_idx) const;

  // Encoder path: auto-generates component_index by appending to cmpd table.
  Result<uint32_t> add_component(uint32_t width, uint32_t height,
                                 uint16_t component_type,
                                 heif_channel_datatype datatype, int bit_depth,
                                 const heif_security_limits* limits);

  // Decoder path: uses a pre-populated cmpd table to look up the component type.
  Result<uint32_t> add_component_for_index(uint32_t component_index,
                                            uint32_t width, uint32_t height,
                                            heif_channel_datatype datatype, int bit_depth,
                                            const heif_security_limits* limits);

  // Populate the cmpd component types table (decoder path).
  void set_cmpd_component_types(std::vector<uint16_t> types) { m_cmpd_component_types = std::move(types); }

  // Returns the sorted list of component_indices of all planes that have pixel data.
  std::vector<uint32_t> get_used_component_indices() const;

  uint8_t* get_component(uint32_t component_idx, size_t* out_stride);
  const uint8_t* get_component(uint32_t component_idx, size_t* out_stride) const;

  template <typename T>
  T* get_component_data(uint32_t component_idx, size_t* out_stride)
  {
    auto* comp = find_component_by_index(component_idx);
    if (!comp) {
      if (out_stride) *out_stride = 0;
      return nullptr;
    }

    if (out_stride) {
      *out_stride = comp->stride / sizeof(T);
    }
    return static_cast<T*>(comp->mem);
  }

  template <typename T>
  const T* get_component_data(uint32_t component_idx, size_t* out_stride) const
  {
    return const_cast<HeifPixelImage*>(this)->get_component_data<T>(component_idx, out_stride);
  }

  Error copy_new_plane_from(const std::shared_ptr<const HeifPixelImage>& src_image,
                            heif_channel src_channel,
                            heif_channel dst_channel,
                            const heif_security_limits* limits);

  Error extract_alpha_from_RGBA(const std::shared_ptr<const HeifPixelImage>& srcimage, const heif_security_limits* limits);

  void fill_plane(heif_channel dst_channel, uint16_t value);

  Error fill_new_plane(heif_channel dst_channel, uint16_t value, int width, int height, int bpp, const heif_security_limits* limits);

  void transfer_plane_from_image_as(const std::shared_ptr<HeifPixelImage>& source,
                                    heif_channel src_channel,
                                    heif_channel dst_channel);

  Error copy_image_to(const std::shared_ptr<const HeifPixelImage>& source, uint32_t x0, uint32_t y0);

  Result<std::shared_ptr<HeifPixelImage>> rotate_ccw(int angle_degrees, const heif_security_limits* limits);

  Result<std::shared_ptr<HeifPixelImage>> mirror_inplace(heif_transform_mirror_direction, const heif_security_limits* limits);

  Result<std::shared_ptr<HeifPixelImage>> crop(uint32_t left, uint32_t right, uint32_t top, uint32_t bottom,
                                               const heif_security_limits* limits) const;

  Error fill_RGB_16bit(uint16_t r, uint16_t g, uint16_t b, uint16_t a);

  Error overlay(std::shared_ptr<HeifPixelImage>& overlay, int32_t dx, int32_t dy);

  Error scale_nearest_neighbor(std::shared_ptr<HeifPixelImage>& output, uint32_t width, uint32_t height,
                               const heif_security_limits* limits) const;

  void forward_all_metadata_from(const std::shared_ptr<const HeifPixelImage>& src_image);

  void debug_dump() const;

  Error extend_padding_to_size(uint32_t width, uint32_t height, bool adjust_size,
                               const heif_security_limits* limits);

  Error extend_to_size_with_zero(uint32_t width, uint32_t height, const heif_security_limits* limits);

  Result<std::shared_ptr<HeifPixelImage>> extract_image_area(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                                                             const heif_security_limits* limits) const;


  // --- sequences

  void set_sample_duration(uint32_t d) { m_sample_duration = d; }

  uint32_t get_sample_duration() const { return m_sample_duration; }

  // --- warnings

  void add_warning(Error warning) { m_warnings.emplace_back(std::move(warning)); }

  void add_warnings(const std::vector<Error>& warning) { for (const auto& err : warning) m_warnings.emplace_back(err); }

  const std::vector<Error>& get_warnings() const { return m_warnings; }

private:
  struct ImageComponent
  {
    heif_channel m_channel = heif_channel_Y;
    uint32_t m_component_index = 0;  // index into the cmpd component definition table

    // limits=nullptr disables the limits
    Error alloc(uint32_t width, uint32_t height, heif_channel_datatype datatype, int bit_depth,
                int num_interleaved_components,
                const heif_security_limits* limits,
                MemoryHandle& memory_handle);

    heif_channel_datatype m_datatype = heif_channel_datatype_unsigned_integer;

    // logical bit depth per component
    // For interleaved formats, it is the number of bits for one component.
    // It is not the storage width.
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
    size_t   allocation_size = 0;
    size_t   stride = 0; // bytes per line

    int get_bytes_per_pixel() const;

    template <typename T> void mirror_inplace(heif_transform_mirror_direction);

    template<typename T>
    void rotate_ccw(int angle_degrees, ImageComponent& out_plane) const;

    void crop(uint32_t left, uint32_t right, uint32_t top, uint32_t bottom, int bytes_per_pixel, ImageComponent& out_plane) const;
  };

  ImageComponent* find_component_for_channel(heif_channel channel);
  const ImageComponent* find_component_for_channel(heif_channel channel) const;

  ImageComponent* find_component_by_index(uint32_t component_index);
  const ImageComponent* find_component_by_index(uint32_t component_index) const;

  uint32_t m_width = 0;
  uint32_t m_height = 0;
  heif_colorspace m_colorspace = heif_colorspace_undefined;
  heif_chroma m_chroma = heif_chroma_undefined;

  std::vector<ImageComponent> m_planes;
  std::vector<uint16_t> m_cmpd_component_types;  // indexed by cmpd index
  MemoryHandle m_memory_handle;

  uint32_t m_sample_duration = 0; // duration of a sequence frame

  std::vector<Error> m_warnings;
};

#endif
