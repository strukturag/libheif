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
  unc_encoder();

  virtual ~unc_encoder() = default;

  std::shared_ptr<Box_cmpd> get_cmpd() const { return m_cmpd; }
  std::shared_ptr<Box_uncC> get_uncC() const { return m_uncC; }


  [[nodiscard]] virtual std::vector<uint8_t> encode_tile(const std::shared_ptr<const HeifPixelImage>& image) const = 0;

  Result<Encoder::CodedImageData> encode_static(const std::shared_ptr<const HeifPixelImage>& src_image,
                                                const heif_encoding_options& options) const;

  static Result<Encoder::CodedImageData> encode_full_image(const std::shared_ptr<const HeifPixelImage>& src_image,
                                                           const heif_encoding_options& options);

protected:
  std::shared_ptr<Box_cmpd> m_cmpd;
  std::shared_ptr<Box_uncC> m_uncC;
};


class unc_encoder_factory
{
public:
  virtual ~unc_encoder_factory() = default;

  static Result<std::unique_ptr<const unc_encoder> > get_unc_encoder(const std::shared_ptr<const HeifPixelImage>& prototype_image,
                                                                     const heif_encoding_options& options);

  virtual bool can_encode(const std::shared_ptr<const HeifPixelImage>& image,
                          const heif_encoding_options& options) const = 0;

  virtual std::unique_ptr<const unc_encoder> create(const std::shared_ptr<const HeifPixelImage>& prototype_image,
                                                    const heif_encoding_options& options) const = 0;
};

#endif //LIBHEIF_UNC_ENCODER_H
