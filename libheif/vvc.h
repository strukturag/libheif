/*
 * HEIF VVC codec.
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

#ifndef LIBHEIF_VVC_H
#define LIBHEIF_VVC_H

#include "box.h"
#include <string>
#include <vector>


class Box_vvcC : public Box
{
public:
  Box_vvcC()
  {
    set_short_type(fourcc("vvcC"));
  }

  struct configuration
  {
    uint8_t configurationVersion = 1;
    uint16_t avgFrameRate_times_256;
    uint8_t constantFrameRate; // 2 bits
    uint8_t numTemporalLayers; // 3 bits
    uint8_t lengthSize;        // 2 bits
    bool ptl_present_flag;
    //if (ptl_present_flag) {
    //  VvcPTLRecord(numTemporalLayers) track_ptl;
    //  uint16_t output_layer_set_idx;
    //}
    bool chroma_format_present_flag;
    uint8_t chroma_format_idc;

    bool bit_depth_present_flag;
    uint8_t bit_depth;
  };


  std::string dump(Indent&) const override;

  bool get_headers(std::vector<uint8_t>* dest) const
  {
    // TODO

#if 0
    *dest = m_config_NALs;
#endif
    return true;
  }

  void set_configuration(const configuration& config) { m_configuration = config; }

  const configuration& get_configuration() const { return m_configuration; }

  void append_nal_data(const std::vector<uint8_t>& nal);
  void append_nal_data(const uint8_t* data, size_t size);

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;

private:
    struct NalArray
    {
      bool m_array_completeness;
      uint8_t m_NAL_unit_type;

      std::vector<std::vector<uint8_t> > m_nal_units;
    };

  configuration m_configuration;
  //uint8_t m_length_size = 4; // default: 4 bytes for NAL unit lengths

  std::vector<NalArray> m_nal_array;
  //std::vector<uint8_t> m_config_NALs;
};


#endif // LIBHEIF_VVC_H
