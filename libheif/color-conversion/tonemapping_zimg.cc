/*
 * HEIF codec.
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
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

#include <cassert>
#include "tonemapping_zimg.h"
#include "zimg.h"
// Enable multithreaded tonemapping
#ifdef ENABLE_PARALLEL_TILE_DECODING
#define ENABLE_PARALLEL_ZIMG ENABLE_PARALLEL_TILE_DECODING
#endif

#ifdef ENABLE_PARALLEL_ZIMG
#include <deque>
#include <future>
#endif

// TODO: This was copied from zimg/common/alloc.h
#ifdef _WIN32
#include <malloc.h>
inline void* zimg_x_aligned_malloc(size_t size, size_t alignment) { return _aligned_malloc(size, alignment); }
inline void zimg_x_aligned_free(void* ptr) { _aligned_free(ptr); }
#else
#include <stdlib.h>
inline void* zimg_x_aligned_malloc(size_t size, size_t alignment) { void* p; if (posix_memalign(&p, alignment, size)) return nullptr; else return p; }
inline void zimg_x_aligned_free(void* ptr) { free(ptr); }
#endif

void convert_zimg_chroma(zimg_image_format& zimg_fmt, heif_colorspace colorspace, heif_chroma chroma) {
  // Color space
  switch (colorspace) {
  case heif_colorspace_YCbCr:
    zimg_fmt.color_family = ZIMG_COLOR_YUV;
    break;
  case heif_colorspace_RGB:
    zimg_fmt.color_family = ZIMG_COLOR_RGB;
    break;
  case heif_colorspace_monochrome:
  default:
    zimg_fmt.color_family = ZIMG_COLOR_GREY;
    break;
  }
  // Chroma subsampling
  if (zimg_fmt.color_family == ZIMG_COLOR_YUV) {
    switch (chroma) {
    case heif_chroma_420:
      zimg_fmt.subsample_w = 1;
      zimg_fmt.subsample_h = 1;
      break;
    case heif_chroma_422:
      zimg_fmt.subsample_w = 1;
      zimg_fmt.subsample_h = 0;
      break;
    case heif_chroma_444:
      zimg_fmt.subsample_w = 0;
      zimg_fmt.subsample_h = 0;
      break;
    }
  }
  else {
    zimg_fmt.subsample_w = 0;
    zimg_fmt.subsample_h = 0;
  }
}

void convert_zimg_nclx(zimg_image_format& zimg_fmt, const nclx_profile& nclx) {
  zimg_fmt.color_primaries = (zimg_color_primaries_e)nclx.get_colour_primaries();
  zimg_fmt.matrix_coefficients = (zimg_matrix_coefficients_e)nclx.get_matrix_coefficients();
  zimg_fmt.transfer_characteristics = (zimg_transfer_characteristics_e)nclx.get_transfer_characteristics();
  zimg_fmt.pixel_range = nclx.get_full_range_flag() ? ZIMG_RANGE_FULL : ZIMG_RANGE_LIMITED;
}

std::vector<ColorStateWithCost>
Op_zimg::state_after_conversion(const ColorState& input_state,
  const ColorState& target_state,
  const heif_color_conversion_options& options,
  const heif_color_conversion_options_ext& options_ext) const
{
  if (input_state.bits_per_pixel > 16 || input_state.bits_per_pixel < 8) {
    return {};
  }

  if (input_state.alpha_bits_per_pixel != 0 && input_state.alpha_bits_per_pixel != input_state.bits_per_pixel) {
    return {};
  }

  // zimg needs planar input/output
  if (input_state.chroma == heif_chroma_interleaved_RGB || input_state.chroma == heif_chroma_interleaved_RGBA ||
    input_state.chroma == heif_chroma_interleaved_RRGGBB_BE || input_state.chroma == heif_chroma_interleaved_RRGGBBAA_BE ||
    input_state.chroma == heif_chroma_interleaved_RRGGBB_LE || input_state.chroma == heif_chroma_interleaved_RRGGBBAA_LE) {
    return {};
  }

  // alpha image should have full image resolution
  // ...

  // check for valid target YCbCr chroma formats
  // ...

  std::vector<ColorStateWithCost> states;

  ColorState output_state;

  // --- output bit depth = input bit depth or target

  output_state = input_state;
  bool islossy = input_state.bits_per_pixel > 8 && target_state.bits_per_pixel <= 8;
  bool hasalpha = input_state.alpha_bits_per_pixel > 0;
  output_state.bits_per_pixel = target_state.bits_per_pixel;
  output_state.alpha_bits_per_pixel = hasalpha ? target_state.alpha_bits_per_pixel : 0;
  // output planar
  if (target_state.colorspace == heif_colorspace_RGB) {
    output_state.chroma = heif_chroma_444;
  }
  else {
    output_state.chroma = target_state.chroma;
  }
  output_state.nclx.m_transfer_characteristics = target_state.nclx.m_transfer_characteristics;
  output_state.nclx.m_colour_primaries = target_state.nclx.m_colour_primaries;
  output_state.nclx.m_matrix_coefficients = target_state.nclx.m_matrix_coefficients;
  output_state.nclx.m_full_range_flag = target_state.nclx.get_full_range_flag();
  if (output_state.nclx.m_matrix_coefficients == heif_matrix_coefficients_RGB_GBR) {
    output_state.colorspace = heif_colorspace_RGB;
  }

  states.emplace_back(output_state, SpeedCosts_Slow, islossy);
  
  // TODO: Sometimes low-quality conversion. Check whether it is useful to use up to 16-bit internal precision
  return states;
}

static Error run_zimg(const zimg_filter_graph* pipeline, const zimg_image_buffer_const* img_buf_in, zimg_image_buffer* img_buf_out, size_t offset, size_t chroma_offset_in, size_t chroma_offset_out, const heif_security_limits* limits) {
  size_t tmp_size = 0;
  if (zimg_filter_graph_get_tmp_size(pipeline, &tmp_size) != ZIMG_ERROR_SUCCESS) {
    return Error::InternalError;
  }
  // add y offset to descriptors
  zimg_image_buffer_const descriptor_in;
  zimg_image_buffer descriptor_out;
  descriptor_in = *img_buf_in;
  descriptor_out = *img_buf_out;
  if(descriptor_in.plane[0].data) {
    descriptor_in.plane[0].data = (const char*)descriptor_in.plane[0].data + offset * descriptor_in.plane[0].stride;
    descriptor_out.plane[0].data = (char*)descriptor_out.plane[0].data + offset * descriptor_out.plane[0].stride;
  }
  if(descriptor_in.plane[1].data) {
    descriptor_in.plane[1].data = (const char*)descriptor_in.plane[1].data + chroma_offset_in * descriptor_in.plane[1].stride;
    descriptor_out.plane[1].data = (char*)descriptor_out.plane[1].data + chroma_offset_out * descriptor_out.plane[1].stride;
  }
  if(descriptor_in.plane[2].data) {
    descriptor_in.plane[2].data = (const char*)descriptor_in.plane[2].data + chroma_offset_in * descriptor_in.plane[2].stride;
    descriptor_out.plane[2].data = (char*)descriptor_out.plane[2].data + chroma_offset_out * descriptor_out.plane[2].stride;
  }
  if(descriptor_in.plane[3].data) {
    descriptor_in.plane[3].data = (const char*)descriptor_in.plane[3].data + offset * descriptor_in.plane[3].stride;
    descriptor_out.plane[3].data = (char*)descriptor_out.plane[3].data + offset * descriptor_out.plane[3].stride;
  }
  // enforce memory limits
  MemoryHandle limiter;
  Error alloc_error = limiter.alloc(tmp_size, limits, "zimg temporary buffer");
  if (alloc_error.error_code != 0) {
    return alloc_error;
  }
  // alignment of all images should be 32 bytes
  void* tmp = zimg_x_aligned_malloc(tmp_size, 32);
  if (!tmp)
    return Error(heif_error_Memory_allocation_error, heif_suberror_Unspecified, "");
  std::unique_ptr<void, void(*)(void*)> tmp_free(tmp, &zimg_x_aligned_free);
  switch (zimg_filter_graph_process(pipeline, &descriptor_in, &descriptor_out, tmp, nullptr, nullptr, nullptr, nullptr)) {
  case ZIMG_ERROR_SUCCESS:
    return Error::Ok;
  case ZIMG_ERROR_OUT_OF_MEMORY:
    return Error(heif_error_Memory_allocation_error, heif_suberror_Unspecified, "");
  case ZIMG_ERROR_COLOR_FAMILY_MISMATCH:
  case ZIMG_ERROR_GREYSCALE_SUBSAMPLING:
  case ZIMG_ERROR_UNSUPPORTED_OPERATION:
  case ZIMG_ERROR_UNSUPPORTED_SUBSAMPLING:
  case ZIMG_ERROR_NO_COLORSPACE_CONVERSION:
    return Error(heif_error_Unsupported_feature, heif_suberror_Unsupported_color_conversion, "");
  default:
    return Error::InternalError;
  }
}

#ifdef ENABLE_PARALLEL_ZIMG
static void wait_for_jobs(std::deque<std::future<Error>>* jobs) {
  if (jobs->empty()) {
    return;
  }

  while (!jobs->empty()) {
    jobs->front().get();
    jobs->pop_front();
  }
}
#endif

Result<std::shared_ptr<HeifPixelImage>>
Op_zimg::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
  const ColorState& input_state,
  const ColorState& target_state,
  const heif_color_conversion_options& options,
  const heif_color_conversion_options_ext& options_ext,
  const heif_security_limits* limits) const
{
  uint32_t width = input->get_width();
  uint32_t height = input->get_height();
  zimg_image_format image_format_in;
  zimg_image_format_default(&image_format_in, ZIMG_API_VERSION);
  // Alpha
  image_format_in.alpha = input->has_alpha() ? (input->is_premultiplied_alpha() ? ZIMG_ALPHA_PREMULTIPLIED : ZIMG_ALPHA_STRAIGHT) : ZIMG_ALPHA_NONE;
  // Size
  image_format_in.width = width;
  image_format_in.height = height;
  if (input->get_colorspace() == heif_colorspace_YCbCr && (input->get_chroma_format() == heif_chroma_420 || input->get_chroma_format() == heif_chroma_422)) {
    // TODO: zimg can't convert it, so we increase width or height based on assumption that HeifPixelImage allocated the padding by rounded_size()
    if(input->has_odd_width()) {
      image_format_in.width += 1;
    }
    if(input->has_odd_height()) {
      image_format_in.height += 1;
    }
  }
#ifdef ENABLE_PARALLEL_ZIMG
  // TODO: zimg can't modify slice height after pipeline is constructed, so the size must exactly divide.
  int threads = 1;
  uint32_t slice_height;
  if (image_format_in.width >= 1000 && image_format_in.height >= 1000) { // Don't use multithreading for small images
    for (int i = std::thread::hardware_concurrency(); i >= 1; i--) { // Try each thread number to see if it divides evenly. Must terminate when i=1
      if((image_format_in.height % (i * 2)) == 0 && image_format_in.height / i >= 128) { // Each thread process 128 lines at least and don't break chroma subsampling
        threads = i;
        break;
      }
    }
  }
  slice_height = image_format_in.height / threads;
  image_format_in.height = slice_height;
#else
  uint32_t slice_height = image_format_in.height;
#endif
  zimg_filter_graph* graph;
  // Bit depth
  image_format_in.depth = input->get_visual_image_bits_per_pixel();
  assert(image_format_in.depth >= 8 && image_format_in.depth <= 16);
  // Data type
  image_format_in.pixel_type = image_format_in.depth <= 8 ? ZIMG_PIXEL_BYTE : ZIMG_PIXEL_WORD;
  convert_zimg_chroma(image_format_in, input->get_colorspace(), input->get_chroma_format());
  image_format_in.field_parity = ZIMG_FIELD_PROGRESSIVE;
  // Chop region (none)
  // NCLX
  nclx_profile input_profile = input->get_color_profile_nclx_with_fallback();
  convert_zimg_nclx(image_format_in, input_profile);
  // chroma location
  if (input->has_chroma_location()) {
    image_format_in.chroma_location = (zimg_chroma_location_e)input->get_chroma_location(); // TODO: Validate
  }

  zimg_image_format image_format_out;
  image_format_out = image_format_in;
  // Bit depth
  if (target_state.bits_per_pixel) {
    image_format_out.depth = target_state.bits_per_pixel;
  }
  // Data type
  image_format_out.pixel_type = image_format_out.depth <= 8 ? ZIMG_PIXEL_BYTE : ZIMG_PIXEL_WORD;
  // Color space
  convert_zimg_chroma(image_format_out, target_state.colorspace, target_state.chroma);
  // NCLX
  nclx_profile target_profile_copy = target_state.nclx;
  if (target_profile_copy.get_colour_primaries() == heif_color_primaries_unspecified)
    target_profile_copy.set_colour_primaries(input_profile.get_colour_primaries());
  if (target_profile_copy.get_matrix_coefficients() == heif_matrix_coefficients_unspecified)
    target_profile_copy.set_matrix_coefficients(input_profile.get_matrix_coefficients());
  if (target_profile_copy.get_transfer_characteristics() == heif_transfer_characteristic_unspecified)
    target_profile_copy.set_transfer_characteristics(input_profile.get_transfer_characteristics());
  convert_zimg_nclx(image_format_out, target_profile_copy);

  // conversion options
  zimg_graph_builder_params opt;
  zimg_graph_builder_params_default(&opt, ZIMG_API_VERSION);
  switch (options.preferred_chroma_upsampling_algorithm) {
  case heif_chroma_upsampling_bilinear:
  default:
    opt.resample_filter_uv = ZIMG_RESIZE_BILINEAR;
    break;
  case heif_chroma_upsampling_nearest_neighbor:
    opt.resample_filter_uv = ZIMG_RESIZE_POINT;
    break;
  }
  heif_content_light_level cll = input->get_clli();
  // TODO: zimg doesn't have good HDR to SDR operator.
  // Don't set peak luminance here. The image appears dark when the peak luminance is too high,
  // and bright when the peak luminance is too low.
  // Will need a different HDR to SDR tone mapping operator for higher quality.
  
  // if (cll.max_content_light_level != 0) {
  //   opt.nominal_peak_luminance = cll.max_content_light_level; // TODO: 
  // }

  // create color conversion pipeline (TODO: 0.01 seconds, but cannot be moved to state_after_conversion because computations made there might not used.
  std::unique_ptr<zimg_filter_graph, void(*)(zimg_filter_graph*)> pipeline(zimg_filter_graph_build(&image_format_in, &image_format_out, &opt), &zimg_filter_graph_free);
  if (!pipeline) {
    return Error::InternalError;
  }
  zimg_image_buffer_const descriptor_in;
  zimg_image_buffer descriptor_out;
  descriptor_in.version = ZIMG_API_VERSION;
  // Input buffer descriptor
  switch (input->get_colorspace()) {
  case heif_colorspace_YCbCr:
      descriptor_in.plane[0].data = input->get_channel_memory(heif_channel_Y, (size_t*)&descriptor_in.plane[0].stride);
      descriptor_in.plane[0].mask = zimg_select_buffer_mask(slice_height);
      descriptor_in.plane[1].data = input->get_channel_memory(heif_channel_Cb, (size_t*)&descriptor_in.plane[1].stride);
      descriptor_in.plane[1].mask = zimg_select_buffer_mask(chroma_height(slice_height, input_state.chroma));
      descriptor_in.plane[2].data = input->get_channel_memory(heif_channel_Cr, (size_t*)&descriptor_in.plane[2].stride);
      descriptor_in.plane[2].mask = zimg_select_buffer_mask(chroma_height(slice_height, input_state.chroma));
    break;
  case heif_colorspace_RGB:
    if (input->get_chroma_format() == heif_chroma_444) {
      descriptor_in.plane[0].data = input->get_channel_memory(heif_channel_R, (size_t*)&descriptor_in.plane[0].stride);
      descriptor_in.plane[0].mask = zimg_select_buffer_mask(slice_height);
      descriptor_in.plane[1].data = input->get_channel_memory(heif_channel_G, (size_t*)&descriptor_in.plane[1].stride);
      descriptor_in.plane[1].mask = zimg_select_buffer_mask(slice_height);
      descriptor_in.plane[2].data = input->get_channel_memory(heif_channel_B, (size_t*)&descriptor_in.plane[2].stride);
      descriptor_in.plane[2].mask = zimg_select_buffer_mask(slice_height);
    }
    else {
      return Error::InternalError;
    }
  }
  auto outimg = std::make_shared<HeifPixelImage>();
  // Output image and descriptor setup
  outimg->create(width, height, target_state.colorspace, target_state.chroma);
  descriptor_out.version = ZIMG_API_VERSION;
  switch (target_state.colorspace) {
  case heif_colorspace_YCbCr:
    if (auto err = outimg->add_channel(heif_channel_Y, width, height, target_state.bits_per_pixel, limits)) {
      return err;
    }
    if (auto err = outimg->add_channel(heif_channel_Cb, chroma_width(width, target_state.chroma), chroma_height(height, target_state.chroma), target_state.bits_per_pixel, limits)) {
      return err;
    }
    if (auto err = outimg->add_channel(heif_channel_Cr, chroma_width(width, target_state.chroma), chroma_height(height, target_state.chroma), target_state.bits_per_pixel, limits)) {
      return err;
    }
    descriptor_out.plane[0].data = outimg->get_channel_memory(heif_channel_Y, (size_t*)&descriptor_out.plane[0].stride);
    descriptor_out.plane[0].mask = zimg_select_buffer_mask(slice_height);
    descriptor_out.plane[1].data = outimg->get_channel_memory(heif_channel_Cb, (size_t*)&descriptor_out.plane[1].stride);
    descriptor_out.plane[1].mask = zimg_select_buffer_mask(chroma_height(slice_height, target_state.chroma));
    descriptor_out.plane[2].data = outimg->get_channel_memory(heif_channel_Cr, (size_t*)&descriptor_out.plane[2].stride);
    descriptor_out.plane[2].mask = zimg_select_buffer_mask(chroma_height(slice_height, target_state.chroma));
    break;
  case heif_colorspace_RGB:
    if (auto err = outimg->add_channel(heif_channel_R, width, height, target_state.bits_per_pixel, limits)) {
      return err;
    }
    if (auto err = outimg->add_channel(heif_channel_G, width, height, target_state.bits_per_pixel, limits)) {
      return err;
    }
    if (auto err = outimg->add_channel(heif_channel_B, width, height, target_state.bits_per_pixel, limits)) {
      return err;
    }
    descriptor_out.plane[0].data = outimg->get_channel_memory(heif_channel_R, (size_t*)&descriptor_out.plane[0].stride);
    descriptor_out.plane[0].mask = zimg_select_buffer_mask(slice_height);
    descriptor_out.plane[1].data = outimg->get_channel_memory(heif_channel_G, (size_t*)&descriptor_out.plane[1].stride);
    descriptor_out.plane[1].mask = zimg_select_buffer_mask(slice_height);
    descriptor_out.plane[2].data = outimg->get_channel_memory(heif_channel_B, (size_t*)&descriptor_out.plane[2].stride);
    descriptor_out.plane[2].mask = zimg_select_buffer_mask(slice_height);
    break;
  case heif_colorspace_monochrome:
    if (auto err = outimg->add_channel(heif_channel_Y, width, height, target_state.bits_per_pixel, limits)) {
      return err;
    }
    descriptor_out.plane[0].data = outimg->get_channel_memory(heif_channel_Y, (size_t*)&descriptor_out.plane[0].stride);
    descriptor_out.plane[0].mask = zimg_select_buffer_mask(slice_height);
    break;
  default:
    return Error::InternalError;
  }
  bool has_alpha = input->has_channel(heif_channel_Alpha);
  bool want_alpha = target_state.has_alpha;
  if (want_alpha) {
    if (auto err = outimg->add_channel(heif_channel_Alpha, width, height, target_state.bits_per_pixel, limits)) {
      return err;
    }
    if (has_alpha) {
      descriptor_in.plane[3].data = input->get_channel_memory(heif_channel_Alpha, (size_t*) & descriptor_in.plane[3].stride);
      descriptor_in.plane[3].mask = zimg_select_buffer_mask(slice_height);
    }
    else {
      descriptor_in.plane[3].data = 0;
      descriptor_in.plane[3].mask = 0;
      descriptor_in.plane[3].stride = 0;
    }
    descriptor_out.plane[3].data = outimg->get_channel_memory(heif_channel_Alpha, (size_t*)&descriptor_out.plane[3].stride);
    descriptor_out.plane[3].mask = zimg_select_buffer_mask(outimg->get_component_height(heif_channel_Alpha));
  }
  else {
    descriptor_in.plane[3].data = 0;
    descriptor_in.plane[3].mask = 0;
    descriptor_in.plane[3].stride = 0;
    descriptor_out.plane[3].data = 0;
    descriptor_out.plane[3].mask = 0;
    descriptor_out.plane[3].stride = 0;
  }
#ifdef ENABLE_PARALLEL_ZIMG
  // Multithreaded color conversion
  if (threads > 1) {
    std::deque<std::future<Error>> errs;
    for (int thread = 0; thread < threads; thread++) {
      size_t offset = slice_height * thread;
      size_t chroma_offset_in, chroma_offset_out;
      if (input->get_chroma_format() == heif_chroma_420) {
        chroma_offset_in = offset / 2; // slice_height is an even number
      }else{
        chroma_offset_in = offset;
      }
      if (target_state.chroma == heif_chroma_420) {
        chroma_offset_out = offset / 2;
      }else{
        chroma_offset_out = offset;
      }
      errs.push_back(std::async(std::launch::async,
          &run_zimg, pipeline.get(), &descriptor_in, &descriptor_out, offset, chroma_offset_in, chroma_offset_out, limits));
    }
    while (!errs.empty()) {
      Error e = errs.front().get();
      errs.pop_front();
      if (e) {
        wait_for_jobs(&errs);
        return e;
      }
    }
    return outimg;
  }
#endif
  // Singlethreaded color conversion
  Error err = run_zimg(pipeline.get(), &descriptor_in, &descriptor_out, 0, 0, 0, limits);
  if (err.error_code != 0)
    return err;
  return outimg;
}
