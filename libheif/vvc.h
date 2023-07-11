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
    uint8_t constantFrameRate;
    uint8_t numTemporalLayers;
    uint8_t lengthSize;
    bool ptl_present_flag;
    //if (ptl_present_flag) {
    //  VvcPTLRecord(numTemporalLayers) track_ptl;
    //  uint16_t output_layer_set_idx;
    //}
    bool chroma_format_present_flag;
    uint8_t chroma_format_idc;

    bool bit_depth_present_flag;
    uint8_t bit_depth;

    uint8_t numOfArrays;
#if 0
    for (j=0; j < numOfArrays; j++) {
      unsigned int(1) array_completeness;
      bit(1) reserved = 0;
      unsigned int(6) NAL_unit_type;
      unsigned int(16) numNalus;
      for (i=0; i< numNalus; i++) {
        unsigned int(16) nalUnitLength;
        bit(8*nalUnitLength) nalUnit;
      }
    }
#endif
  };


  std::string dump(Indent&) const override;

  bool get_headers(std::vector<uint8_t>* dest) const
  {
    *dest = m_config_NALs;
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

  std::vector<uint8_t> m_config_NALs;
};


#endif // LIBHEIF_VVC_H
