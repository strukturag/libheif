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

class Box_container : public Box
{
public:
  Box_container(const char* type)
  {
    set_short_type(fourcc(type));
  }

  std::string dump(Indent&) const override;

protected:
  Error parse(BitstreamRange& range, const heif_security_limits*) override;
};


class Box_moov : public Box_container
{
public:
  Box_moov() : Box_container("moov") {}
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


class Box_trak : public Box_container
{
public:
  Box_trak() : Box_container("trak") {}
};


class Box_tkhd : public FullBox
{
public:
  Box_tkhd()
  {
    set_short_type(fourcc("tkhd"));
  }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

  void derive_box_version() override;

  float get_volume() const { return float(m_volume) / float(0x100); }

  double get_matrix_element(int idx) const;

protected:
  Error parse(BitstreamRange& range, const heif_security_limits*) override;

private:
  uint64_t m_creation_time;
  uint64_t m_modification_time;
  uint32_t m_track_id;
  uint64_t m_duration;

  uint16_t m_layer;
  uint16_t m_alternate_group;
  uint16_t m_volume = 0x0100; // typically, full volume

  uint32_t m_matrix[9] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};

  uint32_t m_width;
  uint32_t m_height;
};


class Box_mdia : public Box_container
{
public:
  Box_mdia() : Box_container("mdia") {}
};


class Box_mdhd : public FullBox
{
public:
  Box_mdhd()
  {
    set_short_type(fourcc("mdhd"));
  }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

  void derive_box_version() override;

  double get_matrix_element(int idx) const;

protected:
  Error parse(BitstreamRange& range, const heif_security_limits*) override;

private:
  uint64_t m_creation_time;
  uint64_t m_modification_time;
  uint32_t m_timescale;
  uint64_t m_duration;

  char m_language[4];
};


class Box_minf : public Box_container
{
public:
  Box_minf() : Box_container("minf") {}
};


class Box_vmhd : public FullBox
{
public:
  Box_vmhd()
  {
    set_short_type(fourcc("vmhd"));
    set_flags(1);
  }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range, const heif_security_limits*) override;

private:
  uint16_t m_graphics_mode = 0;
  uint16_t m_op_color[3] = { 0,0,0 };
};


class Box_stbl : public Box_container
{
public:
  Box_stbl() : Box_container("stbl") {}
};


#endif //SEQ_BOXES_H
