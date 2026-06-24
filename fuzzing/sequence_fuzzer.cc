/*
 * HEIF codec.
 * Copyright (c) 2026 struktur AG, Joachim Bauch <bauch@struktur.de>
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

#include <stdint.h>
#include <stdlib.h>

#include "libheif/heif.h"
#include "libheif/heif_sequences.h"

#define MAX_FRAMES 32

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  heif_context* ctx = heif_context_alloc();
  if (!ctx) {
    return 0;
  }

  auto* limits = heif_context_get_security_limits(ctx);
  limits->max_total_memory = UINT64_C(2) * 1024 * 1024 * 1024;
  limits->max_memory_block_size = 128 * 1024 * 1024;

  heif_error err = heif_context_read_from_memory(ctx, data, size, nullptr);
  if (err.code != heif_error_Ok) {
    heif_context_free(ctx);
    return 0;
  }

  if (heif_context_has_sequence(ctx)) {
    heif_track* track = heif_context_get_track(ctx, 0);
    if (track) {
      uint16_t w = 0, h = 0;
      heif_track_get_image_resolution(track, &w, &h);
      heif_track_get_timescale(track);
      heif_track_get_track_handler_type(track);

      int frames = 0;
      while (frames++ < MAX_FRAMES) {
        heif_image* img = nullptr;
        err = heif_track_decode_next_image(track, &img,
                                           heif_colorspace_YCbCr,
                                           heif_chroma_420, nullptr);
        if (err.code != heif_error_Ok || img == nullptr) {
          break;
        }
        heif_image_release(img);
      }

      heif_track_release(track);
    }
  }

  heif_context_free(ctx);
  return 0;
}
