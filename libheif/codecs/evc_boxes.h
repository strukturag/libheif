/*
 * HEIF EVC codec.
 * Copyright (c) 2024 Brad Hards <bradh@frogmouth.net>
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

#ifndef HEIF_EVC_BOXES_H
#define HEIF_EVC_BOXES_H

#include "box.h"
#include "error.h"
#include <string>
#include <vector>

class Box_evcC : public Box {
public:
  Box_evcC() { set_short_type(fourcc("evcC")); }

  bool is_essential() const override { return true; }

  static const uint8_t CHROMA_FORMAT_MONOCHROME = 0;
  static const uint8_t CHROMA_FORMAT_420 = 1;
  static const uint8_t CHROMA_FORMAT_422 = 2;
  static const uint8_t CHROMA_FORMAT_444 = 3;

  struct configuration {
    uint8_t configurationVersion = 1;
    uint8_t profile_idc;
    uint8_t level_idc;
    uint32_t toolset_idc_h;
    uint32_t toolset_idc_l;
    uint8_t chroma_format_idc;
    uint8_t bit_depth_luma;
    uint8_t bit_depth_chroma;
    uint16_t pic_width_in_luma_samples;
    uint16_t pic_height_in_luma_samples;
    uint8_t lengthSize = 0;
  };

  void set_configuration(const configuration& config)
  {
    m_configuration = config;
  }

  const configuration& get_configuration() const
  {
    return m_configuration;
  }

  std::string dump(Indent &) const override;

  Error write(StreamWriter &writer) const override;

  void get_header_nals(std::vector<uint8_t>& data) const;

protected:
  Error parse(BitstreamRange &range, const heif_security_limits* limits) override;

private:
  configuration m_configuration;
  struct NalArray
  {
    bool array_completeness;
    uint8_t NAL_unit_type;

    std::vector<std::vector<uint8_t> > nal_units;
  };

  std::vector<NalArray> m_nal_array;

  std::string get_profile_as_text() const;
  std::string get_chroma_format_as_text() const;
  std::string get_NAL_unit_type_as_text(uint8_t nal_unit_type) const;
};

#endif
