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

#include "unc_encoder.h"

#include <cassert>
#include <cstring>

#include "pixelimage.h"
#include "unc_boxes.h"
#include "unc_encoder_component_interleave.h"
#include "unc_encoder_bytealign_component_interleave.h"
#include "unc_encoder_rgb_block_pixel_interleave.h"
#include "unc_encoder_rgb_pixel_interleave.h"
#include "unc_encoder_rgb_bytealign_pixel_interleave.h"
#include "libheif/heif_uncompressed.h"


heif_uncompressed_component_type heif_channel_to_component_type(heif_channel channel)
{
  switch (channel) {
    case heif_channel_Y: return heif_uncompressed_component_type::component_type_Y;
    case heif_channel_Cb: return heif_uncompressed_component_type::component_type_Cb;
    case heif_channel_Cr: return heif_uncompressed_component_type::component_type_Cr;
    case heif_channel_R: return heif_uncompressed_component_type::component_type_red;
    case heif_channel_G: return heif_uncompressed_component_type::component_type_green;
    case heif_channel_B: return heif_uncompressed_component_type::component_type_blue;
    case heif_channel_Alpha: return heif_uncompressed_component_type::component_type_alpha;
    case heif_channel_interleaved: assert(false);
      break;
    case heif_channel_filter_array: return heif_uncompressed_component_type::component_type_filter_array;
    case heif_channel_depth: return heif_uncompressed_component_type::component_type_depth;
    case heif_channel_disparity: return heif_uncompressed_component_type::component_type_disparity;
    case heif_channel_unknown: return heif_uncompressed_component_type::component_type_padded;
  }

  return heif_uncompressed_component_type::component_type_padded;
}


heif_uncompressed_component_format to_unc_component_format(heif_channel_datatype channel_datatype)
{
  switch (channel_datatype) {
    case heif_channel_datatype_signed_integer:
      return component_format_signed;

    case heif_channel_datatype_floating_point:
      return component_format_float;

    case heif_channel_datatype_complex_number:
      return component_format_complex;

    case heif_channel_datatype_unsigned_integer:
    case heif_channel_datatype_undefined:
    default:
      return component_format_unsigned;
  }
}


unc_encoder::unc_encoder()
{
  m_cmpd = std::make_shared<Box_cmpd>();
  m_uncC = std::make_shared<Box_uncC>();
}


Result<std::unique_ptr<const unc_encoder> > unc_encoder_factory::get_unc_encoder(const std::shared_ptr<const HeifPixelImage>& prototype_image,
                                                                                 const heif_encoding_options& options)
{
  static unc_encoder_factory_rgb_pixel_interleave enc_rgb_pixel_interleave;
  static unc_encoder_factory_rgb_block_pixel_interleave enc_rgb_block_pixel_interleave;
  static unc_encoder_factory_rgb_bytealign_pixel_interleave enc_rgb_bytealign_pixel_interleave;
  static unc_encoder_factory_component_interleave enc_component_interleave;
  static unc_encoder_factory_bytealign_component_interleave enc_bytealign_component_interleave;

  static const unc_encoder_factory* encoders[]{
    &enc_rgb_pixel_interleave,
    &enc_rgb_block_pixel_interleave,
    &enc_rgb_bytealign_pixel_interleave,
    &enc_component_interleave,
    &enc_bytealign_component_interleave
  };

  for (const unc_encoder_factory* enc : encoders) {
    if (enc->can_encode(prototype_image, options)) {
      return {enc->create(prototype_image, options)};
    }
  }

  return Error{
    heif_error_Unsupported_filetype,
    heif_suberror_Unspecified,
    "Input image configuration unsupported by uncompressed codec."
  };
}


heif_uncompressed_component_format to_unc_component_format(const std::shared_ptr<const HeifPixelImage>& image, heif_channel channel)
{
  heif_channel_datatype datatype = image->get_datatype(channel);
  heif_uncompressed_component_format component_format = to_unc_component_format(datatype);
  return component_format;
}


Result<Encoder::CodedImageData> unc_encoder::encode_full_image(const std::shared_ptr<const HeifPixelImage>& src_image,
                                                               const heif_encoding_options& options)
{
  auto uncEncoder = unc_encoder_factory::get_unc_encoder(src_image, options);
  if (uncEncoder.error()) {
    return uncEncoder.error();
  }

  return (*uncEncoder)->encode_static(src_image, options);
}


Result<Encoder::CodedImageData> unc_encoder::encode_static(const std::shared_ptr<const HeifPixelImage>& src_image,
                                                           const heif_encoding_options& in_options) const
{
  auto parameters = std::unique_ptr<heif_unci_image_parameters,
                                    void (*)(heif_unci_image_parameters*)>(heif_unci_image_parameters_alloc(),
                                                                           heif_unci_image_parameters_release);

  parameters->image_width = src_image->get_width();
  parameters->image_height = src_image->get_height();
  parameters->tile_width = parameters->image_width;
  parameters->tile_height = parameters->image_height;


  heif_encoding_options options = in_options;
  if (src_image->has_alpha() && !options.save_alpha_channel) {
    // TODO: drop alpha channel
  }


  // --- generate configuration property boxes

  auto uncC = this->get_uncC();

  Encoder::CodedImageData codedImageData;
  codedImageData.properties.push_back(uncC);
  if (!uncC->is_minimized()) {
    codedImageData.properties.push_back(this->get_cmpd());
  }


  // --- encode image

  Result<std::vector<uint8_t> > codedBitstreamResult = this->encode_tile(src_image);
  if (!codedBitstreamResult) {
    return codedBitstreamResult.error();
  }

  codedImageData.bitstream = *codedBitstreamResult;

  return codedImageData;
}
