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

#ifndef LIBHEIF_CHUNK_H
#define LIBHEIF_CHUNK_H

#include <codecs/decoder.h>


class HeifContext;


class Chunk {
  Chunk(HeifContext* ctx);

  Chunk(HeifContext* ctx, uint32_t track_id,
        uint32_t first_sample, uint32_t num_samples, uint64_t file_offset, const uint32_t* sample_sizes);

  virtual ~Chunk() = default;

  virtual std::shared_ptr<class Decoder> get_decoder() const { return nullptr; }

private:
  HeifContext* m_ctx;
  uint32_t track_id;

  uint32_t m_first_sample;
  uint32_t m_last_sample;

  uint32_t m_sample_description_index;

  uint32_t m_next_sample_to_be_decoded = 0;

  struct SampleRange {
    uint64_t offset;
    uint32_t size;
  };

  std::vector<SampleRange> m_sample_ranges;

  //std::shared_ptr<class Decoder> decoder;
};


#endif //LIBHEIF_CHUNK_H
