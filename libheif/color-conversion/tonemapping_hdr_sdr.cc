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
#include "tonemapping_hdr_sdr.h"
#include "rec2020_rec2100.h"

template<class Pixel>
std::vector<ColorStateWithCost>
Op_tonemapping_hdr_to_sdr_planes<Pixel>::state_after_conversion(const ColorState& input_state,
                                         const ColorState& target_state,
                                         const heif_color_conversion_options& options,
                                         const heif_color_conversion_options_ext& options_ext) const
{
  constexpr bool is8bit = sizeof(Pixel) == 1;
  if (input_state.colorspace != heif_colorspace_RGB) {
    return {};
  }

  if (input_state.chroma != heif_chroma_444) {
    return {};
  }

  if (input_state.bits_per_pixel > 16 || input_state.bits_per_pixel <= 8 || (target_state.bits_per_pixel == 8 && !is8bit) || (target_state.bits_per_pixel > 8 && is8bit)) {
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

  if (target_state.nclx.m_transfer_characteristics != heif_transfer_characteristic_IEC_61966_2_1) {
    return {};
  }

  if (input_state.alpha_bits_per_pixel != 0 && input_state.alpha_bits_per_pixel != input_state.bits_per_pixel) {
    return {};
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;

  // --- output bit depth = 8

  output_state = input_state;
  output_state.bits_per_pixel = is8bit ? 8 : target_state.bits_per_pixel;
  if (is8bit) {
    output_state.alpha_bits_per_pixel = 8;
    output_state.chroma = heif_chroma_interleaved_RGBA;
  }
  else {
    output_state.alpha_bits_per_pixel = input_state.alpha_bits_per_pixel;
    output_state.chroma = heif_chroma_interleaved_RRGGBBAA_LE;
  }
  output_state.nclx.m_transfer_characteristics = heif_transfer_characteristic_IEC_61966_2_1;
  output_state.nclx.m_colour_primaries = heif_color_primaries_ITU_R_BT_709_5;

  states.emplace_back(output_state, SpeedCosts_Slow, true);
  // with and without alpha, linear and gamma color space
  if (is8bit) {
    output_state.chroma = heif_chroma_interleaved_RGB;
    output_state.alpha_bits_per_pixel = 0;
  }
  else {
    output_state.chroma = heif_chroma_interleaved_RRGGBB_LE;
    output_state.alpha_bits_per_pixel = 0;
  }

  states.emplace_back(output_state, SpeedCosts_Slow, true);

  output_state.nclx.m_transfer_characteristics = heif_transfer_characteristic_linear;

  states.emplace_back(output_state, SpeedCosts_Slow, true);

  if (is8bit) {
    output_state.alpha_bits_per_pixel = 8;
    output_state.chroma = heif_chroma_interleaved_RGBA;
  }
  else {
    output_state.alpha_bits_per_pixel = input_state.alpha_bits_per_pixel;
    output_state.chroma = heif_chroma_interleaved_RRGGBBAA_LE;
  }

  states.emplace_back(output_state, SpeedCosts_Slow, true);
  return states;
}

template class Op_tonemapping_hdr_to_sdr_planes<uint8_t>;
template class Op_tonemapping_hdr_to_sdr_planes<uint16_t>;

template<>
Result<std::shared_ptr<HeifPixelImage>>
Op_tonemapping_hdr_to_sdr_planes<uint8_t>::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                     const ColorState& input_state,
                                     const ColorState& target_state,
                                     const heif_color_conversion_options& options,
                                     const heif_color_conversion_options_ext& options_ext,
                                     const heif_security_limits* limits) const
{
  std::vector<float> HDR_LUT;
  HDR_LUT.resize(1 << input_state.bits_per_pixel);
  for (uint16_t x = 0; x != HDR_LUT.size(); x++) {
    // TODO: limited range
    float E = ldexpf((float)x, -input_state.bits_per_pixel); // 0-1 PQ/HLG value
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
  case heif_color_primaries_SMPTE_RP_431_2: // DCI-P3
      color_matrix[0][0] = 1.2249f;
      color_matrix[1][0] = -0.0420f;
      color_matrix[2][0] = -0.0197f;
      color_matrix[0][1] = -0.2247f;
      color_matrix[1][1] = 1.0419f;
      color_matrix[2][1] = -0.0786f;
      color_matrix[0][2] = 0.0000f;
      color_matrix[1][2] = 0.0001f;
      color_matrix[2][2] = 1.0983f;
      break;
  case heif_color_primaries_ITU_R_BT_2020_2_and_2100_0:
      color_matrix[0][0] = 1.6605f;
      color_matrix[1][0] = -0.1246f;
      color_matrix[2][0] = -0.0182f;
      color_matrix[0][1] = -0.5876f;
      color_matrix[1][1] = 1.1329f;
      color_matrix[2][1] = -0.1006f;
      color_matrix[0][2] = -0.0728f;
      color_matrix[1][2] = -0.0083f;
      color_matrix[2][2] = 1.1187f;
      break;
  case heif_color_primaries_ITU_R_BT_709_5: // sRGB
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
  if (auto err = outimg->add_plane(heif_channel_interleaved, width, height, 8, limits)) {
    return err;
  }

  bool has_alpha = input->has_channel(heif_channel_Alpha);
  bool want_alpha = target_state.has_alpha;
  int mask = HDR_LUT.size() - 1;
  
  const uint16_t* p_in[4];
  size_t stride_in, stride_in2;
  p_in[0] = (uint16_t*)input->get_plane(heif_channel_R, &stride_in);
  p_in[1] = (uint16_t*)input->get_plane(heif_channel_G, &stride_in2);
  assert(stride_in2 == stride_in);
  p_in[2] = (uint16_t*)input->get_plane(heif_channel_B, &stride_in2);
  assert(stride_in2 == stride_in);
  if (has_alpha) {
    p_in[3] = (uint16_t*)input->get_plane(heif_channel_Alpha, &stride_in2);
    assert(stride_in2 == stride_in);
  }
  int shift_alpha = 0;
  if (want_alpha) {
    shift_alpha = input_state.alpha_bits_per_pixel - 8;
    assert(shift_alpha > 0 && shift_alpha <= 8);
  }
  stride_in /= 2;
  
  uint8_t* p_out;
  size_t stride_out;
  p_out = outimg->get_plane(heif_channel_interleaved, &stride_out);
  
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
      // Normalize to 80 nits
      red /= 80.0f;
      red = fmaxf(red, 0.0f); // TODO: clipping negative colors also reduces luminance
      green /= 80.0f;
      green = fmax(green, 0.0f);
      blue /= 80.0f;
      blue = fmax(blue, 0.0f);
      if (target_state.nclx.get_transfer_characteristics() == heif_transfer_characteristic_IEC_61966_2_1) {
        // sRGB. Apply Reinhard tone mapping
        red = red / (red + 1.0f);
        green = green / (green + 1.0f);
        blue = blue / (blue + 1.0f);
        // Save in sRGB
        red = sRGB_inv_EOTF(red);
        green = sRGB_inv_EOTF(green);
        blue = sRGB_inv_EOTF(blue);
      } // heif_transfer_characteristic_linear will preserve linear values and clip
      if (want_alpha) {
        p_out[y * stride_out + 4 * x] = clip_f_u8(red * 255.0f);
        p_out[y * stride_out + 4 * x + 1] = clip_f_u8(green * 255.0f);
        p_out[y * stride_out + 4 * x + 2] = clip_f_u8(blue * 255.0f);
        if (has_alpha) {
          uint16_t in = p_in[3][y * stride_in + x];
          p_out[y * stride_out + 4 * x + 3] = in >> shift_alpha;
        }
        else {
          p_out[y * stride_out + 4 * x + 3] = 0xFF;
        }
      }
      else {
        p_out[y * stride_out + 3 * x] = clip_f_u8(red * 255.0f);
        p_out[y * stride_out + 3 * x + 1] = clip_f_u8(green * 255.0f);
        p_out[y * stride_out + 3 * x + 2] = clip_f_u8(blue * 255.0f);
      }
    }
  }
  outimg->add_warning({ heif_error_Ok, heif_suberror_Lossy_Tonemapping, "A lossy tone mapping from HDR to SDR occured" });
  return outimg;
}

// This is HDR tone mapped to 16-bit SDR
template<>
Result<std::shared_ptr<HeifPixelImage>>
Op_tonemapping_hdr_to_sdr_planes<uint16_t>::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
  const ColorState& input_state,
  const ColorState& target_state,
  const heif_color_conversion_options& options,
  const heif_color_conversion_options_ext& options_ext,
  const heif_security_limits* limits) const
{
  std::vector<float> HDR_LUT;
  HDR_LUT.resize(1 << input_state.bits_per_pixel);
  for (uint16_t x = 0; x != HDR_LUT.size(); x++) {
    // TODO: limited range
    float E = ldexpf((float)x, -input_state.bits_per_pixel); // 0-1 PQ/HLG value
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
  case heif_color_primaries_SMPTE_RP_431_2: // DCI-P3
    color_matrix[0][0] = 1.2249f;
    color_matrix[1][0] = -0.0420f;
    color_matrix[2][0] = -0.0197f;
    color_matrix[0][1] = -0.2247f;
    color_matrix[1][1] = 1.0419f;
    color_matrix[2][1] = -0.0786f;
    color_matrix[0][2] = 0.0000f;
    color_matrix[1][2] = 0.0001f;
    color_matrix[2][2] = 1.0983f;
    break;
  case heif_color_primaries_ITU_R_BT_2020_2_and_2100_0:
    color_matrix[0][0] = 1.6605f;
    color_matrix[1][0] = -0.1246f;
    color_matrix[2][0] = -0.0182f;
    color_matrix[0][1] = -0.5876f;
    color_matrix[1][1] = 1.1329f;
    color_matrix[2][1] = -0.1006f;
    color_matrix[0][2] = -0.0728f;
    color_matrix[1][2] = -0.0083f;
    color_matrix[2][2] = 1.1187f;
    break;
  case heif_color_primaries_ITU_R_BT_709_5: // sRGB
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
  float out_scale_f = (float)out_scale;

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
      // Normalize to 80 nits
      red /= 80.0f;
      red = fmaxf(red, 0.0f); // TODO: clipping negative colors also reduces luminance
      green /= 80.0f;
      green = fmax(green, 0.0f);
      blue /= 80.0f;
      blue = fmax(blue, 0.0f);
      if (target_state.nclx.get_transfer_characteristics() == heif_transfer_characteristic_IEC_61966_2_1) {
        // sRGB. Apply Reinhard tone mapping
        red = red / (red + 1.0f);
        green = green / (green + 1.0f);
        blue = blue / (blue + 1.0f);
        // Save in sRGB
        red = sRGB_inv_EOTF(red);
        green = sRGB_inv_EOTF(green);
        blue = sRGB_inv_EOTF(blue);
      } // heif_transfer_characteristic_linear will preserve linear values and clip
      if (want_alpha) {
        p_out[y * stride_out + 4 * x] = clip_f_u16(red * out_scale_f, out_scale);
        p_out[y * stride_out + 4 * x + 1] = clip_f_u16(green * out_scale_f, out_scale);
        p_out[y * stride_out + 4 * x + 2] = clip_f_u16(blue * out_scale_f, out_scale);
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
        p_out[y * stride_out + 3 * x] = clip_f_u16(red * out_scale_f, out_scale);
        p_out[y * stride_out + 3 * x + 1] = clip_f_u16(green * out_scale_f, out_scale);
        p_out[y * stride_out + 3 * x + 2] = clip_f_u16(blue * out_scale_f, out_scale);
      }
    }
  }
  outimg->add_warning({ heif_error_Ok, heif_suberror_Lossy_Tonemapping, "A lossy tone mapping from HDR to SDR occured" });
  return outimg;
}
