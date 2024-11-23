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

#ifndef SEQ_BOXES_H
#define SEQ_BOXES_H

#include "box.h"

class Box_moov : public Box
{
public:
  Box_moov()
  {
    set_short_type(fourcc("moov"));
  }

  std::string dump(Indent&) const override;

protected:
  Error parse(BitstreamRange& range, const heif_security_limits*) override;
};


class Box_mvhd : public FullBox
{
public:
  Box_mvhd()
  {
    set_short_type(fourcc("mvhd"));
  }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

  void derive_box_version() override;

  double get_rate() const { return m_rate / double(0x10000); }
  float get_volume() const { return float(m_volume) / float(0x100); }

  double get_matrix_element(int idx) const;

protected:
  Error parse(BitstreamRange& range, const heif_security_limits*) override;

private:
  uint64_t m_creation_time;
  uint64_t m_modification_time;
  uint64_t m_timescale;
  uint64_t m_duration;

  uint32_t m_rate = 0x00010000; // typically 1.0
  uint16_t m_volume = 0x0100; // typically, full volume

  uint32_t m_matrix[9] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
  uint32_t m_next_track_ID;
};


class Box_trak : public Box
{
public:
  Box_trak()
  {
    set_short_type(fourcc("trak"));
  }

  std::string dump(Indent&) const override;

protected:
  Error parse(BitstreamRange& range, const heif_security_limits*) override;
};


#endif //SEQ_BOXES_H
