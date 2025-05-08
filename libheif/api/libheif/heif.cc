/*
 * HEIF codec.
 * Copyright (c) 2017 Dirk Farin <dirk.farin@gmail.com>
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
static struct heif_error error_unsupported_parameter = {heif_error_Usage_error,
                                                        heif_suberror_Unsupported_parameter,
                                                        "Unsupported encoder parameter"};
static struct heif_error error_invalid_parameter_value = {heif_error_Usage_error,
                                                          heif_suberror_Invalid_parameter_value,
                                                          "Invalid parameter value"};
static struct heif_error error_null_parameter = {heif_error_Usage_error,
                                                 heif_suberror_Null_pointer_argument,
                                                 "NULL passed"};


const struct heif_security_limits* heif_get_global_security_limits()
{
  return &global_security_limits;
}


const struct heif_security_limits* heif_get_disabled_security_limits()
{
  return &disabled_security_limits;
}


struct heif_security_limits* heif_context_get_security_limits(const struct heif_context* ctx)
{
  if (!ctx) {
    return nullptr;
  }

  return ctx->context->get_security_limits();
}


struct heif_error heif_context_set_security_limits(struct heif_context* ctx, const struct heif_security_limits* limits)
{
  if (ctx==nullptr || limits==nullptr) {
    return {heif_error_Usage_error,
            heif_suberror_Null_pointer_argument};
  }

  ctx->context->set_security_limits(limits);

  return heif_error_ok;
}



heif_context* heif_context_alloc()
{
  load_plugins_if_not_initialized_yet();

  struct heif_context* ctx = new heif_context;
  ctx->context = std::make_shared<HeifContext>();

  return ctx;
}

void heif_context_free(heif_context* ctx)
{
  delete ctx;
}

heif_error heif_context_read_from_file(heif_context* ctx, const char* filename,
                                       const struct heif_reading_options*)
{
  Error err = ctx->context->read_from_file(filename);
  return err.error_struct(ctx->context.get());
}

heif_error heif_context_read_from_memory(heif_context* ctx, const void* mem, size_t size,
                                         const struct heif_reading_options*)
{
  Error err = ctx->context->read_from_memory(mem, size, true);
  return err.error_struct(ctx->context.get());
}

heif_error heif_context_read_from_memory_without_copy(heif_context* ctx, const void* mem, size_t size,
                                                      const struct heif_reading_options*)
{
  Error err = ctx->context->read_from_memory(mem, size, false);
  return err.error_struct(ctx->context.get());
}

heif_error heif_context_read_from_reader(struct heif_context* ctx,
                                         const struct heif_reader* reader_func_table,
                                         void* userdata,
                                         const struct heif_reading_options*)
{
  auto reader = std::make_shared<StreamReader_CApi>(reader_func_table, userdata);

  Error err = ctx->context->read(reader);
  return err.error_struct(ctx->context.get());
}

// TODO: heif_error heif_context_read_from_file_descriptor(heif_context*, int fd);

void heif_context_debug_dump_boxes_to_file(struct heif_context* ctx, int fd)
{
  if (!ctx) {
    return;
  }

  std::string dump = ctx->context->debug_dump_boxes();
  // TODO(fancycode): Should we return an error if writing fails?
#ifdef _WIN32
  auto written = _write(fd, dump.c_str(), static_cast<unsigned int>(dump.size()));
#else
  auto written = write(fd, dump.c_str(), dump.size());
#endif
  (void) written;
}

heif_error heif_context_get_primary_image_handle(heif_context* ctx, heif_image_handle** img)
{
  if (!img) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(ctx->context.get());
  }

  std::shared_ptr<ImageItem> primary_image = ctx->context->get_primary_image(true);

  // It is a requirement of an HEIF file there is always a primary image.
  // If there is none, an error is generated when loading the file.
  if (!primary_image) {
    Error err(heif_error_Invalid_input,
              heif_suberror_No_or_invalid_primary_item);
    return err.error_struct(ctx->context.get());
  }

  if (auto errImage = std::dynamic_pointer_cast<ImageItem_Error>(primary_image)) {
    Error error = errImage->get_item_error();
    return error.error_struct(ctx->context.get());
  }

  *img = new heif_image_handle();
  (*img)->image = std::move(primary_image);
  (*img)->context = ctx->context;

  return Error::Ok.error_struct(ctx->context.get());
}


struct heif_error heif_context_get_primary_image_ID(struct heif_context* ctx, heif_item_id* id)
{
  if (!id) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(ctx->context.get());
  }

  std::shared_ptr<ImageItem> primary = ctx->context->get_primary_image(true);
  if (!primary) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_or_invalid_primary_item).error_struct(ctx->context.get());
  }

  *id = primary->get_id();

  return Error::Ok.error_struct(ctx->context.get());
}


int heif_context_is_top_level_image_ID(struct heif_context* ctx, heif_item_id id)
{
  const std::vector<std::shared_ptr<ImageItem>> images = ctx->context->get_top_level_images(true);

  for (const auto& img : images) {
    if (img->get_id() == id) {
      return true;
    }
  }

  return false;
}


int heif_context_get_number_of_top_level_images(heif_context* ctx)
{
  return (int) ctx->context->get_top_level_images(true).size();
}


int heif_context_get_list_of_top_level_image_IDs(struct heif_context* ctx,
                                                 heif_item_id* ID_array,
                                                 int count)
{
  if (ID_array == nullptr || count == 0 || ctx == nullptr) {
    return 0;
  }


  // fill in ID values into output array

  const std::vector<std::shared_ptr<ImageItem>> imgs = ctx->context->get_top_level_images(true);
  int n = (int) std::min(count, (int) imgs.size());
  for (int i = 0; i < n; i++) {
    ID_array[i] = imgs[i]->get_id();
  }

  return n;
}


struct heif_error heif_context_get_image_handle(struct heif_context* ctx,
                                                heif_item_id id,
                                                struct heif_image_handle** imgHdl)
{
  if (!imgHdl) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, ""};
  }

  auto image = ctx->context->get_image(id, true);

  if (auto errImage = std::dynamic_pointer_cast<ImageItem_Error>(image)) {
    Error error = errImage->get_item_error();
    return error.error_struct(ctx->context.get());
  }

  if (!image) {
    *imgHdl = nullptr;

    return {heif_error_Usage_error, heif_suberror_Nonexisting_item_referenced, ""};
  }

  *imgHdl = new heif_image_handle();
  (*imgHdl)->image = std::move(image);
  (*imgHdl)->context = ctx->context;

  return heif_error_success;
}


int heif_image_handle_is_primary_image(const struct heif_image_handle* handle)
{
  return handle->image->is_primary();
}


heif_item_id heif_image_handle_get_item_id(const struct heif_image_handle* handle)
{
  return handle->image->get_id();
}


int heif_image_handle_get_number_of_thumbnails(const struct heif_image_handle* handle)
{
  return (int) handle->image->get_thumbnails().size();
}


int heif_image_handle_get_list_of_thumbnail_IDs(const struct heif_image_handle* handle,
                                                heif_item_id* ids, int count)
{
  if (ids == nullptr) {
    return 0;
  }

  auto thumbnails = handle->image->get_thumbnails();
  int n = (int) std::min(count, (int) thumbnails.size());

  for (int i = 0; i < n; i++) {
    ids[i] = thumbnails[i]->get_id();
  }

  return n;
}


heif_error heif_image_handle_get_thumbnail(const struct heif_image_handle* handle,
                                           heif_item_id thumbnail_id,
                                           struct heif_image_handle** out_thumbnail_handle)
{
  if (!out_thumbnail_handle) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(handle->image.get());
  }

  auto thumbnails = handle->image->get_thumbnails();
  for (const auto& thumb : thumbnails) {
    if (thumb->get_id() == thumbnail_id) {
      *out_thumbnail_handle = new heif_image_handle();
      (*out_thumbnail_handle)->image = thumb;
      (*out_thumbnail_handle)->context = handle->context;

      return Error::Ok.error_struct(handle->image.get());
    }
  }

  Error err(heif_error_Usage_error, heif_suberror_Nonexisting_item_referenced);
  return err.error_struct(handle->image.get());
}


int heif_image_handle_get_number_of_auxiliary_images(const struct heif_image_handle* handle,
                                                     int include_alpha_image)
{
  return (int) handle->image->get_aux_images(include_alpha_image).size();
}


int heif_image_handle_get_list_of_auxiliary_image_IDs(const struct heif_image_handle* handle,
                                                      int include_alpha_image,
                                                      heif_item_id* ids, int count)
{
  if (ids == nullptr) {
    return 0;
  }

  auto auxImages = handle->image->get_aux_images(include_alpha_image);
  int n = (int) std::min(count, (int) auxImages.size());

  for (int i = 0; i < n; i++) {
    ids[i] = auxImages[i]->get_id();
  }

  return n;
}


struct heif_error heif_image_handle_get_auxiliary_type(const struct heif_image_handle* handle,
                                                       const char** out_type)
{
  if (out_type == nullptr) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(handle->image.get());
  }

  *out_type = nullptr;

  const auto& auxType = handle->image->get_aux_type();

  char* buf = (char*) malloc(auxType.length() + 1);

  if (buf == nullptr) {
    return Error(heif_error_Memory_allocation_error,
                 heif_suberror_Unspecified,
                 "Failed to allocate memory for the type string").error_struct(handle->image.get());
  }

  strcpy(buf, auxType.c_str());
  *out_type = buf;

  return heif_error_success;
}


void heif_image_handle_release_auxiliary_type(const struct heif_image_handle* handle,
                                              const char** out_type)
{
  if (out_type && *out_type) {
    free((void*) *out_type);
    *out_type = nullptr;
  }
}

// DEPRECATED (typo)
void heif_image_handle_free_auxiliary_types(const struct heif_image_handle* handle,
                                            const char** out_type)
{
  heif_image_handle_release_auxiliary_type(handle, out_type);
}


struct heif_error heif_image_handle_get_auxiliary_image_handle(const struct heif_image_handle* main_image_handle,
                                                               heif_item_id auxiliary_id,
                                                               struct heif_image_handle** out_auxiliary_handle)
{
  if (!out_auxiliary_handle) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(main_image_handle->image.get());
  }

  *out_auxiliary_handle = nullptr;

  auto auxImages = main_image_handle->image->get_aux_images();
  for (const auto& aux : auxImages) {
    if (aux->get_id() == auxiliary_id) {
      *out_auxiliary_handle = new heif_image_handle();
      (*out_auxiliary_handle)->image = aux;
      (*out_auxiliary_handle)->context = main_image_handle->context;

      return Error::Ok.error_struct(main_image_handle->image.get());
    }
  }

  Error err(heif_error_Usage_error, heif_suberror_Nonexisting_item_referenced);
  return err.error_struct(main_image_handle->image.get());
}


int heif_image_handle_get_width(const struct heif_image_handle* handle)
{
  if (handle && handle->image) {
    return handle->image->get_width();
  }
  else {
    return 0;
  }
}


int heif_image_handle_get_height(const struct heif_image_handle* handle)
{
  if (handle && handle->image) {
    return handle->image->get_height();
  }
  else {
    return 0;
  }
}


int heif_image_handle_get_ispe_width(const struct heif_image_handle* handle)
{
  if (handle && handle->image) {
    return handle->image->get_ispe_width();
  }
  else {
    return 0;
  }
}


int heif_image_handle_get_ispe_height(const struct heif_image_handle* handle)
{
  if (handle && handle->image) {
    return handle->image->get_ispe_height();
  }
  else {
    return 0;
  }
}


struct heif_context* heif_image_handle_get_context(const struct heif_image_handle* handle)
{
  auto ctx = new heif_context();
  ctx->context = handle->context;
  return ctx;
}


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


struct heif_entity_group* heif_context_get_entity_groups(const struct heif_context* ctx,
                                                         uint32_t type_filter,
                                                         heif_item_id item_filter,
                                                         int* out_num_groups)
{
  std::shared_ptr<Box_grpl> grplBox = ctx->context->get_heif_file()->get_grpl_box();
  if (!grplBox) {
    *out_num_groups = 0;
    return nullptr;
  }

  std::vector<std::shared_ptr<Box>> all_entity_group_boxes = grplBox->get_all_child_boxes();
  if (all_entity_group_boxes.empty()) {
    *out_num_groups = 0;
    return nullptr;
  }

  // --- filter groups

  std::vector<std::shared_ptr<Box_EntityToGroup>> entity_group_boxes;
  for (auto& group : all_entity_group_boxes) {
    if (type_filter != 0 && group->get_short_type() != type_filter) {
      continue;
    }

    auto groupBox = std::dynamic_pointer_cast<Box_EntityToGroup>(group);
    const std::vector<heif_item_id>& items = groupBox->get_item_ids();

    if (item_filter != 0 && std::all_of(items.begin(), items.end(), [item_filter](heif_item_id item) {
      return item != item_filter;
    })) {
      continue;
    }

    entity_group_boxes.emplace_back(groupBox);
  }

  // --- convert to C structs

  auto* groups = new heif_entity_group[entity_group_boxes.size()];
  for (size_t i = 0; i < entity_group_boxes.size(); i++) {
    const auto& groupBox = entity_group_boxes[i];
    const std::vector<heif_item_id>& items = groupBox->get_item_ids();

    groups[i].entity_group_id = groupBox->get_group_id();
    groups[i].entity_group_type = groupBox->get_short_type();
    groups[i].entities = (items.empty() ? nullptr : new heif_item_id[items.size()]);
    groups[i].num_entities = static_cast<uint32_t>(items.size());

    if (groups[i].entities) { // avoid clang static analyzer false positive
      for (size_t k = 0; k < items.size(); k++) {
        groups[i].entities[k] = items[k];
      }
    }
  }

  *out_num_groups = static_cast<int>(entity_group_boxes.size());
  return groups;
}


void heif_entity_groups_release(struct heif_entity_group* grp, int num_groups)
{
  for (int i=0;i<num_groups;i++) {
    delete[] grp[i].entities;
  }

  delete[] grp;
}


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


struct heif_error heif_image_handle_get_preferred_decoding_colorspace(const struct heif_image_handle* image_handle,
                                                                      enum heif_colorspace* out_colorspace,
                                                                      enum heif_chroma* out_chroma)
{
  Error err = image_handle->image->get_coded_image_colorspace(out_colorspace, out_chroma);
  if (err) {
    return err.error_struct(image_handle->image.get());
  }

  return heif_error_success;
}


int heif_image_handle_has_alpha_channel(const struct heif_image_handle* handle)
{
  // TODO: for now, also scan the grid tiles for alpha information (issue #708), but depending about
  // how the discussion about this structure goes forward, we might remove this again.

  return handle->context->has_alpha(handle->image->get_id());   // handle case in issue #708
  //return handle->image->get_alpha_channel() != nullptr;       // old alpha check that fails on alpha in grid tiles
}


int heif_image_handle_is_premultiplied_alpha(const struct heif_image_handle* handle)
{
  // TODO: what about images that have the alpha in the grid tiles (issue #708) ?
  return handle->image->is_premultiplied_alpha();
}


int heif_image_handle_get_luma_bits_per_pixel(const struct heif_image_handle* handle)
{
  return handle->image->get_luma_bits_per_pixel();
}


int heif_image_handle_get_chroma_bits_per_pixel(const struct heif_image_handle* handle)
{
  return handle->image->get_chroma_bits_per_pixel();
}


int heif_image_handle_has_depth_image(const struct heif_image_handle* handle)
{
  return handle->image->get_depth_channel() != nullptr;
}

void heif_depth_representation_info_free(const struct heif_depth_representation_info* info)
{
  delete info;
}

int heif_image_handle_get_depth_image_representation_info(const struct heif_image_handle* handle,
                                                          heif_item_id depth_image_id,
                                                          const struct heif_depth_representation_info** out)
{
  std::shared_ptr<ImageItem> depth_image;

  if (out) {
    if (handle->image->is_depth_channel()) {
      // Because of an API bug before v1.11.0, the input handle may be the depth image (#422).
      depth_image = handle->image;
    }
    else {
      depth_image = handle->image->get_depth_channel();
    }

    if (depth_image->has_depth_representation_info()) {
      auto info = new heif_depth_representation_info;
      *info = depth_image->get_depth_representation_info();
      *out = info;
      return true;
    }
    else {
      *out = nullptr;
    }
  }

  return false;
}


int heif_image_handle_get_number_of_depth_images(const struct heif_image_handle* handle)
{
  auto depth_image = handle->image->get_depth_channel();

  if (depth_image) {
    return 1;
  }
  else {
    return 0;
  }
}


int heif_image_handle_get_list_of_depth_image_IDs(const struct heif_image_handle* handle,
                                                  heif_item_id* ids, int count)
{
  auto depth_image = handle->image->get_depth_channel();

  if (count == 0) {
    return 0;
  }

  if (depth_image) {
    ids[0] = depth_image->get_id();
    return 1;
  }
  else {
    return 0;
  }
}


struct heif_error heif_image_handle_get_depth_image_handle(const struct heif_image_handle* handle,
                                                           heif_item_id depth_id,
                                                           struct heif_image_handle** out_depth_handle)
{
  if (out_depth_handle == nullptr) {
    return {heif_error_Usage_error,
            heif_suberror_Null_pointer_argument,
            "NULL out_depth_handle passed to heif_image_handle_get_depth_image_handle()"};
  }

  auto depth_image = handle->image->get_depth_channel();

  if (depth_image->get_id() != depth_id) {
    *out_depth_handle = nullptr;

    Error err(heif_error_Usage_error, heif_suberror_Nonexisting_item_referenced);
    return err.error_struct(handle->image.get());
  }

  *out_depth_handle = new heif_image_handle();
  (*out_depth_handle)->image = depth_image;
  (*out_depth_handle)->context = handle->context;

  return Error::Ok.error_struct(handle->image.get());
}


void fill_default_decoding_options(heif_decoding_options& options)
{
  options.version = 7;

  options.ignore_transformations = false;

  options.start_progress = nullptr;
  options.on_progress = nullptr;
  options.end_progress = nullptr;
  options.progress_user_data = nullptr;

  // version 2

  options.convert_hdr_to_8bit = false;

  // version 3

  options.strict_decoding = false;

  // version 4

  options.decoder_id = nullptr;

  // version 5

  options.color_conversion_options.version = 1;
  options.color_conversion_options.preferred_chroma_downsampling_algorithm = heif_chroma_downsampling_average;
  options.color_conversion_options.preferred_chroma_upsampling_algorithm = heif_chroma_upsampling_bilinear;
  options.color_conversion_options.only_use_preferred_chroma_algorithm = false;

  // version 6

  options.cancel_decoding = nullptr;

  // version 7

  options.color_conversion_options_ext = nullptr;
}


// overwrite the (possibly lower version) input options over the default options
static heif_decoding_options normalize_options(const heif_decoding_options* input_options)
{
  heif_decoding_options options{};
  fill_default_decoding_options(options);

  if (input_options) {
    switch (input_options->version) {
      case 7:
        options.color_conversion_options_ext = input_options->color_conversion_options_ext;
        // fallthrough
      case 6:
        options.cancel_decoding = input_options->cancel_decoding;
        // fallthrough
      case 5:
        options.color_conversion_options = input_options->color_conversion_options;
        // fallthrough
      case 4:
        options.decoder_id = input_options->decoder_id;
        // fallthrough
      case 3:
        options.strict_decoding = input_options->strict_decoding;
        // fallthrough
      case 2:
        options.convert_hdr_to_8bit = input_options->convert_hdr_to_8bit;
        // fallthrough
      case 1:
        options.ignore_transformations = input_options->ignore_transformations;

        options.start_progress = input_options->start_progress;
        options.on_progress = input_options->on_progress;
        options.end_progress = input_options->end_progress;
        options.progress_user_data = input_options->progress_user_data;
    }
  }

  return options;
}


void heif_color_conversion_options_set_defaults(struct heif_color_conversion_options* options)
{
  options->version = 1;
#if HAVE_LIBSHARPYUV
  options->preferred_chroma_downsampling_algorithm = heif_chroma_downsampling_sharp_yuv;
#else
  options->preferred_chroma_downsampling_algorithm = heif_chroma_downsampling_average;
#endif

  options->preferred_chroma_upsampling_algorithm = heif_chroma_upsampling_bilinear;
  options->only_use_preferred_chroma_algorithm = true;
}


heif_decoding_options* heif_decoding_options_alloc()
{
  auto options = new heif_decoding_options;

  fill_default_decoding_options(*options);

  return options;
}


void heif_decoding_options_free(heif_decoding_options* options)
{
  delete options;
}


void fill_default_color_conversion_options_ext(heif_color_conversion_options_ext& options)
{
  options.version = 1;
  options.alpha_composition_mode = heif_alpha_composition_mode_none;
  options.background_red = options.background_green = options.background_blue = 0xFFFF;
  options.secondary_background_red = options.secondary_background_green = options.secondary_background_blue = 0xCCCC;
  options.checkerboard_square_size = 16;
}


// overwrite the (possibly lower version) input options over the default options
heif_color_conversion_options_ext normalize_options(const heif_color_conversion_options_ext* input_options)
{
  heif_color_conversion_options_ext options{};
  fill_default_color_conversion_options_ext(options);

  if (input_options) {
    switch (input_options->version) {
      case 1:
        options.alpha_composition_mode = input_options->alpha_composition_mode;
        options.background_red = input_options->background_red;
        options.background_green = input_options->background_green;
        options.background_blue = input_options->background_blue;
        options.secondary_background_red = input_options->secondary_background_red;
        options.secondary_background_green = input_options->secondary_background_green;
        options.secondary_background_blue = input_options->secondary_background_blue;
        options.checkerboard_square_size = input_options->checkerboard_square_size;
    }
  }

  return options;
}




struct heif_color_conversion_options_ext* heif_color_conversion_options_ext_alloc()
{
  auto options = new heif_color_conversion_options_ext;

  fill_default_color_conversion_options_ext(*options);

  return options;
}


void heif_color_conversion_options_ext_free(struct heif_color_conversion_options_ext* options)
{
  delete options;
}


struct heif_error heif_decode_image(const struct heif_image_handle* in_handle,
                                    struct heif_image** out_img,
                                    heif_colorspace colorspace,
                                    heif_chroma chroma,
                                    const struct heif_decoding_options* input_options)
{
  if (out_img == nullptr) {
    return {heif_error_Usage_error,
            heif_suberror_Null_pointer_argument,
            "NULL out_img passed to heif_decode_image()"};
  }

  if (in_handle == nullptr) {
    return {heif_error_Usage_error,
            heif_suberror_Null_pointer_argument,
            "NULL heif_image_handle passed to heif_decode_image()"};
  }

  *out_img = nullptr;
  heif_item_id id = in_handle->image->get_id();

  heif_decoding_options dec_options = normalize_options(input_options);

  Result<std::shared_ptr<HeifPixelImage>> decodingResult = in_handle->context->decode_image(id,
                                                                                            colorspace,
                                                                                            chroma,
                                                                                            dec_options,
                                                                                            false, 0,0);
  if (decodingResult.error.error_code != heif_error_Ok) {
    return decodingResult.error.error_struct(in_handle->image.get());
  }

  std::shared_ptr<HeifPixelImage> img = decodingResult.value;

  *out_img = new heif_image();
  (*out_img)->image = std::move(img);

  return Error::Ok.error_struct(in_handle->image.get());
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

  heif_decoding_options dec_options = normalize_options(input_options);

  Result<std::shared_ptr<HeifPixelImage>> decodingResult = in_handle->context->decode_image(id,
                                                                                            colorspace,
                                                                                            chroma,
                                                                                            dec_options,
                                                                                            true, x0,y0);
  if (decodingResult.error.error_code != heif_error_Ok) {
    return decodingResult.error.error_struct(in_handle->image.get());
  }

  std::shared_ptr<HeifPixelImage> img = decodingResult.value;

  *out_img = new heif_image();
  (*out_img)->image = std::move(img);

  return Error::Ok.error_struct(in_handle->image.get());
}



int heif_image_handle_get_pixel_aspect_ratio(const struct heif_image_handle* handle, uint32_t* aspect_h, uint32_t* aspect_v)
{
  auto pasp = handle->image->get_property<Box_pasp>();
  if (pasp) {
    *aspect_h = pasp->hSpacing;
    *aspect_v = pasp->vSpacing;
    return 1;
  }
  else {
    *aspect_h = 1;
    *aspect_v = 1;
    return 0;
  }
}

void heif_image_handle_release(const struct heif_image_handle* handle)
{
  delete handle;
}




/*
int  heif_image_get_number_of_data_chunks(heif_image* img);

void heif_image_get_data_chunk(heif_image* img, int chunk_index,
                               uint8_t const*const* dataptr,
                               int const* data_size);

void heif_image_free_data_chunk(heif_image* img, int chunk_index);
*/


/*
void heif_context_reset(struct heif_context* ctx)
{
  ctx->context->reset_to_empty_heif();
}
*/

static struct heif_error heif_file_writer_write(struct heif_context* ctx,
                                                const void* data, size_t size, void* userdata)
{
  const char* filename = static_cast<const char*>(userdata);

#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_MSC_VER)
  std::ofstream ostr(HeifFile::convert_utf8_path_to_utf16(filename).c_str(), std::ios_base::binary);
#else
  std::ofstream ostr(filename, std::ios_base::binary);
#endif
  ostr.write(static_cast<const char*>(data), size);
  // TODO: handle write errors
  return Error::Ok.error_struct(ctx->context.get());
}


struct heif_error heif_context_write_to_file(struct heif_context* ctx,
                                             const char* filename)
{
  heif_writer writer;
  writer.writer_api_version = 1;
  writer.write = heif_file_writer_write;
  return heif_context_write(ctx, &writer, (void*) filename);
}


struct heif_error heif_context_write(struct heif_context* ctx,
                                     struct heif_writer* writer,
                                     void* userdata)
{
  if (!writer) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(ctx->context.get());
  }
  else if (writer->writer_api_version != 1) {
    Error err(heif_error_Usage_error, heif_suberror_Unsupported_writer_version);
    return err.error_struct(ctx->context.get());
  }

  StreamWriter swriter;
  ctx->context->write(swriter);

  const auto& data = swriter.get_data();
  heif_error writer_error = writer->write(ctx, data.data(), data.size(), userdata);
  if (!writer_error.message) {
    // It is now allowed to return a NULL error message on success. It will be replaced by "Success". An error message is still required when there is an error.
    if (writer_error.code == heif_error_Ok) {
      writer_error.message = Error::kSuccess;
      return writer_error;
    }
    else {
      return heif_error{heif_error_Usage_error, heif_suberror_Null_pointer_argument, "heif_writer callback returned a null error text"};
    }
  }
  else {
    return writer_error;
  }
}


void heif_context_add_compatible_brand(struct heif_context* ctx,
                                       heif_brand2 compatible_brand)
{
  ctx->context->get_heif_file()->get_ftyp_box()->add_compatible_brand(compatible_brand);
}


int heif_context_get_encoder_descriptors(struct heif_context* ctx,
                                         enum heif_compression_format format,
                                         const char* name,
                                         const struct heif_encoder_descriptor** out_encoder_descriptors,
                                         int count)
{
  return heif_get_encoder_descriptors(format, name, out_encoder_descriptors, count);
}


int heif_get_encoder_descriptors(enum heif_compression_format format,
                                 const char* name,
                                 const struct heif_encoder_descriptor** out_encoder_descriptors,
                                 int count)
{
  if (out_encoder_descriptors != nullptr && count <= 0) {
    return 0;
  }

  std::vector<const struct heif_encoder_descriptor*> descriptors;
  descriptors = get_filtered_encoder_descriptors(format, name);

  if (out_encoder_descriptors == nullptr) {
    return static_cast<int>(descriptors.size());
  }

  int i;
  for (i = 0; i < count && static_cast<size_t>(i) < descriptors.size(); i++) {
    out_encoder_descriptors[i] = descriptors[i];
  }

  return i;
}


const char* heif_encoder_descriptor_get_name(const struct heif_encoder_descriptor* descriptor)
{
  return descriptor->plugin->get_plugin_name();
}


const char* heif_encoder_descriptor_get_id_name(const struct heif_encoder_descriptor* descriptor)
{
  return descriptor->plugin->id_name;
}


int heif_get_decoder_descriptors(enum heif_compression_format format_filter,
                                 const struct heif_decoder_descriptor** out_decoders,
                                 int count)
{
  struct decoder_with_priority
  {
    const heif_decoder_plugin* plugin;
    int priority;
  };

  std::vector<decoder_with_priority> plugins;
  std::vector<heif_compression_format> formats;
  if (format_filter == heif_compression_undefined) {
    formats = {heif_compression_HEVC, heif_compression_AV1, heif_compression_JPEG, heif_compression_JPEG2000, heif_compression_HTJ2K, heif_compression_VVC};
  }
  else {
    formats.emplace_back(format_filter);
  }

  for (const auto* plugin : get_decoder_plugins()) {
    for (auto& format : formats) {
      int priority = plugin->does_support_format(format);
      if (priority) {
        plugins.push_back({plugin, priority});
        break;
      }
    }
  }

  if (out_decoders == nullptr) {
    return (int) plugins.size();
  }

  std::sort(plugins.begin(), plugins.end(), [](const decoder_with_priority& a, const decoder_with_priority& b) {
    return a.priority > b.priority;
  });

  int nDecodersReturned = std::min(count, (int) plugins.size());

  for (int i = 0; i < nDecodersReturned; i++) {
    out_decoders[i] = (heif_decoder_descriptor*) (plugins[i].plugin);
  }

  return nDecodersReturned;
}


const char* heif_decoder_descriptor_get_name(const struct heif_decoder_descriptor* descriptor)
{
  auto decoder = (heif_decoder_plugin*) descriptor;
  return decoder->get_plugin_name();
}


const char* heif_decoder_descriptor_get_id_name(const struct heif_decoder_descriptor* descriptor)
{
  auto decoder = (heif_decoder_plugin*) descriptor;
  if (decoder->plugin_api_version < 3) {
    return nullptr;
  }
  else {
    return decoder->id_name;
  }
}


enum heif_compression_format
heif_encoder_descriptor_get_compression_format(const struct heif_encoder_descriptor* descriptor)
{
  return descriptor->plugin->compression_format;
}


int heif_encoder_descriptor_supports_lossy_compression(const struct heif_encoder_descriptor* descriptor)
{
  return descriptor->plugin->supports_lossy_compression;
}


int heif_encoder_descriptor_supports_lossless_compression(const struct heif_encoder_descriptor* descriptor)
{
  return descriptor->plugin->supports_lossless_compression;
}


// DEPRECATED: typo in function name
int heif_encoder_descriptor_supportes_lossy_compression(const struct heif_encoder_descriptor* descriptor)
{
  return descriptor->plugin->supports_lossy_compression;
}


// DEPRECATED: typo in function name
int heif_encoder_descriptor_supportes_lossless_compression(const struct heif_encoder_descriptor* descriptor)
{
  return descriptor->plugin->supports_lossless_compression;
}


const char* heif_encoder_get_name(const struct heif_encoder* encoder)
{
  return encoder->plugin->get_plugin_name();
}


struct heif_error heif_context_get_encoder(struct heif_context* context,
                                           const struct heif_encoder_descriptor* descriptor,
                                           struct heif_encoder** encoder)
{
  // Note: be aware that context may be NULL as we explicitly allowed that in an earlier documentation.

  if (!descriptor || !encoder) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(context ? context->context.get() : nullptr);
  }

  *encoder = new struct heif_encoder(descriptor->plugin);
  return (*encoder)->alloc();
}


int heif_have_decoder_for_format(enum heif_compression_format format)
{
  auto plugin = get_decoder(format, nullptr);
  return plugin != nullptr;
}


int heif_have_encoder_for_format(enum heif_compression_format format)
{
  auto plugin = get_encoder(format);
  return plugin != nullptr;
}


struct heif_error heif_context_get_encoder_for_format(struct heif_context* context,
                                                      enum heif_compression_format format,
                                                      struct heif_encoder** encoder)
{
  // Note: be aware that context may be NULL as we explicitly allowed that in an earlier documentation.

  if (!encoder) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(context ? context->context.get() : nullptr);
  }

  std::vector<const struct heif_encoder_descriptor*> descriptors;
  descriptors = get_filtered_encoder_descriptors(format, nullptr);

  if (descriptors.size() > 0) {
    *encoder = new struct heif_encoder(descriptors[0]->plugin);
    return (*encoder)->alloc();
  }
  else {
    *encoder = nullptr;
    Error err(heif_error_Unsupported_filetype, // TODO: is this the right error code?
              heif_suberror_Unspecified);
    return err.error_struct(context ? context->context.get() : nullptr);
  }
}


void heif_encoder_release(struct heif_encoder* encoder)
{
  if (encoder) {
    delete encoder;
  }
}


//struct heif_encoder_param* heif_encoder_get_param(struct heif_encoder* encoder)
//{
//  return nullptr;
//}


//void heif_encoder_release_param(struct heif_encoder_param* param)
//{
//}


// Set a 'quality' factor (0-100). How this is mapped to actual encoding parameters is
// encoder dependent.
struct heif_error heif_encoder_set_lossy_quality(struct heif_encoder* encoder,
                                                 int quality)
{
  if (!encoder) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(nullptr);
  }

  return encoder->plugin->set_parameter_quality(encoder->encoder, quality);
}


struct heif_error heif_encoder_set_lossless(struct heif_encoder* encoder, int enable)
{
  if (!encoder) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(nullptr);
  }

  return encoder->plugin->set_parameter_lossless(encoder->encoder, enable);
}


struct heif_error heif_encoder_set_logging_level(struct heif_encoder* encoder, int level)
{
  if (!encoder) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(nullptr);
  }

  if (encoder->plugin->set_parameter_logging_level) {
    return encoder->plugin->set_parameter_logging_level(encoder->encoder, level);
  }

  return heif_error_success;
}


const struct heif_encoder_parameter* const* heif_encoder_list_parameters(struct heif_encoder* encoder)
{
  return encoder->plugin->list_parameters(encoder->encoder);
}


const char* heif_encoder_parameter_get_name(const struct heif_encoder_parameter* param)
{
  return param->name;
}

enum heif_encoder_parameter_type
heif_encoder_parameter_get_type(const struct heif_encoder_parameter* param)
{
  return param->type;
}


struct heif_error heif_encoder_set_parameter_integer(struct heif_encoder* encoder,
                                                     const char* parameter_name,
                                                     int value)
{
  // --- check if parameter is valid

  for (const struct heif_encoder_parameter* const* params = heif_encoder_list_parameters(encoder);
       *params;
       params++) {
    if (strcmp((*params)->name, parameter_name) == 0) {

      int have_minimum = 0, have_maximum = 0, minimum = 0, maximum = 0, num_valid_values = 0;
      const int* valid_values = nullptr;
      heif_error err = heif_encoder_parameter_get_valid_integer_values((*params), &have_minimum, &have_maximum,
                                                                       &minimum, &maximum,
                                                                       &num_valid_values,
                                                                       &valid_values);
      if (err.code) {
        return err;
      }

      if ((have_minimum && value < minimum) ||
          (have_maximum && value > maximum)) {
        return error_invalid_parameter_value;
      }

      if (num_valid_values > 0) {
        bool found = false;
        for (int i = 0; i < num_valid_values; i++) {
          if (valid_values[i] == value) {
            found = true;
            break;
          }
        }

        if (!found) {
          return error_invalid_parameter_value;
        }
      }
    }
  }


  // --- parameter is ok, pass it to the encoder plugin

  return encoder->plugin->set_parameter_integer(encoder->encoder, parameter_name, value);
}

struct heif_error heif_encoder_get_parameter_integer(struct heif_encoder* encoder,
                                                     const char* parameter_name,
                                                     int* value_ptr)
{
  return encoder->plugin->get_parameter_integer(encoder->encoder, parameter_name, value_ptr);
}

struct heif_error
heif_encoder_parameter_get_valid_integer_range(const struct heif_encoder_parameter* param,
                                               int* have_minimum_maximum,
                                               int* minimum, int* maximum)
{
  if (param->type != heif_encoder_parameter_type_integer) {
    return error_unsupported_parameter; // TODO: correct error ?
  }

  if (param->integer.have_minimum_maximum) {
    if (minimum) {
      *minimum = param->integer.minimum;
    }

    if (maximum) {
      *maximum = param->integer.maximum;
    }
  }

  if (have_minimum_maximum) {
    *have_minimum_maximum = param->integer.have_minimum_maximum;
  }

  return heif_error_success;
}


struct heif_error heif_encoder_parameter_get_valid_integer_values(const struct heif_encoder_parameter* param,
                                                                  int* have_minimum, int* have_maximum,
                                                                  int* minimum, int* maximum,
                                                                  int* num_valid_values,
                                                                  const int** out_integer_array)
{
  if (param->type != heif_encoder_parameter_type_integer) {
    return error_unsupported_parameter; // TODO: correct error ?
  }


  // --- range of values

  if (param->integer.have_minimum_maximum) {
    if (minimum) {
      *minimum = param->integer.minimum;
    }

    if (maximum) {
      *maximum = param->integer.maximum;
    }
  }

  if (have_minimum) {
    *have_minimum = param->integer.have_minimum_maximum;
  }

  if (have_maximum) {
    *have_maximum = param->integer.have_minimum_maximum;
  }


  // --- set of valid values

  if (param->integer.num_valid_values > 0) {
    if (out_integer_array) {
      *out_integer_array = param->integer.valid_values;
    }
  }

  if (num_valid_values) {
    *num_valid_values = param->integer.num_valid_values;
  }

  return heif_error_success;
}


struct heif_error
heif_encoder_parameter_get_valid_string_values(const struct heif_encoder_parameter* param,
                                               const char* const** out_stringarray)
{
  if (param->type != heif_encoder_parameter_type_string) {
    return error_unsupported_parameter; // TODO: correct error ?
  }

  if (out_stringarray) {
    *out_stringarray = param->string.valid_values;
  }

  return heif_error_success;
}

struct heif_error heif_encoder_parameter_integer_valid_range(struct heif_encoder* encoder,
                                                             const char* parameter_name,
                                                             int* have_minimum_maximum,
                                                             int* minimum, int* maximum)
{
  for (const struct heif_encoder_parameter* const* params = heif_encoder_list_parameters(encoder);
       *params;
       params++) {
    if (strcmp((*params)->name, parameter_name) == 0) {
      return heif_encoder_parameter_get_valid_integer_range(*params, have_minimum_maximum,
                                                            minimum, maximum);
    }
  }

  return error_unsupported_parameter;
}

struct heif_error heif_encoder_set_parameter_boolean(struct heif_encoder* encoder,
                                                     const char* parameter_name,
                                                     int value)
{
  return encoder->plugin->set_parameter_boolean(encoder->encoder, parameter_name, value);
}

struct heif_error heif_encoder_get_parameter_boolean(struct heif_encoder* encoder,
                                                     const char* parameter_name,
                                                     int* value_ptr)
{
  return encoder->plugin->get_parameter_boolean(encoder->encoder, parameter_name, value_ptr);
}

struct heif_error heif_encoder_set_parameter_string(struct heif_encoder* encoder,
                                                    const char* parameter_name,
                                                    const char* value)
{
  return encoder->plugin->set_parameter_string(encoder->encoder, parameter_name, value);
}

struct heif_error heif_encoder_get_parameter_string(struct heif_encoder* encoder,
                                                    const char* parameter_name,
                                                    char* value_ptr, int value_size)
{
  return encoder->plugin->get_parameter_string(encoder->encoder, parameter_name,
                                               value_ptr, value_size);
}

struct heif_error heif_encoder_parameter_string_valid_values(struct heif_encoder* encoder,
                                                             const char* parameter_name,
                                                             const char* const** out_stringarray)
{
  for (const struct heif_encoder_parameter* const* params = heif_encoder_list_parameters(encoder);
       *params;
       params++) {
    if (strcmp((*params)->name, parameter_name) == 0) {
      return heif_encoder_parameter_get_valid_string_values(*params, out_stringarray);
    }
  }

  return error_unsupported_parameter;
}

struct heif_error heif_encoder_parameter_integer_valid_values(struct heif_encoder* encoder,
                                                              const char* parameter_name,
                                                              int* have_minimum, int* have_maximum,
                                                              int* minimum, int* maximum,
                                                              int* num_valid_values,
                                                              const int** out_integer_array)
{
  for (const struct heif_encoder_parameter* const* params = heif_encoder_list_parameters(encoder);
       *params;
       params++) {
    if (strcmp((*params)->name, parameter_name) == 0) {
      return heif_encoder_parameter_get_valid_integer_values(*params, have_minimum, have_maximum, minimum, maximum,
                                                             num_valid_values, out_integer_array);
    }
  }

  return error_unsupported_parameter;
}


static bool parse_boolean(const char* value)
{
  if (strcmp(value, "true") == 0) {
    return true;
  }
  else if (strcmp(value, "false") == 0) {
    return false;
  }
  else if (strcmp(value, "1") == 0) {
    return true;
  }
  else if (strcmp(value, "0") == 0) {
    return false;
  }

  return false;
}


struct heif_error heif_encoder_set_parameter(struct heif_encoder* encoder,
                                             const char* parameter_name,
                                             const char* value)
{
  for (const struct heif_encoder_parameter* const* params = heif_encoder_list_parameters(encoder);
       *params;
       params++) {
    if (strcmp((*params)->name, parameter_name) == 0) {
      switch ((*params)->type) {
        case heif_encoder_parameter_type_integer:
          return heif_encoder_set_parameter_integer(encoder, parameter_name, atoi(value));

        case heif_encoder_parameter_type_boolean:
          return heif_encoder_set_parameter_boolean(encoder, parameter_name, parse_boolean(value));

        case heif_encoder_parameter_type_string:
          return heif_encoder_set_parameter_string(encoder, parameter_name, value);
          break;
      }

      return heif_error_success;
    }
  }

  return heif_encoder_set_parameter_string(encoder, parameter_name, value);

  //return error_unsupported_parameter;
}


struct heif_error heif_encoder_get_parameter(struct heif_encoder* encoder,
                                             const char* parameter_name,
                                             char* value_ptr, int value_size)
{
  for (const struct heif_encoder_parameter* const* params = heif_encoder_list_parameters(encoder);
       *params;
       params++) {
    if (strcmp((*params)->name, parameter_name) == 0) {
      switch ((*params)->type) {
        case heif_encoder_parameter_type_integer: {
          int value;
          struct heif_error error = heif_encoder_get_parameter_integer(encoder, parameter_name, &value);
          if (error.code) {
            return error;
          }
          else {
            snprintf(value_ptr, value_size, "%d", value);
          }
        }
          break;

        case heif_encoder_parameter_type_boolean: {
          int value;
          struct heif_error error = heif_encoder_get_parameter_boolean(encoder, parameter_name, &value);
          if (error.code) {
            return error;
          }
          else {
            snprintf(value_ptr, value_size, "%d", value);
          }
        }
          break;

        case heif_encoder_parameter_type_string: {
          struct heif_error error = heif_encoder_get_parameter_string(encoder, parameter_name,
                                                                      value_ptr, value_size);
          if (error.code) {
            return error;
          }
        }
          break;
      }

      return heif_error_success;
    }
  }

  return error_unsupported_parameter;
}


int heif_encoder_has_default(struct heif_encoder* encoder,
                             const char* parameter_name)
{
  for (const struct heif_encoder_parameter* const* params = heif_encoder_list_parameters(encoder);
       *params;
       params++) {
    if (strcmp((*params)->name, parameter_name) == 0) {

      if ((*params)->version >= 2) {
        return (*params)->has_default;
      }
      else {
        return true;
      }
    }
  }

  return false;
}


void set_default_encoding_options(heif_encoding_options& options)
{
  options.version = 7;

  options.save_alpha_channel = true;
  options.macOS_compatibility_workaround = false;
  options.save_two_colr_boxes_when_ICC_and_nclx_available = false;
  options.output_nclx_profile = nullptr;
  options.macOS_compatibility_workaround_no_nclx_profile = false;
  options.image_orientation = heif_orientation_normal;

  options.color_conversion_options.version = 1;
  options.color_conversion_options.preferred_chroma_downsampling_algorithm = heif_chroma_downsampling_average;
  options.color_conversion_options.preferred_chroma_upsampling_algorithm = heif_chroma_upsampling_bilinear;
  options.color_conversion_options.only_use_preferred_chroma_algorithm = false;

  options.prefer_uncC_short_form = true;
}

void copy_options(heif_encoding_options& options, const heif_encoding_options& input_options)
{
  switch (input_options.version) {
    case 7:
      options.prefer_uncC_short_form = input_options.prefer_uncC_short_form;
      // fallthrough
    case 6:
      options.color_conversion_options = input_options.color_conversion_options;
      // fallthrough
    case 5:
      options.image_orientation = input_options.image_orientation;
      // fallthrough
    case 4:
      options.output_nclx_profile = input_options.output_nclx_profile;
      options.macOS_compatibility_workaround_no_nclx_profile = input_options.macOS_compatibility_workaround_no_nclx_profile;
      // fallthrough
    case 3:
      options.save_two_colr_boxes_when_ICC_and_nclx_available = input_options.save_two_colr_boxes_when_ICC_and_nclx_available;
      // fallthrough
    case 2:
      options.macOS_compatibility_workaround = input_options.macOS_compatibility_workaround;
      // fallthrough
    case 1:
      options.save_alpha_channel = input_options.save_alpha_channel;
  }
}


heif_encoding_options* heif_encoding_options_alloc()
{
  auto options = new heif_encoding_options;

  set_default_encoding_options(*options);

  return options;
}


void heif_encoding_options_free(heif_encoding_options* options)
{
  delete options;
}

struct heif_error heif_context_encode_image(struct heif_context* ctx,
                                            const struct heif_image* input_image,
                                            struct heif_encoder* encoder,
                                            const struct heif_encoding_options* input_options,
                                            struct heif_image_handle** out_image_handle)
{
  if (!encoder) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(ctx->context.get());
  }

  if (out_image_handle) {
    *out_image_handle = nullptr;
  }

  heif_encoding_options options;
  heif_color_profile_nclx nclx;
  set_default_encoding_options(options);
  if (input_options) {
    copy_options(options, *input_options);

    if (options.output_nclx_profile == nullptr) {
      auto input_nclx = input_image->image->get_color_profile_nclx();
      if (input_nclx) {
        options.output_nclx_profile = &nclx;
        nclx.version = 1;
        nclx.color_primaries = (enum heif_color_primaries) input_nclx->get_colour_primaries();
        nclx.transfer_characteristics = (enum heif_transfer_characteristics) input_nclx->get_transfer_characteristics();
        nclx.matrix_coefficients = (enum heif_matrix_coefficients) input_nclx->get_matrix_coefficients();
        nclx.full_range_flag = input_nclx->get_full_range_flag();
      }
    }
  }

  auto encodingResult = ctx->context->encode_image(input_image->image,
                                     encoder,
                                     options,
                                     heif_image_input_class_normal);
  if (encodingResult.error != Error::Ok) {
    return encodingResult.error.error_struct(ctx->context.get());
  }

  std::shared_ptr<ImageItem> image = *encodingResult;

  // mark the new image as primary image

  if (ctx->context->is_primary_image_set() == false) {
    ctx->context->set_primary_image(image);
  }

  if (out_image_handle) {
    *out_image_handle = new heif_image_handle;
    (*out_image_handle)->image = std::move(image);
    (*out_image_handle)->context = ctx->context;
  }

  return heif_error_success;
}


struct heif_error heif_context_encode_grid(struct heif_context* ctx,
                                           struct heif_image** tiles,
                                           uint16_t columns,
                                           uint16_t rows,
                                           struct heif_encoder* encoder,
                                           const struct heif_encoding_options* input_options,
                                           struct heif_image_handle** out_image_handle)
{
  if (!encoder || !tiles) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(ctx->context.get());
  }
  else if (rows == 0 || columns == 0) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Invalid_parameter_value).error_struct(ctx->context.get());
  }

  // TODO: Don't repeat this code from heif_context_encode_image()
  heif_encoding_options options;
  heif_color_profile_nclx nclx;
  set_default_encoding_options(options);
  if (input_options) {
    copy_options(options, *input_options);

    if (options.output_nclx_profile == nullptr) {
      auto input_nclx = tiles[0]->image->get_color_profile_nclx();
      if (input_nclx) {
        options.output_nclx_profile = &nclx;
        nclx.version = 1;
        nclx.color_primaries = (enum heif_color_primaries) input_nclx->get_colour_primaries();
        nclx.transfer_characteristics = (enum heif_transfer_characteristics) input_nclx->get_transfer_characteristics();
        nclx.matrix_coefficients = (enum heif_matrix_coefficients) input_nclx->get_matrix_coefficients();
        nclx.full_range_flag = input_nclx->get_full_range_flag();
      }
    }
  }

  // Convert heif_images to a vector of HeifPixelImages
  std::vector<std::shared_ptr<HeifPixelImage>> pixel_tiles;
  for (int i=0; i<rows*columns; i++) {
    pixel_tiles.push_back(tiles[i]->image);
  }

  // Encode Grid
  std::shared_ptr<ImageItem> out_grid;
  auto addGridResult = ImageItem_Grid::add_and_encode_full_grid(ctx->context.get(),
                                                                pixel_tiles,
                                                                rows, columns,
                                                                encoder,
                                                                options);
  if (addGridResult.error) {
    return addGridResult.error.error_struct(ctx->context.get());
  }

  out_grid = addGridResult.value;

  // Mark as primary image
  if (ctx->context->is_primary_image_set() == false) {
    ctx->context->set_primary_image(out_grid);
  }

  if (out_image_handle) {
    *out_image_handle = new heif_image_handle;
    (*out_image_handle)->image = std::move(out_grid);
    (*out_image_handle)->context = ctx->context;
  }

  return heif_error_success;
}


struct heif_error heif_context_add_grid_image(struct heif_context* ctx,
                                              uint32_t image_width,
                                              uint32_t image_height,
                                              uint32_t tile_columns,
                                              uint32_t tile_rows,
                                              const struct heif_encoding_options* encoding_options,
                                              struct heif_image_handle** out_grid_image_handle)
{
  if (tile_rows == 0 || tile_columns == 0) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Invalid_parameter_value).error_struct(ctx->context.get());
  }
  else if (tile_rows > 0xFFFF || tile_columns > 0xFFFF) {
    return heif_error{heif_error_Usage_error,
                      heif_suberror_Invalid_image_size,
                      "Number of tile rows/columns may not exceed 65535"};
  }

  auto generateGridItemResult = ImageItem_Grid::add_new_grid_item(ctx->context.get(),
                                                                  image_width,
                                                                  image_height,
                                                                  static_cast<uint16_t>(tile_rows),
                                                                  static_cast<uint16_t>(tile_columns),
                                                                  encoding_options);
  if (generateGridItemResult.error) {
    return generateGridItemResult.error.error_struct(ctx->context.get());
  }

  if (out_grid_image_handle) {
    *out_grid_image_handle = new heif_image_handle;
    (*out_grid_image_handle)->image = generateGridItemResult.value;
    (*out_grid_image_handle)->context = ctx->context;
  }

  return heif_error_success;
}


struct heif_error heif_context_add_overlay_image(struct heif_context* ctx,
                                                 uint32_t image_width,
                                                 uint32_t image_height,
                                                 uint16_t nImages,
                                                 const heif_item_id* image_ids,
                                                 int32_t* offsets,
                                                 const uint16_t background_rgba[4],
                                                 struct heif_image_handle** out_iovl_image_handle)
{
  if (!image_ids) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(ctx->context.get());
  }
  else if (nImages == 0) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Invalid_parameter_value).error_struct(ctx->context.get());
  }


  std::vector<heif_item_id> refs;
  refs.insert(refs.end(), image_ids, image_ids + nImages);

  ImageOverlay overlay;
  overlay.set_canvas_size(image_width, image_height);

  if (background_rgba) {
    overlay.set_background_color(background_rgba);
  }

  for (uint16_t i=0;i<nImages;i++) {
    overlay.add_image_on_top(image_ids[i],
                             offsets ? offsets[2 * i] : 0,
                             offsets ? offsets[2 * i + 1] : 0);
  }

  Result<std::shared_ptr<ImageItem_Overlay>> addImageResult = ImageItem_Overlay::add_new_overlay_item(ctx->context.get(), overlay);

  if (addImageResult.error != Error::Ok) {
    return addImageResult.error.error_struct(ctx->context.get());
  }

  std::shared_ptr<ImageItem> iovlimage = addImageResult.value;


  if (out_iovl_image_handle) {
    *out_iovl_image_handle = new heif_image_handle;
    (*out_iovl_image_handle)->image = std::move(iovlimage);
    (*out_iovl_image_handle)->context = ctx->context;
  }

  return heif_error_success;
}


struct heif_error heif_context_add_tiled_image(struct heif_context* ctx,
                                               const struct heif_tiled_image_parameters* parameters,
                                               const struct heif_encoding_options* options, // TODO: do we need this?
                                               const struct heif_encoder* encoder,
                                               struct heif_image_handle** out_grid_image_handle)
{
  if (out_grid_image_handle) {
    *out_grid_image_handle = nullptr;
  }

  Result<std::shared_ptr<ImageItem_Tiled>> gridImageResult;
  gridImageResult = ImageItem_Tiled::add_new_tiled_item(ctx->context.get(), parameters, encoder);

  if (gridImageResult.error != Error::Ok) {
    return gridImageResult.error.error_struct(ctx->context.get());
  }

  if (out_grid_image_handle) {
    *out_grid_image_handle = new heif_image_handle;
    (*out_grid_image_handle)->image = gridImageResult.value;
    (*out_grid_image_handle)->context = ctx->context;
  }

  return heif_error_success;
}


struct heif_error heif_context_add_image_tile(struct heif_context* ctx,
                                              struct heif_image_handle* tiled_image,
                                              uint32_t tile_x, uint32_t tile_y,
                                              const struct heif_image* image,
                                              struct heif_encoder* encoder)
{
  if (auto tili_image = std::dynamic_pointer_cast<ImageItem_Tiled>(tiled_image->image)) {
    Error err = tili_image->add_image_tile(tile_x, tile_y, image->image, encoder);
    return err.error_struct(ctx->context.get());
  }
#if WITH_UNCOMPRESSED_CODEC
  else if (auto unci = std::dynamic_pointer_cast<ImageItem_uncompressed>(tiled_image->image)) {
    Error err = unci->add_image_tile(tile_x, tile_y, image->image);
    return err.error_struct(ctx->context.get());
  }
#endif
  else if (auto grid_item = std::dynamic_pointer_cast<ImageItem_Grid>(tiled_image->image)) {
    Error err = grid_item->add_image_tile(tile_x, tile_y, image->image, encoder);
    return err.error_struct(ctx->context.get());
  }
  else {
    return {
      heif_error_Usage_error,
      heif_suberror_Unspecified,
      "Cannot add tile to a non-tiled image"
    };
  }
}


struct heif_error heif_context_add_unci_image(struct heif_context* ctx,
                                              const struct heif_unci_image_parameters* parameters,
                                              const struct heif_encoding_options* encoding_options,
                                              const heif_image* prototype,
                                              struct heif_image_handle** out_unci_image_handle)
{
#if WITH_UNCOMPRESSED_CODEC
  Result<std::shared_ptr<ImageItem_uncompressed>> unciImageResult;
  unciImageResult = ImageItem_uncompressed::add_unci_item(ctx->context.get(), parameters, encoding_options, prototype->image);

  if (unciImageResult.error != Error::Ok) {
    return unciImageResult.error.error_struct(ctx->context.get());
  }

  if (out_unci_image_handle) {
    *out_unci_image_handle = new heif_image_handle;
    (*out_unci_image_handle)->image = unciImageResult.value;
    (*out_unci_image_handle)->context = ctx->context;
  }

  return heif_error_success;
#else
  return {heif_error_Unsupported_feature,
          heif_suberror_Unspecified,
          "support for uncompressed images (ISO23001-17) has been disabled."};
#endif
}



struct heif_error heif_context_assign_thumbnail(struct heif_context* ctx,
                                                const struct heif_image_handle* master_image,
                                                const struct heif_image_handle* thumbnail_image)
{
  Error error = ctx->context->assign_thumbnail(thumbnail_image->image, master_image->image);
  return error.error_struct(ctx->context.get());
}


struct heif_error heif_context_encode_thumbnail(struct heif_context* ctx,
                                                const struct heif_image* image,
                                                const struct heif_image_handle* image_handle,
                                                struct heif_encoder* encoder,
                                                const struct heif_encoding_options* input_options,
                                                int bbox_size,
                                                struct heif_image_handle** out_image_handle)
{
  heif_encoding_options options;
  set_default_encoding_options(options);

  if (input_options != nullptr) {
    copy_options(options, *input_options);
  }

  auto encodingResult = ctx->context->encode_thumbnail(image->image,
                                               encoder,
                                               options,
                                               bbox_size);
  if (encodingResult.error != Error::Ok) {
    return encodingResult.error.error_struct(ctx->context.get());
  }

  std::shared_ptr<ImageItem> thumbnail_image = *encodingResult;

  if (!thumbnail_image) {
    Error err(heif_error_Usage_error,
              heif_suberror_Invalid_parameter_value,
              "Thumbnail images must be smaller than the original image.");
    return err.error_struct(ctx->context.get());
  }

  Error error = ctx->context->assign_thumbnail(image_handle->image, thumbnail_image);
  if (error != Error::Ok) {
    return error.error_struct(ctx->context.get());
  }


  if (out_image_handle) {
    *out_image_handle = new heif_image_handle;
    (*out_image_handle)->image = thumbnail_image;
    (*out_image_handle)->context = ctx->context;
  }

  return heif_error_success;
}


struct heif_error heif_context_set_primary_image(struct heif_context* ctx,
                                                 struct heif_image_handle* image_handle)
{
  ctx->context->set_primary_image(image_handle->image);

  return heif_error_success;
}


struct heif_error heif_context_add_exif_metadata(struct heif_context* ctx,
                                                 const struct heif_image_handle* image_handle,
                                                 const void* data, int size)
{
  Error error = ctx->context->add_exif_metadata(image_handle->image, data, size);
  if (error != Error::Ok) {
    return error.error_struct(ctx->context.get());
  }
  else {
    return heif_error_success;
  }
}


struct heif_error heif_context_add_XMP_metadata(struct heif_context* ctx,
                                                const struct heif_image_handle* image_handle,
                                                const void* data, int size)
{
  return heif_context_add_XMP_metadata2(ctx, image_handle, data, size,
                                        heif_metadata_compression_off);
}


struct heif_error heif_context_add_XMP_metadata2(struct heif_context* ctx,
                                                 const struct heif_image_handle* image_handle,
                                                 const void* data, int size,
                                                 heif_metadata_compression compression)
{
  Error error = ctx->context->add_XMP_metadata(image_handle->image, data, size, compression);
  if (error != Error::Ok) {
    return error.error_struct(ctx->context.get());
  }
  else {
    return heif_error_success;
  }
}


struct heif_error heif_context_add_generic_metadata(struct heif_context* ctx,
                                                    const struct heif_image_handle* image_handle,
                                                    const void* data, int size,
                                                    const char* item_type, const char* content_type)
{
  if (item_type == nullptr || strlen(item_type) != 4) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "called heif_context_add_generic_metadata() with invalid 'item_type'."};
  }

  Error error = ctx->context->add_generic_metadata(image_handle->image, data, size,
                                                   fourcc(item_type), content_type, nullptr, heif_metadata_compression_off, nullptr);
  if (error != Error::Ok) {
    return error.error_struct(ctx->context.get());
  }
  else {
    return heif_error_success;
  }
}


struct heif_error heif_context_add_generic_uri_metadata(struct heif_context* ctx,
                                                        const struct heif_image_handle* image_handle,
                                                        const void* data, int size,
                                                        const char* item_uri_type,
                                                        heif_item_id* out_item_id)
{
  Error error = ctx->context->add_generic_metadata(image_handle->image, data, size,
                                                   fourcc("uri "), nullptr, item_uri_type, heif_metadata_compression_off, out_item_id);
  if (error != Error::Ok) {
    return error.error_struct(ctx->context.get());
  }
  else {
    return heif_error_success;
  }
}


void heif_context_set_maximum_image_size_limit(struct heif_context* ctx, int maximum_width)
{
  ctx->context->get_security_limits()->max_image_size_pixels = static_cast<uint64_t>(maximum_width) * maximum_width;
}


void heif_context_set_max_decoding_threads(struct heif_context* ctx, int max_threads)
{
  ctx->context->set_max_decoding_threads(max_threads);
}

