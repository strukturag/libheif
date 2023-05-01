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

#ifndef LIBHEIF_COLORCONVERSION_H
#define LIBHEIF_COLORCONVERSION_H

#include "libheif/pixelimage.h"
#include <memory>
#include <string>
#include <vector>

struct ColorState
{
  heif_colorspace colorspace = heif_colorspace_undefined;
  heif_chroma chroma = heif_chroma_undefined;
  bool has_alpha = false;
  int bits_per_pixel = 8;
  std::shared_ptr<const color_profile_nclx> nclx_profile;

  ColorState() = default;

  ColorState(heif_colorspace colorspace, heif_chroma chroma, bool has_alpha, int bits_per_pixel)
      : colorspace(colorspace), chroma(chroma), has_alpha(has_alpha), bits_per_pixel(bits_per_pixel) {}

  bool operator==(const ColorState&) const;
};

std::ostream& operator<<(std::ostream& ostr, const ColorState& state);

// These are some integer constants for typical color conversion Op speed costs.
// The integer value is the speed cost. Any other integer can be assigned to the speed cost.
enum SpeedCosts
{
  SpeedCosts_Trivial = 1,
  SpeedCosts_Hardware = 2,
  SpeedCosts_OptimizedSoftware = 5 + 1,
  SpeedCosts_Unoptimized = 10 + 1,
  SpeedCosts_Slow = 15 + 1
};


struct ColorStateWithCost
{
  ColorState color_state;

  int speed_costs;
};


class ColorConversionOperation
{
public:
  virtual ~ColorConversionOperation() = default;

  // We specify the target state to control the conversion into a direction that is most
  // suitable for reaching the target state. That allows one conversion operation to
  // provide a range of conversion options.
  // Also returns the cost for this conversion.
  virtual std::vector<ColorStateWithCost>
  state_after_conversion(const ColorState& input_state,
                         const ColorState& target_state,
                         const heif_color_conversion_options& options) const = 0;

  virtual std::shared_ptr<HeifPixelImage>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     const ColorState& target_state,
                     const heif_color_conversion_options& options) const = 0;
};


class ColorConversionPipeline
{
public:
  static void init_ops();
  static void release_ops();

  bool construct_pipeline(const ColorState& input_state,
                          const ColorState& target_state,
                          const heif_color_conversion_options& options);

  std::shared_ptr<HeifPixelImage>
  convert_image(const std::shared_ptr<HeifPixelImage>& input);

  std::string debug_dump_pipeline() const;

private:
  static std::vector<ColorConversionOperation*> m_operation_pool;

  struct ConversionStep {
    const ColorConversionOperation* operation;
    ColorState output_state;
  };

  std::vector<ConversionStep> m_conversion_steps;

  heif_color_conversion_options m_options;
};


std::shared_ptr<HeifPixelImage> convert_colorspace(const std::shared_ptr<HeifPixelImage>& input,
                                                   heif_colorspace colorspace,
                                                   heif_chroma chroma,
                                                   const std::shared_ptr<const color_profile_nclx>& target_profile,
                                                   int output_bpp,
                                                   const heif_color_conversion_options& options);

#endif
