/*
 * HEIF codec.
 * Copyright (c) 2026 struktur AG
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
 * Companion to file_fuzzer that exercises API surfaces beyond the basic
 * decode-and-iterate path: image sequences, region items, depth and
 * auxiliary images, tile-by-tile grid decode, item-level data access,
 * entity groups, color profiles, and item properties.
 */

#include <cstdint>
#include <cstddef>
#include <cstdlib>

#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "libheif/heif_regions.h"
#include "libheif/heif_aux_images.h"
#include "libheif/heif_tiling.h"
#include "libheif/heif_properties.h"
#include "libheif/heif_entity_groups.h"
#include "libheif/heif_items.h"
#include "libheif/heif_color.h"

static void TestSequences(struct heif_context* ctx)
{
  if (!heif_context_has_sequence(ctx)) {
    return;
  }

  int ntracks = heif_context_number_of_sequence_tracks(ctx);
  if (ntracks <= 0 || ntracks > 16) {
    return;
  }

  uint32_t* track_ids = static_cast<uint32_t*>(malloc(ntracks * sizeof(uint32_t)));
  if (!track_ids) return;
  heif_context_get_track_ids(ctx, track_ids);

  for (int t = 0; t < ntracks && t < 4; t++) {
    struct heif_track* track = heif_context_get_track(ctx, track_ids[t]);
    if (!track) continue;

    heif_track_get_track_handler_type(track);

    uint16_t tw = 0, th = 0;
    heif_track_get_image_resolution(track, &tw, &th);

    for (int f = 0; f < 8; f++) {
      struct heif_image* img = nullptr;
      struct heif_error err = heif_track_decode_next_image(track, &img,
          heif_colorspace_YCbCr, heif_chroma_420, nullptr);
      if (err.code != heif_error_Ok || !img) break;
      int stride;
      heif_image_get_plane_readonly(img, heif_channel_Y, &stride);
      heif_image_release(img);
    }
    heif_track_release(track);
  }
  free(track_ids);
}

static void TestRegions(struct heif_context* ctx, const struct heif_image_handle* handle)
{
  int nregion_items = heif_image_handle_get_number_of_region_items(handle);
  if (nregion_items <= 0 || nregion_items > 32) {
    return;
  }

  heif_item_id* region_ids = static_cast<heif_item_id*>(malloc(nregion_items * sizeof(heif_item_id)));
  if (!region_ids) return;
  heif_image_handle_get_list_of_region_item_ids(handle, region_ids, nregion_items);

  for (int r = 0; r < nregion_items && r < 8; r++) {
    struct heif_region_item* region_item = nullptr;
    struct heif_error err = heif_context_get_region_item(ctx, region_ids[r], &region_item);
    if (err.code != heif_error_Ok || !region_item) continue;

    uint32_t ref_w = 0, ref_h = 0;
    heif_region_item_get_reference_size(region_item, &ref_w, &ref_h);

    int nregions = heif_region_item_get_number_of_regions(region_item);
    if (nregions > 0 && nregions < 64) {
      struct heif_region** regions = static_cast<struct heif_region**>(malloc(nregions * sizeof(void*)));
      if (regions) {
        int got = heif_region_item_get_list_of_regions(region_item, regions, nregions);
        for (int i = 0; i < got && i < 16; i++) {
          enum heif_region_type rtype = heif_region_get_type(regions[i]);
          (void)rtype;

          int npts = heif_region_get_polygon_num_points(regions[i]);
          if (npts > 0 && npts < 1024) {
            int32_t* pts = static_cast<int32_t*>(malloc(2 * npts * sizeof(int32_t)));
            if (pts) {
              heif_region_get_polygon_points(regions[i], pts);
              free(pts);
            }
          }

          size_t mask_len = heif_region_get_inline_mask_data_len(regions[i]);
          if (mask_len > 0 && mask_len < 1024 * 1024) {
            uint8_t* mask_data = static_cast<uint8_t*>(malloc(mask_len));
            if (mask_data) {
              int32_t mx, my;
              uint32_t mw, mh;
              heif_region_get_inline_mask_data(regions[i], &mx, &my, &mw, &mh, mask_data);
              free(mask_data);
            }
          }
        }
        heif_region_release_many(regions, got);
        free(regions);
      }
    }
    heif_region_item_release(region_item);
  }
  free(region_ids);
}

static void TestDepthAux(const struct heif_image_handle* handle)
{
  if (heif_image_handle_has_depth_image(handle)) {
    int ndepth = heif_image_handle_get_number_of_depth_images(handle);
    if (ndepth > 0 && ndepth < 8) {
      heif_item_id depth_ids[8];
      heif_image_handle_get_list_of_depth_image_IDs(handle, depth_ids, ndepth);
      for (int i = 0; i < ndepth && i < 2; i++) {
        struct heif_image_handle* depth_handle = nullptr;
        struct heif_error err = heif_image_handle_get_depth_image_handle(handle, depth_ids[i], &depth_handle);
        if (err.code == heif_error_Ok && depth_handle) {
          struct heif_image* img = nullptr;
          heif_decode_image(depth_handle, &img,
              heif_colorspace_monochrome, heif_chroma_monochrome, nullptr);
          if (img) heif_image_release(img);
          heif_image_handle_release(depth_handle);
        }
      }
    }

    const struct heif_depth_representation_info* depth_info = nullptr;
    heif_image_handle_get_depth_image_representation_info(handle, 0, &depth_info);
    if (depth_info) {
      heif_depth_representation_info_free(depth_info);
    }
  }

  int naux = heif_image_handle_get_number_of_auxiliary_images(handle, 0);
  if (naux > 0 && naux < 16) {
    heif_item_id aux_ids[16];
    heif_image_handle_get_list_of_auxiliary_image_IDs(handle, 0, aux_ids, naux);
    for (int i = 0; i < naux && i < 4; i++) {
      struct heif_image_handle* aux_handle = nullptr;
      struct heif_error err = heif_image_handle_get_auxiliary_image_handle(handle, aux_ids[i], &aux_handle);
      if (err.code == heif_error_Ok && aux_handle) {
        const char* aux_type = nullptr;
        heif_image_handle_get_auxiliary_type(aux_handle, &aux_type);
        if (aux_type) heif_image_handle_free_auxiliary_types(aux_handle, &aux_type);

        struct heif_image* img = nullptr;
        heif_decode_image(aux_handle, &img,
            heif_colorspace_YCbCr, heif_chroma_420, nullptr);
        if (img) heif_image_release(img);
        heif_image_handle_release(aux_handle);
      }
    }
  }
}

static void TestGridTiles(const struct heif_image_handle* handle)
{
  struct heif_image_tiling tiling = {};
  struct heif_error err = heif_image_handle_get_image_tiling(handle, 1, &tiling);
  if (err.code != heif_error_Ok) {
    return;
  }
  if (tiling.num_columns == 0 || tiling.num_rows == 0) {
    return;
  }
  if (tiling.num_columns > 8 || tiling.num_rows > 8) {
    return;
  }

  int decoded = 0;
  for (uint32_t y = 0; y < tiling.num_rows && decoded < 4; y++) {
    for (uint32_t x = 0; x < tiling.num_columns && decoded < 4; x++) {
      struct heif_image* tile_img = nullptr;
      err = heif_image_handle_decode_image_tile(handle, &tile_img,
          heif_colorspace_YCbCr, heif_chroma_420, nullptr, x, y);
      if (err.code == heif_error_Ok && tile_img) {
        int stride;
        heif_image_get_plane_readonly(tile_img, heif_channel_Y, &stride);
        heif_image_release(tile_img);
      }
      decoded++;
    }
  }
}

static void TestProperties(struct heif_context* ctx, const struct heif_image_handle* handle)
{
  heif_item_id item_id = heif_image_handle_get_item_id(handle);

  heif_property_id trans_props[16];
  int ntrans = heif_item_get_transformation_properties(ctx, item_id, trans_props, 16);
  (void)ntrans;

  size_t icc_size = heif_image_handle_get_raw_color_profile_size(handle);
  if (icc_size > 0 && icc_size < 2 * 1024 * 1024) {
    uint8_t* icc_data = static_cast<uint8_t*>(malloc(icc_size));
    if (icc_data) {
      heif_image_handle_get_raw_color_profile(handle, icc_data);
      free(icc_data);
    }
  }

  struct heif_color_profile_nclx* nclx = nullptr;
  heif_image_handle_get_nclx_color_profile(handle, &nclx);
  if (nclx) heif_nclx_color_profile_free(nclx);
}

static void TestEntityGroups(struct heif_context* ctx)
{
  int num_groups = 0;
  struct heif_entity_group* groups = heif_context_get_entity_groups(ctx, 0, 0, &num_groups);
  if (groups && num_groups > 0) {
    heif_entity_groups_release(groups, num_groups);
  }
}

static void TestItems(struct heif_context* ctx)
{
  int nItems = heif_context_get_number_of_items(ctx);
  if (nItems <= 0 || nItems > 128) {
    return;
  }

  heif_item_id* item_ids = static_cast<heif_item_id*>(malloc(nItems * sizeof(heif_item_id)));
  if (!item_ids) return;
  heif_context_get_list_of_item_IDs(ctx, item_ids, nItems);

  for (int i = 0; i < nItems && i < 16; i++) {
    uint32_t item_type = heif_item_get_item_type(ctx, item_ids[i]);
    (void)item_type;
    int hidden = heif_item_is_item_hidden(ctx, item_ids[i]);
    (void)hidden;

    const char* mime_type = heif_item_get_mime_item_content_type(ctx, item_ids[i]);
    (void)mime_type;
    const char* uri_type = heif_item_get_uri_item_uri_type(ctx, item_ids[i]);
    (void)uri_type;
    const char* item_name = heif_item_get_item_name(ctx, item_ids[i]);
    (void)item_name;

    enum heif_metadata_compression compression;
    uint8_t* item_data = nullptr;
    size_t item_data_size = 0;
    struct heif_error err = heif_item_get_item_data(ctx, item_ids[i], &compression,
                                                    &item_data, &item_data_size);
    if (err.code == heif_error_Ok && item_data) {
      heif_release_item_data(ctx, &item_data);
    }

    char* ext_lang = nullptr;
    heif_item_get_property_extended_language(ctx, item_ids[i], &ext_lang);
    if (ext_lang) heif_string_release(ext_lang);
  }
  free(item_ids);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  if (size < 12 || size > 512 * 1024) {
    return 0;
  }

  struct heif_context* ctx = heif_context_alloc();
  if (!ctx) return 0;

  auto* limits = heif_context_get_security_limits(ctx);
  limits->max_memory_block_size = 32 * 1024 * 1024;
  limits->max_total_memory = 128 * 1024 * 1024;
  limits->max_image_size_pixels = 512 * 512;

  struct heif_error err = heif_context_read_from_memory(ctx, data, size, nullptr);
  if (err.code != heif_error_Ok) {
    goto quit;
  }

  TestSequences(ctx);
  TestEntityGroups(ctx);
  TestItems(ctx);

  {
    struct heif_image_handle* primary = nullptr;
    err = heif_context_get_primary_image_handle(ctx, &primary);
    if (err.code == heif_error_Ok && primary) {
      TestRegions(ctx, primary);
      TestDepthAux(primary);
      TestGridTiles(primary);
      TestProperties(ctx, primary);
      heif_image_handle_release(primary);
    }
  }

quit:
  heif_context_free(ctx);
  return 0;
}
