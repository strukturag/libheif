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

#ifndef LIBHEIF_TRACK_METADATA_H
#define LIBHEIF_TRACK_METADATA_H

#include "track.h"
#include <string>
#include <memory>
#include <vector>


class Track_Metadata : public Track {
public:
  //Track(HeifContext* ctx);

  Track_Metadata(HeifContext* ctx, uint32_t track_id, std::string uri, TrackOptions* options);

  Track_Metadata(HeifContext* ctx, const std::shared_ptr<Box_trak>&); // when reading the file

  ~Track_Metadata() override = default;

  struct Metadata {
    ~Metadata() { heif_tai_timestamp_packet_release(timestamp); }

    std::vector<uint8_t> raw_metadata;
    uint32_t duration = 0;

    const heif_tai_timestamp_packet* timestamp = nullptr;
    std::string gimi_contentID;
  };

  Result<std::shared_ptr<const Metadata>> read_next_metadata_sample();

  Error write_raw_metadata(const Metadata&);

private:
  std::string m_uri;
};

#endif //LIBHEIF_TRACK_METADATA_H
