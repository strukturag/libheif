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


  std::string dump(Indent &) const override;

  Error write(StreamWriter &writer) const override;

protected:
  Error parse(BitstreamRange &range) override;

  std::string profileIndicationAsText() const;

private:
  configuration m_configuration;
  std::vector< std::vector<uint8_t> > m_sps;
  std::vector< std::vector<uint8_t> > m_pps;
};

#endif
