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

#ifndef HEIF_AVIF_BOXES_H
#define HEIF_AVIF_BOXES_H

#include <cassert>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "libheif/heif.h"
#include "box.h"
#include "error.h"
#include "image-items/image_item.h"
#include "sequences/seq_boxes.h"


class Box_av1C : public Box
{

// allow access to protected parse() method
friend class Box_mini;

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
    uint8_t chroma_subsampling_x = 0; // (minus 1) that is: either 0 or 1
    uint8_t chroma_subsampling_y = 0; // (minus 1)
    uint8_t chroma_sample_position = 0;
    //uint8_t reserved = 0;

    uint8_t initial_presentation_delay_present = 0;
    uint8_t initial_presentation_delay_minus_one = 0;

    //unsigned int (8)[] configOBUs;

    heif_chroma get_heif_chroma() const {
      if (monochrome) {
        return heif_chroma_monochrome;
      }
      else if (chroma_subsampling_x==1 && chroma_subsampling_y==1) {
        return heif_chroma_420;
      }
      else if (chroma_subsampling_x==1 && chroma_subsampling_y==0) {
        return heif_chroma_422;
      }
      else if (chroma_subsampling_x==0 && chroma_subsampling_y==0) {
        return heif_chroma_444;
      }
      else {
        return heif_chroma_undefined;
      }
    }
  };


  void dump(std::ostream&, Indent&, bool full_log) const override;

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
  Error parse(BitstreamRange& range, const heif_security_limits* limits) override;

private:
  configuration m_configuration;

  std::vector<uint8_t> m_config_OBUs;
};


class Box_av01 : public Box_VisualSampleEntry
{
public:
  Box_av01()
  {
    set_short_type(fourcc("av01"));
  }
};

class Box_a1op : public Box
{
public:
  Box_a1op()
  {
    set_short_type(fourcc("a1op"));
  }

  uint8_t op_index = 0;

  void dump(std::ostream&, Indent&, bool full_log) const override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range, const heif_security_limits* limits) override;
};


class Box_a1lx : public Box
{
public:
  Box_a1lx()
  {
    set_short_type(fourcc("a1lx"));
  }

  uint32_t layer_size[3]{};

  void dump(std::ostream&, Indent&, bool full_log) const override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range, const heif_security_limits* limits) override;
};


class HeifPixelImage;

Error fill_av1C_configuration(Box_av1C::configuration* inout_config, const std::shared_ptr<HeifPixelImage>& image);

bool fill_av1C_configuration_from_stream(Box_av1C::configuration* out_config, const uint8_t* data, int dataSize);

// In an AV1 ISOBMFF sample, make a terminal OBU without a size field explicit:
// set obu_has_size_field and insert a LEB128 size covering the sample remainder.
// This makes the framing passed to a decoder unambiguous. All preceding OBUs
// must carry valid sizes. `sample_offset` skips any prepended av1C configOBUs.
Error normalize_av1_sample_data(std::vector<uint8_t>& data, size_t sample_offset);

// Scan an explicitly-sized AV1 OBU stream for every OBU_SEQUENCE_HEADER and
// return the largest coded frame size (in luma samples) declared by any of them.
// The bool result is false when a complete scan found no sequence header, in
// which case the size outputs are left untouched. Malformed input that prevents
// a complete scan is returned as an error so that a partial result cannot be
// used as a security bound.
Result<bool> find_max_av1_frame_size_in_stream(const uint8_t* data, size_t dataSize,
                                               uint32_t* out_max_width, uint32_t* out_max_height,
                                               uint32_t* out_frame_count = nullptr);

#endif
