/*
 * HEIF codec.
 * Copyright (c) 2017-2025 Dirk Farin <dirk.farin@gmail.com>
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

#include "heif_plugin.h"
#include "security_limits.h"
#include "region.h"
#include "common_utils.h"
#include <cstdint>
#include "heif.h"
#include "file.h"
#include "pixelimage.h"
#include "api_structs.h"
#include "context.h"
#include "plugin_registry.h"
#include "error.h"
#include "bitstream.h"
#include "init.h"
#include "image-items/grid.h"
#include "image-items/overlay.h"
#include "image-items/tiled.h"
#include <set>
#include <limits>

#if WITH_UNCOMPRESSED_CODEC
#include "image-items/unc_image.h"
#endif

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_STANDALONE_WASM__)
#include "heif_emscripten.h"
#endif

#include <algorithm>
#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <cstring>
#include <array>

#ifdef _WIN32
// for _write
#include <io.h>
#else

#include <unistd.h>

#endif

#include <cassert>


const struct heif_error heif_error_success = {heif_error_Ok, heif_suberror_Unspecified, Error::kSuccess};
static struct heif_error error_null_parameter = {heif_error_Usage_error,
                                                 heif_suberror_Null_pointer_argument,
                                                 "NULL passed"};



heif_error heif_image_handle_get_image_tiling(const struct heif_image_handle* handle, int process_image_transformations, struct heif_image_tiling* tiling)
{
  if (!handle || !tiling) {
    return {heif_error_Usage_error,
            heif_suberror_Null_pointer_argument,
            "NULL passed to heif_image_handle_get_image_tiling()"};
  }

  *tiling = handle->image->get_heif_image_tiling();

  if (process_image_transformations) {
    Error error = handle->image->process_image_transformations_on_tiling(*tiling);
    if (error) {
      return error.error_struct(handle->context.get());
    }
  }

  return heif_error_ok;
}


struct heif_error heif_image_handle_get_grid_image_tile_id(const struct heif_image_handle* handle,
                                                           int process_image_transformations,
                                                           uint32_t tile_x, uint32_t tile_y,
                                                           heif_item_id* tile_item_id)
{
  if (!handle || !tile_item_id) {
    return { heif_error_Usage_error,
             heif_suberror_Null_pointer_argument };
  }

  std::shared_ptr<ImageItem_Grid> gridItem = std::dynamic_pointer_cast<ImageItem_Grid>(handle->image);
  if (!gridItem) {
    return { heif_error_Usage_error,
             heif_suberror_Unspecified,
             "Image is no grid image" };
  }

  const ImageGrid& gridspec = gridItem->get_grid_spec();
  if (tile_x >= gridspec.get_columns() || tile_y >= gridspec.get_rows()) {
    return { heif_error_Usage_error,
             heif_suberror_Unspecified,
             "Grid tile index out of range" };
  }

  if (process_image_transformations) {
    gridItem->transform_requested_tile_position_to_original_tile_position(tile_x, tile_y);
  }

  *tile_item_id = gridItem->get_grid_tiles()[tile_y * gridspec.get_columns() + tile_x];

  return heif_error_ok;
}


#if 0
// TODO: do we need this ? This does not handle rotations. We can use heif_image_handle_get_image_tiling() to get the same information.
struct heif_error heif_image_handle_get_tile_size(const struct heif_image_handle* handle,
                                                  uint32_t* tile_width, uint32_t* tile_height)
{
  if (!handle) {
    return error_null_parameter;
  }


  uint32_t w,h;

  handle->image->get_tile_size(w,h);

  if (tile_width) {
    *tile_width = w;
  }

  if (tile_height) {
    *tile_height = h;
  }

  return heif_error_success;
}
#endif


struct heif_error heif_context_add_pyramid_entity_group(struct heif_context* ctx,
                                                        const heif_item_id* layer_item_ids,
                                                        size_t num_layers,
                                                        /*
                                                        uint16_t tile_width,
                                                        uint16_t tile_height,
                                                        uint32_t num_layers,
                                                        const heif_pyramid_layer_info* in_layers,
                                                         */
                                                        heif_item_id* out_group_id)
{
  if (!layer_item_ids) {
    return error_null_parameter;
  }

  if (num_layers == 0) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "Number of layers cannot be 0."};
  }

  std::vector<heif_item_id> layers(num_layers);
  for (size_t i = 0; i < num_layers; i++) {
    layers[i] = layer_item_ids[i];
  }

  Result<heif_item_id> result = ctx->context->add_pyramid_group(layers);

  if (result) {
    if (out_group_id) {
      *out_group_id = result.value;
    }
    return heif_error_success;
  }
  else {
    return result.error.error_struct(ctx->context.get());
  }
}


struct heif_pyramid_layer_info* heif_context_get_pyramid_entity_group_info(struct heif_context* ctx, heif_entity_group_id id, int* out_num_layers)
{
  if (!out_num_layers) {
    return nullptr;
  }

  std::shared_ptr<Box_EntityToGroup> groupBox = ctx->context->get_heif_file()->get_entity_group(id);
  if (!groupBox) {
    return nullptr;
  }

  const auto pymdBox = std::dynamic_pointer_cast<Box_pymd>(groupBox);
  if (!pymdBox) {
    return nullptr;
  }

  const std::vector<Box_pymd::LayerInfo> pymd_layers = pymdBox->get_layers();
  if (pymd_layers.empty()) {
    return nullptr;
  }

  auto items = pymdBox->get_item_ids();
  assert(items.size() == pymd_layers.size());

  auto* layerInfo = new heif_pyramid_layer_info[pymd_layers.size()];
  for (size_t i=0; i<pymd_layers.size(); i++) {
    layerInfo[i].layer_image_id = items[i];
    layerInfo[i].layer_binning = pymd_layers[i].layer_binning;
    layerInfo[i].tile_rows_in_layer = pymd_layers[i].tiles_in_layer_row_minus1 + 1;
    layerInfo[i].tile_columns_in_layer = pymd_layers[i].tiles_in_layer_column_minus1 + 1;
  }

  *out_num_layers = static_cast<int>(pymd_layers.size());

  return layerInfo;
}


void heif_pyramid_layer_info_release(struct heif_pyramid_layer_info* infos)
{
  delete[] infos;
}





struct heif_error heif_image_handle_decode_image_tile(const struct heif_image_handle* in_handle,
                                                      struct heif_image** out_img,
                                                      enum heif_colorspace colorspace,
                                                      enum heif_chroma chroma,
                                                      const struct heif_decoding_options* input_options,
                                                      uint32_t x0, uint32_t y0)
{
  if (!in_handle) {
    return error_null_parameter;
  }

  heif_item_id id = in_handle->image->get_id();

  heif_decoding_options* dec_options = heif_decoding_options_alloc();
  heif_decoding_options_copy(dec_options, input_options);

  Result<std::shared_ptr<HeifPixelImage>> decodingResult = in_handle->context->decode_image(id,
                                                                                            colorspace,
                                                                                            chroma,
                                                                                            *dec_options,
                                                                                            true, x0,y0);
  heif_decoding_options_free(dec_options);

  if (decodingResult.error.error_code != heif_error_Ok) {
    return decodingResult.error.error_struct(in_handle->image.get());
  }

  std::shared_ptr<HeifPixelImage> img = decodingResult.value;

  *out_img = new heif_image();
  (*out_img)->image = std::move(img);

  return Error::Ok.error_struct(in_handle->image.get());
}
