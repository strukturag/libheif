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

#include "chunk.h"
#include "context.h"

Chunk::Chunk(HeifContext* ctx)
    : m_ctx(ctx)
{
}

Chunk::Chunk(HeifContext* ctx, uint32_t track_id, std::shared_ptr<const Box_VisualSampleEntry> sample_description_box,
             uint32_t first_sample, uint32_t num_samples, uint64_t file_offset, const uint32_t* sample_sizes)
{
  m_ctx = ctx;
  m_track_id = track_id;

  m_first_sample = first_sample;
  m_last_sample = first_sample + num_samples - 1;

  m_next_sample_to_be_decoded = first_sample;

  for (uint32_t i=0;i<num_samples;i++) {
    SampleFileRange range;
    range.offset = file_offset;
    range.size = sample_sizes[i];
    m_sample_ranges.push_back(range);

    file_offset += range.size;
  }

  m_decoder = Decoder::alloc_for_sequence_sample_description_box(sample_description_box);
}


DataExtent Chunk::get_data_extent_for_sample(uint32_t n) const
{
  assert(n>= m_first_sample);
  assert(n<= m_last_sample);

  DataExtent extent;
  extent.set_file_range(m_ctx->get_heif_file(),
                        m_sample_ranges[n - m_first_sample].offset,
                        m_sample_ranges[n - m_first_sample].size);
  return extent;
}