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

#include "track.h"
#include "context.h"
#include "codecs/decoder.h"
#include "sequences/seq_boxes.h"
#include "codecs/hevc_boxes.h"


std::shared_ptr<class HeifFile> Track::get_file() const
{
  return m_heif_context->get_heif_file();
}


Track::Track(HeifContext* ctx, const std::shared_ptr<Box_trak>& trak_box)
{
  m_heif_context = ctx;

  auto tkhd = trak_box->get_child_box<Box_tkhd>();
  if (!tkhd) {
    return; // TODO: error or dummy error track ?
  }

  m_id = tkhd->get_track_id();

  auto mdia = trak_box->get_child_box<Box_mdia>();
  if (!mdia) {
    return;
  }

  auto minf = mdia->get_child_box<Box_minf>();
  if (!minf) {
    return;
  }

  auto stbl = minf->get_child_box<Box_stbl>();
  if (!stbl) {
    return;
  }

  auto stsd = stbl->get_child_box<Box_stsd>();
  if (!stsd) {
    return;
  }

  auto stsc = stbl->get_child_box<Box_stsc>();
  if (!stsc) {
    return;
  }

  auto stco = stbl->get_child_box<Box_stco>();
  if (!stco) {
    return;
  }

  const std::vector<uint32_t>& chunk_offsets = stco->get_offsets();
  assert(chunk_offsets.size() <= (size_t) std::numeric_limits<uint32_t>::max()); // There cannot be more than uint32_t chunks.

  uint32_t current_sample_idx = 0;

  for (size_t chunk_idx = 0; chunk_idx < chunk_offsets.size(); chunk_idx++) {
    auto* s2c = stsc->get_chunk(static_cast<uint32_t>(chunk_idx + 1));
    if (!s2c) {
      return;
    }

    Box_stsc::SampleToChunk sampleToChunk = *s2c;

    auto sample_description = stsd->get_sample_entry(sampleToChunk.sample_description_index - 1);
    if (!sample_description) {
      return;
    }

    //auto chunk = std::make_shared<Chunk>(ctx, m_id,
    //uint32_t first_sample, uint32_t
    //num_samples, uint64_t
    //file_offset,
    //const uint32_t* sample_sizes);

    current_sample_idx += sampleToChunk.samples_per_chunk;
  }

#if 0
  for (const auto& sample_entry : stsd->get_all_child_boxes()) {
    if (auto hvc1 = std::dynamic_pointer_cast<Box_hvc1>(sample_entry)) {
      auto chunk_hvc1 =
    }
  }
#endif
}


Result<std::shared_ptr<HeifPixelImage>> Track::decode_next_compressed_image_sample(const struct heif_decoding_options& options)
{
#if 0
  DataExtent extent;
  extent.set_from_image_item(get_file(), get_id());

  auto decoder = get_decoder();
  assert(decoder);

  decoder->set_data_extent(std::move(extent));

  return decoder->decode_single_frame_from_compressed_data(options);
#else
  return {};
#endif
}
