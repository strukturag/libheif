/*
 * HEIF codec.
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
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

#include <cassert>
#include <sstream>

#include "unc_decoder.h"
#include "unc_decoder_component_interleave.h"
#include "unc_decoder_pixel_interleave.h"
#include "unc_decoder_mixed_interleave.h"
#include "unc_decoder_row_interleave.h"
#include "unc_decoder_tile_component_interleave.h"
#include "unc_codec.h"
#include "unc_boxes.h"
#include "codecs/decoder.h"
#include "security_limits.h"


// --- unc_decoder ---

unc_decoder::unc_decoder(uint32_t width, uint32_t height,
                         const std::shared_ptr<const Box_cmpd>& cmpd,
                         const std::shared_ptr<const Box_uncC>& uncC)
    : m_width(width),
      m_height(height),
      m_cmpd(cmpd),
      m_uncC(uncC)
{
  m_tile_height = m_height / m_uncC->get_number_of_tile_rows();
  m_tile_width = m_width / m_uncC->get_number_of_tile_columns();

  assert(m_tile_width > 0);
  assert(m_tile_height > 0);
}


Error unc_decoder::decode_image(const DataExtent& extent,
                                const UncompressedImageCodec::unci_properties& properties,
                                std::shared_ptr<HeifPixelImage>& img)
{
  uint32_t tile_width = m_width / m_uncC->get_number_of_tile_columns();
  uint32_t tile_height = m_height / m_uncC->get_number_of_tile_rows();

  for (uint32_t tile_y0 = 0; tile_y0 < m_height; tile_y0 += tile_height)
    for (uint32_t tile_x0 = 0; tile_x0 < m_width; tile_x0 += tile_width) {
      Error error = decode_tile(extent, properties, img, tile_x0, tile_y0,
                                m_width, m_height,
                                tile_x0 / tile_width, tile_y0 / tile_height);
      if (error) {
        return error;
      }
    }

  return Error::Ok;
}


// --- unc_decoder_factory ---

Result<std::unique_ptr<unc_decoder>> unc_decoder_factory::get_unc_decoder(
    uint32_t width, uint32_t height,
    const std::shared_ptr<const Box_cmpd>& cmpd,
    const std::shared_ptr<const Box_uncC>& uncC)
{
  static unc_decoder_factory_component_interleave dec_component;
  static unc_decoder_factory_pixel_interleave dec_pixel;
  static unc_decoder_factory_mixed_interleave dec_mixed;
  static unc_decoder_factory_row_interleave dec_row;
  static unc_decoder_factory_tile_component_interleave dec_tile_component;

  static const unc_decoder_factory* decoders[]{
    &dec_component, &dec_pixel, &dec_mixed, &dec_row, &dec_tile_component
  };

  for (const unc_decoder_factory* dec : decoders) {
    if (dec->can_decode(uncC)) {
      return {dec->create(width, height, cmpd, uncC)};
    }
  }

  std::stringstream sstr;
  sstr << "Uncompressed interleave_type of " << ((int) uncC->get_interleave_type()) << " is not implemented yet";
  return Error{heif_error_Unsupported_feature, heif_suberror_Unsupported_data_version, sstr.str()};
}


// --- decode orchestration ---

Result<std::shared_ptr<HeifPixelImage>> unc_decoder::decode_full_image(
    const UncompressedImageCodec::unci_properties& properties,
    const DataExtent& extent,
    const heif_security_limits* limits)
{
  const std::shared_ptr<const Box_ispe>& ispe = properties.ispe;
  const std::shared_ptr<const Box_cmpd>& cmpd = properties.cmpd;
  const std::shared_ptr<const Box_uncC>& uncC = properties.uncC;

  assert(ispe);
  uint32_t width = ispe->get_width();
  uint32_t height = ispe->get_height();

  Result<std::shared_ptr<HeifPixelImage>> createImgResult = UncompressedImageCodec::create_image(cmpd, uncC, width, height, limits);
  if (!createImgResult) {
    return createImgResult.error();
  }

  auto img = *createImgResult;

  auto decoderResult = unc_decoder_factory::get_unc_decoder(width, height, cmpd, uncC);
  if (!decoderResult) {
    return decoderResult.error();
  }

  auto& decoder = *decoderResult;

  Error error = decoder->decode_image(extent, properties, img);
  if (error) {
    return error;
  }

  return img;
}
