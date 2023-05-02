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

#include "chroma_sampling.h"
#include <cstring>


template<class Pixel>
std::vector<ColorStateWithCost>
Op_YCbCr444_to_YCbCr420_average<Pixel>::state_after_conversion(const ColorState& input_state,
                                                               const ColorState& target_state,
                                                               const heif_color_conversion_options& options) const
{
  if (input_state.colorspace != heif_colorspace_YCbCr) {
    return {};
  }

  if (input_state.chroma != heif_chroma_444) {
    return {};
  }

  // this Op only implements the averaging algorithm

  if (options.preferred_chroma_downsampling_algorithm != heif_chroma_downsampling_average) {
    return {};
  }

  bool hdr = !std::is_same<Pixel, uint8_t>::value;

  if ((input_state.bits_per_pixel != 8) != hdr) {
    return {};
  }

  if (input_state.nclx_profile) {
    if (input_state.nclx_profile->get_matrix_coefficients() == 0) {
      return {};
    }
  }

  if (target_state.chroma != heif_chroma_420) {
    return {};
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;

  // --- convert to 4:2:0

  output_state.colorspace = heif_colorspace_YCbCr;
  output_state.chroma = heif_chroma_420;
  output_state.has_alpha = input_state.has_alpha;  // we simply keep the old alpha plane
  output_state.bits_per_pixel = input_state.bits_per_pixel;

  states.push_back({output_state, SpeedCosts_Unoptimized});

  return states;
}


template<class Pixel>
std::shared_ptr<HeifPixelImage>
Op_YCbCr444_to_YCbCr420_average<Pixel>::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                                           const ColorState& target_state,
                                                           const heif_color_conversion_options& options) const
{
  bool hdr = !std::is_same<Pixel, uint8_t>::value;

  int bpp_y = input->get_bits_per_pixel(heif_channel_Y);
  int bpp_cb = input->get_bits_per_pixel(heif_channel_Cb);
  int bpp_cr = input->get_bits_per_pixel(heif_channel_Cr);
  int bpp_a = 0;

  bool has_alpha = input->has_channel(heif_channel_Alpha);

  if (has_alpha) {
    bpp_a = input->get_bits_per_pixel(heif_channel_Alpha);
  }

  if (!hdr) {
    if (bpp_y != 8 ||
        bpp_cb != 8 ||
        bpp_cr != 8) {
      return nullptr;
    }
  }
  else {
    if (bpp_y == 8 ||
        bpp_cb == 8 ||
        bpp_cr == 8) {
      return nullptr;
    }
  }


  if (bpp_y != bpp_cb ||
      bpp_y != bpp_cr) {
    // TODO: test with varying bit depths when we have a test image
    return nullptr;
  }


  auto colorProfile = input->get_color_profile_nclx();

  int width = input->get_width();
  int height = input->get_height();

  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(width, height, heif_colorspace_YCbCr, heif_chroma_420);

  int cwidth = (width + 1) / 2;
  int cheight = (height + 1) / 2;

  if (!outimg->add_plane(heif_channel_Y, width, height, bpp_y) ||
      !outimg->add_plane(heif_channel_Cb, cwidth, cheight, bpp_cb) ||
      !outimg->add_plane(heif_channel_Cr, cwidth, cheight, bpp_cr)) {
    return nullptr;
  }

  if (has_alpha) {
    if (!outimg->add_plane(heif_channel_Alpha, width, height, bpp_a)) {
      return nullptr;
    }
  }

  const Pixel* in_y, * in_cb, * in_cr, * in_a;
  int in_y_stride = 0, in_cb_stride = 0, in_cr_stride = 0, in_a_stride = 0;

  Pixel* out_y, * out_cb, * out_cr, * out_a;
  int out_y_stride = 0, out_cb_stride = 0, out_cr_stride = 0, out_a_stride = 0;

  in_y = (const Pixel*) input->get_plane(heif_channel_Y, &in_y_stride);
  in_cb = (const Pixel*) input->get_plane(heif_channel_Cb, &in_cb_stride);
  in_cr = (const Pixel*) input->get_plane(heif_channel_Cr, &in_cr_stride);
  out_y = (Pixel*) outimg->get_plane(heif_channel_Y, &out_y_stride);
  out_cb = (Pixel*) outimg->get_plane(heif_channel_Cb, &out_cb_stride);
  out_cr = (Pixel*) outimg->get_plane(heif_channel_Cr, &out_cr_stride);

  if (has_alpha) {
    in_a = (const Pixel*) input->get_plane(heif_channel_Alpha, &in_a_stride);
    out_a = (Pixel*) outimg->get_plane(heif_channel_Alpha, &out_a_stride);
  }
  else {
    in_a = nullptr;
    out_a = nullptr;
  }


  if (hdr) {
    in_y_stride /= 2;
    in_cb_stride /= 2;
    in_cr_stride /= 2;
    in_a_stride /= 2;
    out_y_stride /= 2;
    out_cb_stride /= 2;
    out_cr_stride /= 2;
    out_a_stride /= 2;
  }

  // --- fill right and bottom borders if the image size is odd

  if (height & 1) {
    for (int x = 0; x < width - 1; x += 2) {
      out_cb[(cheight - 1) * out_cb_stride + x / 2] = (Pixel) ((in_cb[(height - 1) * in_cb_stride + x] +
                                                                in_cb[(height - 1) * in_cb_stride + x + 1] + 1) / 2);
      out_cr[(cheight - 1) * out_cr_stride + x / 2] = (Pixel) ((in_cr[(height - 1) * in_cr_stride + x] +
                                                                in_cr[(height - 1) * in_cr_stride + x + 1] + 1) / 2);
    }
  }

  if (width & 1) {
    for (int y = 0; y < height - 1; y += 2) {
      out_cb[(y / 2) * out_cb_stride + cwidth - 1] = (Pixel) ((in_cb[(y + 0) * in_cb_stride + width - 1] +
                                                               in_cb[(y + 1) * in_cb_stride + width - 1] + 1) / 2);
      out_cr[(y / 2) * out_cr_stride + cwidth - 1] = (Pixel) ((in_cr[(y + 0) * in_cr_stride + width - 1] +
                                                               in_cr[(y + 1) * in_cr_stride + width - 1] + 1) / 2);
    }
  }

  if ((width & 1) && (height & 1)) {
    out_cb[(cheight - 1) * out_cb_stride + cwidth - 1] = in_cb[(height - 1) * in_cb_stride + width - 1];
    out_cr[(cheight - 1) * out_cr_stride + cwidth - 1] = in_cr[(height - 1) * in_cr_stride + width - 1];
  }


  // --- averaging filter

  int x, y;
  for (y = 0; y < height - 1; y += 2) {
    for (x = 0; x < width - 1; x += 2) {
      Pixel cb00 = in_cb[y * in_cb_stride + x];
      Pixel cr00 = in_cr[y * in_cr_stride + x];
      Pixel cb01 = in_cb[y * in_cb_stride + x + 1];
      Pixel cr01 = in_cr[y * in_cr_stride + x + 1];
      Pixel cb10 = in_cb[(y + 1) * in_cb_stride + x];
      Pixel cr10 = in_cr[(y + 1) * in_cr_stride + x];
      Pixel cb11 = in_cb[(y + 1) * in_cb_stride + x + 1];
      Pixel cr11 = in_cr[(y + 1) * in_cr_stride + x + 1];

      out_cb[(y / 2) * out_cb_stride + x / 2] = (Pixel) ((cb00 + cb01 + cb10 + cb11 + 2) / 4);
      out_cr[(y / 2) * out_cr_stride + x / 2] = (Pixel) ((cr00 + cr01 + cr10 + cr11 + 2) / 4);
    }
  }

  // TODO: check whether we can use HeifPixelImage::transfer_plane_from_image_as() instead of copying Y and Alpha

  for (y = 0; y < height; y++) {
    int copyWidth = (hdr ? width * 2 : width);

    memcpy(&out_y[y * out_y_stride], &in_y[y * in_y_stride], copyWidth);

    if (has_alpha) {
      memcpy(&out_a[y * out_a_stride], &in_a[y * in_a_stride], copyWidth);
    }
  }

  return outimg;
}

template class Op_YCbCr444_to_YCbCr420_average<uint8_t>;
template class Op_YCbCr444_to_YCbCr420_average<uint16_t>;



template<class Pixel>
std::vector<ColorStateWithCost>
Op_YCbCr420_bilinear_to_YCbCr444<Pixel>::state_after_conversion(const ColorState& input_state,
                                                                const ColorState& target_state,
                                                                const heif_color_conversion_options& options) const
{
  if (input_state.colorspace != heif_colorspace_YCbCr) {
    return {};
  }

  if (input_state.chroma != heif_chroma_420) {
    return {};
  }

  // this Op only implements the bilinear algorithm

  if (options.preferred_chroma_upsampling_algorithm != heif_chroma_upsampling_bilinear) {
    return {};
  }

  bool hdr = !std::is_same<Pixel, uint8_t>::value;

  if ((input_state.bits_per_pixel != 8) != hdr) {
    return {};
  }

  if (input_state.nclx_profile) {
    if (input_state.nclx_profile->get_matrix_coefficients() == 0) {
      return {};
    }
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;

  // --- convert to 4:4:4

  output_state.colorspace = heif_colorspace_YCbCr;
  output_state.chroma = heif_chroma_444;
  output_state.has_alpha = input_state.has_alpha;  // we simply keep the old alpha plane
  output_state.bits_per_pixel = input_state.bits_per_pixel;

  states.push_back({output_state, SpeedCosts_Unoptimized});

  return states;
}


template<class Pixel>
std::shared_ptr<HeifPixelImage>
Op_YCbCr420_bilinear_to_YCbCr444<Pixel>::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                                            const ColorState& target_state,
                                                            const heif_color_conversion_options& options) const
{
  bool hdr = !std::is_same<Pixel, uint8_t>::value;

  int bpp_y = input->get_bits_per_pixel(heif_channel_Y);
  int bpp_cb = input->get_bits_per_pixel(heif_channel_Cb);
  int bpp_cr = input->get_bits_per_pixel(heif_channel_Cr);
  int bpp_a = 0;

  bool has_alpha = input->has_channel(heif_channel_Alpha);

  if (has_alpha) {
    bpp_a = input->get_bits_per_pixel(heif_channel_Alpha);
  }

  if (!hdr) {
    if (bpp_y != 8 ||
        bpp_cb != 8 ||
        bpp_cr != 8) {
      return nullptr;
    }
  }
  else {
    if (bpp_y == 8 ||
        bpp_cb == 8 ||
        bpp_cr == 8) {
      return nullptr;
    }
  }


  if (bpp_y != bpp_cb ||
      bpp_y != bpp_cr) {
    // TODO: test with varying bit depths when we have a test image
    return nullptr;
  }


  auto colorProfile = input->get_color_profile_nclx();

  int width = input->get_width();
  int height = input->get_height();

  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(width, height, heif_colorspace_YCbCr, heif_chroma_444);

  if (!outimg->add_plane(heif_channel_Y, width, height, bpp_y) ||
      !outimg->add_plane(heif_channel_Cb, width, height, bpp_cb) ||
      !outimg->add_plane(heif_channel_Cr, width, height, bpp_cr)) {
    return nullptr;
  }

  if (has_alpha) {
    if (!outimg->add_plane(heif_channel_Alpha, width, height, bpp_a)) {
      return nullptr;
    }
  }

  const Pixel* in_y, * in_cb, * in_cr, * in_a;
  int in_y_stride = 0, in_cb_stride = 0, in_cr_stride = 0, in_a_stride = 0;

  Pixel* out_y, * out_cb, * out_cr, * out_a;
  int out_y_stride = 0, out_cb_stride = 0, out_cr_stride = 0, out_a_stride = 0;

  in_y = (const Pixel*) input->get_plane(heif_channel_Y, &in_y_stride);
  in_cb = (const Pixel*) input->get_plane(heif_channel_Cb, &in_cb_stride);
  in_cr = (const Pixel*) input->get_plane(heif_channel_Cr, &in_cr_stride);
  out_y = (Pixel*) outimg->get_plane(heif_channel_Y, &out_y_stride);
  out_cb = (Pixel*) outimg->get_plane(heif_channel_Cb, &out_cb_stride);
  out_cr = (Pixel*) outimg->get_plane(heif_channel_Cr, &out_cr_stride);

  if (has_alpha) {
    in_a = (const Pixel*) input->get_plane(heif_channel_Alpha, &in_a_stride);
    out_a = (Pixel*) outimg->get_plane(heif_channel_Alpha, &out_a_stride);
  }
  else {
    in_a = nullptr;
    out_a = nullptr;
  }


  if (hdr) {
    in_y_stride /= 2;
    in_cb_stride /= 2;
    in_cr_stride /= 2;
    in_a_stride /= 2;
    out_y_stride /= 2;
    out_cb_stride /= 2;
    out_cr_stride /= 2;
    out_a_stride /= 2;
  }

  /*
   *  We assume that chroma pixels are located in the center of 2x2 luma pixels.
   *  The image border 'b' is handled separately.
   *  The right and bottom border are not processed when the size is odd.
   *  Then, each 2x2 square between 4 chroma samples is computed in one iteration.
   *
   *  Upsampling weights are 3/4, 1/4. For example:
   *    A = 3/4*3/4 * C1 + 3/4*1/4 * C2 + 1/4*3/4 * C3 + 1/4*1/4 * C4
   *
   *    +---+---+---+---+
   *    | b | b | b | b |
   *    +---C1--+---C2--+
   *    | b | A |   | b |
   *    +---+---+---+---+
   *    | b |   |   | b |
   *    +---C3--+---C4--+
   *    | b | b | b | b |
   *    +---+---+---+---+
   */

  // --- fill borders

  // top left corner
  out_cb[0] = in_cb[0];
  out_cr[0] = in_cr[0];

  // top border
  for (int cx = 0; cx < (width - 1) / 2; cx++) {
    out_cb[0 * out_cb_stride + 2 * cx + 1] = (Pixel) ((3 * in_cb[cx / 2] + 1 * in_cb[cx / 2 + 1] + 2) / 4);
    out_cb[0 * out_cb_stride + 2 * cx + 2] = (Pixel) ((1 * in_cb[cx / 2] + 3 * in_cb[cx / 2 + 1] + 2) / 4);
    out_cr[0 * out_cr_stride + 2 * cx + 1] = (Pixel) ((3 * in_cr[cx / 2] + 1 * in_cr[cx / 2 + 1] + 2) / 4);
    out_cr[0 * out_cr_stride + 2 * cx + 2] = (Pixel) ((1 * in_cr[cx / 2] + 3 * in_cr[cx / 2 + 1] + 2) / 4);
  }

  // top right corner
  if (width % 2 == 0) {
    out_cb[width - 1] = in_cb[width / 2 - 1];
    out_cr[width - 1] = in_cr[width / 2 - 1];
  }

  // left border
  for (int cy = 0; cy < (height - 1) / 2; cy++) {
    out_cb[(2 * cy + 1) * out_cb_stride + 0] = (Pixel) ((3 * in_cb[cy / 2 * in_cb_stride] + 1 * in_cb[(cy / 2 + 1) * in_cb_stride] + 2) / 4);
    out_cb[(2 * cy + 2) * out_cb_stride + 0] = (Pixel) ((1 * in_cb[cy / 2 * in_cb_stride] + 3 * in_cb[(cy / 2 + 1) * in_cb_stride] + 2) / 4);
    out_cr[(2 * cy + 1) * out_cr_stride + 0] = (Pixel) ((3 * in_cr[cy / 2 * in_cr_stride] + 1 * in_cr[(cy / 2 + 1) * in_cr_stride] + 2) / 4);
    out_cr[(2 * cy + 2) * out_cr_stride + 0] = (Pixel) ((1 * in_cr[cy / 2 * in_cr_stride] + 3 * in_cr[(cy / 2 + 1) * in_cr_stride] + 2) / 4);
  }

  // bottom left corner
  if (height % 2 == 0) {
    out_cb[(height - 1) * out_cb_stride] = in_cb[(height / 2 - 1) * in_cb_stride];
    out_cr[(height - 1) * out_cr_stride] = in_cr[(height / 2 - 1) * in_cr_stride];
  }

  // right border
  if (width % 2 == 0) {
    for (int cy = 0; cy < (height - 1) / 2; cy++) {
      out_cb[(2 * cy + 1) * out_cb_stride + width - 1] = (Pixel) ((3 * in_cb[cy / 2 * in_cb_stride + width / 2 - 1] + 1 * in_cb[(cy / 2 + 1) * in_cb_stride + width / 2 - 1] + 2) / 4);
      out_cb[(2 * cy + 2) * out_cb_stride + width - 1] = (Pixel) ((1 * in_cb[cy / 2 * in_cb_stride + width / 2 - 1] + 3 * in_cb[(cy / 2 + 1) * in_cb_stride + width / 2 - 1] + 2) / 4);
      out_cr[(2 * cy + 1) * out_cr_stride + width - 1] = (Pixel) ((3 * in_cr[cy / 2 * in_cr_stride + width / 2 - 1] + 1 * in_cr[(cy / 2 + 1) * in_cr_stride + width / 2 - 1] + 2) / 4);
      out_cr[(2 * cy + 2) * out_cr_stride + width - 1] = (Pixel) ((1 * in_cr[cy / 2 * in_cr_stride + width / 2 - 1] + 3 * in_cr[(cy / 2 + 1) * in_cr_stride + width / 2 - 1] + 2) / 4);
    }
  }

  // bottom border
  if (height % 2 == 0) {
    for (int cx = 0; cx < (width - 1) / 2; cx++) {
      out_cb[(height - 1) * out_cb_stride + 2 * cx + 1] = (Pixel) ((3 * in_cb[(height / 2 - 1) * in_cb_stride + cx / 2] + 1 * in_cb[(height / 2 - 1) * in_cb_stride + cx / 2 + 1] + 2) / 4);
      out_cb[(height - 1) * out_cb_stride + 2 * cx + 2] = (Pixel) ((1 * in_cb[(height / 2 - 1) * in_cb_stride + cx / 2] + 3 * in_cb[(height / 2 - 1) * in_cb_stride + cx / 2 + 1] + 2) / 4);
      out_cr[(height - 1) * out_cr_stride + 2 * cx + 1] = (Pixel) ((3 * in_cr[(height / 2 - 1) * in_cr_stride + cx / 2] + 1 * in_cr[(height / 2 - 1) * in_cr_stride + cx / 2 + 1] + 2) / 4);
      out_cr[(height - 1) * out_cr_stride + 2 * cx + 2] = (Pixel) ((1 * in_cr[(height / 2 - 1) * in_cr_stride + cx / 2] + 3 * in_cr[(height / 2 - 1) * in_cr_stride + cx / 2 + 1] + 2) / 4);
    }
  }

  // bottom right corner
  if (width % 2 == 0 && height % 2 == 0) {
    out_cb[(height - 1) * out_cb_stride + width - 1] = in_cb[(height / 2 - 1) * in_cb_stride + width / 2 - 1];
    out_cr[(height - 1) * out_cr_stride + width - 1] = in_cr[(height / 2 - 1) * in_cr_stride + width / 2 - 1];
  }


  // --- bilinear filtering of inner part

  int x, y;
  for (y = 1; y < height - 1; y += 2) {
    for (x = 1; x < width - 1; x += 2) {
      int cx = x / 2;
      int cy = y / 2;

      Pixel cb00 = in_cb[cy * in_cb_stride + cx];
      Pixel cr00 = in_cr[cy * in_cr_stride + cx];
      Pixel cb01 = in_cb[cy * in_cb_stride + cx + 1];
      Pixel cr01 = in_cr[cy * in_cr_stride + cx + 1];
      Pixel cb10 = in_cb[(cy + 1) * in_cb_stride + cx];
      Pixel cr10 = in_cr[(cy + 1) * in_cr_stride + cx];
      Pixel cb11 = in_cb[(cy + 1) * in_cb_stride + cx + 1];
      Pixel cr11 = in_cr[(cy + 1) * in_cr_stride + cx + 1];

      out_cb[(y + 0) * out_cb_stride + x + 0] = (Pixel) ((cb00 * 3 * 3 + cb01 * 1 * 3 + cb10 * 3 * 1 + cb11 * 1 * 1 + 8) / 16);
      out_cb[(y + 0) * out_cb_stride + x + 1] = (Pixel) ((cb00 * 1 * 3 + cb01 * 3 * 3 + cb10 * 1 * 1 + cb11 * 3 * 1 + 8) / 16);
      out_cb[(y + 1) * out_cb_stride + x + 0] = (Pixel) ((cb00 * 3 * 1 + cb01 * 1 * 1 + cb10 * 3 * 3 + cb11 * 1 * 3 + 8) / 16);
      out_cb[(y + 1) * out_cb_stride + x + 1] = (Pixel) ((cb00 * 1 * 1 + cb01 * 3 * 1 + cb10 * 1 * 3 + cb11 * 3 * 3 + 8) / 16);

      out_cr[(y + 0) * out_cr_stride + x + 0] = (Pixel) ((cr00 * 3 * 3 + cr01 * 1 * 3 + cr10 * 3 * 1 + cr11 * 1 * 1 + 8) / 16);
      out_cr[(y + 0) * out_cr_stride + x + 1] = (Pixel) ((cr00 * 1 * 3 + cr01 * 3 * 3 + cr10 * 1 * 1 + cr11 * 3 * 1 + 8) / 16);
      out_cr[(y + 1) * out_cr_stride + x + 0] = (Pixel) ((cr00 * 3 * 1 + cr01 * 1 * 1 + cr10 * 3 * 3 + cr11 * 1 * 3 + 8) / 16);
      out_cr[(y + 1) * out_cr_stride + x + 1] = (Pixel) ((cr00 * 1 * 1 + cr01 * 3 * 1 + cr10 * 1 * 3 + cr11 * 3 * 3 + 8) / 16);
    }
  }

  // TODO: check whether we can use HeifPixelImage::transfer_plane_from_image_as() instead of copying Y and Alpha

  for (y = 0; y < height; y++) {
    int copyWidth = (hdr ? width * 2 : width);

    memcpy(&out_y[y * out_y_stride], &in_y[y * in_y_stride], copyWidth);

    if (has_alpha) {
      memcpy(&out_a[y * out_a_stride], &in_a[y * in_a_stride], copyWidth);
    }
  }

  return outimg;
}

template class Op_YCbCr420_bilinear_to_YCbCr444<uint8_t>;
template class Op_YCbCr420_bilinear_to_YCbCr444<uint16_t>;
