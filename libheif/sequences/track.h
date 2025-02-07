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


class Track : public ErrorBuffer {
public:
  //Track(HeifContext* ctx);

  Track(HeifContext* ctx, uint32_t track_id, uint16_t width, uint16_t height,
        heif_track_info* info);

  Track(HeifContext* ctx, const std::shared_ptr<Box_trak>&); // when reading the file

  virtual ~Track();

  heif_item_id get_id() const { return m_id; }

  std::shared_ptr<class HeifFile> get_file() const;

  uint32_t get_handler() const { return m_handler_type; }

  bool is_visual_track() const;

  uint16_t get_width() const { return m_width; }

  uint16_t get_height() const { return m_height; }

  uint64_t get_duration_in_media_units() const;

  uint32_t get_timescale() const;

  // The context will compute the duration in global movie units and set this.
  void set_track_duration_in_movie_units(uint64_t total_duration);

  bool end_of_sequence_reached() const;

  Result<std::shared_ptr<HeifPixelImage>> decode_next_image_sample(const struct heif_decoding_options& options);

  Error encode_image(std::shared_ptr<HeifPixelImage> image,
                     struct heif_encoder* encoder,
                     const struct heif_encoding_options& options,
                     heif_image_input_class image_class);

  // Compute some parameters after all frames have been encoded (for example: track duration).
  void finalize_track();

private:
  HeifContext* m_heif_context = nullptr;
  uint32_t m_id = 0;
  uint32_t m_handler_type = 0;
  uint16_t m_width = 0;
  uint16_t m_height = 0;

  heif_track_info* m_track_info = nullptr;

  uint32_t m_num_samples = 0;
  uint32_t m_current_chunk = 0;
  uint32_t m_next_sample_to_be_decoded = 0;

  std::vector<std::shared_ptr<Chunk>> m_chunks;

  std::shared_ptr<class Box_moov> m_moov;
  std::shared_ptr<class Box_tkhd> m_tkhd;
  std::shared_ptr<class Box_mdhd> m_mdhd;
  std::shared_ptr<class Box_stbl> m_stbl;
  std::shared_ptr<class Box_stsd> m_stsd;
  std::shared_ptr<class Box_stsc> m_stsc;
  std::shared_ptr<class Box_stco> m_stco;
  std::shared_ptr<class Box_stts> m_stts;
  std::shared_ptr<class Box_stss> m_stss;
  std::shared_ptr<class Box_stsz> m_stsz;

  // --- sample auxiliary information

  std::unique_ptr<SampleAuxInfoHelper> m_aux_helper_tai_timestamps;
  std::unique_ptr<SampleAuxInfoHelper> m_aux_helper_content_ids;
};


#endif //LIBHEIF_TRACK_H
