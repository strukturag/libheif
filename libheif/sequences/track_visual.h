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

  Track_Visual(HeifContext* ctx);

  ~Track_Visual() override;

  // load track from file
  Error load(const std::shared_ptr<Box_trak>&) override;

  void initialize_after_parsing(HeifContext* ctx, const std::vector<std::shared_ptr<Track>>& all_tracks) override;

  uint16_t get_width() const { return m_width; }

  uint16_t get_height() const { return m_height; }

  bool has_alpha_channel() const override { return m_aux_alpha_track != nullptr; }

  Result<std::shared_ptr<HeifPixelImage>> decode_next_image_sample(const heif_decoding_options& options);

  Error encode_image(std::shared_ptr<HeifPixelImage> image,
                     heif_encoder* encoder,
                     const heif_sequence_encoding_options* options,
                     heif_image_input_class image_class);

  Error encode_end_of_sequence(heif_encoder* encoder);

  heif_brand2 get_compatible_brand() const;

private:
  uint16_t m_width = 0;
  uint16_t m_height = 0;

  uintptr_t m_current_frame_nr = 0;

  struct FrameUserData
  {
    int sample_duration = 0;

    std::optional<std::string> gimi_content_id;
    heif_tai_timestamp_packet* tai_timestamp = nullptr;

    void release()
    {
      heif_tai_timestamp_packet_release(tai_timestamp);
      tai_timestamp = nullptr;
    }
  };

  // map frame number to user data
  std::map<uintptr_t, FrameUserData> m_frame_user_data;

  int m_sample_duration = 0; // TODO: pass this through encoder or handle it correctly with frame reordering

  // If there is an alpha-channel track associated with this color track, we reference it from here
  std::shared_ptr<Track_Visual> m_aux_alpha_track;

  Result<bool> process_encoded_data(heif_encoder* encoder);
};



#endif //LIBHEIF_TRACK_VISUAL_H
