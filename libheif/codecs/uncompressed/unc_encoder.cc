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

#include <cstring>

#include "pixelimage.h"
#include "unc_boxes.h"
#include "unc_encoder_planar.h"
#include "unc_encoder_rgb_hdr_packed_interleave.h"
#include "unc_encoder_rgb3_rgba.h"
#include "unc_encoder_rrggbb.h"
#include "libheif/heif_uncompressed.h"


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


Result<const unc_encoder*> unc_encoder::get_unc_encoder(const std::shared_ptr<const HeifPixelImage>& prototype_image,
                                                        const heif_encoding_options& options)
{
  static unc_encoder_rgb3_rgba enc_rgb3_rgba;
  static unc_encoder_rgb_hdr_packed_interleave enc_rgb10_12;
  static unc_encoder_rrggbb enc_rrggbb;
  static unc_encoder_planar enc_planar;

  static const unc_encoder* encoders[] {
    &enc_rgb3_rgba,
    &enc_rgb10_12,
    &enc_rrggbb,
    &enc_planar
  };

  for (const unc_encoder* enc : encoders) {
    if (enc->can_encode(prototype_image, options)) {
      return {enc};
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
  auto uncEncoder = get_unc_encoder(src_image, options);
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

  std::shared_ptr<Box_uncC> uncC = std::make_shared<Box_uncC>();
  std::shared_ptr<Box_cmpd> cmpd = std::make_shared<Box_cmpd>();

  this->fill_cmpd_and_uncC(cmpd, uncC, src_image, options);

  Encoder::CodedImageData codedImageData;
  codedImageData.properties.push_back(uncC);
  if (!uncC->is_minimized()) {
    codedImageData.properties.push_back(cmpd);
  }


  // --- encode image

  Result<std::vector<uint8_t> > codedBitstreamResult = encode_tile(src_image, options);
  if (!codedBitstreamResult) {
    return codedBitstreamResult.error();
  }

  codedImageData.bitstream = *codedBitstreamResult;

  return codedImageData;
}


