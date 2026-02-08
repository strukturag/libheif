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

#ifndef LIBHEIF_UNC_DECODER_H
#define LIBHEIF_UNC_DECODER_H

#include <cstdint>
#include <memory>

#include "error.h"
#include "unc_codec.h"
#include "unc_boxes.h"

class HeifPixelImage;
struct DataExtent;
struct heif_security_limits;


class unc_decoder
{
public:
  virtual ~unc_decoder() = default;

  virtual Error decode_tile(const DataExtent& dataExtent,
                            const UncompressedImageCodec::unci_properties& properties,
                            std::shared_ptr<HeifPixelImage>& img,
                            uint32_t out_x0, uint32_t out_y0,
                            uint32_t image_width, uint32_t image_height,
                            uint32_t tile_x, uint32_t tile_y) = 0;

  Error decode_image(const DataExtent& extent,
                     const UncompressedImageCodec::unci_properties& properties,
                     std::shared_ptr<HeifPixelImage>& img);

  static Result<std::shared_ptr<HeifPixelImage>> decode_full_image(
      const UncompressedImageCodec::unci_properties& properties,
      const DataExtent& extent,
      const heif_security_limits* limits);

protected:
  unc_decoder(uint32_t width, uint32_t height,
              const std::shared_ptr<const Box_cmpd>& cmpd,
              const std::shared_ptr<const Box_uncC>& uncC);

  const uint32_t m_width;
  const uint32_t m_height;
  const std::shared_ptr<const Box_cmpd> m_cmpd;
  const std::shared_ptr<const Box_uncC> m_uncC;
  uint32_t m_tile_height;
  uint32_t m_tile_width;
};


class unc_decoder_factory
{
public:
  virtual ~unc_decoder_factory() = default;

  static Result<std::unique_ptr<unc_decoder>> get_unc_decoder(
      uint32_t width, uint32_t height,
      const std::shared_ptr<const Box_cmpd>& cmpd,
      const std::shared_ptr<const Box_uncC>& uncC);

private:
  virtual bool can_decode(const std::shared_ptr<const Box_uncC>& uncC) const = 0;

  virtual std::unique_ptr<unc_decoder> create(
      uint32_t width, uint32_t height,
      const std::shared_ptr<const Box_cmpd>& cmpd,
      const std::shared_ptr<const Box_uncC>& uncC) const = 0;
};

#endif
