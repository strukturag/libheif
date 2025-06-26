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

#ifndef LIBHEIF_TRACK_VISUAL_H
#define LIBHEIF_TRACK_VISUAL_H

#include "track.h"
#include <string>
#include <memory>
#include <vector>


class Track_Visual : public Track {
public:
  //Track(HeifContext* ctx);

  Track_Visual(HeifContext* ctx, uint32_t track_id, uint16_t width, uint16_t height,
               const TrackOptions* options, uint32_t handler_type);

  Track_Visual(HeifContext* ctx, const std::shared_ptr<Box_trak>&); // when reading the file

  ~Track_Visual() override = default;

  uint16_t get_width() const { return m_width; }

  uint16_t get_height() const { return m_height; }

  Result<std::shared_ptr<HeifPixelImage>> decode_next_image_sample(const struct heif_decoding_options& options);

  Error encode_image(std::shared_ptr<HeifPixelImage> image,
                     struct heif_encoder* encoder,
                     const struct heif_encoding_options& options,
                     heif_image_input_class image_class);

  heif_brand2 get_compatible_brand() const;

private:
  uint16_t m_width = 0;
  uint16_t m_height = 0;
};



#endif //LIBHEIF_TRACK_VISUAL_H
