/*
 * HEIF AVC codec.
 * Copyright (c) 2023 Brad Hards <bradh@frogmouth.net>
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

#ifndef HEIF_AVC_H
#define HEIF_AVC_H

#include "box.h"
#include "error.h"
#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include "codecs/image_item.h"


class Box_avcC : public Box {
public:
  Box_avcC() { set_short_type(fourcc("avcC")); }

  bool is_essential() const override { return true; }

  struct configuration {
    uint8_t configuration_version;
    uint8_t AVCProfileIndication; // profile_idc
    uint8_t profile_compatibility; // constraint set flags
    uint8_t AVCLevelIndication; // level_idc
    uint8_t lengthSize;
    heif_chroma chroma_format = heif_chroma_420; // Note: avcC integer value can be cast to heif_chroma enum
    uint8_t bit_depth_luma = 8;
    uint8_t bit_depth_chroma = 8;
  };

  void set_configuration(const configuration& config)
  {
    m_configuration = config;
  }

  const configuration& get_configuration() const
  {
    return m_configuration;
  }

  const std::vector< std::vector<uint8_t> > getSequenceParameterSets() const
  {
    return m_sps;
  }

  const std::vector< std::vector<uint8_t> > getPictureParameterSets() const
  {
    return m_pps;
  }

  const std::vector< std::vector<uint8_t> > getSequenceParameterSetExt() const
  {
    return m_sps_ext;
  }

  void get_header_nals(std::vector<uint8_t>& data) const;

  std::string dump(Indent &) const override;

  Error write(StreamWriter &writer) const override;

protected:
  Error parse(BitstreamRange &range) override;

  std::string profileIndicationAsText() const;

private:
  configuration m_configuration;
  std::vector< std::vector<uint8_t> > m_sps;
  std::vector< std::vector<uint8_t> > m_pps;
  std::vector< std::vector<uint8_t> > m_sps_ext;
};


class ImageItem_AVC : public ImageItem
{
public:
  ImageItem_AVC(HeifContext* ctx, heif_item_id id) : ImageItem(ctx, id) {}

  ImageItem_AVC(HeifContext* ctx) : ImageItem(ctx) {}

  uint32_t get_infe_type() const override { return fourcc("avc1"); }

  const char* get_auxC_alpha_channel_type() const override { return "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha"; }

  const heif_color_profile_nclx* get_forced_output_nclx() const override { return nullptr; }

  heif_compression_format get_compression_format() const override { return heif_compression_AVC; }

  Error on_load_file() override;

protected:
  std::shared_ptr<Decoder> get_decoder() const override;

public:
  Result<CodedImageData> encode(const std::shared_ptr<HeifPixelImage>& image,
                                struct heif_encoder* encoder,
                                const struct heif_encoding_options& options,
                                enum heif_image_input_class input_class) override;

  std::shared_ptr<class Decoder_AVC> m_decoder;
};

#endif
