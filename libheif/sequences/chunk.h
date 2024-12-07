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

class Box_VisualSampleEntry;


class Chunk
{
public:
  Chunk(HeifContext* ctx);

  Chunk(HeifContext* ctx, uint32_t track_id, std::shared_ptr<const Box_VisualSampleEntry> sample_description_box,
        uint32_t first_sample, uint32_t num_samples, uint64_t file_offset, const uint32_t* sample_sizes);

  virtual ~Chunk() = default;

  virtual std::shared_ptr<class Decoder> get_decoder() const { return m_decoder; }

  uint32_t first_sample_number() const { return m_first_sample; }

  uint32_t last_sample_number() const { return m_last_sample; }

  DataExtent get_data_extent_for_sample(uint32_t n) const;

private:
  HeifContext* m_ctx = nullptr;
  uint32_t m_track_id = 0;

  uint32_t m_first_sample = 0;
  uint32_t m_last_sample = 0;

  uint32_t m_sample_description_index = 0;

  uint32_t m_next_sample_to_be_decoded = 0;

  struct SampleFileRange
  {
    uint64_t offset = 0;
    uint32_t size = 0;
  };

  std::vector<SampleFileRange> m_sample_ranges;

  std::shared_ptr<class Decoder> m_decoder;
};


#endif //LIBHEIF_CHUNK_H
