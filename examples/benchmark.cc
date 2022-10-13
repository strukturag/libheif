/*
 * HEIF codec.
 * Copyright (c) 2022 Dirk Farin <dirk.farin@gmail.com>
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

#include "benchmark.h"
#include "libheif/heif.h"
#include <math.h>


double compute_psnr(heif_image* original_image, const std::string& encoded_file)
{
  double psnr = 0.0;

  // read encoded image

  struct heif_context* ctx = nullptr;
  struct heif_image_handle* handle = nullptr;
  struct heif_image* image = nullptr;
  heif_error err{};

  int orig_stride = 0;
  const uint8_t* orig_p = nullptr;
  int compressed_stride = 0;
  const uint8_t* compressed_p = nullptr;

  int w = 0, h = 0;
  double mse = 0.0;


  if (heif_image_get_colorspace(original_image) != heif_colorspace_YCbCr &&
      heif_image_get_colorspace(original_image) != heif_colorspace_monochrome) {
    fprintf(stderr, "Benchmark can only be computed on YCbCr or monochrome images\n");
    goto cleanup;
  }


  ctx = heif_context_alloc();

  err = heif_context_read_from_file(ctx, encoded_file.c_str(), nullptr);
  if (err.code) {
    fprintf(stderr, "Error reading encoded file: %s\n", err.message);
    goto cleanup;
  }

  err = heif_context_get_primary_image_handle(ctx, &handle);
  if (err.code) {
    fprintf(stderr, "Error getting primary image handle: %s\n", err.message);
    goto cleanup;
  }

  err = heif_decode_image(handle, &image,
                          heif_image_get_colorspace(original_image),
                          heif_image_get_chroma_format(original_image),
                          nullptr);
  if (err.code) {
    fprintf(stderr, "Error decoding image: %s\n", err.message);
    goto cleanup;
  }

  w = heif_image_get_width(original_image, heif_channel_Y);
  h = heif_image_get_height(original_image, heif_channel_Y);

  orig_p = heif_image_get_plane_readonly(original_image, heif_channel_Y, &orig_stride);
  compressed_p = heif_image_get_plane_readonly(image, heif_channel_Y, &compressed_stride);

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int d = orig_p[y * orig_stride + x] - compressed_p[y * compressed_stride + x];
      mse += d * d;
    }
  }

  mse /= w * h;

  psnr = 10 * log10(255.0 * 255.0 / mse);

  cleanup:
  heif_image_release(image);
  heif_image_handle_release(handle);
  heif_context_free(ctx);

  return psnr;
}
