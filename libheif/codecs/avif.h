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

#ifndef HEIF_AVIF_H
#define HEIF_AVIF_H

#include <cassert>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "libheif/heif.h"
#include "box.h"
#include "error.h"
#include <codecs/image_item.h>


class Box_av1C : public Box
{
public:
  Box_av1C()
  {
    set_short_type(fourcc("av1C"));
  }

  bool is_essential() const override { return true; }

  struct configuration
  {
    //unsigned int (1) marker = 1;
    uint8_t version = 1;
    uint8_t seq_profile = 0;
    uint8_t seq_level_idx_0 = 0;
    uint8_t seq_tier_0 = 0;
    uint8_t high_bitdepth = 0;
    uint8_t twelve_bit = 0;
    uint8_t monochrome = 0;
    uint8_t chroma_subsampling_x = 0;
    uint8_t chroma_subsampling_y = 0;
    uint8_t chroma_sample_position = 0;
    //uint8_t reserved = 0;

    uint8_t initial_presentation_delay_present = 0;
    uint8_t initial_presentation_delay_minus_one = 0;

    //unsigned int (8)[] configOBUs;

    heif_chroma get_heif_chroma() const {
      if (chroma_subsampling_x==2 && chroma_subsampling_y==2) {
        return heif_chroma_420;
      }
      else if (chroma_subsampling_x==2 && chroma_subsampling_y==1) {
        return heif_chroma_422;
      }
      else if (chroma_subsampling_x==1 && chroma_subsampling_y==1) {
        return heif_chroma_444;
      }
      else {
        return heif_chroma_undefined;
      }
    }
  };


  std::string dump(Indent&) const override;

  bool get_headers(std::vector<uint8_t>* dest) const
  {
    *dest = m_config_OBUs;
    return true;
  }

  void set_configuration(const configuration& config) { m_configuration = config; }

  const configuration& get_configuration() const { return m_configuration; }

  //void append_nal_data(const std::vector<uint8_t>& nal);
  //void append_nal_data(const uint8_t* data, size_t size);

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;

private:
  configuration m_configuration;

  std::vector<uint8_t> m_config_OBUs;
};


class Box_a1op : public Box
{
public:
  Box_a1op()
  {
    set_short_type(fourcc("a1op"));
  }

  uint8_t op_index = 0;

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;
};


class Box_a1lx : public Box
{
public:
  Box_a1lx()
  {
    set_short_type(fourcc("a1lx"));
  }

  uint32_t layer_size[3]{};

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;
};


class HeifPixelImage;

Error fill_av1C_configuration(Box_av1C::configuration* inout_config, const std::shared_ptr<HeifPixelImage>& image);

bool fill_av1C_configuration_from_stream(Box_av1C::configuration* out_config, const uint8_t* data, int dataSize);


class ImageItem_AVIF : public ImageItem
{
public:
  ImageItem_AVIF(HeifContext* ctx, heif_item_id id) : ImageItem(ctx, id) {}

  ImageItem_AVIF(HeifContext* ctx) : ImageItem(ctx) {}

  uint32_t get_infe_type() const override { return fourcc("av01"); }

  const char* get_auxC_alpha_channel_type() const override { return "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha"; }

  const heif_color_profile_nclx* get_forced_output_nclx() const override { return nullptr; }

  heif_compression_format get_compression_format() const override { return heif_compression_AV1; }

  Error on_load_file() override;

public:
  Result<CodedImageData> encode(const std::shared_ptr<HeifPixelImage>& image,
                                struct heif_encoder* encoder,
                                const struct heif_encoding_options& options,
                                enum heif_image_input_class input_class) override;

protected:
  Result<std::vector<uint8_t>> read_bitstream_configuration_data(heif_item_id itemId) const override;

  std::shared_ptr<class Decoder> get_decoder() const override;

private:
  std::shared_ptr<class Decoder_AVIF> m_decoder;
};

#endif
