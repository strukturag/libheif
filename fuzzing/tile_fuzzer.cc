/*
 * HEIF codec.
 * Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>
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

// Fuzz harness for the per-tile read API (heif_image_handle_get_image_tiling,
// heif_image_handle_get_grid_image_tile_id, heif_image_handle_decode_image_tile).
// file_fuzzer.cc only calls heif_decode_image, which routes grid images through
// decode_full_grid_image and never exercises decode_grid_tile or the surrounding
// tiling-API surface. This harness fills that gap.

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include "libheif/heif.h"
#include "libheif/heif_tiling.h"

static const enum heif_colorspace kFuzzColorSpace = heif_colorspace_YCbCr;
static const enum heif_chroma kFuzzChroma = heif_chroma_420;

// Cap how many tiles we try to decode per image. Synthetic grids can claim
// millions of tiles; bounding here keeps the per-input time budget sane
// without losing meaningful coverage (crashes show up on the first few tiles).
static const uint32_t kMaxTilesPerImage = 32;

static void TestTileAPI(const struct heif_image_handle* handle,
                        int process_image_transformations)
{
  struct heif_image_tiling tiling = {};
  struct heif_error err = heif_image_handle_get_image_tiling(
      handle, process_image_transformations, &tiling);
  if (err.code != heif_error_Ok) {
    return;
  }

  // Probe per-tile id lookup and decode within the declared grid, capped.
  uint32_t cols = tiling.num_columns;
  uint32_t rows = tiling.num_rows;
  uint32_t total = 0;

  for (uint32_t ty = 0; ty < rows && total < kMaxTilesPerImage; ty++) {
    for (uint32_t tx = 0; tx < cols && total < kMaxTilesPerImage; tx++) {
      heif_item_id tile_id = 0;
      // Return value intentionally ignored; we only care that the call does
      // not crash on malformed input.
      heif_image_handle_get_grid_image_tile_id(
          handle, process_image_transformations, tx, ty, &tile_id);

      struct heif_image* tile_img = nullptr;
      err = heif_image_handle_decode_image_tile(
          handle, &tile_img, kFuzzColorSpace, kFuzzChroma, nullptr, tx, ty);
      if (err.code == heif_error_Ok && tile_img != nullptr) {
        heif_image_release(tile_img);
      } else if (tile_img != nullptr) {
        // Defensive: some error paths may still produce an image we own.
        heif_image_release(tile_img);
      }

      total++;
    }
  }
}

static int clip_int(size_t size)
{
  return size > INT_MAX ? INT_MAX : static_cast<int>(size);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  struct heif_context* ctx;
  struct heif_error err;
  struct heif_image_handle* primary_handle = nullptr;
  int images_count;
  heif_item_id* image_IDs = nullptr;
  bool explicit_init = size == 0 || data[size - 1] & 1;

  if (explicit_init) {
    heif_init(nullptr);
  }

  heif_check_filetype(data, clip_int(size));

  ctx = heif_context_alloc();
  assert(ctx);

  auto* limits = heif_context_get_security_limits(ctx);
  limits->max_total_memory = UINT64_C(2) * 1024 * 1024 * 1024;
  limits->max_memory_block_size = 128 * 1024 * 1024;

  err = heif_context_read_from_memory(ctx, data, size, nullptr);
  if (err.code != heif_error_Ok) {
    goto quit;
  }

  err = heif_context_get_primary_image_handle(ctx, &primary_handle);
  if (err.code == heif_error_Ok) {
    TestTileAPI(primary_handle, /*process_image_transformations=*/1);
    TestTileAPI(primary_handle, /*process_image_transformations=*/0);
    heif_image_handle_release(primary_handle);
    primary_handle = nullptr;
  }

  images_count = heif_context_get_number_of_top_level_images(ctx);
  if (!images_count) {
    goto quit;
  }

  image_IDs = static_cast<heif_item_id*>(malloc(images_count * sizeof(heif_item_id)));
  assert(image_IDs);
  images_count = heif_context_get_list_of_top_level_image_IDs(ctx, image_IDs, images_count);
  if (!images_count) {
    goto quit;
  }

  for (int i = 0; i < images_count; ++i) {
    struct heif_image_handle* image_handle = nullptr;
    err = heif_context_get_image_handle(ctx, image_IDs[i], &image_handle);
    if (err.code != heif_error_Ok) {
      heif_image_handle_release(image_handle);
      continue;
    }

    TestTileAPI(image_handle, /*process_image_transformations=*/1);

    // Also iterate thumbnails — these can themselves be grid/overlay items
    // and have separate decoder paths.
    int num_thumbnails = heif_image_handle_get_number_of_thumbnails(image_handle);
    for (int t = 0; t < num_thumbnails; ++t) {
      struct heif_image_handle* thumbnail_handle = nullptr;
      heif_image_handle_get_thumbnail(image_handle, t, &thumbnail_handle);
      if (thumbnail_handle) {
        TestTileAPI(thumbnail_handle, /*process_image_transformations=*/1);
        heif_image_handle_release(thumbnail_handle);
      }
    }

    heif_image_handle_release(image_handle);
  }

quit:
  heif_image_handle_release(primary_handle);
  heif_context_free(ctx);
  free(image_IDs);

  if (explicit_init) {
    heif_deinit();
  }

  return 0;
}
