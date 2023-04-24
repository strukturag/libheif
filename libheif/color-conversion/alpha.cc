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

#include "alpha.h"


std::vector<ColorStateWithCost>
Op_drop_alpha_plane::state_after_conversion(const ColorState& input_state,
                                            const ColorState& target_state,
                                            const heif_color_conversion_options& options) const
{
  // only drop alpha plane if it is not needed in output

  if ((input_state.chroma != heif_chroma_monochrome &&
       input_state.chroma != heif_chroma_420 &&
       input_state.chroma != heif_chroma_422 &&
       input_state.chroma != heif_chroma_444) ||
      input_state.has_alpha == false ||
      target_state.has_alpha == true) {
    return {};
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;

  // --- drop alpha plane

  output_state = input_state;
  output_state.has_alpha = false;

  states.push_back({output_state, SpeedCosts_Trivial});

  return states;
}


std::shared_ptr<HeifPixelImage>
Op_drop_alpha_plane::convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                        const ColorState& target_state,
                                        const heif_color_conversion_options& options) const
{
  int width = input->get_width();
  int height = input->get_height();

  auto outimg = std::make_shared<HeifPixelImage>();

  outimg->create(width, height,
                 input->get_colorspace(),
                 input->get_chroma_format());

  for (heif_channel channel : {heif_channel_Y,
                               heif_channel_Cb,
                               heif_channel_Cr,
                               heif_channel_R,
                               heif_channel_G,
                               heif_channel_B}) {
    if (input->has_channel(channel)) {
      outimg->copy_new_plane_from(input, channel, channel);
    }
  }

  return outimg;
}
