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

class HeifContext;

class HeifPixelImage;

class Chunk;

class Box_trak;


class Track : public ErrorBuffer {
public:
  //Track(HeifContext* ctx);

  Track(HeifContext* ctx, uint32_t track_id, uint16_t width, uint16_t height);

  Track(HeifContext* ctx, const std::shared_ptr<Box_trak>&); // when reading the file

  virtual ~Track() = default;

  heif_item_id get_id() const { return m_id; }

  std::shared_ptr<class HeifFile> get_file() const;

  uint32_t get_handler() const { return m_handler_type; }

  bool is_visual_track() const;

  uint16_t get_width() const { return m_width; }

  uint16_t get_height() const { return m_height; }

  bool end_of_sequence_reached() const;

  Result<std::shared_ptr<HeifPixelImage>> decode_next_image_sample(const struct heif_decoding_options& options);

  Error encode_image(std::shared_ptr<HeifPixelImage> image,
                     struct heif_encoder* encoder,
                     const struct heif_encoding_options& options,
                     heif_image_input_class image_class);

private:
  HeifContext* m_heif_context = nullptr;
  uint32_t m_id = 0;
  uint32_t m_handler_type = 0;
  uint16_t m_width = 0;
  uint16_t m_height = 0;

  uint32_t m_num_samples = 0;
  uint32_t m_current_chunk = 0;
  uint32_t m_next_sample_to_be_decoded = 0;

  std::vector<std::shared_ptr<Chunk>> m_chunks;

  std::shared_ptr<class Box_moov> m_moov;
  std::shared_ptr<class Box_stsc> m_stsc;
  std::shared_ptr<class Box_stts> m_stts;
  std::shared_ptr<class Box_stss> m_stss;
  std::shared_ptr<class Box_stsz> m_stsz;

  std::vector<uint8_t> m_video_data; // TODO: do this at a central place and optionally write to temp file
};


#endif //LIBHEIF_TRACK_H
