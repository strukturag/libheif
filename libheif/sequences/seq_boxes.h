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


// Movie Box
class Box_moov : public Box_container
{
public:
  Box_moov() : Box_container("moov") {}
};


// Movie Header Box
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


// Track Box
class Box_trak : public Box_container
{
public:
  Box_trak() : Box_container("trak") {}
};


// Track Header Box
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

  double get_width() const { return float(m_width) / double(0x10000); }

  double get_height() const { return float(m_height) / double(0x10000); }

  uint32_t get_track_id() const { return m_track_id; }

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


// Media Box
class Box_mdia : public Box_container
{
public:
  Box_mdia() : Box_container("mdia") {}
};


// Media Header Box
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


// Media Information Box (container)
class Box_minf : public Box_container
{
public:
  Box_minf() : Box_container("minf") {}
};


// Video Media Header
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


// Sample Table Box (container)
class Box_stbl : public Box_container
{
public:
  Box_stbl() : Box_container("stbl") {}
};


// Sample Description Box
class Box_stsd : public FullBox
{
public:
  Box_stsd()
  {
    set_short_type(fourcc("stsd"));
  }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

  std::shared_ptr<const class Box_VisualSampleEntry> get_sample_entry(size_t idx) const {
    if (idx >= m_sample_entries.size()) {
      return nullptr;
    }
    else {
      return m_sample_entries[idx];
    }
  }

protected:
  Error parse(BitstreamRange& range, const heif_security_limits*) override;

private:
  std::vector<std::shared_ptr<class Box_VisualSampleEntry>> m_sample_entries;
};


// Decoding Time to Sample Box
class Box_stts : public FullBox
{
public:
  Box_stts()
  {
    set_short_type(fourcc("stts"));
  }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

  struct TimeToSample
  {
    uint32_t sample_count;
    uint32_t sample_delta;
  };

protected:
  Error parse(BitstreamRange& range, const heif_security_limits*) override;

private:
  std::vector<TimeToSample> m_entries;
};


// Sample to Chunk Box
class Box_stsc : public FullBox
{
public:
  Box_stsc()
  {
    set_short_type(fourcc("stsc"));
  }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

  struct SampleToChunk
  {
    uint32_t first_chunk;
    uint32_t samples_per_chunk;
    uint32_t sample_description_index;
  };

  const std::vector<SampleToChunk>& get_chunks() const { return m_entries; }

  // idx counting starts at 1
  const SampleToChunk* get_chunk(uint32_t idx) const;

protected:
  Error parse(BitstreamRange& range, const heif_security_limits*) override;

private:
  std::vector<SampleToChunk> m_entries;
};


// Chunk Offset Box
class Box_stco : public FullBox
{
public:
  Box_stco()
  {
    set_short_type(fourcc("stco"));
  }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

  const std::vector<uint32_t>& get_offsets() const { return m_offsets; }

protected:
  Error parse(BitstreamRange& range, const heif_security_limits*) override;

private:
  std::vector<uint32_t> m_offsets;
};


// Sample Size Box
class Box_stsz : public FullBox
{
public:
  Box_stsz()
  {
    set_short_type(fourcc("stsz"));
  }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

  const std::vector<uint32_t>& get_sample_sizes() const { return m_sample_sizes; }

protected:
  Error parse(BitstreamRange& range, const heif_security_limits*) override;

private:
  uint32_t m_fixed_sample_size;
  uint32_t m_sample_count;
  std::vector<uint32_t> m_sample_sizes;
};


// Sync Sample Box
class Box_stss : public FullBox
{
public:
  Box_stss()
  {
    set_short_type(fourcc("stss"));
  }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range, const heif_security_limits*) override;

private:
  std::vector<uint32_t> m_sync_samples;
};


// Coding Constraints Box
class Box_ccst : public FullBox
{
public:
  Box_ccst()
  {
    set_short_type(fourcc("ccst"));
  }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range, const heif_security_limits*) override;

private:
  bool all_ref_pics_intra;
  bool intra_pred_used;
  uint8_t max_ref_per_pic; // 4 bit
};


struct VisualSampleEntry
{
  // from SampleEntry
  //const unsigned int(8)[6] reserved = 0;
  uint16_t data_reference_index;

  // VisualSampleEntry

  uint16_t pre_defined = 0;
  //uint16_t reserved = 0;
  uint32_t pre_defined2[3] = {0,0,0};
  uint16_t width;
  uint16_t height;
  uint32_t horizresolution = 0x00480000; // 72 dpi
  uint32_t vertresolution = 0x00480000; // 72 dpi
  //uint32_t reserved = 0;
  uint16_t frame_count = 1;
  std::string compressorname; // max 32 characters
  uint16_t depth = 0x0018;
  int16_t pre_defined3 = -1;
  // other boxes from derived specifications
  //std::shared_ptr<Box_clap> clap; // optional
  //std::shared_ptr<Box_pixi> pixi; // optional

  double get_horizontal_resolution() const { return horizresolution / double(0x10000); }

  double get_vertical_resolution() const { return vertresolution / double(0x10000); }

  Error parse(BitstreamRange& range, const heif_security_limits*);

  std::string dump(Indent&) const;
};


class Box_VisualSampleEntry : public Box
{
public:
  virtual const VisualSampleEntry& get_VisualSampleEntry() const = 0;
};

#endif //SEQ_BOXES_H