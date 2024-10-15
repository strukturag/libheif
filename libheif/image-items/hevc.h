/*
 * HEIF codec.
 * Copyright (c) 2017 Dirk Farin <dirk.farin@gmail.com>
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

#ifndef HEIF_HEVC_H
#define HEIF_HEVC_H

#include "libheif/heif.h"
#include "box.h"
#include "error.h"

#include <memory>
#include <string>
#include <vector>
#include "image_item.h"


class ImageItem_HEVC : public ImageItem
{
public:
  ImageItem_HEVC(HeifContext* ctx, heif_item_id id) : ImageItem(ctx, id) {}

  ImageItem_HEVC(HeifContext* ctx) : ImageItem(ctx) {}

  uint32_t get_infe_type() const override { return fourcc("hvc1"); }

  // TODO: MIAF says that the *:hevc:* urn is deprecated and we should use "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha"
  const char* get_auxC_alpha_channel_type() const override { return "urn:mpeg:hevc:2015:auxid:1"; }

  const heif_color_profile_nclx* get_forced_output_nclx() const override { return nullptr; }

  heif_compression_format get_compression_format() const override { return heif_compression_HEVC; }

  Error on_load_file() override;

  Result<CodedImageData> encode(const std::shared_ptr<HeifPixelImage>& image,
                                struct heif_encoder* encoder,
                                const struct heif_encoding_options& options,
                                enum heif_image_input_class input_class) override;

  // currently not used
  void set_preencoded_hevc_image(const std::vector<uint8_t>& data);

protected:
  Result<std::vector<uint8_t>> read_bitstream_configuration_data(heif_item_id itemId) const override;

  std::shared_ptr<class Decoder> get_decoder() const override;

private:
  std::shared_ptr<class Decoder_HEVC> m_decoder;
};

#endif
