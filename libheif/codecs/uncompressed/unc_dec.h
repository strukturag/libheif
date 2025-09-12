/*
 * HEIF codec.
 * Copyright (c) 2024 Dirk Farin <dirk.farin@gmail.com>
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

#ifndef HEIF_UNC_DEC_H
#define HEIF_UNC_DEC_H

#include "libheif/heif.h"
#include "box.h"
#include "error.h"

#include <memory>
#include <vector>
#include "codecs/decoder.h"

class Box_uncC;
class Box_cmpd;


class Decoder_uncompressed : public Decoder
{
public:
  explicit Decoder_uncompressed(const std::shared_ptr<const Box_uncC>& uncC,
                                const std::shared_ptr<const Box_cmpd>& cmpd,
                                const std::shared_ptr<const Box_ispe>& ispe) : m_uncC(uncC), m_cmpd(cmpd), m_ispe(ispe) {}

  heif_compression_format get_compression_format() const override { return heif_compression_uncompressed; }

  int get_luma_bits_per_pixel() const override;

  int get_chroma_bits_per_pixel() const override;

  Error get_coded_image_colorspace(heif_colorspace*, heif_chroma*) const override;

  bool has_alpha_component() const;

  Result<std::vector<uint8_t>> read_bitstream_configuration_data() const override;

  Result<std::shared_ptr<HeifPixelImage>>
  decode_single_frame_from_compressed_data(const struct heif_decoding_options& options,
                                           const struct heif_security_limits* limits) override;

private:
  const std::shared_ptr<const Box_uncC> m_uncC;
  const std::shared_ptr<const Box_cmpd> m_cmpd;
  const std::shared_ptr<const Box_ispe> m_ispe;
};

#endif
