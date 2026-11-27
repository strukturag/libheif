/*
 * HEIF codec.
 * Copyright (c) 2026 Google LLC
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

/*
 * Fuzz harness for the sequence *write* direction, which no existing target
 * exercises: heif_context_add_visual_sequence_track(),
 * heif_track_encode_sequence_image(), heif_track_encode_end_of_sequence(),
 * heif_context_add_uri_metadata_sequence_track(),
 * heif_track_add_raw_sequence_sample(), and the file writing that serialises
 * the sequence boxes ('seq_boxes.cc' write half, 'track.cc', 'track_visual.cc').
 *
 * Threat model: an application embedding libheif writes HEIF image sequences
 * from content that can be attacker-influenced (frames and metadata samples
 * derived from untrusted files), so the write path must never corrupt memory
 * or hang regardless of the sample data it is fed.  The harness fuzzes the
 * sample/frame bytes and a few bounded producer-side parameters (frame count,
 * lossless mode, presence of a metadata track); dimensions are bounded to keep
 * each input's encode time far below the runner timeout.
 *
 * After writing, the generated file is re-read so the read side sees
 * sequence boxes produced by the write half (matching what round-trip
 * processing does in real applications).
 */

#include <fuzzer/FuzzedDataProvider.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "libheif/heif.h"
#include "libheif/heif_sequences.h"

namespace {

constexpr int kMinDim = 16;
constexpr int kMaxDim = 96;
constexpr int kMaxFrames = 3;
constexpr size_t kMaxMetadataSampleSize = 4096;

// Collects the bytes handed to the heif_writer callback into one buffer.
class MemoryWriter
{
public:
  heif_error write(const void* data, size_t size)
  {
    try {
      m_buffer.insert(m_buffer.end(), static_cast<const uint8_t*>(data),
                      static_cast<const uint8_t*>(data) + size);
      return heif_error{heif_error_Ok, heif_suberror_Unspecified, nullptr};
    }
    catch (...) {
      return heif_error{heif_error_Encoding_error, heif_suberror_Unspecified, "oom"};
    }
  }

  const std::vector<uint8_t>& buffer() const { return m_buffer; }

private:
  std::vector<uint8_t> m_buffer;
};

heif_error writer_callback(heif_context*, const void* data, size_t size, void* userdata)
{
  return static_cast<MemoryWriter*>(userdata)->write(data, size);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  FuzzedDataProvider fdp(data, size);

  const int width = fdp.ConsumeIntegralInRange<int>(kMinDim, kMaxDim);
  const int height = fdp.ConsumeIntegralInRange<int>(kMinDim, kMaxDim);
  const int frames = fdp.ConsumeIntegralInRange<int>(1, kMaxFrames);
  const bool lossless = fdp.ConsumeBool();
  const bool with_metadata_track = fdp.ConsumeBool();

  // Everything that is left drives pixel / metadata sample content.
  std::vector<uint8_t> payload = fdp.ConsumeRemainingBytes<uint8_t>();

  heif_context* ctx = heif_context_alloc();
  if (!ctx) {
    return 0;
  }

  heif_track* track = nullptr;
  heif_encoder* encoder = nullptr;
  heif_error err;

  err = heif_context_add_visual_sequence_track(ctx, static_cast<uint16_t>(width),
                                               static_cast<uint16_t>(height),
                                               heif_track_type_video, nullptr, nullptr,
                                               &track);
  if (err.code != heif_error_Ok || !track) {
    // Track creation failure is a valid error path; nothing more to do.
    if (track) {
      heif_track_release(track);
    }
    heif_context_free(ctx);
    return 0;
  }

  err = heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);
  if (err.code == heif_error_Ok && encoder) {
    heif_encoder_set_lossless(encoder, lossless ? 1 : 0);

    for (int f = 0; f < frames && err.code == heif_error_Ok; f++) {
      heif_image* image = nullptr;
      err = heif_image_create(width, height, heif_colorspace_YCbCr, heif_chroma_420,
                              &image);
      if (err.code != heif_error_Ok) {
        break;
      }

      const int cw = (width + 1) / 2;
      const int ch = (height + 1) / 2;
      bool ok = heif_image_add_plane(image, heif_channel_Y, width, height, 8).code ==
                    heif_error_Ok &&
                heif_image_add_plane(image, heif_channel_Cb, cw, ch, 8).code ==
                    heif_error_Ok &&
                heif_image_add_plane(image, heif_channel_Cr, cw, ch, 8).code ==
                    heif_error_Ok;

      if (ok) {
        // Fill the planes with a deterministic pattern derived from the fuzzed
        // payload; each frame uses a different phase so the encoder sees
        // differing content.
        const struct
        {
          heif_channel channel;
          int w;
          int h;
        } planes[3] = {{heif_channel_Y, width, height},
                       {heif_channel_Cb, cw, ch},
                       {heif_channel_Cr, cw, ch}};
        for (const auto& p : planes) {
          int stride = 0;
          uint8_t* plane = heif_image_get_plane(image, p.channel, &stride);
          if (!plane) {
            ok = false;
            break;
          }
          for (int y = 0; y < p.h; y++) {
            for (int x = 0; x < p.w; x++) {
              size_t idx = (static_cast<size_t>(f) * 7919 +
                            static_cast<size_t>(y) * static_cast<size_t>(p.w) +
                            static_cast<size_t>(x)) %
                           (payload.empty() ? 1 : payload.size());
              plane[static_cast<size_t>(y) * stride + x] =
                  payload.empty() ? static_cast<uint8_t>(x ^ y) : payload[idx];
            }
          }
        }
      }

      if (ok) {
        heif_image_set_duration(image, 512);
        err = heif_track_encode_sequence_image(track, image, encoder, nullptr);
      }
      else {
        err = heif_error{heif_error_Encoding_error, heif_suberror_Unspecified,
                         "plane setup failed"};
      }

      heif_image_release(image);
    }

    if (err.code == heif_error_Ok) {
      heif_track_encode_end_of_sequence(track, encoder);
    }

    // NOTE: the public API also documents freeing the encoder right after an
    // explicit end-of-sequence ("Using this function has the advantage that
    // you can free the encoder afterwards"), but that flow currently hits a
    // heap-use-after-free inside Track_Visual::finalize_track() at write time
    // (reported as a finding; see the run report). To keep fuzzing the rest
    // of the write path, we use the other documented pattern and keep the
    // encoder alive until after heif_context_write().
    // The release happens below, after the file has been written.
  }

  heif_track_release(track);
  track = nullptr;

  if (with_metadata_track) {
    heif_track* meta_track = nullptr;
    if (heif_context_add_uri_metadata_sequence_track(ctx, "urn:example:fuzz:1", nullptr,
                                                     &meta_track)
            .code == heif_error_Ok &&
        meta_track) {
      size_t offset = 0;
      for (int s = 0; s < 2; s++) {
        size_t remaining = payload.size() > offset ? payload.size() - offset : 0;
        size_t sample_size = remaining > 0 ? std::min(remaining, kMaxMetadataSampleSize)
                                           : 4;
        uint8_t tiny[4] = {0x46, 0x55, 0x5A, 0x5A};
        heif_raw_sequence_sample* sample = heif_raw_sequence_sample_alloc();
        if (!sample) {
          break;
        }
        heif_raw_sequence_sample_set_data(
            sample, remaining > 0 ? payload.data() + offset : tiny,
            remaining > 0 ? sample_size : sizeof(tiny));
        heif_raw_sequence_sample_set_duration(sample, 512 + static_cast<uint32_t>(s));
        heif_track_add_raw_sequence_sample(meta_track, sample);
        heif_raw_sequence_sample_release(sample);
        offset += sample_size;
      }
      heif_track_release(meta_track);
    }
  }

  // Write the sequence out and re-read it, so the read side exercises the
  // boxes the write half just produced.
  MemoryWriter writer;
  heif_writer writer_iface;
  writer_iface.writer_api_version = 1;
  writer_iface.write = writer_callback;
  heif_error write_err = heif_context_write(ctx, &writer_iface, &writer);
  if (encoder) {
    heif_encoder_release(encoder);
  }
  if (write_err.code == heif_error_Ok && !writer.buffer().empty()) {
    heif_context* rctx = heif_context_alloc();
    if (rctx) {
      if (heif_context_read_from_memory(rctx, writer.buffer().data(),
                                        writer.buffer().size(),
                                        nullptr).code == heif_error_Ok) {
        (void)heif_context_has_sequence(rctx);
        int ntracks = heif_context_number_of_sequence_tracks(rctx);
        if (ntracks > 0 && ntracks < 8) {
          std::vector<uint32_t> track_ids(static_cast<size_t>(ntracks));
          heif_context_get_track_ids(rctx, track_ids.data());
          (void)heif_context_get_sequence_duration(rctx);
        }
      }
      heif_context_free(rctx);
    }
  }

  heif_context_free(ctx);
  return 0;
}
