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
  static unc_encoder_rrggbb enc_rrggbb;

  static const unc_encoder* encoders[] {
    &enc_rgb3_rgba,
    &enc_rrggbb
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


Error fill_cmpd_and_uncC(std::shared_ptr<Box_cmpd>& cmpd,
                         std::shared_ptr<Box_uncC>& uncC,
                         const std::shared_ptr<const HeifPixelImage>& image,
                         const heif_unci_image_parameters* parameters,
                         bool save_alpha_channel)
{
  uint32_t nTileColumns = parameters->image_width / parameters->tile_width;
  uint32_t nTileRows = parameters->image_height / parameters->tile_height;

  const heif_colorspace colourspace = image->get_colorspace();
  if (colourspace == heif_colorspace_YCbCr) {
    if (!(image->has_channel(heif_channel_Y) && image->has_channel(heif_channel_Cb) && image->has_channel(heif_channel_Cr))) {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   "Invalid colourspace / channel combination - YCbCr");
    }
    Box_cmpd::Component yComponent = {component_type_Y};
    cmpd->add_component(yComponent);
    Box_cmpd::Component cbComponent = {component_type_Cb};
    cmpd->add_component(cbComponent);
    Box_cmpd::Component crComponent = {component_type_Cr};
    cmpd->add_component(crComponent);
    uint8_t bpp_y = image->get_bits_per_pixel(heif_channel_Y);
    Box_uncC::Component component0 = {0, bpp_y, component_format_unsigned, 0};
    uncC->add_component(component0);
    uint8_t bpp_cb = image->get_bits_per_pixel(heif_channel_Cb);
    Box_uncC::Component component1 = {1, bpp_cb, component_format_unsigned, 0};
    uncC->add_component(component1);
    uint8_t bpp_cr = image->get_bits_per_pixel(heif_channel_Cr);
    Box_uncC::Component component2 = {2, bpp_cr, component_format_unsigned, 0};
    uncC->add_component(component2);
    if (image->get_chroma_format() == heif_chroma_444) {
      uncC->set_sampling_type(sampling_mode_no_subsampling);
    }
    else if (image->get_chroma_format() == heif_chroma_422) {
      uncC->set_sampling_type(sampling_mode_422);
    }
    else if (image->get_chroma_format() == heif_chroma_420) {
      uncC->set_sampling_type(sampling_mode_420);
    }
    else {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   "Unsupported YCbCr sub-sampling type");
    }
    uncC->set_interleave_type(interleave_mode_component);
    uncC->set_block_size(0);
    uncC->set_components_little_endian(false);
    uncC->set_block_pad_lsb(false);
    uncC->set_block_little_endian(false);
    uncC->set_block_reversed(false);
    uncC->set_pad_unknown(false);
    uncC->set_pixel_size(0);
    uncC->set_row_align_size(0);
    uncC->set_tile_align_size(0);
    uncC->set_number_of_tile_columns(nTileColumns);
    uncC->set_number_of_tile_rows(nTileRows);
  }
  else if (colourspace == heif_colorspace_RGB) {
    if (!((image->get_chroma_format() == heif_chroma_444) ||
          (image->get_chroma_format() == heif_chroma_interleaved_RGB) ||
          (image->get_chroma_format() == heif_chroma_interleaved_RGBA) ||
          (image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_BE) ||
          (image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_LE) ||
          (image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_BE) ||
          (image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE))) {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   "Unsupported colourspace / chroma combination - RGB");
    }

    Box_cmpd::Component rComponent = {component_type_red};
    cmpd->add_component(rComponent);
    Box_cmpd::Component gComponent = {component_type_green};
    cmpd->add_component(gComponent);
    Box_cmpd::Component bComponent = {component_type_blue};
    cmpd->add_component(bComponent);

    if (save_alpha_channel &&
        (image->get_chroma_format() == heif_chroma_interleaved_RGBA ||
         image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_BE ||
         image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE ||
         image->has_channel(heif_channel_Alpha))) {
      Box_cmpd::Component alphaComponent = {component_type_alpha};
      cmpd->add_component(alphaComponent);
    }

    if (image->get_chroma_format() == heif_chroma_interleaved_RGB ||
        image->get_chroma_format() == heif_chroma_interleaved_RGBA ||
        image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_BE ||
        image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_LE ||
        image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_BE ||
        image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE) {
      uncC->set_interleave_type(interleave_mode_pixel);
      int bpp = image->get_bits_per_pixel(heif_channel_interleaved);
      uint8_t component_align = 1;
      if (bpp == 8) {
        component_align = 0;
      }
      else if (bpp > 8) {
        component_align = 2;
      }

      Box_uncC::Component component0 = {0, (uint8_t) (bpp), component_format_unsigned, component_align};
      uncC->add_component(component0);
      Box_uncC::Component component1 = {1, (uint8_t) (bpp), component_format_unsigned, component_align};
      uncC->add_component(component1);
      Box_uncC::Component component2 = {2, (uint8_t) (bpp), component_format_unsigned, component_align};
      uncC->add_component(component2);

      if (save_alpha_channel &&
          (image->get_chroma_format() == heif_chroma_interleaved_RGBA ||
           image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_BE ||
           image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE)) {
        Box_uncC::Component component3 = {
            3, (uint8_t) (bpp), component_format_unsigned, component_align};
        uncC->add_component(component3);
      }
    }
    else {
      uncC->set_interleave_type(interleave_mode_component);

      int bpp_red = image->get_bits_per_pixel(heif_channel_R);
      Box_uncC::Component component0 = {0, (uint8_t) (bpp_red), component_format_unsigned, 0};
      uncC->add_component(component0);

      int bpp_green = image->get_bits_per_pixel(heif_channel_G);
      Box_uncC::Component component1 = {1, (uint8_t) (bpp_green), component_format_unsigned, 0};
      uncC->add_component(component1);

      int bpp_blue = image->get_bits_per_pixel(heif_channel_B);
      Box_uncC::Component component2 = {2, (uint8_t) (bpp_blue), component_format_unsigned, 0};
      uncC->add_component(component2);

      if (save_alpha_channel && image->has_channel(heif_channel_Alpha)) {
        int bpp_alpha = image->get_bits_per_pixel(heif_channel_Alpha);
        Box_uncC::Component component3 = {3, (uint8_t) (bpp_alpha), component_format_unsigned, 0};
        uncC->add_component(component3);
      }
    }

    uncC->set_sampling_type(sampling_mode_no_subsampling);
    uncC->set_block_size(0);

    if ((image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_LE) ||
        (image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE)) {
      uncC->set_components_little_endian(true);
    }
    else {
      uncC->set_components_little_endian(false);
    }

    uncC->set_block_pad_lsb(false);
    uncC->set_block_little_endian(false);
    uncC->set_block_reversed(false);
    uncC->set_pad_unknown(false);
    uncC->set_pixel_size(0);
    uncC->set_row_align_size(0);
    uncC->set_tile_align_size(0);
    uncC->set_number_of_tile_columns(nTileColumns);
    uncC->set_number_of_tile_rows(nTileRows);
  }
  else if (colourspace == heif_colorspace_monochrome) {
    Box_cmpd::Component monoComponent = {component_type_monochrome};
    cmpd->add_component(monoComponent);

    if (save_alpha_channel && image->has_channel(heif_channel_Alpha)) {
      Box_cmpd::Component alphaComponent = {component_type_alpha};
      cmpd->add_component(alphaComponent);
    }

    int bpp = image->get_bits_per_pixel(heif_channel_Y);
    heif_uncompressed_component_format format = to_unc_component_format(image, heif_channel_Y);
    Box_uncC::Component component0 = {0, (uint8_t) (bpp), (uint8_t) format, 0};
    uncC->add_component(component0);

    if (save_alpha_channel && image->has_channel(heif_channel_Alpha)) {
      heif_uncompressed_component_format format_alpha = to_unc_component_format(image, heif_channel_Alpha);
      bpp = image->get_bits_per_pixel(heif_channel_Alpha);
      Box_uncC::Component component1 = {1, (uint8_t) (bpp), (uint8_t) format_alpha, 0};
      uncC->add_component(component1);
    }

    uncC->set_sampling_type(sampling_mode_no_subsampling);
    uncC->set_interleave_type(interleave_mode_component);
    uncC->set_block_size(0);
    uncC->set_components_little_endian(false);
    uncC->set_block_pad_lsb(false);
    uncC->set_block_little_endian(false);
    uncC->set_block_reversed(false);
    uncC->set_pad_unknown(false);
    uncC->set_pixel_size(0);
    uncC->set_row_align_size(0);
    uncC->set_tile_align_size(0);
    uncC->set_number_of_tile_columns(nTileColumns);
    uncC->set_number_of_tile_rows(nTileRows);
  }
  else {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Unsupported colourspace");
  }
  return Error::Ok;
}



static void maybe_make_minimised_uncC(std::shared_ptr<Box_uncC>& uncC, const std::shared_ptr<const HeifPixelImage>& image)
{
  uncC->set_version(0);
  if (image->get_colorspace() != heif_colorspace_RGB) {
    return;
  }
  if (!((image->get_chroma_format() == heif_chroma_interleaved_RGB) || (image->get_chroma_format() == heif_chroma_interleaved_RGBA))) {
    return;
  }
  if (image->get_bits_per_pixel(heif_channel_interleaved) != 8) {
    return;
  }
  if (image->get_chroma_format() == heif_chroma_interleaved_RGBA) {
    uncC->set_profile(fourcc("rgba"));
  } else {
    uncC->set_profile(fourcc("rgb3"));
  }
  uncC->set_version(1);
}



Result<unciHeaders> generate_headers(const std::shared_ptr<const HeifPixelImage>& src_image,
                                     const heif_unci_image_parameters* parameters,
                                     const heif_encoding_options& options)
{
  unciHeaders headers;

  bool uses_tiles = (parameters->tile_width != parameters->image_width ||
                     parameters->tile_height != parameters->image_height);

  std::shared_ptr<Box_uncC> uncC = std::make_shared<Box_uncC>();
  if (options.prefer_uncC_short_form && !uses_tiles) {
    maybe_make_minimised_uncC(uncC, src_image);
  }

  if (uncC->get_version() == 1) {
    headers.uncC = std::move(uncC);
  } else {
    std::shared_ptr<Box_cmpd> cmpd = std::make_shared<Box_cmpd>();

    Error error = fill_cmpd_and_uncC(cmpd, uncC, src_image, parameters, options.save_alpha_channel);
    if (error) {
      return error;
    }

    headers.cmpd = std::move(cmpd);
    headers.uncC = std::move(uncC);
  }

  return headers;
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




Result<std::vector<uint8_t>> encode_image_tile(const std::shared_ptr<const HeifPixelImage>& src_image, bool save_alpha)
{
  std::vector<uint8_t> data;

  if (src_image->get_colorspace() == heif_colorspace_YCbCr)
  {
    uint64_t offset = 0;
    for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr})
    {
      if (src_image->get_bits_per_pixel(channel) != 8) {
        return Error(heif_error_Unsupported_feature,
                     heif_suberror_Unsupported_data_version,
                     "Unsupported colourspace");
      }

      size_t src_stride;
      uint32_t src_width = src_image->get_width(channel);
      uint32_t src_height = src_image->get_height(channel);
      const uint8_t* src_data = src_image->get_plane(channel, &src_stride);
      uint64_t out_size = src_width * uint64_t{src_height};
      data.resize(data.size() + out_size);
      for (uint32_t y = 0; y < src_height; y++) {
        memcpy(data.data() + offset + y * src_width, src_data + src_stride * y, src_width);
      }
      offset += out_size;
    }

    return data;
  }
  else if (src_image->get_colorspace() == heif_colorspace_RGB)
  {
    if (src_image->get_chroma_format() == heif_chroma_444)
    {
      uint64_t offset = 0;
      std::vector<heif_channel> channels = {heif_channel_R, heif_channel_G, heif_channel_B};
      if (src_image->has_channel(heif_channel_Alpha))
      {
        channels.push_back(heif_channel_Alpha);
      }
      for (heif_channel channel : channels)
      {
        size_t src_stride;
        const uint8_t* src_data = src_image->get_plane(channel, &src_stride);
        uint64_t out_size = static_cast<uint64_t>(src_image->get_height()) * src_image->get_width();

        data.resize(data.size() + out_size);
        for (uint32_t y = 0; y < src_image->get_height(); y++) {
          memcpy(data.data() + offset + y * src_image->get_width(), src_data + y * src_stride, src_image->get_width());
        }

        offset += out_size;
      }

      return data;
    }
    else if ((save_alpha && (src_image->get_chroma_format() == heif_chroma_interleaved_RGB ||
                             src_image->get_chroma_format() == heif_chroma_interleaved_RGBA ||
                             src_image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_BE ||
                             src_image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_LE ||
                             src_image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_BE ||
                             src_image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE))
             ||
             (!save_alpha && (src_image->get_chroma_format() == heif_chroma_interleaved_RGB ||
                              src_image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_BE ||
                              src_image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_LE)))
    {
      int bytes_per_pixel = 0;
      switch (src_image->get_chroma_format()) {
        case heif_chroma_interleaved_RGB:
          bytes_per_pixel=3;
          break;
        case heif_chroma_interleaved_RGBA:
          bytes_per_pixel=4;
          break;
        case heif_chroma_interleaved_RRGGBB_BE:
        case heif_chroma_interleaved_RRGGBB_LE:
          bytes_per_pixel=6;
          break;
        case heif_chroma_interleaved_RRGGBBAA_BE:
        case heif_chroma_interleaved_RRGGBBAA_LE:
          bytes_per_pixel=8;
          break;
        default:
          assert(false);
      }

      size_t src_stride;
      const uint8_t* src_data = src_image->get_plane(heif_channel_interleaved, &src_stride);
      uint64_t out_size = static_cast<uint64_t>(src_image->get_height()) * src_image->get_width() * bytes_per_pixel;
      data.resize(out_size);
      for (uint32_t y = 0; y < src_image->get_height(); y++) {
        memcpy(data.data() + y * src_image->get_width() * bytes_per_pixel, src_data + src_stride * y, src_image->get_width() * bytes_per_pixel);
      }

      return data;
    }
    else
    if (!save_alpha && (src_image->get_chroma_format() == heif_chroma_interleaved_RGBA ||
                        src_image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_BE ||
                        src_image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE)) {
      int bytes_per_pixel = 0;
      switch (src_image->get_chroma_format()) {
        case heif_chroma_interleaved_RGBA:
          bytes_per_pixel = 3;
          break;
        case heif_chroma_interleaved_RRGGBBAA_BE:
        case heif_chroma_interleaved_RRGGBBAA_LE:
          bytes_per_pixel = 6;
          break;
        default:
          assert(false);
      }

      size_t src_stride;
      const uint8_t* src_data = src_image->get_plane(heif_channel_interleaved, &src_stride);
      uint64_t out_size = static_cast<uint64_t>(src_image->get_height()) * src_image->get_width() * bytes_per_pixel;
      data.resize(out_size);

      if (src_image->get_chroma_format() == heif_chroma_interleaved_RGBA) {
        for (uint32_t y = 0; y < src_image->get_height(); y++) {
          for (uint32_t x = 0; x < src_image->get_width(); x++) {
            data[y * src_image->get_width() * bytes_per_pixel + 3 * x + 0] = src_data[src_stride * y + 4 * x + 0];
            data[y * src_image->get_width() * bytes_per_pixel + 3 * x + 1] = src_data[src_stride * y + 4 * x + 1];
            data[y * src_image->get_width() * bytes_per_pixel + 3 * x + 2] = src_data[src_stride * y + 4 * x + 2];
          }
        }
      }
      else {
        for (uint32_t y = 0; y < src_image->get_height(); y++) {
          for (uint32_t x = 0; x < src_image->get_width(); x++) {
            for (int i = 0; i < 6; i++) {
              data[y * src_image->get_width() * bytes_per_pixel + 6 * x + i] = src_data[src_stride * y + 8 * x + i];
            }
          }
        }
      }

      return data;
    }
    else {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   "Unsupported RGB chroma");
    }
  }
  else if (src_image->get_colorspace() == heif_colorspace_monochrome)
  {
    uint64_t offset = 0;
    std::vector<heif_channel> channels;
    if (src_image->has_channel(heif_channel_Alpha))
    {
      channels = {heif_channel_Y, heif_channel_Alpha};
    }
    else
    {
      channels = {heif_channel_Y};
    }

    for (heif_channel channel : channels)
    {
      if (src_image->get_bits_per_pixel(channel) != 8) {
        return Error(heif_error_Unsupported_feature,
                     heif_suberror_Unsupported_data_version,
                     "Unsupported colourspace");
      }

      size_t src_stride;
      const uint8_t* src_data = src_image->get_plane(channel, &src_stride);
      uint64_t out_size = static_cast<uint64_t>(src_image->get_height()) * src_stride;
      data.resize(data.size() + out_size);
      memcpy(data.data() + offset, src_data, out_size);
      offset += out_size;
    }

    return data;
  }
  else
  {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Unsupported colourspace");
  }
}


