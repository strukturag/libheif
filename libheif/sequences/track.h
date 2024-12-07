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

class HeifContext;

class HeifPixelImage;

class Chunk;

class Box_trak;


class Track : public ErrorBuffer {
public:
  Track(HeifContext* ctx);

  Track(HeifContext* ctx, uint32_t track_id);

  Track(HeifContext* ctx, const std::shared_ptr<Box_trak>&); // when reading the file

  virtual ~Track() = default;

  heif_item_id get_id() const { return m_id; }

  std::shared_ptr<class HeifFile> get_file() const;

  //uint32_t get_handler() const;

  bool is_visual_track() const;

  bool end_of_sequence_reached() const;

  Result<std::shared_ptr<HeifPixelImage>> decode_next_image_sample(const struct heif_decoding_options& options);

private:
  HeifContext* m_heif_context = nullptr;
  uint32_t m_id = 0;

  uint32_t m_num_samples = 0;
  uint32_t m_current_chunk = 0;
  uint32_t m_next_sample_to_be_decoded = 0;

  std::vector<std::shared_ptr<Chunk>> m_chunks;
};


#endif //LIBHEIF_TRACK_H