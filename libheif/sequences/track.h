/*
 * HEIF image base codec.
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

#ifndef LIBHEIF_TRACK_H
#define LIBHEIF_TRACK_H

#include "error.h"
#include "libheif/heif_plugin.h"
#include "libheif/heif_experimental.h"

class HeifContext;

class HeifPixelImage;

class Chunk;

class Box_trak;


class SampleAuxInfoHelper
{
public:
  SampleAuxInfoHelper(bool interleaved = false);

  void set_aux_info_type(uint32_t aux_info_type, uint32_t aux_info_type_parameter = 0);

  Error add_sample_info(const std::vector<uint8_t>& data);

  void add_nonpresent_sample();

  void write_interleaved(const std::shared_ptr<class HeifFile>& file);

  void write_all(const std::shared_ptr<class Box>& parent, const std::shared_ptr<class HeifFile>& file);

private:
  std::shared_ptr<class Box_saiz> m_saiz;
  std::shared_ptr<class Box_saio> m_saio;

  std::vector<uint8_t> m_data;

  bool m_interleaved;
};


class SampleAuxInfoReader
{
public:
  SampleAuxInfoReader(std::shared_ptr<Box_saiz>,
                      std::shared_ptr<Box_saio>);

  Result<std::vector<uint8_t>> get_sample_info(const HeifFile* file, uint32_t idx);

private:
  std::shared_ptr<class Box_saiz> m_saiz;
  std::shared_ptr<class Box_saio> m_saio;

  bool m_contiguous;
  std::vector<uint64_t> m_contiguous_offsets;
};


class Track : public ErrorBuffer {
public:
  //Track(HeifContext* ctx);

  Track(HeifContext* ctx, uint32_t track_id, heif_track_info* info, uint32_t handler_type);

  Track(HeifContext* ctx, const std::shared_ptr<Box_trak>&); // when reading the file

  virtual ~Track();

  // Allocate a Track of the correct sub-class (visual or metadata)
  static std::shared_ptr<Track> alloc_track(HeifContext*, const std::shared_ptr<Box_trak>&);

  heif_item_id get_id() const { return m_id; }

  std::shared_ptr<class HeifFile> get_file() const;

  uint32_t get_handler() const { return m_handler_type; }

  bool is_visual_track() const;

  uint32_t get_first_cluster_sample_entry_type() const;

  std::string get_first_cluster_urim_uri() const;

  uint64_t get_duration_in_media_units() const;

  uint32_t get_timescale() const;

  // The context will compute the duration in global movie units and set this.
  void set_track_duration_in_movie_units(uint64_t total_duration);

  std::shared_ptr<class Box_taic> get_first_cluster_taic() { return m_first_taic; }

  bool end_of_sequence_reached() const;

  // Compute some parameters after all frames have been encoded (for example: track duration).
  void finalize_track();

  const heif_track_info* get_track_info() const { return m_track_info; }

  void add_reference_to_track(uint32_t referenceType, uint32_t to_track_id);

protected:
  HeifContext* m_heif_context = nullptr;
  uint32_t m_id = 0;
  uint32_t m_handler_type = 0;

  heif_track_info* m_track_info = nullptr;

  uint32_t m_num_samples = 0;
  uint32_t m_current_chunk = 0;
  uint32_t m_next_sample_to_be_processed = 0;

  std::vector<std::shared_ptr<Chunk>> m_chunks;

  std::shared_ptr<class Box_moov> m_moov;
  std::shared_ptr<class Box_trak> m_trak;
  std::shared_ptr<class Box_tkhd> m_tkhd;
  std::shared_ptr<class Box_minf> m_minf;
  std::shared_ptr<class Box_mdhd> m_mdhd;
  std::shared_ptr<class Box_hdlr> m_hdlr;
  std::shared_ptr<class Box_stbl> m_stbl;
  std::shared_ptr<class Box_stsd> m_stsd;
  std::shared_ptr<class Box_stsc> m_stsc;
  std::shared_ptr<class Box_stco> m_stco;
  std::shared_ptr<class Box_stts> m_stts;
  std::shared_ptr<class Box_stss> m_stss;
  std::shared_ptr<class Box_stsz> m_stsz;

  std::shared_ptr<class Box_tref> m_tref; // optional

  // --- sample auxiliary information

  std::unique_ptr<SampleAuxInfoHelper> m_aux_helper_tai_timestamps;
  std::unique_ptr<SampleAuxInfoHelper> m_aux_helper_content_ids;

  std::unique_ptr<SampleAuxInfoReader> m_aux_reader_tai_timestamps;
  std::unique_ptr<SampleAuxInfoReader> m_aux_reader_content_ids;

  std::shared_ptr<class Box_taic> m_first_taic; // the TAIC of the first chunk


  // --- Helper functions for writing samples.

  // Call when we begin a new chunk of samples, e.g. because the compression format changed
  void add_chunk(heif_compression_format format);

  // Call to set the sample_description_box for the last added chunk.
  // Has to be called when we call add_chunk().
  // It is not merged with add_chunk() because the sample_description_box may need information from the
  // first encoded frame.
  void set_sample_description_box(std::shared_ptr<Box> sample_description_box);

  // Write the actual sample data. `tai` may be null and `gimi_contentID` may be empty.
  // In these cases, no timestamp or no contentID will be written, respectively.
  Error write_sample_data(const std::vector<uint8_t>& raw_data, uint32_t sample_duration, bool is_sync_sample,
                          const heif_tai_timestamp_packet* tai, const std::string& gimi_contentID);
};


#endif //LIBHEIF_TRACK_H
