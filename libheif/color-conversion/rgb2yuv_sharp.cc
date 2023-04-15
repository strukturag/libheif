/*
 * HEIF codec.
 * Copyright (c) 2023, Dirk Farin <dirk.farin@gmail.com>
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
#include "rgb2yuv_sharp.h"

#ifdef HAVE_LIBSHARPYUV

#include "third-party/libwebp/build/dist/include/webp/sharpyuv/sharpyuv.h"
#include "third-party/libwebp/build/dist/include/webp/sharpyuv/sharpyuv_csp.h"
#include "libheif/nclx.h"
#include "libheif/common_utils.h"

#endif




std::vector<ColorStateWithCost>
Op_RGB24_32_to_YCbCr_Sharp::state_after_conversion(const ColorState& input_state,
                                                   const ColorState& target_state,
                                                   const heif_color_conversion_options& options)
{
#ifdef HAVE_LIBSHARPYUV
  // this Op only implements the sharp_yuv algorithm

  if (options.preferred_chroma_downsampling_algorithm != heif_chroma_downsampling_sharp_yuv &&
      options.only_use_preferred_chroma_algorithm) {
    return {};
  }

  if (input_state.colorspace != heif_colorspace_RGB ||
      (input_state.chroma != heif_chroma_interleaved_RGB &&
       input_state.chroma != heif_chroma_interleaved_RGBA)) {
    return {};
  }

  if (target_state.chroma != heif_chroma_420) {
    return {};
  }

  if (target_state.nclx_profile) {
    if (target_state.nclx_profile->get_matrix_coefficients() == 0) {
      return {};
    }
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;

  // --- convert RGB24

  if (input_state.chroma == heif_chroma_interleaved_RGB) {
    output_state.colorspace = heif_colorspace_YCbCr;
    output_state.chroma = heif_chroma_420;
    output_state.has_alpha = false;
    output_state.bits_per_pixel = 8;
    states.push_back({output_state, SpeedCosts_Slow});
  }

  // --- convert RGB32

  if (input_state.chroma == heif_chroma_interleaved_RGBA) {
    output_state.colorspace = heif_colorspace_YCbCr;
    output_state.chroma = heif_chroma_420;
    output_state.has_alpha = true;
    output_state.bits_per_pixel = 8;
    states.push_back({output_state, SpeedCosts_Slow});
  }

  return states;
#else
  return {};
#endif
}


std::shared_ptr<HeifPixelImage>
Op_RGB24_32_to_YCbCr_Sharp::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                               const ColorState& target_state,
                                               const heif_color_conversion_options& options)
{
#ifdef HAVE_LIBSHARPYUV
  int width = input->get_width();
  int height = input->get_height();

  auto outimg = std::make_shared<HeifPixelImage>();

  auto chroma = target_state.chroma;
  assert(chroma == heif_chroma_420);  // Only 420 is supported by libsharpyuv.
  uint8_t chromaSubH = chroma_h_subsampling(chroma);
  uint8_t chromaSubV = chroma_v_subsampling(chroma);

  outimg->create(width, height, heif_colorspace_YCbCr, chroma);

  int chroma_width = (width + chromaSubH - 1) / chromaSubH;
  int chroma_height = (height + chromaSubV - 1) / chromaSubV;

  const bool has_alpha = (input->get_chroma_format() == heif_chroma_interleaved_32bit);

  if (!outimg->add_plane(heif_channel_Y, width, height, 8) ||
      !outimg->add_plane(heif_channel_Cb, chroma_width, chroma_height, 8) ||
      !outimg->add_plane(heif_channel_Cr, chroma_width, chroma_height, 8)) {
    return nullptr;
  }

  if (has_alpha) {
    if (!outimg->add_plane(heif_channel_Alpha, width, height, 8)) {
      return nullptr;
    }
  }

  int in_stride = 0;
  const uint8_t* in_p = input->get_plane(heif_channel_interleaved, &in_stride);

  const uint8_t* in_r = &in_p[0];
  const uint8_t* in_g = &in_p[1];
  const uint8_t* in_b = &in_p[2];

  int out_cb_stride = 0, out_cr_stride = 0, out_y_stride = 0;
  uint8_t* out_y = outimg->get_plane(heif_channel_Y, &out_y_stride);
  uint8_t* out_cb = outimg->get_plane(heif_channel_Cb, &out_cb_stride);
  uint8_t* out_cr = outimg->get_plane(heif_channel_Cr, &out_cr_stride);

  bool full_range_flag = true;
  Kr_Kb kr_kb = heif::Kr_Kb::defaults();
  if (target_state.nclx_profile) {
    full_range_flag = target_state.nclx_profile->get_full_range_flag();
    kr_kb =
        heif::get_Kr_Kb(target_state.nclx_profile->get_matrix_coefficients(),
                        target_state.nclx_profile->get_colour_primaries());
  }
  int rgb_bit_depth = 8;
  SharpYuvColorSpace color_space = {
      kr_kb.Kr, kr_kb.Kb, rgb_bit_depth,
      full_range_flag ? kSharpYuvRangeFull : kSharpYuvRangeLimited};
  SharpYuvConversionMatrix yuv_matrix;
  SharpYuvComputeConversionMatrix(&color_space, &yuv_matrix);

  int bytes_per_pixel = (has_alpha ? 4 : 3);

  int sharpyuv_ok =
      SharpYuvConvert(in_r, in_g, in_b, bytes_per_pixel, in_stride,
                      rgb_bit_depth, out_y, out_y_stride, out_cb, out_cb_stride,
                      out_cr, out_cr_stride, target_state.bits_per_pixel,
                      input->get_width(), input->get_height(), &yuv_matrix);
  if (!sharpyuv_ok) {
    return nullptr;
  }

  if (has_alpha) {
    int out_a_stride;
    uint8_t* out_a = outimg->get_plane(heif_channel_Alpha, &out_a_stride);
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        uint8_t a = in_p[y * in_stride + x * 4 + 3];

        // alpha
        out_a[y * out_a_stride + x] = a;
      }
    }
  }

  return outimg;
#else
  return nullptr;
#endif
}


