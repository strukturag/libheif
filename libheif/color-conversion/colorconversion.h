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

#ifndef LIBHEIF_COLORCONVERSION_H
#define LIBHEIF_COLORCONVERSION_H

#include "image/pixelimage.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>


struct ColorState
{
  heif_colorspace colorspace = heif_colorspace_undefined;
  heif_chroma chroma = heif_chroma_undefined;

  // Bit depth of each plane. A value of 0 means that the plane does not exist in this state.
  // The depths may differ from each other (e.g. 'unci' declares a depth per component).
  //
  // The interleaved RGB chroma formats store all components in a single plane. They are
  // represented by their per-component depth in R/G/B (and alpha, if the format has one),
  // so that operators see the same fields as for planar RGB.
  int bits_per_pixel_R = 0;
  int bits_per_pixel_G = 0;
  int bits_per_pixel_B = 0;
  int bits_per_pixel_Y = 0;
  int bits_per_pixel_Cb = 0;
  int bits_per_pixel_Cr = 0;
  int bits_per_pixel_alpha = 0;
  int bits_per_pixel_filter_array = 0;

  // ColorConversionOperations can assume that the input and target nclx has no 'unspecified' values
  // if the colorspace is heif_colorspace_YCbCr. Otherwise, the values should preferably be 'unspecified'.
  nclx_profile nclx;

  ColorState() = default;

  // Convenience constructor: all colour planes of the given colorspace/chroma get 'bpp' and,
  // if 'with_alpha' is set, an alpha plane of the same depth is added.
  ColorState(heif_colorspace cs, heif_chroma chr, bool with_alpha, int bpp);

  bool has_alpha() const { return bits_per_pixel_alpha != 0; }

  // Bit depth of a single plane, 0 if the plane does not exist.
  // 'heif_channel_interleaved' maps to the R/G/B depth.
  int get_bits_per_pixel(heif_channel channel) const;
  void set_bits_per_pixel(heif_channel channel, int bpp);

  // Sets all colour planes that exist for the current colorspace/chroma to 'bpp' and clears
  // all other colour planes. The alpha plane is not touched.
  // Call this after 'colorspace' and 'chroma' have been set.
  void set_color_bits_per_pixel(int bpp);

  // Bit depth of the first colour plane (Y for YCbCr/monochrome, R for RGB, or the filter array).
  // This only characterizes the whole image when all colour planes have the same depth,
  // see color_channels_have_same_bpp().
  int get_color_bits_per_pixel() const;

  // Maximum bit depth over all existing planes, including alpha.
  int get_max_bits_per_pixel() const;

  // True if all existing colour planes (R/G/B or Y/Cb/Cr) have the same bit depth.
  bool color_channels_have_same_bpp() const;

  // True if all existing planes, including alpha, have the same bit depth.
  bool all_channels_have_same_bpp() const;

  // True if all existing planes, including alpha, have at most 8 bits.
  bool all_channels_sdr() const;

  // True if all existing planes, including alpha, have more than 8 bits.
  bool all_channels_hdr() const;

  bool operator==(const ColorState&) const;
};

std::ostream& operator<<(std::ostream& ostr, const ColorState& state);


// True if 'state' has a colour or alpha component wider than 16 bits.
//
// Every conversion operator reads and writes sample data through uint8_t* or uint16_t*
// and derives shift amounts from the bit depth, so none of them can handle a wider
// component. Images with wider components do exist: 'unci' components may be up to 256
// bits and we store up to 128 of them (64-bit integers, 32/64-bit floats, complex
// numbers) so that they can be read through the component API.
//
// An operator that cannot handle a bit depth must not offer itself to the pipeline for
// it, so each operator states its own supported range in state_after_conversion(). This
// helper spells out the upper bound they currently all share; operators with a tighter
// or different range (an exact 8 bits, or an explicit list) say so themselves instead.
// When an operator gains support for wider samples it simply stops calling this, and the
// catch-all in convert_colorspace() can go away.
inline bool has_samples_wider_than_16bit(const ColorState& state)
{
  return state.get_max_bits_per_pixel() > 16;
}

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
  ColorStateWithCost(ColorState c, int s) : color_state(c), speed_costs(s) {}

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
                         const heif_color_conversion_options& options,
                         const heif_color_conversion_options_ext& options_ext) const = 0;

  virtual Result<std::shared_ptr<HeifPixelImage>>
  convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                     const ColorState& input_state,
                     const ColorState& target_state,
                     const heif_color_conversion_options& options,
                     const heif_color_conversion_options_ext& options_ext,
                     const heif_security_limits* limits) const = 0;
};


class ColorConversionPipeline
{
public:
  static void init_ops();
  static void release_ops();

  bool is_nop() const { return m_conversion_steps.empty(); }

  bool construct_pipeline(const ColorState& input_state,
                          const ColorState& target_state,
                          const heif_color_conversion_options& options,
                          const heif_color_conversion_options_ext& options_ext);

  Result<std::shared_ptr<HeifPixelImage>> convert_image(const std::shared_ptr<HeifPixelImage>& input,
                                                        const heif_security_limits* limits);

  std::string debug_dump_pipeline() const;

private:
  static std::vector<std::shared_ptr<ColorConversionOperation>> m_operation_pool;

  struct ConversionStep {
    std::shared_ptr<ColorConversionOperation> operation;
    ColorState input_state;
    ColorState output_state;
  };

  std::vector<ConversionStep> m_conversion_steps;

  heif_color_conversion_options m_options;
  heif_color_conversion_options_ext m_options_ext;
};


// If no conversion is required, the input is simply passed through without copy.
// The input image is never modified by this function, but the input is still non-const because we may pass it through.
Result<std::shared_ptr<HeifPixelImage>> convert_colorspace(const std::shared_ptr<HeifPixelImage>& input,
                                                           heif_colorspace colorspace,
                                                           heif_chroma chroma,
                                                           const nclx_profile& target_profile,
                                                           int output_bpp,
                                                           const heif_color_conversion_options& options,
                                                           const heif_color_conversion_options_ext* options_ext,
                                                           const heif_security_limits* limits);

Result<std::shared_ptr<const HeifPixelImage>> convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                                                 heif_colorspace colorspace,
                                                                 heif_chroma chroma,
                                                                 const nclx_profile& target_profile,
                                                                 int output_bpp,
                                                                 const heif_color_conversion_options& options,
                                                                 const heif_color_conversion_options_ext* options_ext,
                                                                 const heif_security_limits* limits);

#endif
