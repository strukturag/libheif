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
#include "tonemapping_hdr_hdr.h"
#include "rec2020_rec2100.h"

std::vector<ColorStateWithCost>
Op_tonemapping_hdr_to_hdr::state_after_conversion(const ColorState& input_state,
                                         const ColorState& target_state,
                                         const heif_color_conversion_options& options,
                                         const heif_color_conversion_options_ext& options_ext) const
{
  if (input_state.colorspace != heif_colorspace_RGB) {
    return {};
  }

  if (input_state.chroma != heif_chroma_444) {
    return {};
  }

  if (input_state.bits_per_pixel > 16 || input_state.bits_per_pixel <= 8) {
    return {};
  }

  if (input_state.nclx.m_transfer_characteristics != heif_transfer_characteristic_ITU_R_BT_2100_0_PQ &&
      input_state.nclx.m_transfer_characteristics != heif_transfer_characteristic_ITU_R_BT_2100_0_HLG) {
    return {};
  }

  if (input_state.nclx.m_colour_primaries != heif_color_primaries_ITU_R_BT_2020_2_and_2100_0 &&
      input_state.nclx.m_colour_primaries != heif_color_primaries_ITU_R_BT_709_5 &&
      input_state.nclx.m_colour_primaries != heif_color_primaries_SMPTE_RP_431_2) {
    return {};
  }

  if (target_state.nclx.m_transfer_characteristics != heif_transfer_characteristic_ITU_R_BT_2100_0_PQ &&
      target_state.nclx.m_transfer_characteristics != heif_transfer_characteristic_ITU_R_BT_2100_0_HLG) {
    return {};
  }

  if (target_state.nclx.m_colour_primaries != heif_color_primaries_ITU_R_BT_2020_2_and_2100_0) {
    return {};
  }

  if (input_state.alpha_bits_per_pixel != 0 && input_state.alpha_bits_per_pixel != input_state.bits_per_pixel) {
    return {};
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;

  // --- output bit depth = input bit depth or target

  output_state = input_state;
  for (bool alpha : {false, true}) {
    for (heif_transfer_characteristics transfer_characteristics : {heif_transfer_characteristic_ITU_R_BT_2100_0_PQ, heif_transfer_characteristic_ITU_R_BT_2100_0_HLG}) {
      for (bool full_range : {false, true}) {
        output_state.bits_per_pixel = input_state.bits_per_pixel;
        output_state.alpha_bits_per_pixel = input_state.alpha_bits_per_pixel;
        output_state.chroma = alpha ? heif_chroma_interleaved_RRGGBBAA_LE : heif_chroma_interleaved_RRGGBB_LE;
        output_state.nclx.m_transfer_characteristics = transfer_characteristics;
        output_state.nclx.m_colour_primaries = heif_color_primaries_ITU_R_BT_2020_2_and_2100_0;
        output_state.nclx.m_full_range_flag = full_range;

        states.emplace_back(output_state, SpeedCosts_Slow); // HDR <-> HDR Without lossy flag

        if (target_state.bits_per_pixel != 12) {
          // Also allow up conversion to 12-bit per pixel
          output_state.bits_per_pixel = 12;
          output_state.alpha_bits_per_pixel = 12;
          states.emplace_back(output_state, SpeedCosts_Slow); // HDR <-> HDR Without lossy flag to remove HDR meta data
        }
      }
    }
  }
  return states;
}

// This is HDR tone mapped to 16-bit HDR
Result<std::shared_ptr<HeifPixelImage>>
Op_tonemapping_hdr_to_hdr::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
  const ColorState& input_state,
  const ColorState& target_state,
  const heif_color_conversion_options& options,
  const heif_color_conversion_options_ext& options_ext,
  const heif_security_limits* limits) const
{
  std::vector<float> HDR_LUT;
  float target_range_bias = (float)(target_state.nclx.get_full_range_flag() ? 1 : 1 << (target_state.bits_per_pixel - 4));
  float target_range_scale = (float)(target_state.nclx.get_full_range_flag() ? (1 << target_state.bits_per_pixel) : 219 << (target_state.bits_per_pixel - 8));
  if (input_state.bits_per_pixel > 16 || input_state.bits_per_pixel <= 8)
    return Error(heif_error_Invalid_input, heif_suberror_Unsupported_bit_depth, "Internal error");
  if (limits && sizeof(float) * ((size_t)1 << input_state.bits_per_pixel) >= limits->max_memory_block_size) {
    return Error{ heif_error_Memory_allocation_error,
            heif_suberror_Security_limit_exceeded,
            "HDR EOTF lookup table exceeds memory buffer size limit" };
  }
  HDR_LUT.resize((size_t)1 << input_state.bits_per_pixel);
  for (uint16_t x = 0; x != HDR_LUT.size(); x++) {
    float E;
    if (input_state.nclx.get_full_range_flag()) {
      // full range
      E = ldexpf((float)x, -input_state.bits_per_pixel); // 0-1 PQ/HLG value
    }
    else { // limited range
      E = (float)(x - (1 << (input_state.bits_per_pixel - 4))) / (float)(219 << (input_state.bits_per_pixel - 8));
      E = fminf(fmaxf(E, 0.0f), 1.0f);
    }
    switch (input_state.nclx.m_transfer_characteristics) {
    case heif_transfer_characteristic_ITU_R_BT_2100_0_PQ:
      E = PQ_EOTF(E);
      break;
    case heif_transfer_characteristic_ITU_R_BT_2100_0_HLG:
      E = HLG_inv_OETF(E);
      break;
    }
    E *= 10000.0f; // linear map from [0,1] to [0,10000] nits
    HDR_LUT[x] = E;
  }
  float color_matrix[3][3];
  switch (input_state.nclx.get_colour_primaries()) {
  case heif_color_primaries_SMPTE_RP_431_2: // DCI-P3 upconversion to BT 2020
    color_matrix[0][0] = 0.753832436725703f;
    color_matrix[1][0] = 0.0458269544609706f;
    color_matrix[2][0] = -0.00122468761313897f;
    color_matrix[0][1] = 0.198676184652891f;
    color_matrix[1][1] = 0.941655537103336f;
    color_matrix[2][1] = 0.0176512501951178f;
    color_matrix[0][2] = 0.0475584467548115f;
    color_matrix[1][2] = 0.0125255529962738f;
    color_matrix[2][2] = 0.983664641425193f;
    break;
  case heif_color_primaries_ITU_R_BT_709_5: // sRGB upconversion to BT 2020
    color_matrix[0][0] = 0.627409440461722f;
    color_matrix[1][0] = 0.0691248390441791f;
    color_matrix[2][0] = 0.0164233580265020f;
    color_matrix[0][1] = 0.329260252193869f;
    color_matrix[1][1] = 0.919548613786419f;
    color_matrix[2][1] = 0.0880478476238868f;
    color_matrix[0][2] = 0.0432718935897224f;
    color_matrix[1][2] = 0.0113207667621735f;
    color_matrix[2][2] = 0.895616713685177f;
    break;
  case heif_color_primaries_ITU_R_BT_2020_2_and_2100_0:
  default:
    color_matrix[0][0] = 1.0f;
    color_matrix[1][0] = 0.0f;
    color_matrix[2][0] = 0.0f;
    color_matrix[0][1] = 0.0f;
    color_matrix[1][1] = 1.0f;
    color_matrix[2][1] = 0.0f;
    color_matrix[0][2] = 0.0f;
    color_matrix[1][2] = 0.0f;
    color_matrix[2][2] = 1.0f;
    break;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(input->get_width(),
    input->get_height(),
    heif_colorspace_RGB,
    target_state.chroma);

  uint32_t width = input->get_width();
  uint32_t height = input->get_height();
  if (auto err = outimg->add_plane(heif_channel_interleaved, width, height, target_state.bits_per_pixel, limits)) {
    return err;
  }

  bool has_alpha = input->has_channel(heif_channel_Alpha);
  bool want_alpha = target_state.has_alpha;
  int mask = HDR_LUT.size() - 1;

  const uint16_t* p_in[4];
  size_t stride_in, stride_in2;
  p_in[0] = input->get_channel<uint16_t>(heif_channel_R, &stride_in);
  p_in[1] = input->get_channel<uint16_t>(heif_channel_G, &stride_in2);
  assert(stride_in2 == stride_in);
  p_in[2] = input->get_channel<uint16_t>(heif_channel_B, &stride_in2);
  assert(stride_in2 == stride_in);
  if (has_alpha) {
    p_in[3] = input->get_channel<uint16_t>(heif_channel_Alpha, &stride_in2);
    assert(stride_in2 == stride_in);
  }
  int shift_alpha = 0;
  if (want_alpha) {
    shift_alpha = input_state.alpha_bits_per_pixel - target_state.alpha_bits_per_pixel;
    assert(shift_alpha >= -8 && shift_alpha <= 8);
  }

  uint16_t* p_out;
  size_t stride_out;
  p_out = outimg->get_channel<uint16_t>(heif_channel_interleaved, &stride_out);

  int out_scale = (1 << target_state.bits_per_pixel) - 1;

  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      uint16_t red_u16 = p_in[0][y * stride_in + x];
      uint16_t green_u16 = p_in[1][y * stride_in + x];
      uint16_t blue_u16 = p_in[2][y * stride_in + x];
      // Apply HDR EOTF
      float red_hdr = HDR_LUT[red_u16 & mask];
      float green_hdr = HDR_LUT[green_u16 & mask];
      float blue_hdr = HDR_LUT[blue_u16 & mask];
      // color primaries conversion
      float red = color_matrix[0][0] * red_hdr + color_matrix[0][1] * green_hdr + color_matrix[0][2] * blue_hdr;
      float green = color_matrix[1][0] * red_hdr + color_matrix[1][1] * green_hdr + color_matrix[1][2] * blue_hdr;
      float blue = color_matrix[2][0] * red_hdr + color_matrix[2][1] * green_hdr + color_matrix[2][2] * blue_hdr;
      // Color space clipping
      red = fmaxf(red, 0.0f); // TODO: clipping negative colors also reduces luminance
      green = fmax(green, 0.0f);
      blue = fmax(blue, 0.0f);
      if (target_state.nclx.get_transfer_characteristics() == heif_transfer_characteristic_ITU_R_BT_2100_0_PQ) {
        // Save in PQ
        red = PQ_inv_EOTF(red);
        green = PQ_inv_EOTF(green);
        blue = PQ_inv_EOTF(blue);
      }
      else if (target_state.nclx.get_transfer_characteristics() == heif_transfer_characteristic_ITU_R_BT_2100_0_HLG) {
        // Save in HLG
        red = HLG_OETF(red);
        green = HLG_OETF(green);
        blue = HLG_OETF(blue);
      }
      red = fmaf(red, target_range_scale, target_range_bias);
      green = fmaf(green, target_range_scale, target_range_bias);
      blue = fmaf(blue, target_range_scale, target_range_bias);
      if (want_alpha) {
        p_out[y * stride_out + 4 * x] = clip_f_u16(red, out_scale);
        p_out[y * stride_out + 4 * x + 1] = clip_f_u16(green, out_scale);
        p_out[y * stride_out + 4 * x + 2] = clip_f_u16(blue, out_scale);
        if (has_alpha) {
          uint16_t in = p_in[3][y * stride_in + x];
          if(shift_alpha >= 0)
            p_out[y * stride_out + 4 * x + 3] = in >> shift_alpha;
          else
            p_out[y * stride_out + 4 * x + 3] = (in << -shift_alpha) | (in >> 16 + shift_alpha);
        }
        else {
          p_out[y * stride_out + 4 * x + 3] = out_scale;
        }
      }
      else {
        p_out[y * stride_out + 3 * x] = clip_f_u16(red, out_scale);
        p_out[y * stride_out + 3 * x + 1] = clip_f_u16(green, out_scale);
        p_out[y * stride_out + 3 * x + 2] = clip_f_u16(blue, out_scale);
      }
    }
  }
  return outimg;
}

// Constant luminance to non constant luminance

std::vector<ColorStateWithCost>
Op_tonemapping_hdr_constant_luminance_to_hdr::state_after_conversion(const ColorState& input_state,
  const ColorState& target_state,
  const heif_color_conversion_options& options,
  const heif_color_conversion_options_ext& options_ext) const
{
  if (input_state.colorspace != heif_colorspace_YCbCr) {
    return {};
  }

  if (input_state.nclx.m_matrix_coefficients != heif_matrix_coefficients_ICtCp) {
    return {};
  }

  if (input_state.chroma != heif_chroma_444) {
    return {};
  }

  if (input_state.bits_per_pixel > 16 || input_state.bits_per_pixel <= 8) {
    return {};
  }

  if (input_state.nclx.m_transfer_characteristics != heif_transfer_characteristic_ITU_R_BT_2100_0_PQ &&
    input_state.nclx.m_transfer_characteristics != heif_transfer_characteristic_ITU_R_BT_2100_0_HLG) {
    return {};
  }

  if (input_state.alpha_bits_per_pixel != 0 && input_state.alpha_bits_per_pixel != input_state.bits_per_pixel) {
    return {};
  }

  if (input_state.nclx.get_full_range_flag() != true)
    return {}; // TODO: limited range

  std::vector<ColorStateWithCost> states;

  ColorState output_state;

  // --- output bit depth = input bit depth or target

  output_state = input_state;
  output_state.colorspace = heif_colorspace_RGB;
  for (bool alpha : {false, true}) {
    for (heif_transfer_characteristics transfer_characteristics : {heif_transfer_characteristic_ITU_R_BT_2100_0_PQ, heif_transfer_characteristic_ITU_R_BT_2100_0_HLG}) {
      for (bool full_range : {false, true}) {
        output_state.bits_per_pixel = input_state.bits_per_pixel;
        output_state.alpha_bits_per_pixel = input_state.alpha_bits_per_pixel;
        output_state.chroma = alpha ? heif_chroma_interleaved_RRGGBBAA_LE : heif_chroma_interleaved_RRGGBB_LE;
        output_state.nclx.m_transfer_characteristics = transfer_characteristics;
        output_state.nclx.m_colour_primaries = input_state.nclx.m_colour_primaries;
        output_state.nclx.m_full_range_flag = full_range;
        output_state.nclx.m_matrix_coefficients = heif_matrix_coefficients_RGB_GBR;

        states.emplace_back(output_state, SpeedCosts_Slow); // HDR <-> HDR Without lossy flag

        if (target_state.bits_per_pixel != 12) {
          // Also allow up conversion to 12-bit per pixel
          output_state.bits_per_pixel = 12;
          output_state.alpha_bits_per_pixel = 12;
          states.emplace_back(output_state, SpeedCosts_Slow); // HDR <-> HDR Without lossy flag to remove HDR meta data
        }
      }
    }
  }
  return states;
}

Result<std::shared_ptr<HeifPixelImage>>
Op_tonemapping_hdr_constant_luminance_to_hdr::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
  const ColorState& input_state,
  const ColorState& target_state,
  const heif_color_conversion_options& options,
  const heif_color_conversion_options_ext& options_ext,
  const heif_security_limits* limits) const
{
  float target_range_bias = (float)(target_state.nclx.get_full_range_flag() ? 1 : 1 << (target_state.bits_per_pixel - 4));
  float target_range_scale = (float)(target_state.nclx.get_full_range_flag() ? (1 << target_state.bits_per_pixel) : 219 << (target_state.bits_per_pixel - 8));
  if (input_state.bits_per_pixel > 16 || input_state.bits_per_pixel <= 8)
    return Error(heif_error_Invalid_input, heif_suberror_Unsupported_bit_depth, "Internal error");
  float color_matrix[3][3];
  switch (input_state.nclx.get_colour_primaries()) {
  case heif_color_primaries_SMPTE_RP_431_2: // DCI-P3 upconversion to BT 2020
    color_matrix[0][0] = 0.753832436725703f;
    color_matrix[1][0] = 0.0458269544609706f;
    color_matrix[2][0] = -0.00122468761313897f;
    color_matrix[0][1] = 0.198676184652891f;
    color_matrix[1][1] = 0.941655537103336f;
    color_matrix[2][1] = 0.0176512501951178f;
    color_matrix[0][2] = 0.0475584467548115f;
    color_matrix[1][2] = 0.0125255529962738f;
    color_matrix[2][2] = 0.983664641425193f;
    break;
  case heif_color_primaries_ITU_R_BT_709_5: // sRGB upconversion to BT 2020
    color_matrix[0][0] = 0.627409440461722f;
    color_matrix[1][0] = 0.0691248390441791f;
    color_matrix[2][0] = 0.0164233580265020f;
    color_matrix[0][1] = 0.329260252193869f;
    color_matrix[1][1] = 0.919548613786419f;
    color_matrix[2][1] = 0.0880478476238868f;
    color_matrix[0][2] = 0.0432718935897224f;
    color_matrix[1][2] = 0.0113207667621735f;
    color_matrix[2][2] = 0.895616713685177f;
    break;
  case heif_color_primaries_ITU_R_BT_2020_2_and_2100_0:
  default:
    color_matrix[0][0] = 1.0f;
    color_matrix[1][0] = 0.0f;
    color_matrix[2][0] = 0.0f;
    color_matrix[0][1] = 0.0f;
    color_matrix[1][1] = 1.0f;
    color_matrix[2][1] = 0.0f;
    color_matrix[0][2] = 0.0f;
    color_matrix[1][2] = 0.0f;
    color_matrix[2][2] = 1.0f;
    break;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(input->get_width(),
    input->get_height(),
    heif_colorspace_RGB,
    target_state.chroma);

  uint32_t width = input->get_width();
  uint32_t height = input->get_height();
  if (auto err = outimg->add_plane(heif_channel_interleaved, width, height, target_state.bits_per_pixel, limits)) {
    return err;
  }

  bool has_alpha = input->has_channel(heif_channel_Alpha);
  bool want_alpha = target_state.has_alpha;

  const uint16_t* p_in[4];
  size_t stride_in, stride_in2;
  p_in[0] = input->get_channel<uint16_t>(heif_channel_Y, &stride_in);
  p_in[1] = input->get_channel<uint16_t>(heif_channel_Cb, &stride_in2);
  assert(stride_in2 == stride_in);
  p_in[2] = input->get_channel<uint16_t>(heif_channel_Cr, &stride_in2);
  assert(stride_in2 == stride_in);
  if (has_alpha) {
    p_in[3] = input->get_channel<uint16_t>(heif_channel_Alpha, &stride_in2);
    assert(stride_in2 == stride_in);
  }
  int shift_alpha = 0;
  if (want_alpha) {
    shift_alpha = input_state.alpha_bits_per_pixel - target_state.alpha_bits_per_pixel;
    assert(shift_alpha >= -8 && shift_alpha <= 8);
  }

  uint16_t* p_out;
  size_t stride_out;
  p_out = outimg->get_channel<uint16_t>(heif_channel_interleaved, &stride_out);
  float input_y_range_bias = 16 << (input_state.bits_per_pixel - 8);
  float input_y_range_scale = 219 * (1 << (input_state.bits_per_pixel - 8));
  float input_c_range_bias = 128 << (input_state.bits_per_pixel - 8);
  float input_c_range_scale = 224 * (1 << (input_state.bits_per_pixel - 8));

  int out_scale = (1 << target_state.bits_per_pixel) - 1;

  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      uint16_t y_u16 = p_in[0][y * stride_in + x];
      uint16_t cb_u16 = p_in[1][y * stride_in + x];
      uint16_t cr_u16 = p_in[2][y * stride_in + x];
      float y_f, cb_f, cr_f;
      if (input_state.nclx.get_full_range_flag() == true) {
        y_f = ldexpf(y_u16, -input_state.bits_per_pixel);
        cb_f = ldexpf(cb_u16, -input_state.bits_per_pixel + 1) - 0.5f;
        cr_f = ldexpf(cr_u16, -input_state.bits_per_pixel + 1) - 0.5f;
      }
      else {
        y_f = (y_u16 - input_y_range_bias) / input_y_range_scale;
        cb_f = (cb_u16 - input_c_range_bias) / input_c_range_scale;
        cb_f = (cr_u16 - input_c_range_bias) / input_c_range_scale;
      }
      float red_hdr, green_hdr, blue_hdr;
      // ICtCp
      // Convert to LMS
      float L, M, S;
      if (input_state.nclx.get_transfer_characteristics() == heif_transfer_characteristic_ITU_R_BT_2100_0_PQ) {
        L = ldexpf(y_f + cb_f, -1);
        M = ldexpf(13220 * y_f - 27226 * cb_f + 14006 * cr_f, -13);
        S = ldexpf(35866 * y_f - 34780 * cb_f - 1086 * cr_f, -13);
        L = PQ_EOTF(L);
        M = PQ_EOTF(M);
        S = PQ_EOTF(S);
      }
      else {
        L = y_f + 6144.0f / 390875.0f * cb_f + 16384.0f / 78175.0f * cr_f;
        M = y_f - 6144.0f / 390875.0f * cb_f - 16384.0f / 78175.0f * cr_f;
        S = y_f + 1197568.0f / 1172625.0f * cb_f - 141952.0f / 234525.0f * cr_f;
        L = HLG_inv_OETF(L);
        M = HLG_inv_OETF(M);
        S = HLG_inv_OETF(S);
      }
      red_hdr = 1074053.0f / 312533.0f * L - 783349.0f / 312533.0f * M + 21829.0f / 312533.0f * S;
      green_hdr = -1236583.0f / 1562665.0f * L + 3099703.0f / 1562665.0f * M - 60091.0f / 312533.0f * S;
      blue_hdr = -40551.0f / 1562665.0f * L - 154569.0f / 1562665.0f * M + 351557.0f / 312533.0f * S;
      // color primaries conversion
      float red = color_matrix[0][0] * red_hdr + color_matrix[0][1] * green_hdr + color_matrix[0][2] * blue_hdr;
      float green = color_matrix[1][0] * red_hdr + color_matrix[1][1] * green_hdr + color_matrix[1][2] * blue_hdr;
      float blue = color_matrix[2][0] * red_hdr + color_matrix[2][1] * green_hdr + color_matrix[2][2] * blue_hdr;
      // Color space clipping
      red = fmaxf(red, 0.0f); // TODO: clipping negative colors also reduces luminance
      green = fmax(green, 0.0f);
      blue = fmax(blue, 0.0f);
      if (target_state.nclx.get_transfer_characteristics() == heif_transfer_characteristic_ITU_R_BT_2100_0_PQ) {
        // Save in PQ
        red = PQ_inv_EOTF(red);
        green = PQ_inv_EOTF(green);
        blue = PQ_inv_EOTF(blue);
      }
      else if (target_state.nclx.get_transfer_characteristics() == heif_transfer_characteristic_ITU_R_BT_2100_0_HLG) {
        // Save in HLG
        red = HLG_OETF(red);
        green = HLG_OETF(green);
        blue = HLG_OETF(blue);
      }
      red = fmaf(red, target_range_scale, target_range_bias);
      green = fmaf(green, target_range_scale, target_range_bias);
      blue = fmaf(blue, target_range_scale, target_range_bias);
      if (want_alpha) {
        p_out[y * stride_out + 4 * x] = clip_f_u16(red, out_scale);
        p_out[y * stride_out + 4 * x + 1] = clip_f_u16(green, out_scale);
        p_out[y * stride_out + 4 * x + 2] = clip_f_u16(blue, out_scale);
        if (has_alpha) {
          uint16_t in = p_in[3][y * stride_in + x];
          if (shift_alpha >= 0)
            p_out[y * stride_out + 4 * x + 3] = in >> shift_alpha;
          else
            p_out[y * stride_out + 4 * x + 3] = (in << -shift_alpha) | (in >> 16 + shift_alpha);
        }
        else {
          p_out[y * stride_out + 4 * x + 3] = out_scale;
        }
      }
      else {
        p_out[y * stride_out + 3 * x] = clip_f_u16(red, out_scale);
        p_out[y * stride_out + 3 * x + 1] = clip_f_u16(green, out_scale);
        p_out[y * stride_out + 3 * x + 2] = clip_f_u16(blue, out_scale);
      }
    }
  }
  return outimg;
}

// Constant luminance to non constant luminance

std::vector<ColorStateWithCost>
Op_tonemapping_hdr_to_hdr_constant_luminance::state_after_conversion(const ColorState& input_state,
  const ColorState& target_state,
  const heif_color_conversion_options& options,
  const heif_color_conversion_options_ext& options_ext) const
{
  if (input_state.colorspace != heif_colorspace_RGB) {
    return {};
  }

  if (input_state.chroma != heif_chroma_444) {
    return {};
  }

  if (input_state.bits_per_pixel > 16 || input_state.bits_per_pixel <= 8) {
    return {};
  }

  if (input_state.nclx.m_transfer_characteristics != heif_transfer_characteristic_ITU_R_BT_2100_0_PQ &&
    input_state.nclx.m_transfer_characteristics != heif_transfer_characteristic_ITU_R_BT_2100_0_HLG) {
    return {};
  }

  if (input_state.alpha_bits_per_pixel != 0 && input_state.alpha_bits_per_pixel != input_state.bits_per_pixel) {
    return {};
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;

  // --- output bit depth = input bit depth or target

  output_state = input_state;
  output_state.colorspace = heif_colorspace_YCbCr;
  for (bool alpha : {false, true}) {
    for (heif_transfer_characteristics transfer_characteristics : {heif_transfer_characteristic_ITU_R_BT_2100_0_PQ, heif_transfer_characteristic_ITU_R_BT_2100_0_HLG}) {
      for (bool full_range : {false, true}) {
        output_state.bits_per_pixel = input_state.bits_per_pixel;
        output_state.alpha_bits_per_pixel = input_state.alpha_bits_per_pixel;
        output_state.chroma = heif_chroma_444;
        output_state.nclx.m_transfer_characteristics = transfer_characteristics;
        output_state.nclx.m_colour_primaries = input_state.nclx.m_colour_primaries;
        output_state.nclx.m_full_range_flag = full_range;
        output_state.nclx.m_matrix_coefficients = heif_matrix_coefficients_ICtCp;

        states.emplace_back(output_state, SpeedCosts_Slow); // HDR <-> HDR Without lossy flag

        if (target_state.bits_per_pixel != 12) {
          // Also allow up conversion to 12-bit per pixel
          output_state.bits_per_pixel = 12;
          output_state.alpha_bits_per_pixel = 12;
          states.emplace_back(output_state, SpeedCosts_Slow); // HDR <-> HDR Without lossy flag to remove HDR meta data
        }
      }
    }
  }
  return states;
}

Result<std::shared_ptr<HeifPixelImage>>
Op_tonemapping_hdr_to_hdr_constant_luminance::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
  const ColorState& input_state,
  const ColorState& target_state,
  const heif_color_conversion_options& options,
  const heif_color_conversion_options_ext& options_ext,
  const heif_security_limits* limits) const
{
  std::vector<float> HDR_LUT;
  float target_range_bias = (float)(target_state.nclx.get_full_range_flag() ? 1 : 1 << (target_state.bits_per_pixel - 4));
  float target_range_scale = (float)(target_state.nclx.get_full_range_flag() ? (1 << target_state.bits_per_pixel) : 219 << (target_state.bits_per_pixel - 8));
  if (input_state.bits_per_pixel > 16 || input_state.bits_per_pixel <= 8)
    return Error(heif_error_Invalid_input, heif_suberror_Unsupported_bit_depth, "Internal error");
  if (limits && sizeof(float) * ((size_t)1 << input_state.bits_per_pixel) >= limits->max_memory_block_size) {
    return Error{ heif_error_Memory_allocation_error,
            heif_suberror_Security_limit_exceeded,
            "HDR EOTF lookup table exceeds memory buffer size limit" };
  }
  HDR_LUT.resize((size_t)1 << input_state.bits_per_pixel);
  for (uint16_t x = 0; x != HDR_LUT.size(); x++) {
    float E;
    if (input_state.nclx.get_full_range_flag()) {
      // full range
      E = ldexpf((float)x, -input_state.bits_per_pixel); // 0-1 PQ/HLG value
    }
    else { // limited range
      E = (float)(x - (1 << (input_state.bits_per_pixel - 4))) / (float)(219 << (input_state.bits_per_pixel - 8));
      E = fminf(fmaxf(E, 0.0f), 1.0f);
    }
    switch (input_state.nclx.m_transfer_characteristics) {
    case heif_transfer_characteristic_ITU_R_BT_2100_0_PQ:
      E = PQ_EOTF(E);
      break;
    case heif_transfer_characteristic_ITU_R_BT_2100_0_HLG:
      E = HLG_inv_OETF(E);
      break;
    }
    HDR_LUT[x] = E;
  }
  float color_matrix[3][3];
  switch (input_state.nclx.get_colour_primaries()) {
  case heif_color_primaries_SMPTE_RP_431_2: // DCI-P3 upconversion to BT 2020
    color_matrix[0][0] = 0.753832436725703f;
    color_matrix[1][0] = 0.0458269544609706f;
    color_matrix[2][0] = -0.00122468761313897f;
    color_matrix[0][1] = 0.198676184652891f;
    color_matrix[1][1] = 0.941655537103336f;
    color_matrix[2][1] = 0.0176512501951178f;
    color_matrix[0][2] = 0.0475584467548115f;
    color_matrix[1][2] = 0.0125255529962738f;
    color_matrix[2][2] = 0.983664641425193f;
    break;
  case heif_color_primaries_ITU_R_BT_709_5: // sRGB upconversion to BT 2020
    color_matrix[0][0] = 0.627409440461722f;
    color_matrix[1][0] = 0.0691248390441791f;
    color_matrix[2][0] = 0.0164233580265020f;
    color_matrix[0][1] = 0.329260252193869f;
    color_matrix[1][1] = 0.919548613786419f;
    color_matrix[2][1] = 0.0880478476238868f;
    color_matrix[0][2] = 0.0432718935897224f;
    color_matrix[1][2] = 0.0113207667621735f;
    color_matrix[2][2] = 0.895616713685177f;
    break;
  case heif_color_primaries_ITU_R_BT_2020_2_and_2100_0:
  default:
    color_matrix[0][0] = 1.0f;
    color_matrix[1][0] = 0.0f;
    color_matrix[2][0] = 0.0f;
    color_matrix[0][1] = 0.0f;
    color_matrix[1][1] = 1.0f;
    color_matrix[2][1] = 0.0f;
    color_matrix[0][2] = 0.0f;
    color_matrix[1][2] = 0.0f;
    color_matrix[2][2] = 1.0f;
    break;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(input->get_width(),
    input->get_height(),
    heif_colorspace_YCbCr,
    target_state.chroma);

  uint32_t width = input->get_width();
  uint32_t height = input->get_height();
  if (auto err = outimg->add_plane(heif_channel_Y, width, height, target_state.bits_per_pixel, limits)) {
    return err;
  }
  if (auto err = outimg->add_plane(heif_channel_Cb, width, height, target_state.bits_per_pixel, limits)) {
    return err;
  }
  if (auto err = outimg->add_plane(heif_channel_Cr, width, height, target_state.bits_per_pixel, limits)) {
    return err;
  }

  bool has_alpha = input->has_channel(heif_channel_Alpha);
  bool want_alpha = target_state.has_alpha;
  if (want_alpha) {
    if (auto err = outimg->add_plane(heif_channel_Alpha, width, height, target_state.bits_per_pixel, limits)) {
      return err;
    }
  }
  int mask = HDR_LUT.size() - 1;

  const uint16_t* p_in[4];
  size_t stride_in, stride_in2;
  p_in[0] = input->get_channel<uint16_t>(heif_channel_R, &stride_in);
  p_in[1] = input->get_channel<uint16_t>(heif_channel_G, &stride_in2);
  assert(stride_in2 == stride_in);
  p_in[2] = input->get_channel<uint16_t>(heif_channel_B, &stride_in2);
  assert(stride_in2 == stride_in);
  if (has_alpha) {
    p_in[3] = input->get_channel<uint16_t>(heif_channel_Alpha, &stride_in2);
    assert(stride_in2 == stride_in);
  }
  int shift_alpha = 0;
  if (want_alpha) {
    shift_alpha = input_state.alpha_bits_per_pixel - target_state.alpha_bits_per_pixel;
    assert(shift_alpha >= -8 && shift_alpha <= 8);
  }

  uint16_t* p_out[4];
  size_t stride_out[4];
  p_out[0] = outimg->get_channel<uint16_t>(heif_channel_Y, &stride_out[0]);
  p_out[1] = outimg->get_channel<uint16_t>(heif_channel_Cb, &stride_out[1]);
  p_out[2] = outimg->get_channel<uint16_t>(heif_channel_Cr, &stride_out[2]);
  p_out[3] = outimg->get_channel<uint16_t>(heif_channel_Alpha, &stride_out[3]);
  float target_y_range_bias, target_y_range_scale, target_c_range_bias, target_c_range_scale;
  if (target_state.nclx.get_full_range_flag() == 1) {
    target_y_range_bias = -1.0f;
    target_y_range_scale = ldexpf(1, target_state.bits_per_pixel);
    target_c_range_scale = ldexpf(1, target_state.bits_per_pixel);
    target_c_range_bias = ldexpf(1, target_state.bits_per_pixel - 1) - 1.0f;
  }
  else {
    target_y_range_bias = 16 << (target_state.bits_per_pixel - 8);
    target_y_range_scale = 219 * (1 << (target_state.bits_per_pixel - 8));
    target_c_range_bias = 128 << (target_state.bits_per_pixel - 8);
    target_c_range_scale = 224 * (1 << (target_state.bits_per_pixel - 8));
  }

  int out_scale = (1 << target_state.bits_per_pixel) - 1;

  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      uint16_t red_u16 = p_in[0][y * stride_in + x];
      uint16_t green_u16 = p_in[1][y * stride_in + x];
      uint16_t blue_u16 = p_in[2][y * stride_in + x];
      // Apply HDR EOTF
      float red_hdr = HDR_LUT[red_u16 & mask];
      float green_hdr = HDR_LUT[green_u16 & mask];
      float blue_hdr = HDR_LUT[blue_u16 & mask];
      // color primaries conversion
      float red = color_matrix[0][0] * red_hdr + color_matrix[0][1] * green_hdr + color_matrix[0][2] * blue_hdr;
      float green = color_matrix[1][0] * red_hdr + color_matrix[1][1] * green_hdr + color_matrix[1][2] * blue_hdr;
      float blue = color_matrix[2][0] * red_hdr + color_matrix[2][1] * green_hdr + color_matrix[2][2] * blue_hdr;
      // Conversion to LMS color space
      float L = ldexpf(1688.0f * red + 2146.0f * green + 262.0f * blue, -12);
      float M = ldexpf(683.0f * red + 2951.0f * green + 462.0f * blue, -12);
      float S = ldexpf(99.0f * red + 309.0f * green + 3688.0f * blue, -12);
      float I, CT, CP;
      if (target_state.nclx.get_transfer_characteristics() == heif_transfer_characteristic_ITU_R_BT_2100_0_PQ) {
        // Apply PQ
        L = PQ_inv_EOTF(L);
        M = PQ_inv_EOTF(M);
        S = PQ_inv_EOTF(S);
        // Conversion to ICtCp
        I = (L + M) * 0.5f;
        CT = ldexpf(6610.0f * L - 13613.0f * M + 7003.0f * S, -12);
        CP = ldexpf(17933.0f * L - 17390.0f * M - 543.0f * S, -12);
      }
      else if (target_state.nclx.get_transfer_characteristics() == heif_transfer_characteristic_ITU_R_BT_2100_0_HLG) {
        // Apply HLG
        L = HLG_OETF(L);
        M = HLG_OETF(M);
        S = HLG_OETF(S);
        // Conversion to ICtCp
        I = (L + M) * 0.5f;
        CT = ldexpf(3625.0f * L - 7465.0f * M + 3840.0f * S, -12);
        CP = ldexpf(9500.0f * L - 9212.0f * M - 288.0f * S, -12);
      }
      else {
        return Error(heif_error_code::heif_error_Usage_error, heif_suberror_code::heif_suberror_Unsupported_color_conversion, "Internal Error, ICtCp expects PQ or HLG transfer function.");
      }
      I = fminf(fmaxf(I, 0.0f), 1.0f);
      CT = fminf(fmaxf(CT, -0.5f), 0.5f);
      CP = fminf(fmaxf(CP, -0.5f), 0.5f);
      I = fmaf(I, target_y_range_scale, target_y_range_bias);
      CT = fmaf(CT, target_c_range_scale, target_c_range_bias);
      CP = fmaf(CP, target_c_range_scale, target_c_range_bias);
      p_out[0][y * stride_out[0] + x] = clip_f_u16(I, out_scale);
      p_out[1][y * stride_out[1] + x] = clip_f_u16(CT, out_scale);
      p_out[2][y * stride_out[2] + x] = clip_f_u16(CP, out_scale);
      if (want_alpha) {
        if (has_alpha) {
          uint16_t in = p_in[3][y * stride_in + x];
          if (shift_alpha >= 0)
            p_out[3][y * stride_out[3] + x] = in >> shift_alpha;
          else
            p_out[3][y * stride_out[3] + x] = (in << -shift_alpha) | (in >> 16 + shift_alpha);
        }
        else {
          p_out[3][y * stride_out[3] + x] = out_scale;
        }
      }
    }
  }
  return outimg;
}
