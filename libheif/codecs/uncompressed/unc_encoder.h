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

#ifndef LIBHEIF_UNC_ENCODER_H
#define LIBHEIF_UNC_ENCODER_H
#include <memory>

#include "error.h"
#include "codecs/encoder.h"
#include "libheif/heif_encoding.h"

class Box_uncC;
class Box_cmpd;
class HeifPixelImage;


class unc_encoder
{
public:
  virtual ~unc_encoder() = default;

  virtual bool can_encode(const std::shared_ptr<const HeifPixelImage>& image,
                          const heif_encoding_options& options) const = 0;

  virtual void fill_cmpd_and_uncC(std::shared_ptr<Box_cmpd>& out_cmpd,
                                  std::shared_ptr<Box_uncC>& out_uncC,
                                  const std::shared_ptr<const HeifPixelImage>& image,
                                  const heif_encoding_options& options) const = 0;

  [[nodiscard]] virtual std::vector<uint8_t> encode_tile(const std::shared_ptr<const HeifPixelImage>& image,
                                           const heif_encoding_options& options) const = 0;


  static Result<const unc_encoder*> get_unc_encoder(const std::shared_ptr<const HeifPixelImage>& prototype_image,
                                                    const heif_encoding_options& options);

  Result<Encoder::CodedImageData> encode_static(const std::shared_ptr<const HeifPixelImage>& src_image,
                                                const heif_encoding_options& options) const;

  static Result<Encoder::CodedImageData> encode_full_image(const std::shared_ptr<const HeifPixelImage>& src_image,
                                                           const heif_encoding_options& options);
};

#endif //LIBHEIF_UNC_ENCODER_H
