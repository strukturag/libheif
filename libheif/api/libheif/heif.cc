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
static struct heif_error error_unsupported_parameter = {heif_error_Usage_error,
                                                        heif_suberror_Unsupported_parameter,
                                                        "Unsupported encoder parameter"};
static struct heif_error error_invalid_parameter_value = {heif_error_Usage_error,
                                                          heif_suberror_Invalid_parameter_value,
                                                          "Invalid parameter value"};
static struct heif_error error_null_parameter = {heif_error_Usage_error,
                                                 heif_suberror_Null_pointer_argument,
                                                 "NULL passed"};



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




struct heif_error heif_context_set_primary_image(struct heif_context* ctx,
                                                 struct heif_image_handle* image_handle)
{
  ctx->context->set_primary_image(image_handle->image);

  return heif_error_success;
}


void heif_context_set_max_decoding_threads(struct heif_context* ctx, int max_threads)
{
  ctx->context->set_max_decoding_threads(max_threads);
}

