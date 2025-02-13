/*
 * HEIF image base codec.
 * Copyright (c) 2025 Dirk Farin <dirk.farin@gmail.com>
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

#include "track_metadata.h"
#include "chunk.h"
#include "context.h"
#include "libheif/api_structs.h"


Track_Metadata::Track_Metadata(HeifContext* ctx, const std::shared_ptr<Box_trak>& trak)
    : Track(ctx, trak)
{
  const std::vector<uint32_t>& chunk_offsets = m_stco->get_offsets();

  // Find sequence resolution

  if (!chunk_offsets.empty())  {
    auto* s2c = m_stsc->get_chunk(static_cast<uint32_t>(1));
    if (!s2c) {
      return;
    }

    Box_stsc::SampleToChunk sampleToChunk = *s2c;

    auto sample_description = m_stsd->get_sample_entry(sampleToChunk.sample_description_index - 1);
    if (!sample_description) {
      return;
    }

    // TODO: read URI
  }
}


Track_Metadata::Track_Metadata(HeifContext* ctx, uint32_t track_id, std::string uri, heif_track_info* info)
    : Track(ctx, track_id, info, fourcc("meta")),
      m_uri(uri)
{
  auto nmhd = std::make_shared<Box_nmhd>();
  m_minf->append_child_box(nmhd);
}


Result<std::shared_ptr<const Track_Metadata::Metadata>> Track_Metadata::read_next_metadata_sample()
{
#if 0
  if (m_current_chunk > m_chunks.size()) {
    return Error{heif_error_End_of_sequence,
                 heif_suberror_Unspecified,
                 "End of sequence"};
  }

  while (m_next_sample_to_be_decoded > m_chunks[m_current_chunk]->last_sample_number()) {
    m_current_chunk++;

    if (m_current_chunk > m_chunks.size()) {
      return Error{heif_error_End_of_sequence,
                   heif_suberror_Unspecified,
                   "End of sequence"};
    }
  }

  const std::shared_ptr<Chunk>& chunk = m_chunks[m_current_chunk];

  auto decoder = chunk->get_decoder();
  assert(decoder);

  decoder->set_data_extent(chunk->get_data_extent_for_sample(m_next_sample_to_be_decoded));

  Result<std::shared_ptr<HeifPixelImage>> decodingResult = decoder->decode_single_frame_from_compressed_data(options);
  if (decodingResult.error) {
    m_next_sample_to_be_decoded++;
    return decodingResult.error;
  }

  auto image = decodingResult.value;

  if (m_stts) {
    image->set_sample_duration(m_stts->get_sample_duration(m_next_sample_to_be_decoded));
  }

  // --- read sample auxiliary data

  if (m_aux_reader_content_ids) {
    auto readResult = m_aux_reader_content_ids->get_sample_info(get_file().get(), m_next_sample_to_be_decoded);
    if (readResult.error) {
      return readResult.error;
    }

    Result<std::string> convResult = vector_to_string(readResult.value);
    if (convResult.error) {
      return convResult.error;
    }

    image->set_gimi_content_id(convResult.value);
  }

  if (m_aux_reader_tai_timestamps) {
    auto readResult = m_aux_reader_tai_timestamps->get_sample_info(get_file().get(), m_next_sample_to_be_decoded);
    if (readResult.error) {
      return readResult.error;
    }

    auto resultTai = Box_itai::decode_tai_from_vector(readResult.value);
    if (resultTai.error) {
      return resultTai.error;
    }

    image->set_tai_timestamp(&resultTai.value);
  }

  m_next_sample_to_be_decoded++;

  return image;
#endif
  return {};
}


Error Track_Metadata::write_raw_metadata(const Metadata& metadata)
{
  // generate new chunk for first image or when compression formats don't match

  if (m_chunks.empty()) {

    // --- write URIMetaSampleEntry ('urim')

    auto sample_description_box = std::make_shared<Box_URIMetaSampleEntry>();
    auto uri = std::make_shared<Box_uri>();
    uri->set_uri(m_uri);
    sample_description_box->append_child_box(uri);

    add_chunk(heif_compression_undefined, sample_description_box);
  }

  write_sample_data(metadata.raw_metadata, metadata.duration, true,
                    metadata.timestamp, metadata.gimi_contentID);

  return Error::Ok;
}
