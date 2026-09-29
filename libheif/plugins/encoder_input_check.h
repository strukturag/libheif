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
 * 'supported_bit_depths' is the set this plugin can actually encode. Where that
 * is narrower than what the codec allows because of the encoder library, the
 * plugin has to pass the narrower set: only x265 answers the question at run
 * time, where x265_api_get() returns NULL for a depth the linked build does not
 * provide. uvg_api_get() and kvz_api_get() look like they do the same, but both
 * are 'return &..._8bit_api;' upstream and never fail, so the uvg266 and kvazaar
 * builds are pinned at compile time by UVG_BIT_DEPTH / KVZ_BIT_DEPTH and those
 * are what the two plugins pass here.
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


/*
 * An encoder is opened once per sequence, in *_start_sequence_encoding(), and is
 * configured from the first frame. Encoder_HEVC / Encoder_AVC / Encoder_AVIF /
 * Encoder_VVC::encode_sequence_frame() call it only while the encoder is not
 * running yet, so every later frame goes straight to *_encode_sequence_frame().
 * A plugin that latched a bit depth there and applies it to the planes of a later
 * frame walks a one byte per sample plane at two bytes per sample: x265, for
 * example, hands the plane pointers of the current frame to libx265 together with
 * pic->bitDepth taken from the first frame, and reads past the end of them.
 *
 * So a plugin that keeps the bit depth across frames has to check every frame
 * against that configuration and not only against the codec level set above.
 * Plugins whose depth is fixed at compile time need nothing extra: passing that
 * constant to check_encoder_input_image() already pins all frames to one value.
 *
 * Call this after check_encoder_input_image(), which has already established that
 * the luma channel exists and that the chroma channels have the same depth.
 */
static inline heif_error check_sequence_frame_bit_depth(const heif_image* image,
                                                        int configured_bit_depth)
{
  if (heif_image_get_bits_per_pixel_range(image, heif_channel_Y) != configured_bit_depth) {
    return heif_error{heif_error_Encoder_plugin_error,
                      heif_suberror_Unsupported_bit_depth,
                      "All frames of a sequence must have the bit depth of the first frame"};
  }

  return heif_error_ok;
}

#endif // LIBHEIF_ENCODER_INPUT_CHECK_H
