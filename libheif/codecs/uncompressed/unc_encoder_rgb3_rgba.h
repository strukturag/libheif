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

#ifndef LIBHEIF_UNC_ENCODER_RGB3_RGBA_H
#define LIBHEIF_UNC_ENCODER_RGB3_RGBA_H
#include "unc_encoder.h"


class unc_encoder_rgb3_rgba : public unc_encoder
{
public:
  unc_encoder_rgb3_rgba(const std::shared_ptr<const HeifPixelImage>& image,
                        const heif_encoding_options& options);

  [[nodiscard]] std::vector<uint8_t> encode_tile(const std::shared_ptr<const HeifPixelImage>& image) const override;
};


class unc_encoder_factory_rgb3_rgba : public unc_encoder_factory
{
public:
  [[nodiscard]] bool can_encode(const std::shared_ptr<const HeifPixelImage>& image,
                                const heif_encoding_options& options) const override;

  std::unique_ptr<const unc_encoder> create(const std::shared_ptr<const HeifPixelImage>& image,
                                            const heif_encoding_options& options) const override;
};

#endif //LIBHEIF_UNC_ENCODER_RGB3_RGBA_H
