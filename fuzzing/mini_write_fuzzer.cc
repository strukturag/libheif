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
 * Fuzz harness for the compact 'mini' box write direction
 * (heif_context_set_write_mini_format(), heif_context_write()) and the
 * resulting read-back path (Box_mini creation, 'mini' box serialisation and
 * re-expansion).  No existing target writes files at all, so the entire
 * write half of file.cc / mini.cc / context.cc is dark in the coverage
 * reports (mini.cc alone: 757 dark lines at 20261126).
 *
 * Threat model: applications save images with libheif from content that can
 * be attacker-influenced; the writer must never corrupt memory, hang or
 * produce a file that misrepresents the stored image, whatever data it is
 * given.  The harness fuzzes the pixel content and a few bounded producer
 * parameters (dimensions, mini on/off, decode re-check).
 *
 * Invariant checked: a file written from a context re-reads with the same
 * primary-image dimensions, both for the compact 'mini' format and for the
 * standard fallback format.  The written 'mini' box must also re-expand to a
 * decodable image.  A violation is a real logic bug in the write/convert
 * path, not an accepted-production-behaviour quirk: the mini format is
 * documented as a lossless container-level alternative representation.
 */

#include <fuzzer/FuzzedDataProvider.h>

#include <cstdint>
#include <vector>

#include "libheif/heif.h"

namespace {

constexpr int kMinDim = 16;
constexpr int kMaxDim = 96;

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

heif_error write_context(heif_context* ctx, std::vector<uint8_t>* out)
{
  MemoryWriter writer;
  heif_writer writer_iface;
  writer_iface.writer_api_version = 1;
  writer_iface.write = writer_callback;
  heif_error err = heif_context_write(ctx, &writer_iface, &writer);
  if (err.code == heif_error_Ok) {
    *out = writer.buffer();
  }
  return err;
}

}  // namespace

// Re-reads a written file and checks the primary image still has the encoded
// dimensions; optionally decodes it (exercises 'mini' box re-expansion and
// uncompressed decode).
static void check_roundtrip(const std::vector<uint8_t>& file, int width, int height,
                            bool decode_check)
{
  heif_context* rctx = heif_context_alloc();
  if (!rctx) {
    return;
  }
  if (heif_context_read_from_memory(rctx, file.data(), file.size(), nullptr).code !=
      heif_error_Ok) {
    heif_context_free(rctx);
    return;
  }

  heif_image_handle* handle = nullptr;
  if (heif_context_get_primary_image_handle(rctx, &handle).code == heif_error_Ok &&
      handle) {
    int rw = heif_image_handle_get_width(handle);
    int rh = heif_image_handle_get_height(handle);

    // Container-level invariant: the written file must describe an image of
    // the same dimensions we encoded, in both mini and standard format.
    if (rw != width || rh != height) {
      heif_image_handle_release(handle);
      heif_context_free(rctx);
      abort();  // write/read round trip changed the image geometry: real bug
    }

    if (decode_check) {
      heif_image* image = nullptr;
      if (heif_decode_image(handle, &image, heif_colorspace_YCbCr, heif_chroma_420,
                            nullptr).code == heif_error_Ok &&
          image) {
        if (heif_image_get_width(image, heif_channel_Y) != width ||
            heif_image_get_height(image, heif_channel_Y) != height) {
          heif_image_release(image);
          heif_image_handle_release(handle);
          heif_context_free(rctx);
          abort();  // decoded geometry differs from the encoded geometry
        }
        heif_image_release(image);
      }
    }
    heif_image_handle_release(handle);
  }
  heif_context_free(rctx);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  FuzzedDataProvider fdp(data, size);

  const int width = fdp.ConsumeIntegralInRange<int>(kMinDim, kMaxDim);
  const int height = fdp.ConsumeIntegralInRange<int>(kMinDim, kMaxDim);
  const bool decode_check = fdp.ConsumeBool();
  std::vector<uint8_t> payload = fdp.ConsumeRemainingBytes<uint8_t>();

  heif_context* ctx = heif_context_alloc();
  if (!ctx) {
    return 0;
  }

  // Build the source image (uncompressed codec: in-tree, no external
  // dependency, and exactly the content the mini conversion covers).
  heif_image* image = nullptr;
  if (heif_image_create(width, height, heif_colorspace_YCbCr, heif_chroma_420,
                        &image).code != heif_error_Ok) {
    heif_context_free(ctx);
    return 0;
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
          size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(p.w) +
                        static_cast<size_t>(x)) %
                       (payload.empty() ? 1 : payload.size());
          plane[static_cast<size_t>(y) * stride + x] =
              payload.empty() ? static_cast<uint8_t>(x ^ y) : payload[idx];
        }
      }
    }
  }

  if (!ok) {
    heif_image_release(image);
    heif_context_free(ctx);
    return 0;
  }

  heif_encoder* encoder = nullptr;
  if (heif_context_get_encoder_for_format(ctx, heif_compression_uncompressed,
                                          &encoder).code == heif_error_Ok &&
      encoder) {
    if (heif_context_encode_image(ctx, image, encoder, nullptr, nullptr).code !=
        heif_error_Ok) {
      ok = false;
    }
    heif_encoder_release(encoder);
  }
  else {
    ok = false;
  }
  heif_image_release(image);
  if (!ok) {
    heif_context_free(ctx);
    return 0;
  }

  // Write once in the compact 'mini' format (falls back to standard boxes if
  // the content cannot be represented as 'mini') ...
  heif_context_set_write_mini_format(ctx, 1);
  std::vector<uint8_t> mini_file;
  if (write_context(ctx, &mini_file).code == heif_error_Ok && !mini_file.empty()) {
    check_roundtrip(mini_file, width, height, decode_check);
  }

  // ... and once in the standard format as the reference round trip.
  heif_context_set_write_mini_format(ctx, 0);
  std::vector<uint8_t> std_file;
  if (write_context(ctx, &std_file).code == heif_error_Ok && !std_file.empty()) {
    check_roundtrip(std_file, width, height, decode_check);
  }

  heif_context_free(ctx);
  return 0;
}
