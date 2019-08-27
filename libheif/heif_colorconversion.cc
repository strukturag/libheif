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


#include "heif_colorconversion.h"
#include <typeinfo>
#include <algorithm>
#include <string.h>
#include <assert.h>
#include <iostream>

using namespace heif;

#define DEBUG_ME 0


bool ColorState::operator==(const ColorState& b) const
{
  return (colorspace == b.colorspace &&
          chroma == b.chroma &&
          has_alpha == b.has_alpha &&
          bits_per_pixel == b.bits_per_pixel);
}


class Op_RGB_to_RGB24_32 : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};



std::vector<ColorStateWithCost>
Op_RGB_to_RGB24_32::state_after_conversion(ColorState input_state,
                                           ColorState target_state,
                                           ColorConversionOptions options)
{
  if (input_state.colorspace != heif_colorspace_RGB ||
      input_state.chroma != heif_chroma_444 ||
      input_state.bits_per_pixel != 8) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- convert to RGBA (with alpha)

  output_state.colorspace = heif_colorspace_RGB;
  output_state.chroma = heif_chroma_interleaved_RGBA;
  output_state.has_alpha = true;
  output_state.bits_per_pixel = 8;

  if (input_state.has_alpha == false &&
      target_state.has_alpha == false) {
    costs = ColorConversionCosts(0.1f, 0.0f, 0.25f);
  }
  else {
    costs = ColorConversionCosts(0.1f, 0.0f, 0.0f);
  }

  states.push_back({ output_state, costs });


  // --- convert to RGB (without alpha)

  output_state.colorspace = heif_colorspace_RGB;
  output_state.chroma = heif_chroma_interleaved_RGB;
  output_state.has_alpha = false;
  output_state.bits_per_pixel = 8;

  if (input_state.has_alpha == true &&
      target_state.has_alpha == true) {
    // do not use this conversion because we would lose the alpha channel
  }
  else {
    costs = ColorConversionCosts(0.2f, 0.0f, 0.0f);
  }

  states.push_back({ output_state, costs });

  return states;
}


std::shared_ptr<HeifPixelImage>
Op_RGB_to_RGB24_32::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                       ColorState target_state,
                                       ColorConversionOptions options)
{
  bool has_alpha = input->has_channel(heif_channel_Alpha);

  if (input->get_bits_per_pixel(heif_channel_R) != 8 ||
      input->get_bits_per_pixel(heif_channel_G) != 8 ||
      input->get_bits_per_pixel(heif_channel_B) != 8) {
    return nullptr;
  }

  if (has_alpha && input->get_bits_per_pixel(heif_channel_Alpha) != 8) {
    return nullptr;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  int width = input->get_width();
  int height = input->get_height();

  outimg->create(width, height, heif_colorspace_RGB,
                 has_alpha ? heif_chroma_interleaved_32bit : heif_chroma_interleaved_24bit);

  outimg->add_plane(heif_channel_interleaved, width, height, 8);

  const uint8_t *in_r,*in_g,*in_b,*in_a=nullptr;
  int in_r_stride=0, in_g_stride=0, in_b_stride=0, in_a_stride=0;

  uint8_t *out_p;
  int out_p_stride=0;

  in_r = input->get_plane(heif_channel_R, &in_r_stride);
  in_g = input->get_plane(heif_channel_G, &in_g_stride);
  in_b = input->get_plane(heif_channel_B, &in_b_stride);
  out_p = outimg->get_plane(heif_channel_interleaved, &out_p_stride);

  if (has_alpha) {
    in_a = input->get_plane(heif_channel_Alpha, &in_a_stride);
  }

  int x,y;
  for (y=0;y<height;y++) {

    if (has_alpha) {
      for (x=0;x<width;x++) {
        out_p[y*out_p_stride + 4*x + 0] = in_r[x + y*in_r_stride];
        out_p[y*out_p_stride + 4*x + 1] = in_g[x + y*in_g_stride];
        out_p[y*out_p_stride + 4*x + 2] = in_b[x + y*in_b_stride];
        out_p[y*out_p_stride + 4*x + 3] = in_a[x + y*in_a_stride];
      }
    }
    else {
      for (x=0;x<width;x++) {
        out_p[y*out_p_stride + 3*x + 0] = in_r[x + y*in_r_stride];
        out_p[y*out_p_stride + 3*x + 1] = in_g[x + y*in_g_stride];
        out_p[y*out_p_stride + 3*x + 2] = in_b[x + y*in_b_stride];
      }
    }
  }

  return outimg;
}





class Op_YCbCr420_to_RGB_8bit : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};



std::vector<ColorStateWithCost>
Op_YCbCr420_to_RGB_8bit::state_after_conversion(ColorState input_state,
                                                ColorState target_state,
                                                ColorConversionOptions options)
{
  if (input_state.colorspace != heif_colorspace_YCbCr ||
      input_state.chroma != heif_chroma_420 ||
      input_state.bits_per_pixel != 8) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- convert to RGB

  output_state.colorspace = heif_colorspace_RGB;
  output_state.chroma = heif_chroma_444;
  output_state.has_alpha = input_state.has_alpha;  // we simply keep the old alpha plane
  output_state.bits_per_pixel = 8;

  costs = { 0.5f, 0.0f, 0.0f };

  states.push_back({ output_state, costs });

  return states;
}


static inline uint8_t clip(int x)
{
  if (x<0) return 0;
  if (x>255) return 255;
  return static_cast<uint8_t>(x);
}


std::shared_ptr<HeifPixelImage>
Op_YCbCr420_to_RGB_8bit::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                            ColorState target_state,
                                            ColorConversionOptions options)
{
  if (input->get_bits_per_pixel(heif_channel_Y) != 8 ||
      input->get_bits_per_pixel(heif_channel_Cb) != 8 ||
      input->get_bits_per_pixel(heif_channel_Cr) != 8) {
    return nullptr;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  int bpp = 8; // TODO: how do we specify the output BPPs ?

  int width = input->get_width();
  int height = input->get_height();

  outimg->create(width, height, heif_colorspace_RGB, heif_chroma_444);

  outimg->add_plane(heif_channel_R, width, height, bpp);
  outimg->add_plane(heif_channel_G, width, height, bpp);
  outimg->add_plane(heif_channel_B, width, height, bpp);

  bool has_alpha = input->has_channel(heif_channel_Alpha);
  if (has_alpha) {
    outimg->add_plane(heif_channel_Alpha, width, height, bpp);
  }

  const uint8_t *in_y,*in_cb,*in_cr,*in_a;
  int in_y_stride=0, in_cb_stride=0, in_cr_stride=0, in_a_stride=0;

  uint8_t *out_r,*out_g,*out_b,*out_a;
  int out_r_stride=0, out_g_stride=0, out_b_stride=0, out_a_stride=0;

  in_y  = input->get_plane(heif_channel_Y,  &in_y_stride);
  in_cb = input->get_plane(heif_channel_Cb, &in_cb_stride);
  in_cr = input->get_plane(heif_channel_Cr, &in_cr_stride);
  out_r = outimg->get_plane(heif_channel_R, &out_r_stride);
  out_g = outimg->get_plane(heif_channel_G, &out_g_stride);
  out_b = outimg->get_plane(heif_channel_B, &out_b_stride);

  if (has_alpha) {
    in_a = input->get_plane(heif_channel_Alpha, &in_a_stride);
    out_a = outimg->get_plane(heif_channel_Alpha, &out_a_stride);
  }
  else {
    in_a = nullptr;
    out_a = nullptr;
  }

  int x,y;
  for (y=0;y<height;y++) {
    for (x=0;x<width;x++) {
      int yv = (in_y [y  *in_y_stride  + x] );
      int cb = (in_cb[y/2*in_cb_stride + x/2]-128);
      int cr = (in_cr[y/2*in_cr_stride + x/2]-128);

      out_r[y*out_r_stride + x] = clip(yv + ((359*cr)>>8));
      out_g[y*out_g_stride + x] = clip(yv - ((88*cb + 183*cr)>>8));
      out_b[y*out_b_stride + x] = clip(yv + ((454*cb)>>8));
    }

    if (has_alpha) {
      memcpy(&out_a[y*out_a_stride], &in_a[y*in_a_stride], width);
    }
  }

  return outimg;
}


class Op_YCbCr420_to_RGB_16bit : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};


std::vector<ColorStateWithCost>
Op_YCbCr420_to_RGB_16bit::state_after_conversion(ColorState input_state,
                                                 ColorState target_state,
                                                 ColorConversionOptions options)
{
  if (input_state.colorspace != heif_colorspace_YCbCr ||
      input_state.chroma != heif_chroma_420 ||
      input_state.bits_per_pixel == 8) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- convert to RGB

  output_state.colorspace = heif_colorspace_RGB;
  output_state.chroma = heif_chroma_444;
  output_state.has_alpha = input_state.has_alpha;  // we simply keep the old alpha plane
  output_state.bits_per_pixel = input_state.bits_per_pixel;

  costs = { 0.5f, 0.0f, 0.0f };

  states.push_back({ output_state, costs });

  return states;
}


static inline uint16_t clip(float fx,int32_t maxi)
{
  int x = static_cast<int>(fx);
  if (x<0) return 0;
  if (x>maxi) return (uint16_t)maxi;
  return static_cast<uint16_t>(x);
}


static inline uint16_t clip(int32_t x,int32_t maxi)
{
  if (x<0) return 0;
  if (x>maxi) return (uint16_t)maxi;
  return static_cast<uint16_t>(x);
}


std::shared_ptr<HeifPixelImage>
Op_YCbCr420_to_RGB_16bit::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                             ColorState target_state,
                                             ColorConversionOptions options)
{
  if (input->get_bits_per_pixel(heif_channel_Y) == 8 ||
      input->get_bits_per_pixel(heif_channel_Cb) == 8 ||
      input->get_bits_per_pixel(heif_channel_Cr) == 8) {
    return nullptr;
  }

  if (input->get_bits_per_pixel(heif_channel_Y) != input->get_bits_per_pixel(heif_channel_Cb) ||
      input->get_bits_per_pixel(heif_channel_Y) != input->get_bits_per_pixel(heif_channel_Cr)) {
    return nullptr;
  }


  int width = input->get_width();
  int height = input->get_height();

  int bpp = input->get_bits_per_pixel(heif_channel_Y);

  bool has_alpha = input->has_channel(heif_channel_Alpha);

  if (has_alpha && input->get_bits_per_pixel(heif_channel_Alpha) != bpp) {
    return nullptr;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(width, height, heif_colorspace_RGB, heif_chroma_444);

  outimg->add_plane(heif_channel_R, width, height, bpp);
  outimg->add_plane(heif_channel_G, width, height, bpp);
  outimg->add_plane(heif_channel_B, width, height, bpp);

  if (has_alpha) {
    outimg->add_plane(heif_channel_Alpha, width, height, bpp);
  }

  const uint16_t *in_y,*in_cb,*in_cr,*in_a;
  int in_y_stride=0, in_cb_stride=0, in_cr_stride=0, in_a_stride=0;

  uint16_t *out_r,*out_g,*out_b,*out_a;
  int out_r_stride=0, out_g_stride=0, out_b_stride=0, out_a_stride=0;

  in_y  = (const uint16_t*)input->get_plane(heif_channel_Y,  &in_y_stride);
  in_cb = (const uint16_t*)input->get_plane(heif_channel_Cb, &in_cb_stride);
  in_cr = (const uint16_t*)input->get_plane(heif_channel_Cr, &in_cr_stride);
  out_r = (uint16_t*)outimg->get_plane(heif_channel_R, &out_r_stride);
  out_g = (uint16_t*)outimg->get_plane(heif_channel_G, &out_g_stride);
  out_b = (uint16_t*)outimg->get_plane(heif_channel_B, &out_b_stride);

  if (has_alpha) {
    in_a = (const uint16_t*)input->get_plane(heif_channel_Alpha, &in_a_stride);
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

  int x,y;
  for (y=0;y<height;y++) {
    for (x=0;x<width;x++) {
      float yv = static_cast<float>(in_y [y  *in_y_stride  + x] );
      float cb = static_cast<float>(in_cb[y/2*in_cb_stride + x/2]-halfRange);
      float cr = static_cast<float>(in_cr[y/2*in_cr_stride + x/2]-halfRange);

      out_r[y*out_r_stride + x] = (uint16_t)(clip(yv + 1.402f*cr, fullRange)); // << bdShift);
      out_g[y*out_g_stride + x] = (uint16_t)(clip(yv - 0.344136f*cb - 0.714136f*cr, fullRange)); // << bdShift);
      out_b[y*out_b_stride + x] = (uint16_t)(clip(yv + 1.772f*cb, fullRange)); // << bdShift);
    }

    if (has_alpha) {
      memcpy(&out_a[y*out_a_stride], &in_a[y*in_a_stride], width *2);
    }
  }

  return outimg;
}






class Op_RGB_HDR_to_YCbCr420 : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};


std::vector<ColorStateWithCost>
Op_RGB_HDR_to_YCbCr420::state_after_conversion(ColorState input_state,
                                               ColorState target_state,
                                               ColorConversionOptions options)
{
  if (input_state.colorspace != heif_colorspace_RGB ||
      input_state.chroma != heif_chroma_444 ||
      input_state.bits_per_pixel == 8) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- convert to YCbCr

  output_state.colorspace = heif_colorspace_YCbCr;
  output_state.chroma = heif_chroma_420;
  output_state.has_alpha = input_state.has_alpha;  // we simply keep the old alpha plane
  output_state.bits_per_pixel = input_state.bits_per_pixel;

  costs = { 0.75f, 0.5f, 0.0f };

  states.push_back({ output_state, costs });

  return states;
}


std::shared_ptr<HeifPixelImage>
Op_RGB_HDR_to_YCbCr420::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                           ColorState target_state,
                                           ColorConversionOptions options)
{
  int width = input->get_width();
  int height = input->get_height();

  int bpp = input->get_bits_per_pixel(heif_channel_R);

  bool has_alpha = input->has_channel(heif_channel_Alpha);

  if (has_alpha && input->get_bits_per_pixel(heif_channel_Alpha) != bpp) {
    return nullptr;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(width, height, heif_colorspace_YCbCr, heif_chroma_420);

  int cwidth = (width+1)/2;
  int cheight = (height+1)/2;

  outimg->add_plane(heif_channel_Y, width, height, bpp);
  outimg->add_plane(heif_channel_Cb, cwidth, cheight, bpp);
  outimg->add_plane(heif_channel_Cr, cwidth, cheight, bpp);

  if (has_alpha) {
    outimg->add_plane(heif_channel_Alpha, width, height, bpp);
  }

  const uint16_t *in_r,*in_g,*in_b,*in_a;
  int in_r_stride=0, in_g_stride=0, in_b_stride=0, in_a_stride=0;

  uint16_t *out_y,*out_cb,*out_cr,*out_a;
  int out_y_stride=0, out_cb_stride=0, out_cr_stride=0, out_a_stride=0;

  in_r = (const uint16_t*)input->get_plane(heif_channel_R, &in_r_stride);
  in_g = (const uint16_t*)input->get_plane(heif_channel_G, &in_g_stride);
  in_b = (const uint16_t*)input->get_plane(heif_channel_B, &in_b_stride);
  out_y  = (uint16_t*)outimg->get_plane(heif_channel_Y,  &out_y_stride);
  out_cb = (uint16_t*)outimg->get_plane(heif_channel_Cb, &out_cb_stride);
  out_cr = (uint16_t*)outimg->get_plane(heif_channel_Cr, &out_cr_stride);

  if (has_alpha) {
    in_a = (const uint16_t*)input->get_plane(heif_channel_Alpha, &in_a_stride);
    out_a = (uint16_t*)outimg->get_plane(heif_channel_Alpha, &out_a_stride);
  }
  else {
    in_a = nullptr;
    out_a = nullptr;
  }

  in_r_stride /= 2;
  in_g_stride /= 2;
  in_b_stride /= 2;
  in_a_stride /= 2;
  out_y_stride /= 2;
  out_cb_stride /= 2;
  out_cr_stride /= 2;
  out_a_stride /= 2;

  uint16_t halfRange = (uint16_t)(1<<(bpp-1));
  int32_t fullRange = (1<<bpp)-1;

  int x,y;

  for (y=0;y<height;y++) {
    for (x=0;x<width;x++) {
      float r = in_r[y*in_r_stride + x];
      float g = in_g[y*in_g_stride + x];
      float b = in_b[y*in_b_stride + x];

      out_y[y*out_y_stride + x] = clip((int32_t)(r*0.299f + g*0.587f + b*0.114f), fullRange);
    }
  }

  for (y=0;y<height;y+=2) {
    for (x=0;x<width;x+=2) {
      float r = in_r[y*in_r_stride + x];
      float g = in_g[y*in_g_stride + x];
      float b = in_b[y*in_b_stride + x];

      out_cb[(y/2)*out_cb_stride + (x/2)] = clip(halfRange + (int32_t)(-r*0.168736f - g*0.331264f + b*0.5f), fullRange);
      out_cr[(y/2)*out_cr_stride + (x/2)] = clip(halfRange + (int32_t)(r*0.5f - g*0.418688f - b*0.081312f), fullRange);
    }
  }

  if (has_alpha) {
    for (y=0;y<height;y++) {
      memcpy(&out_a[y*out_a_stride], &in_a[y*in_a_stride], width*2);
    }
  }

  return outimg;
}






class Op_YCbCr420_to_RGB24 : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};


std::vector<ColorStateWithCost>
Op_YCbCr420_to_RGB24::state_after_conversion(ColorState input_state,
                                             ColorState target_state,
                                             ColorConversionOptions options)
{
  if (input_state.colorspace != heif_colorspace_YCbCr ||
      input_state.chroma != heif_chroma_420 ||
      input_state.bits_per_pixel != 8 ||
      input_state.has_alpha == true) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- convert to RGB

  output_state.colorspace = heif_colorspace_RGB;
  output_state.chroma = heif_chroma_interleaved_RGB;
  output_state.has_alpha = false;
  output_state.bits_per_pixel = 8;

  costs = { 0.5f, 0.0f, 0.0f };

  states.push_back({ output_state, costs });

  return states;
}


std::shared_ptr<HeifPixelImage>
Op_YCbCr420_to_RGB24::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                         ColorState target_state,
                                         ColorConversionOptions options)
{
  if (input->get_bits_per_pixel(heif_channel_Y) != 8 ||
      input->get_bits_per_pixel(heif_channel_Cb) != 8 ||
      input->get_bits_per_pixel(heif_channel_Cr) != 8) {
    return nullptr;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  int width = input->get_width();
  int height = input->get_height();

  outimg->create(width, height, heif_colorspace_RGB, heif_chroma_interleaved_24bit);

  outimg->add_plane(heif_channel_interleaved, width, height, 8);

  const uint8_t *in_y,*in_cb,*in_cr;
  int in_y_stride=0, in_cb_stride=0, in_cr_stride=0;

  uint8_t *out_p;
  int out_p_stride=0;

  in_y  = input->get_plane(heif_channel_Y,  &in_y_stride);
  in_cb = input->get_plane(heif_channel_Cb, &in_cb_stride);
  in_cr = input->get_plane(heif_channel_Cr, &in_cr_stride);
  out_p = outimg->get_plane(heif_channel_interleaved, &out_p_stride);

  int x,y;
  for (y=0;y<height;y++) {
    for (x=0;x<width;x++) {
      int yv = (in_y [y  *in_y_stride  + x] );
      int cb = (in_cb[y/2*in_cb_stride + x/2]-128);
      int cr = (in_cr[y/2*in_cr_stride + x/2]-128);

      out_p[y*out_p_stride + 3*x + 0] = clip(yv + ((359*cr)>>8));
      out_p[y*out_p_stride + 3*x + 1] = clip(yv - ((88*cb + 183*cr)>>8));
      out_p[y*out_p_stride + 3*x + 2] = clip(yv + ((454*cb)>>8));
    }
  }

  return outimg;
}






class Op_YCbCr420_to_RGB32 : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};


std::vector<ColorStateWithCost>
Op_YCbCr420_to_RGB32::state_after_conversion(ColorState input_state,
                                             ColorState target_state,
                                             ColorConversionOptions options)
{
  // Note: no input alpha channel required. It will be filled up with 0xFF.

  if (input_state.colorspace != heif_colorspace_YCbCr ||
      input_state.chroma != heif_chroma_420 ||
      input_state.bits_per_pixel != 8) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- convert to RGB

  output_state.colorspace = heif_colorspace_RGB;
  output_state.chroma = heif_chroma_interleaved_RGBA;
  output_state.has_alpha = true;
  output_state.bits_per_pixel = 8;

  costs = { 0.5f, 0.0f, 0.0f };

  states.push_back({ output_state, costs });

  return states;
}


std::shared_ptr<HeifPixelImage>
Op_YCbCr420_to_RGB32::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                         ColorState target_state,
                                         ColorConversionOptions options)
{
  if (input->get_bits_per_pixel(heif_channel_Y) != 8 ||
      input->get_bits_per_pixel(heif_channel_Cb) != 8 ||
      input->get_bits_per_pixel(heif_channel_Cr) != 8) {
    return nullptr;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  int width = input->get_width();
  int height = input->get_height();

  outimg->create(width, height, heif_colorspace_RGB, heif_chroma_interleaved_32bit);

  outimg->add_plane(heif_channel_interleaved, width, height, 8);

  const bool with_alpha = input->has_channel(heif_channel_Alpha);

  const uint8_t *in_y,*in_cb,*in_cr,*in_a = nullptr;
  int in_y_stride=0, in_cb_stride=0, in_cr_stride=0, in_a_stride=0;

  uint8_t *out_p;
  int out_p_stride=0;

  in_y  = input->get_plane(heif_channel_Y,  &in_y_stride);
  in_cb = input->get_plane(heif_channel_Cb, &in_cb_stride);
  in_cr = input->get_plane(heif_channel_Cr, &in_cr_stride);
  if (with_alpha) {
    in_a = input->get_plane(heif_channel_Alpha, &in_a_stride);
  }

  out_p = outimg->get_plane(heif_channel_interleaved, &out_p_stride);

  int x,y;
  for (y=0;y<height;y++) {
    for (x=0;x<width;x++) {

      int yv = (in_y [y  *in_y_stride  + x] );
      int cb = (in_cb[y/2*in_cb_stride + x/2]-128);
      int cr = (in_cr[y/2*in_cr_stride + x/2]-128);

      out_p[y*out_p_stride + 4*x + 0] = clip(yv + ((359*cr)>>8));
      out_p[y*out_p_stride + 4*x + 1] = clip(yv - ((88*cb + 183*cr)>>8));
      out_p[y*out_p_stride + 4*x + 2] = clip(yv + ((454*cb)>>8));


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





class Op_RGB_HDR_to_RRGGBBaa_BE : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};


std::vector<ColorStateWithCost>
Op_RGB_HDR_to_RRGGBBaa_BE::state_after_conversion(ColorState input_state,
                                                  ColorState target_state,
                                                  ColorConversionOptions options)
{
  // Note: no input alpha channel required. It will be filled up with 0xFF.

  if (input_state.colorspace != heif_colorspace_RGB ||
      input_state.chroma != heif_chroma_444 ||
      input_state.bits_per_pixel == 8) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- convert to RRGGBB_BE

  if (input_state.has_alpha == false) {
    output_state.colorspace = heif_colorspace_RGB;
    output_state.chroma = heif_chroma_interleaved_RRGGBB_BE;
    output_state.has_alpha = false;
    output_state.bits_per_pixel = input_state.bits_per_pixel;

    costs = { 0.5f, 0.0f, 0.0f };

    states.push_back({ output_state, costs });
  }


  // --- convert to RRGGBBAA_BE

  output_state.colorspace = heif_colorspace_RGB;
  output_state.chroma = heif_chroma_interleaved_RRGGBBAA_BE;
  output_state.has_alpha = true;
  output_state.bits_per_pixel = input_state.bits_per_pixel;

  costs = { 0.5f, 0.0f, 0.0f };

  states.push_back({ output_state, costs });


  return states;
}


std::shared_ptr<HeifPixelImage>
Op_RGB_HDR_to_RRGGBBaa_BE::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                              ColorState target_state,
                                              ColorConversionOptions options)
{
  if (input->get_bits_per_pixel(heif_channel_R) == 8 ||
      input->get_bits_per_pixel(heif_channel_G) == 8 ||
      input->get_bits_per_pixel(heif_channel_B) == 8) {
    return nullptr;
  }

  //int bpp = input->get_bits_per_pixel(heif_channel_R);

  bool has_alpha = input->has_channel(heif_channel_Alpha);

  if (has_alpha && input->get_bits_per_pixel(heif_channel_Alpha) == 8) {
    return nullptr;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  int width = input->get_width();
  int height = input->get_height();

  outimg->create(width, height, heif_colorspace_RGB,
                 target_state.has_alpha ? heif_chroma_interleaved_RRGGBBAA_BE : heif_chroma_interleaved_RRGGBB_BE);

  outimg->add_plane(heif_channel_interleaved, width, height, input->get_bits_per_pixel(heif_channel_R));

  const uint16_t *in_r,*in_g,*in_b,*in_a=nullptr;
  int in_r_stride=0, in_g_stride=0, in_b_stride=0, in_a_stride=0;

  uint8_t *out_p;
  int out_p_stride=0;

  in_r = (uint16_t*)input->get_plane(heif_channel_R, &in_r_stride);
  in_g = (uint16_t*)input->get_plane(heif_channel_G, &in_g_stride);
  in_b = (uint16_t*)input->get_plane(heif_channel_B, &in_b_stride);
  out_p = outimg->get_plane(heif_channel_interleaved, &out_p_stride);

  if (has_alpha) {
    in_a = (uint16_t*)input->get_plane(heif_channel_Alpha, &in_a_stride);
  }

  in_r_stride /= 2;
  in_g_stride /= 2;
  in_b_stride /= 2;
  in_a_stride /= 2;

  int x,y;
  for (y=0;y<height;y++) {

    if (has_alpha) {
      for (x=0;x<width;x++) {
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
      for (x=0;x<width;x++) {
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




class Op_RRGGBBaa_BE_to_RGB_HDR : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};


std::vector<ColorStateWithCost>
Op_RRGGBBaa_BE_to_RGB_HDR::state_after_conversion(ColorState input_state,
                                                  ColorState target_state,
                                                  ColorConversionOptions options)
{
  // Note: no input alpha channel required. It will be filled up with 0xFF.

  if (input_state.colorspace != heif_colorspace_RGB ||
      (input_state.chroma != heif_chroma_interleaved_RRGGBB_BE &&
       input_state.chroma != heif_chroma_interleaved_RRGGBBAA_BE) ||
      input_state.bits_per_pixel == 8) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- convert to RRGGBB_BE

  output_state.colorspace = heif_colorspace_RGB;
  output_state.chroma = heif_chroma_444;
  output_state.has_alpha = (input_state.chroma == heif_chroma_interleaved_RRGGBBAA_LE ||
                            input_state.chroma == heif_chroma_interleaved_RRGGBBAA_BE);
  output_state.bits_per_pixel = input_state.bits_per_pixel;

  costs = { 0.2f, 0.0f, 0.0f };

  states.push_back({ output_state, costs });


  return states;
}


std::shared_ptr<HeifPixelImage>
Op_RRGGBBaa_BE_to_RGB_HDR::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                              ColorState target_state,
                                              ColorConversionOptions options)
{
  bool has_alpha = (input->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE ||
                    input->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_BE);

  auto outimg = std::make_shared<HeifPixelImage>();

  int width = input->get_width();
  int height = input->get_height();

  outimg->create(width, height, heif_colorspace_RGB, heif_chroma_444);

  outimg->add_plane(heif_channel_R, width, height, input->get_bits_per_pixel(heif_channel_interleaved));
  outimg->add_plane(heif_channel_G, width, height, input->get_bits_per_pixel(heif_channel_interleaved));
  outimg->add_plane(heif_channel_B, width, height, input->get_bits_per_pixel(heif_channel_interleaved));

  if (has_alpha) {
    outimg->add_plane(heif_channel_Alpha, width, height, input->get_bits_per_pixel(heif_channel_interleaved));
  }

  const uint8_t *in_p;
  int in_p_stride=0;

  uint16_t *out_r,*out_g,*out_b,*out_a=nullptr;
  int out_r_stride=0, out_g_stride=0, out_b_stride=0, out_a_stride=0;

  in_p = input->get_plane(heif_channel_interleaved, &in_p_stride);

  out_r = (uint16_t*)outimg->get_plane(heif_channel_R, &out_r_stride);
  out_g = (uint16_t*)outimg->get_plane(heif_channel_G, &out_g_stride);
  out_b = (uint16_t*)outimg->get_plane(heif_channel_B, &out_b_stride);

  if (has_alpha) {
    out_a = (uint16_t*)outimg->get_plane(heif_channel_Alpha, &out_a_stride);
  }

  out_r_stride /= 2;
  out_g_stride /= 2;
  out_b_stride /= 2;
  out_a_stride /= 2;

  int x,y;
  for (y=0;y<height;y++) {

    for (x=0;x<width;x++) {
      uint16_t r = (uint16_t)((in_p[y*in_p_stride + 8*x + 0] << 8) |
                              in_p[y*in_p_stride + 8*x + 1]);
      uint16_t g = (uint16_t)((in_p[y*in_p_stride + 8*x + 2] << 8) |
                              in_p[y*in_p_stride + 8*x + 3]);
      uint16_t b = (uint16_t)((in_p[y*in_p_stride + 8*x + 4] << 8) |
                              in_p[y*in_p_stride + 8*x + 5]);

      out_r[x + y*out_r_stride] = r;
      out_g[x + y*out_g_stride] = g;
      out_b[x + y*out_b_stride] = b;

      if (has_alpha) {
        uint16_t a = (uint16_t)((in_p[y*in_p_stride + 8*x + 6] << 8) |
                                in_p[y*in_p_stride + 8*x + 7]);

        out_a[x + y*out_a_stride] = a;
      }
    }
  }

  return outimg;
}




class Op_RRGGBBaa_swap_endianness : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};


std::vector<ColorStateWithCost>
Op_RRGGBBaa_swap_endianness::state_after_conversion(ColorState input_state,
                                                    ColorState target_state,
                                                    ColorConversionOptions options)
{
  // Note: no input alpha channel required. It will be filled up with 0xFF.

  if (input_state.colorspace != heif_colorspace_RGB ||
      (input_state.chroma != heif_chroma_interleaved_RRGGBB_LE &&
       input_state.chroma != heif_chroma_interleaved_RRGGBB_BE &&
       input_state.chroma != heif_chroma_interleaved_RRGGBBAA_LE &&
       input_state.chroma != heif_chroma_interleaved_RRGGBBAA_BE) ||
      input_state.bits_per_pixel == 8) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- swap RRGGBB

  if (input_state.chroma == heif_chroma_interleaved_RRGGBB_LE ||
      input_state.chroma == heif_chroma_interleaved_RRGGBB_BE) {
    output_state.colorspace = heif_colorspace_RGB;

    if (input_state.chroma == heif_chroma_interleaved_RRGGBB_LE) {
      output_state.chroma = heif_chroma_interleaved_RRGGBB_BE;
    }
    else {
      output_state.chroma = heif_chroma_interleaved_RRGGBB_LE;
    }

    output_state.has_alpha = false;
    output_state.bits_per_pixel = input_state.bits_per_pixel;

    costs = { 0.1f, 0.0f, 0.0f };

    states.push_back({ output_state, costs });
  }


  // --- swap RRGGBBAA

  if (input_state.chroma == heif_chroma_interleaved_RRGGBBAA_LE ||
      input_state.chroma == heif_chroma_interleaved_RRGGBBAA_BE) {
    output_state.colorspace = heif_colorspace_RGB;

    if (input_state.chroma == heif_chroma_interleaved_RRGGBBAA_LE) {
      output_state.chroma = heif_chroma_interleaved_RRGGBBAA_BE;
    }
    else {
      output_state.chroma = heif_chroma_interleaved_RRGGBBAA_LE;
    }

    output_state.has_alpha = true;
    output_state.bits_per_pixel = input_state.bits_per_pixel;

    costs = { 0.1f, 0.0f, 0.0f };

    states.push_back({ output_state, costs });
  }


  return states;
}


std::shared_ptr<HeifPixelImage>
Op_RRGGBBaa_swap_endianness::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                                ColorState target_state,
                                                ColorConversionOptions options)
{
  auto outimg = std::make_shared<HeifPixelImage>();

  int width = input->get_width();
  int height = input->get_height();

  switch (input->get_chroma_format()) {
  case heif_chroma_interleaved_RRGGBB_LE:
    outimg->create(width, height, heif_colorspace_RGB, heif_chroma_interleaved_RRGGBB_BE);
    break;
  case heif_chroma_interleaved_RRGGBB_BE:
    outimg->create(width, height, heif_colorspace_RGB, heif_chroma_interleaved_RRGGBB_LE);
    break;
  case heif_chroma_interleaved_RRGGBBAA_LE:
    outimg->create(width, height, heif_colorspace_RGB, heif_chroma_interleaved_RRGGBBAA_BE);
    break;
  case heif_chroma_interleaved_RRGGBBAA_BE:
    outimg->create(width, height, heif_colorspace_RGB, heif_chroma_interleaved_RRGGBBAA_LE);
    break;
  default:
    return nullptr;
  }

  outimg->add_plane(heif_channel_interleaved, width, height,
                    input->get_bits_per_pixel(heif_channel_interleaved));

  const uint8_t *in_p=nullptr;
  int in_p_stride=0;

  uint8_t *out_p;
  int out_p_stride=0;

  in_p = input->get_plane(heif_channel_interleaved, &in_p_stride);
  out_p = outimg->get_plane(heif_channel_interleaved, &out_p_stride);

  int n_bytes = std::min(in_p_stride, out_p_stride);

  int x,y;
  for (y=0;y<height;y++) {
    for (x=0;x<n_bytes;x+=2) {
      out_p[y*out_p_stride + x + 0] = in_p[y*in_p_stride + x + 1];
      out_p[y*out_p_stride + x + 1] = in_p[y*in_p_stride + x + 0];
    }
  }

  return outimg;
}




class Op_mono_to_YCbCr420 : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};


std::vector<ColorStateWithCost>
Op_mono_to_YCbCr420::state_after_conversion(ColorState input_state,
                                            ColorState target_state,
                                            ColorConversionOptions options)
{
  // Note: no input alpha channel required. It will be filled up with 0xFF.

  if (input_state.colorspace != heif_colorspace_monochrome ||
      input_state.chroma != heif_chroma_monochrome ||
      input_state.bits_per_pixel != 8) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- convert to YCbCr420

  output_state.colorspace = heif_colorspace_YCbCr;
  output_state.chroma = heif_chroma_420;
  output_state.has_alpha = input_state.has_alpha;
  output_state.bits_per_pixel = 8;

  costs = { 0.1f, 0.0f, 0.0f };

  states.push_back({ output_state, costs });

  return states;
}


std::shared_ptr<HeifPixelImage>
Op_mono_to_YCbCr420::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                        ColorState target_state,
                                        ColorConversionOptions options)
{
  auto outimg = std::make_shared<HeifPixelImage>();

  int width = input->get_width();
  int height = input->get_height();

  outimg->create(width, height, heif_colorspace_YCbCr, heif_chroma_420);

  int chroma_width  = (width+1)/2;
  int chroma_height = (height+1)/2;

  outimg->add_plane(heif_channel_Y,  width, height, 8);
  outimg->add_plane(heif_channel_Cb, chroma_width, chroma_height, 8);
  outimg->add_plane(heif_channel_Cr, chroma_width, chroma_height, 8);

  bool has_alpha = input->has_channel(heif_channel_Alpha);
  if (has_alpha) {
    outimg->add_plane(heif_channel_Alpha, width, height, 8);
  }


  uint8_t *out_cb,*out_cr,*out_y;
  int out_cb_stride=0, out_cr_stride=0, out_y_stride=0;

  const uint8_t *in_y;
  int in_y_stride=0;

  in_y  = input->get_plane(heif_channel_Y,  &in_y_stride);

  out_y  = outimg->get_plane(heif_channel_Y,  &out_y_stride);
  out_cb = outimg->get_plane(heif_channel_Cb, &out_cb_stride);
  out_cr = outimg->get_plane(heif_channel_Cr, &out_cr_stride);

  memset(out_cb, 128, out_cb_stride*chroma_height);
  memset(out_cr, 128, out_cr_stride*chroma_height);

  for (int y=0;y<height;y++) {
    memcpy(out_y + y*out_y_stride,
           in_y + y*in_y_stride,
           width);
  }

  if (has_alpha) {
    const uint8_t *in_a;
    uint8_t *out_a;
    int in_a_stride=0;
    int out_a_stride=0;

    in_a = input->get_plane(heif_channel_Alpha, &in_a_stride);
    out_a = outimg->get_plane(heif_channel_Alpha, &out_a_stride);

    for (int y=0;y<height;y++) {
      memcpy(&out_a[y*out_a_stride], &in_a[y*in_a_stride], width);
    }
  }

  return outimg;
}




class Op_mono_to_RGB24_32 : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};


std::vector<ColorStateWithCost>
Op_mono_to_RGB24_32::state_after_conversion(ColorState input_state,
                                            ColorState target_state,
                                            ColorConversionOptions options)
{
  // Note: no input alpha channel required. It will be filled up with 0xFF.

  if ((input_state.colorspace != heif_colorspace_monochrome &&
       input_state.colorspace != heif_colorspace_YCbCr) ||
      input_state.chroma != heif_chroma_monochrome ||
      input_state.bits_per_pixel != 8) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- convert to RGB24

  if (input_state.has_alpha == false) {
    output_state.colorspace = heif_colorspace_RGB;
    output_state.chroma = heif_chroma_interleaved_RGB;
    output_state.has_alpha = false;
    output_state.bits_per_pixel = 8;

    costs = { 0.1f, 0.0f, 0.0f };

    states.push_back({ output_state, costs });
  }


  // --- convert to RGB32

  output_state.colorspace = heif_colorspace_RGB;
  output_state.chroma = heif_chroma_interleaved_RGBA;
  output_state.has_alpha = true;
  output_state.bits_per_pixel = 8;

  costs = { 0.15f, 0.0f, 0.0f };

  states.push_back({ output_state, costs });

  return states;
}


std::shared_ptr<HeifPixelImage>
Op_mono_to_RGB24_32::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                        ColorState target_state,
                                        ColorConversionOptions options)
{
  int width = input->get_width();
  int height = input->get_height();

  if (input->get_bits_per_pixel(heif_channel_Y) != 8) {
    return nullptr;
  }

  auto outimg = std::make_shared<HeifPixelImage>();

  bool has_alpha = input->has_channel(heif_channel_Alpha);

  if (target_state.has_alpha) {
    outimg->create(width, height, heif_colorspace_RGB, heif_chroma_interleaved_32bit);
  } else {
    outimg->create(width, height, heif_colorspace_RGB, heif_chroma_interleaved_24bit);
  }

  outimg->add_plane(heif_channel_interleaved, width, height, 8);

  const uint8_t *in_y, *in_a;
  int in_y_stride=0, in_a_stride;

  uint8_t *out_p;
  int out_p_stride=0;

  in_y = input->get_plane(heif_channel_Y, &in_y_stride);
  if (has_alpha) {
    in_a = input->get_plane(heif_channel_Alpha, &in_a_stride);
  }

  out_p = outimg->get_plane(heif_channel_interleaved, &out_p_stride);

  int x,y;
  for (y=0;y<height;y++) {
    if (target_state.has_alpha==false) {
      for (x=0;x<width;x++) {
        uint8_t v = in_y[x + y*in_y_stride];
        out_p[y*out_p_stride + 3*x + 0] = v;
        out_p[y*out_p_stride + 3*x + 1] = v;
        out_p[y*out_p_stride + 3*x + 2] = v;
      }
    }
    else if (has_alpha) {
      for (x=0;x<width;x++) {
        uint8_t v = in_y[x + y*in_y_stride];
        out_p[y*out_p_stride + 4*x + 0] = v;
        out_p[y*out_p_stride + 4*x + 1] = v;
        out_p[y*out_p_stride + 4*x + 2] = v;
        out_p[y*out_p_stride + 4*x + 3] = in_a[x + y*in_a_stride];
      }
    }
    else {
      for (x=0;x<width;x++) {
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




class Op_RGB24_32_to_YCbCr420 : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};


std::vector<ColorStateWithCost>
Op_RGB24_32_to_YCbCr420::state_after_conversion(ColorState input_state,
                                                ColorState target_state,
                                                ColorConversionOptions options)
{
  // Note: no input alpha channel required. It will be filled up with 0xFF.

  if (input_state.colorspace != heif_colorspace_RGB ||
      (input_state.chroma != heif_chroma_interleaved_RGB &&
       input_state.chroma != heif_chroma_interleaved_RGBA)) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- convert RGB24

  if (input_state.chroma == heif_chroma_interleaved_RGB) {
    output_state.colorspace = heif_colorspace_YCbCr;
    output_state.chroma = heif_chroma_420;
    output_state.has_alpha = false;
    output_state.bits_per_pixel = 8;

    costs = { 0.75f, 0.5f, 0.0f };  // quality not good since we subsample chroma without filtering

    states.push_back({ output_state, costs });
  }


  // --- convert RGB32

  if (input_state.chroma == heif_chroma_interleaved_RGBA) {
    output_state.colorspace = heif_colorspace_YCbCr;
    output_state.chroma = heif_chroma_420;
    output_state.has_alpha = true;
    output_state.bits_per_pixel = 8;

    costs = { 0.75f, 0.5f, 0.0f };  // quality not good since we subsample chroma without filtering

    states.push_back({ output_state, costs });
  }

  return states;
}


static inline uint8_t clip(float fx)
{
  int x = static_cast<int>(fx);
  if (x<0) return 0;
  if (x>255) return 255;
  return static_cast<uint8_t>(x);
}


std::shared_ptr<HeifPixelImage>
Op_RGB24_32_to_YCbCr420::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                            ColorState target_state,
                                            ColorConversionOptions options)
{
  int width = input->get_width();
  int height = input->get_height();

  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(width, height, heif_colorspace_YCbCr, heif_chroma_420);

  int chroma_width  = (width+1)/2;
  int chroma_height = (height+1)/2;

  const bool has_alpha = (input->get_chroma_format() == heif_chroma_interleaved_32bit);

  outimg->add_plane(heif_channel_Y,  width, height, 8);
  outimg->add_plane(heif_channel_Cb, chroma_width, chroma_height, 8);
  outimg->add_plane(heif_channel_Cr, chroma_width, chroma_height, 8);

  if (has_alpha) {
    outimg->add_plane(heif_channel_Alpha, width, height, 8);
  }

  uint8_t *out_cb,*out_cr,*out_y, *out_a;
  int out_cb_stride=0, out_cr_stride=0, out_y_stride=0, out_a_stride=0;

  const uint8_t *in_p;
  int in_stride=0;

  in_p  = input->get_plane(heif_channel_interleaved,  &in_stride);

  out_y  = outimg->get_plane(heif_channel_Y,  &out_y_stride);
  out_cb = outimg->get_plane(heif_channel_Cb, &out_cb_stride);
  out_cr = outimg->get_plane(heif_channel_Cr, &out_cr_stride);

  if (has_alpha) {
    out_a = outimg->get_plane(heif_channel_Alpha, &out_a_stride);
  }


  if (!has_alpha) {
    for (int y=0;y<height;y++) {
      for (int x=0;x<width;x++) {
        uint8_t r = in_p[y*in_stride + x*3 +0];
        uint8_t g = in_p[y*in_stride + x*3 +1];
        uint8_t b = in_p[y*in_stride + x*3 +2];
        out_y[y*out_y_stride + x] = clip(r*0.299f + g*0.587f + b*0.114f);
      }
    }

    for (int y=0;y<height;y+=2) {
      for (int x=0;x<width;x+=2) {
        uint8_t r = in_p[y*in_stride + x*3 +0];
        uint8_t g = in_p[y*in_stride + x*3 +1];
        uint8_t b = in_p[y*in_stride + x*3 +2];
        out_cb[(y/2)*out_cb_stride + (x/2)] = clip(128 - r*0.168736f - g*0.331264f + b*0.5f);
        out_cr[(y/2)*out_cb_stride + (x/2)] = clip(128 + r*0.5f - g*0.418688f - b*0.081312f);
      }
    }
  }
  else {
    for (int y=0;y<height;y++) {
      for (int x=0;x<width;x++) {
        uint8_t r = in_p[y*in_stride + x*4 +0];
        uint8_t g = in_p[y*in_stride + x*4 +1];
        uint8_t b = in_p[y*in_stride + x*4 +2];
        uint8_t a = in_p[y*in_stride + x*4 +3];
        out_y[y*out_y_stride + x] = clip(r*0.299f + g*0.587f + b*0.114f);

        // alpha
        out_a[y*out_a_stride + x] = a;
      }
    }

    for (int y=0;y<height;y+=2) {
      for (int x=0;x<width;x+=2) {
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




class Op_drop_alpha_plane : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};


std::vector<ColorStateWithCost>
Op_drop_alpha_plane::state_after_conversion(ColorState input_state,
                                            ColorState target_state,
                                            ColorConversionOptions options)
{
  // only drop alpha plane if it is not needed in output

  if ((input_state.chroma != heif_chroma_monochrome &&
       input_state.chroma != heif_chroma_420 &&
       input_state.chroma != heif_chroma_422 &&
       input_state.chroma != heif_chroma_444) ||
      input_state.has_alpha == false ||
      target_state.has_alpha == true) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- drop alpha plane

  output_state = input_state;
  output_state.has_alpha = false;

  costs = { 0.1f, 0.0f, 0.0f };

  states.push_back({ output_state, costs });

  return states;
}


std::shared_ptr<HeifPixelImage>
Op_drop_alpha_plane::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                        ColorState target_state,
                                        ColorConversionOptions options)
{
  int width = input->get_width();
  int height = input->get_height();

  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(width, height,
                 input->get_colorspace(),
                 input->get_chroma_format());

  for (heif_channel channel : { heif_channel_Y,
        heif_channel_Cb,
        heif_channel_Cr,
        heif_channel_R,
        heif_channel_G,
        heif_channel_B }) {
    if (input->has_channel(channel)) {
      outimg->copy_new_plane_from(input, channel, channel);
    }
  }

  return outimg;
}


class Op_to_hdr_planes : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};


std::vector<ColorStateWithCost>
Op_to_hdr_planes::state_after_conversion(ColorState input_state,
                                         ColorState target_state,
                                         ColorConversionOptions options)
{
  if ((input_state.chroma != heif_chroma_monochrome &&
       input_state.chroma != heif_chroma_420 &&
       input_state.chroma != heif_chroma_422 &&
       input_state.chroma != heif_chroma_444) ||
      input_state.bits_per_pixel != 8) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- increase bit depth

  output_state = input_state;
  output_state.bits_per_pixel = target_state.bits_per_pixel;

  costs = { 0.2f, 0.0f, 0.5f };

  states.push_back({ output_state, costs });

  return states;
}


std::shared_ptr<HeifPixelImage>
Op_to_hdr_planes::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                     ColorState target_state,
                                     ColorConversionOptions options)
{
  int width = input->get_width();
  int height = input->get_height();

  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(width, height,
                 input->get_colorspace(),
                 input->get_chroma_format());

  for (heif_channel channel : { heif_channel_Y,
        heif_channel_Cb,
        heif_channel_Cr,
        heif_channel_R,
        heif_channel_G,
        heif_channel_B,
        heif_channel_Alpha }) {
    if (input->has_channel(channel)) {
      int width = input->get_width(channel);
      int height = input->get_height(channel);
      outimg->add_plane(channel, width, height, target_state.bits_per_pixel);

      int input_bits = input->get_bits_per_pixel(channel);
      int output_bits = target_state.bits_per_pixel;

      int shift1 = output_bits - input_bits;
      int shift2 = 8-shift1;

      const uint8_t* p_in;
      int stride_in;
      p_in = input->get_plane(channel, &stride_in);

      uint16_t* p_out;
      int stride_out;
      p_out = (uint16_t*)outimg->get_plane(channel, &stride_out);

      for (int y=0;y<height;y++)
        for (int x=0;x<width;x++) {
          int in = p_in[y*stride_in+x];
          p_out[y*stride_out+x] = (uint16_t)((in<<shift1) | (in>>shift2));
        }
    }
  }

  return outimg;
}








class Op_RRGGBBxx_HDR_to_YCbCr420 : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};


std::vector<ColorStateWithCost>
Op_RRGGBBxx_HDR_to_YCbCr420::state_after_conversion(ColorState input_state,
                                                    ColorState target_state,
                                                    ColorConversionOptions options)
{
    if (input_state.colorspace != heif_colorspace_RGB ||
        !(input_state.chroma == heif_chroma_interleaved_RRGGBB_BE ||
          input_state.chroma == heif_chroma_interleaved_RRGGBB_LE ||
          input_state.chroma == heif_chroma_interleaved_RRGGBBAA_BE ||
          input_state.chroma == heif_chroma_interleaved_RRGGBBAA_LE) ||
        input_state.bits_per_pixel == 8) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- convert to YCbCr

  output_state.colorspace = heif_colorspace_YCbCr;
  output_state.chroma = heif_chroma_420;
  output_state.has_alpha = input_state.has_alpha;  // we generate an alpha plane if the source contains data
  output_state.bits_per_pixel = input_state.bits_per_pixel;

  costs = { 0.5f, 0.0f, 0.0f };

  states.push_back({ output_state, costs });

  return states;
}


std::shared_ptr<HeifPixelImage>
Op_RRGGBBxx_HDR_to_YCbCr420::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                                ColorState target_state,
                                                ColorConversionOptions options)
{
  int width = input->get_width();
  int height = input->get_height();

  int bpp = input->get_bits_per_pixel(heif_channel_interleaved);

  bool has_alpha = (input->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_BE ||
                    input->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE);

  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(width, height, heif_colorspace_YCbCr, heif_chroma_420);

  int bytesPerPixel = has_alpha ? 8 : 6;

  int cwidth = (width+1)/2;
  int cheight = (height+1)/2;

  outimg->add_plane(heif_channel_Y, width, height, bpp);
  outimg->add_plane(heif_channel_Cb, cwidth, cheight, bpp);
  outimg->add_plane(heif_channel_Cr, cwidth, cheight, bpp);

  if (has_alpha) {
    outimg->add_plane(heif_channel_Alpha, width, height, bpp);
  }

  const uint8_t *in_p;
  int in_p_stride=0;

  uint16_t *out_y,*out_cb,*out_cr,*out_a = nullptr;
  int out_y_stride=0, out_cb_stride=0, out_cr_stride=0, out_a_stride=0;

  in_p = input->get_plane(heif_channel_interleaved, &in_p_stride);
  out_y  = (uint16_t*)outimg->get_plane(heif_channel_Y,  &out_y_stride);
  out_cb = (uint16_t*)outimg->get_plane(heif_channel_Cb, &out_cb_stride);
  out_cr = (uint16_t*)outimg->get_plane(heif_channel_Cr, &out_cr_stride);

  if (has_alpha) {
    out_a = (uint16_t*)outimg->get_plane(heif_channel_Alpha, &out_a_stride);
  }


  // adapt stride as we are pointing to 16bit integers
  out_y_stride /= 2;
  out_cb_stride /= 2;
  out_cr_stride /= 2;
  out_a_stride /= 2;

  uint16_t halfRange = (uint16_t)(1<<(bpp-1));
  int32_t fullRange = (1<<bpp)-1;

  // le=1 for little endian, le=0 for big endian
  int le = (input->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE ||
            input->get_chroma_format() == heif_chroma_interleaved_RRGGBB_LE) ? 1 : 0;

  for (int y=0;y<height;y++) {
    for (int x=0;x<width;x++) {

      const uint8_t* in = &in_p[y*in_p_stride + bytesPerPixel*x];

      int r = (in[0+le]<<8) | in[1-le];
      int g = (in[2+le]<<8) | in[3-le];
      int b = (in[4+le]<<8) | in[5-le];

      out_y[y * out_y_stride + x] = clip((r*4899 + g*9617 + b*1868)>>14, fullRange);

      if (has_alpha) {
        uint16_t a = (uint16_t)((in[6+le]<<8) | in[7-le]);
        out_a[y*out_a_stride + x] = a;
      }
    }
  }

  for (int y=0;y<height;y+=2) {
    for (int x=0;x<width;x+=2) {
      const uint8_t* in = &in_p[y*in_p_stride + bytesPerPixel*x];

      int r = (in[0+le]<<8) | in[1-le];
      int g = (in[2+le]<<8) | in[3-le];
      int b = (in[4+le]<<8) | in[5-le];

      out_cb[(y/2)*out_cb_stride + (x/2)] = clip(halfRange + ((-r*2765 - g*5427 + (b<<13))>>14), fullRange);
      out_cr[(y/2)*out_cr_stride + (x/2)] = clip(halfRange + (((r<<13) - g*6860 - b*1332)>>14), fullRange);
    }
  }


  return outimg;
}


class Op_YCbCr420_to_RRGGBBaa : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};


std::vector<ColorStateWithCost>
Op_YCbCr420_to_RRGGBBaa::state_after_conversion(ColorState input_state,
                                                ColorState target_state,
                                                ColorConversionOptions options)
{
  if (input_state.colorspace != heif_colorspace_YCbCr ||
      input_state.chroma != heif_chroma_420 ||
      input_state.bits_per_pixel == 8) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- convert to YCbCr

  output_state.colorspace = heif_colorspace_RGB;
  output_state.chroma = (input_state.has_alpha ?
                         heif_chroma_interleaved_RRGGBBAA_LE : heif_chroma_interleaved_RRGGBB_LE);
  output_state.has_alpha = input_state.has_alpha;
  output_state.bits_per_pixel = input_state.bits_per_pixel;

  costs = { 0.5f, 0.0f, 0.0f };

  states.push_back({ output_state, costs });


  output_state.colorspace = heif_colorspace_RGB;
  output_state.chroma = (input_state.has_alpha ?
                         heif_chroma_interleaved_RRGGBBAA_BE : heif_chroma_interleaved_RRGGBB_BE);
  output_state.has_alpha = input_state.has_alpha;
  output_state.bits_per_pixel = input_state.bits_per_pixel;

  costs = {0.5f, 0.0f, 0.0f};

  states.push_back({output_state, costs});

  return states;
}


std::shared_ptr<HeifPixelImage>
Op_YCbCr420_to_RRGGBBaa::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                                ColorState target_state,
                                                ColorConversionOptions options)
{
  int width = input->get_width();
  int height = input->get_height();

  int bpp = input->get_bits_per_pixel(heif_channel_Y);
  bool has_alpha = input->has_channel(heif_channel_Alpha);

  int le = (target_state.chroma == heif_chroma_interleaved_RRGGBB_LE ||
            target_state.chroma == heif_chroma_interleaved_RRGGBBAA_LE) ? 1 : 0;

  auto outimg = std::make_shared<HeifPixelImage>();
  outimg->create(width, height, heif_colorspace_RGB, target_state.chroma);

  int bytesPerPixel = has_alpha ? 8 : 6;

  outimg->add_plane(heif_channel_interleaved, width, height, bpp);

  if (has_alpha) {
    outimg->add_plane(heif_channel_Alpha, width, height, bpp);
  }

  uint8_t *out_p;
  int out_p_stride=0;

  const uint16_t *in_y,*in_cb,*in_cr,*in_a = nullptr;
  int in_y_stride=0, in_cb_stride=0, in_cr_stride=0, in_a_stride=0;

  out_p = outimg->get_plane(heif_channel_interleaved, &out_p_stride);
  in_y  = (uint16_t*)input->get_plane(heif_channel_Y,  &in_y_stride);
  in_cb = (uint16_t*)input->get_plane(heif_channel_Cb, &in_cb_stride);
  in_cr = (uint16_t*)input->get_plane(heif_channel_Cr, &in_cr_stride);

  if (has_alpha) {
    in_a = (uint16_t*)input->get_plane(heif_channel_Alpha, &in_a_stride);
  }

  int maxval = (1<<bpp)-1;

  for (int y=0;y<height;y++) {
    for (int x=0;x<width;x++) {

        int y_ = in_y[y*in_y_stride/2 + x];
        int cb = in_cb[y/2*in_cb_stride/2 + x/2] - (1<<(bpp-1));
        int cr = in_cr[y/2*in_cr_stride/2 + x/2] - (1<<(bpp-1));

        int r = clip((int16_t)(y_ + 1.40200 * cr), maxval);
        int g = clip((int16_t) (y_ - 0.34414 * cb - 0.71414*cr), maxval);
        int b = clip(int16_t(y_ + 1.77200 * cb), maxval);


        out_p[y*out_p_stride + bytesPerPixel*x + 0+le] = (uint8_t)(r >> 8);
        out_p[y*out_p_stride + bytesPerPixel*x + 2+le] = (uint8_t)(g >> 8);
        out_p[y*out_p_stride + bytesPerPixel*x + 4+le] = (uint8_t)(b >> 8);

        out_p[y*out_p_stride + bytesPerPixel*x + 1-le] = (uint8_t)(r & 0xff);
        out_p[y*out_p_stride + bytesPerPixel*x + 3-le] = (uint8_t)(g & 0xff);
        out_p[y*out_p_stride + bytesPerPixel*x + 5-le] = (uint8_t)(b & 0xff);

        if(has_alpha) {
          out_p[y*out_p_stride + 8*x + 6+le] = (uint8_t)(in_a[y*in_a_stride/2+x] >> 8);
          out_p[y*out_p_stride + 8*x + 7-le] = (uint8_t)(in_a[y*in_a_stride/2+x] & 0xff);
        }
    }
  }


  return outimg;
}




struct Node {
  Node() = default;
  Node(int prev, const std::shared_ptr<ColorConversionOperation>& _op, ColorStateWithCost state) {
    prev_processed_idx = prev;
    op = _op;
    color_state = state;
  }

  int prev_processed_idx = -1;
  std::shared_ptr<ColorConversionOperation> op;
  ColorStateWithCost color_state;
};


bool ColorConversionPipeline::construct_pipeline(ColorState input_state,
                                                 ColorState target_state,
                                                 ColorConversionOptions options)
{
  m_operations.clear();

  m_target_state = target_state;
  m_options = options;

  if (input_state == target_state) {
    return true;
  }

  std::vector<std::shared_ptr<ColorConversionOperation>> ops;
  ops.push_back(std::make_shared<Op_RGB_to_RGB24_32>());
  ops.push_back(std::make_shared<Op_YCbCr420_to_RGB_8bit>());
  ops.push_back(std::make_shared<Op_YCbCr420_to_RGB_16bit>());
  ops.push_back(std::make_shared<Op_YCbCr420_to_RGB24>());
  ops.push_back(std::make_shared<Op_YCbCr420_to_RGB32>());
  ops.push_back(std::make_shared<Op_RGB_HDR_to_RRGGBBaa_BE>());
  ops.push_back(std::make_shared<Op_mono_to_YCbCr420>());
  ops.push_back(std::make_shared<Op_mono_to_RGB24_32>());
  ops.push_back(std::make_shared<Op_RRGGBBaa_swap_endianness>());
  ops.push_back(std::make_shared<Op_RRGGBBaa_BE_to_RGB_HDR>());
  ops.push_back(std::make_shared<Op_RGB24_32_to_YCbCr420>());
  ops.push_back(std::make_shared<Op_RGB_HDR_to_YCbCr420>());
  ops.push_back(std::make_shared<Op_drop_alpha_plane>());
  ops.push_back(std::make_shared<Op_to_hdr_planes>());
  ops.push_back(std::make_shared<Op_RRGGBBxx_HDR_to_YCbCr420>());
  ops.push_back(std::make_shared<Op_YCbCr420_to_RRGGBBaa>());


  // --- Dijkstra search for the minimum-cost conversion pipeline

  std::vector<Node> processed_states;
  std::vector<Node> border_states;
  border_states.push_back({ -1, nullptr, { input_state, ColorConversionCosts() }});

  while (!border_states.empty()) {
    size_t minIdx;
    float minCost;
    for (size_t i=0;i<border_states.size();i++) {
      float cost = border_states[i].color_state.costs.total(options.criterion);
      if (i==0 || cost<minCost) {
        minIdx = i;
        minCost = cost;
      }
    }


    // move minimum-cost border_state into processed_states

    processed_states.push_back(border_states[minIdx]);

    border_states[minIdx] = border_states.back();
    border_states.pop_back();

    if (processed_states.back().color_state.color_state == target_state) {
      // end-state found, backtrack path to find conversion pipeline

      size_t idx = processed_states.size()-1;
      int len = 0;
      while (idx>0) {
        idx = processed_states[idx].prev_processed_idx;
        len++;
      }

      m_operations.resize(len);

      idx = processed_states.size()-1;
      int step = 0;
      while (idx>0) {
        m_operations[len-1-step] = processed_states[idx].op;
        //printf("cost: %f\n",processed_states[idx].color_state.costs.total(options.criterion));
        idx = processed_states[idx].prev_processed_idx;
        step++;
      }

#if DEBUG_ME
      debug_dump_pipeline();
#endif

      return true;
    }


    // expand the node with minimum cost

    for (size_t i=0;i<ops.size();i++) {
      auto out_states = ops[i]->state_after_conversion(processed_states.back().color_state.color_state,
                                                       target_state,
                                                       options);
      for (const auto& out_state : out_states) {
        bool state_exists = false;
        for (const auto& s : processed_states) {
          if (s.color_state.color_state==out_state.color_state) {
            state_exists = true;
            break;
          }
        }

        if (!state_exists) {
          for (auto& s : border_states) {
            if (s.color_state.color_state==out_state.color_state) {
              state_exists = true;

              // if we reached the same border node with a lower cost, replace the border node

              ColorConversionCosts new_op_costs = out_state.costs + processed_states.back().color_state.costs;

              if (s.color_state.costs.total(options.criterion) > new_op_costs.total(options.criterion)) {
                s = { (int)(processed_states.size()-1),
                      ops[i],
                      out_state };

                s.color_state.costs = new_op_costs;
              }
              break;
            }
          }
        }


        // enter the new output state into the list of border states

        if (!state_exists) {
          ColorStateWithCost s = out_state;
          s.costs = s.costs + processed_states.back().color_state.costs;

          border_states.push_back({ (int)(processed_states.size()-1), ops[i], s });
        }
      }
    }
  }

  return false;
}


void ColorConversionPipeline::debug_dump_pipeline() const
{
  for (const auto& op : m_operations) {
    std::cerr << "> " << typeid(*op).name() << "\n";
  }
}


std::shared_ptr<HeifPixelImage> ColorConversionPipeline::convert_image(const std::shared_ptr<HeifPixelImage>& input)
{
  std::shared_ptr<HeifPixelImage> in = input;
  std::shared_ptr<HeifPixelImage> out = in;

  for (const auto& op : m_operations) {
    out = op->convert_colorspace(in, m_target_state, m_options);
    assert(out);

    in = out;
  }

  return out;
}
