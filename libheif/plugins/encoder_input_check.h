/*
 * HEIF codec.
 * Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>
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

#ifndef LIBHEIF_ENCODER_INPUT_CHECK_H
#define LIBHEIF_ENCODER_INPUT_CHECK_H

#include "libheif/heif.h"
#include "libheif/heif_plugin.h"

#include <initializer_list>

/*
 * Input validation for encoder plugins that take planar YCbCr or monochrome
 * images with one bit depth for all channels.
 *
 * A plugin cannot assume that it is handed an image it can encode. The image
 * arrives from Encoder::convert_colorspace_for_encoding(), which returns the
 * image unchanged whenever it already has the colorspace and chroma format the
 * plugin asked for, so nothing on the way in inspects the per-channel bit
 * depths. Input images may legitimately differ per channel: the 'unci' codec
 * (ISO/IEC 23001-17) declares component_bit_depth once per component, so
 * decoding such a file and re-encoding it produces, for example, Y at 10 bits
 * and Cb/Cr at 8. HeifPixelImage allocates one byte per sample up to 8 bits and
 * two bytes above that, so a plugin that derives the sample width from the luma
 * channel and applies it to the chroma planes reads past the end of them.
 *
 * Whether an image can be encoded depends on the codec and on the specific
 * encoder implementation, which is why this takes the constraints as arguments
 * rather than hard-coding them:
 *
 *   - H.265 and H.264 signal bit_depth_luma_minus8 and bit_depth_chroma_minus8
 *     separately, so the formats can represent differing luma and chroma bit
 *     depths, but neither x265 nor x264 nor kvazaar can produce it: their APIs
 *     carry a single bit depth.
 *   - AV1 and VVC signal one bit depth for all planes, so the formats cannot
 *     represent it at all.
 *   - JPEG 2000 signals a precision per component and genuinely supports it,
 *     which is why the OpenJPEG and OpenJPH plugins do not use this check.
 *
 * 'supported_bit_depths' is the set the codec allows. It is a codec level
 * constraint and does not replace a plugin's own check against the encoder
 * library actually linked in (x265_api_get(), uvg_api_get() and friends), which
 * is what decides whether this particular build can do 10 or 12 bits.
 *
 * TODO: this per-plugin check is a stopgap. What is really needed is a proper
 * plugin API through which an encoder describes the input formats it accepts
 * (bit depth per channel, chroma formats, and so on). That would let libheif
 * query how an image has to be color-converted to fit a given encoder, instead
 * of the plugin refusing the image outright. It would also feed into encoder
 * selection: two encoders for the same output format may well support different
 * bit depth combinations, so the choice of plugin should depend on what the
 * input image actually needs. Until that API exists, each plugin has to check
 * its own input.
 */
static inline heif_error check_encoder_input_image(const heif_image* image,
                                                   bool supports_monochrome,
                                                   std::initializer_list<int> supported_bit_depths)
{
  const heif_channel channels[3] = {heif_channel_Y, heif_channel_Cb, heif_channel_Cr};
  int num_color_channels;

  switch (heif_image_get_colorspace(image)) {
    case heif_colorspace_monochrome:
      if (!supports_monochrome) {
        return heif_error{heif_error_Encoder_plugin_error,
                          heif_suberror_Unsupported_image_type,
                          "Encoder cannot encode monochrome images"};
      }
      num_color_channels = 1;
      break;

    case heif_colorspace_YCbCr:
      num_color_channels = 3;
      break;

    default:
      return heif_error{heif_error_Encoder_plugin_error,
                        heif_suberror_Unsupported_image_type,
                        "Encoder can only encode YCbCr and monochrome images"};
  }

  for (int i = 0; i < num_color_channels; i++) {
    if (!heif_image_has_channel(image, channels[i])) {
      return heif_error{heif_error_Encoder_plugin_error,
                        heif_suberror_Unsupported_image_type,
                        "Input image is missing one of its color channels"};
    }
  }

  int bpp = heif_image_get_bits_per_pixel_range(image, heif_channel_Y);

  for (int i = 1; i < num_color_channels; i++) {
    if (heif_image_get_bits_per_pixel_range(image, channels[i]) != bpp) {
      return heif_error{heif_error_Encoder_plugin_error,
                        heif_suberror_Unsupported_bit_depth,
                        "Encoder cannot encode images in which the color channels have different bit depths"};
    }
  }

  for (int supported_bpp : supported_bit_depths) {
    if (bpp == supported_bpp) {
      return heif_error_ok;
    }
  }

  return heif_error{heif_error_Encoder_plugin_error,
                    heif_suberror_Unsupported_bit_depth,
                    "Encoder cannot encode images at this bit depth"};
}

#endif // LIBHEIF_ENCODER_INPUT_CHECK_H
