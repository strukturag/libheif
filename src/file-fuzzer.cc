/*
 * HEIF codec.
 * Copyright (c) 2017 struktur AG, Joachim Bauch <bauch@struktur.de>
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
#include <assert.h>
#include <sstream>

#include "libde265/de265.h"

#include "heif.h"

static const enum heif_colorspace kFuzzColorSpace = heif_colorspace_YCbCr;
static const enum heif_chroma kFuzzChroma = heif_chroma_420;

static void TestDecodeImage(struct heif_context* ctx,
    const struct heif_image_handle* handle) {
  struct heif_image* image;
  struct heif_error err;
  int width = 0, height = 0;

  heif_image_handle_is_primary_image(handle);
  heif_image_handle_get_resolution(handle, &width, &height);
  assert(width > 0);
  assert(height > 0);
  err = heif_decode_image(handle, &image, kFuzzColorSpace, kFuzzChroma);
  if (err.code != heif_error_Ok) {
    return;
  }

  assert(heif_image_get_colorspace(image) == kFuzzColorSpace);
  assert(heif_image_get_chroma_format(image) == kFuzzChroma);
  assert(heif_image_get_width(image, heif_channel_Y) == width);
  assert(heif_image_get_height(image, heif_channel_Y) == height);

  // TODO(fancycode): Enable when the API returns the correct values.
#if 0
  assert(heif_image_get_width(image, heif_channel_Cb) == width / 2);
  assert(heif_image_get_height(image, heif_channel_Cb) == height / 2);
  assert(heif_image_get_width(image, heif_channel_Cr) == width / 2);
  assert(heif_image_get_height(image, heif_channel_Cr) == height / 2);
#endif

  // TODO(fancycode): Should we also check the planes?

  heif_image_release(image);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  struct heif_context* ctx;
  struct heif_error err;
  struct heif_image_handle* handle;
  int images_count;

  ctx = heif_context_alloc();
  assert(ctx);
  err = heif_context_read_from_memory(ctx, data, size);
  if (err.code != heif_error_Ok) {
    // Not a valid HEIF file passed (which is most likely while fuzzing).
    goto quit;
  }

  err = heif_context_get_primary_image_handle(ctx, &handle);
  if (err.code == heif_error_Ok) {
    assert(heif_image_handle_is_primary_image(handle));
    TestDecodeImage(ctx, handle);
    heif_image_handle_release(handle);
  }

  images_count = heif_context_get_number_of_images(ctx);
  if (!images_count) {
    // File doesn't contain any images.
    goto quit;
  }

  for (int i = 0; i < images_count; ++i) {
    err = heif_context_get_image_handle(ctx, i, &handle);
    if (err.code != heif_error_Ok) {
      // Ignore, we are only interested in crashes here.
      continue;
    }

    TestDecodeImage(ctx, handle);

    int num_thumbnails = heif_image_handle_get_number_of_thumbnails(handle);
    for (int t = 0; t < num_thumbnails; ++t) {
      struct heif_image_handle* thumbnail_handle = nullptr;
      heif_image_handle_get_thumbnail(handle, t, &thumbnail_handle);
      if (thumbnail_handle) {
        TestDecodeImage(ctx, thumbnail_handle);
        heif_image_handle_release(thumbnail_handle);
      }
    }

    heif_image_handle_release(handle);
  }

quit:
  heif_context_free(ctx);
  return 0;
}
