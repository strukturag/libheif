/*
 * HEIF codec.
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
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

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include "heif.h"
#include "heif_image.h"
#include "heif_api_structs.h"
#include "heif_context.h"
#include "error.h"
#include "bitstream.h"

#if defined(__EMSCRIPTEN__)
#include "heif-emscripten.h"
#endif

#include <algorithm>
#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <string.h>
#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif

#if defined(_MSC_VER)
// for _write
#include <io.h>
#endif

using namespace heif;

struct heif_encoder_options {
  struct heif_encoder* encoder;
  struct heif_encoder_options* options;
};

const char *heif_get_version(void) {
  return (LIBHEIF_VERSION);
}

uint32_t heif_get_version_number(void) {
  return (LIBHEIF_NUMERIC_VERSION);
}

int heif_get_version_number_major(void) {
  return ((LIBHEIF_NUMERIC_VERSION)>>24) & 0xFF;
}

int heif_get_version_number_minor(void) {
  return ((LIBHEIF_NUMERIC_VERSION)>>16) & 0xFF;
}

int heif_get_version_number_maintenance(void) {
  return ((LIBHEIF_NUMERIC_VERSION)>>8) & 0xFF;
}

heif_context* heif_context_alloc()
{
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
  Error err = ctx->context->read_from_memory(mem, size);
  return err.error_struct(ctx->context.get());
}

// TODO: heif_error heif_context_read_from_file_descriptor(heif_context*, int fd);

void heif_context_debug_dump_boxes_to_file(struct heif_context* ctx, int fd) {
  if (!ctx) {
    return;
  }

  std::string dump = ctx->context->debug_dump_boxes();
  // TODO(fancycode): Should we return an error if writing fails?
#if defined(_MSC_VER)
  auto written = _write(fd, dump.c_str(), dump.size());
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

  std::shared_ptr<HeifContext::Image> primary_image = ctx->context->get_primary_image();

  // It is a requirement of an HEIF file there is always a primary image.
  // If there is none, an error is generated when loading the file.
  if (!primary_image) {
    Error err(heif_error_Invalid_input,
              heif_suberror_No_or_invalid_primary_item);
    return err.error_struct(ctx->context.get());
  }

  *img = new heif_image_handle();
  (*img)->image = std::move(primary_image);
  //(*img)->context = ctx->context;

  return Error::Ok.error_struct(ctx->context.get());
}


struct heif_error heif_context_get_primary_image_ID(struct heif_context* ctx, heif_item_id* id)
{
  if (!id) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(ctx->context.get());
  }

  std::shared_ptr<HeifContext::Image> primary = ctx->context->get_primary_image();
  if (!primary) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_or_invalid_primary_item).error_struct(ctx->context.get());
  }

  *id = primary->get_id();

  return Error::Ok.error_struct(ctx->context.get());
}


int heif_context_is_top_level_image_ID(struct heif_context* ctx, heif_item_id id)
{
  const std::vector<std::shared_ptr<HeifContext::Image>> images = ctx->context->get_top_level_images();

  for (const auto& img : images) {
    if (img->get_id() == id) {
      return true;
    }
  }

  return false;
}


int heif_context_get_number_of_top_level_images(heif_context* ctx)
{
  return (int)ctx->context->get_top_level_images().size();
}


int heif_context_get_list_of_top_level_image_IDs(struct heif_context* ctx,
                                                 heif_item_id* ID_array,
                                                 int count)
{
  if (ID_array == nullptr || count==0 || ctx==nullptr) {
    return 0;
  }


  // fill in ID values into output array

  const std::vector<std::shared_ptr<HeifContext::Image>> imgs = ctx->context->get_top_level_images();
  int n = (int)std::min(count,(int)imgs.size());
  for (int i=0;i<n;i++) {
    ID_array[i] = imgs[i]->get_id();
  }

  return n;
}


struct heif_error heif_context_get_image_handle(struct heif_context* ctx,
                                                heif_item_id id,
                                                struct heif_image_handle** img)
{
  if (!img) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(ctx->context.get());
  }

  const std::vector<std::shared_ptr<HeifContext::Image>> images = ctx->context->get_top_level_images();

  std::shared_ptr<HeifContext::Image> image;
  for (auto& img : images) {
    if (img->get_id() == id) {
      image = img;
      break;
    }
  }

  if (!image) {
    Error err(heif_error_Usage_error, heif_suberror_Nonexisting_item_referenced);
    return err.error_struct(ctx->context.get());
  }

  *img = new heif_image_handle();
  (*img)->image = image;
  // (*img)->context = ctx->context;

  return Error::Ok.error_struct(ctx->context.get());
}


int heif_image_handle_is_primary_image(const struct heif_image_handle* handle)
{
  return handle->image->is_primary();
}


int heif_image_handle_get_number_of_thumbnails(const struct heif_image_handle* handle)
{
  return (int)handle->image->get_thumbnails().size();
}


int heif_image_handle_get_list_of_thumbnail_IDs(const struct heif_image_handle* handle,
                                                heif_item_id* ids, int count)
{
  if (ids==nullptr) {
    return 0;
  }

  auto thumbnails = handle->image->get_thumbnails();
  int n = (int)std::min(count, (int)thumbnails.size());

  for (int i=0;i<n;i++) {
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
  for (auto thumb : thumbnails) {
    if (thumb->get_id() == thumbnail_id) {
      *out_thumbnail_handle = new heif_image_handle();
      (*out_thumbnail_handle)->image = thumb;

      return Error::Ok.error_struct(handle->image.get());
    }
  }

  Error err(heif_error_Usage_error, heif_suberror_Nonexisting_item_referenced);
  return err.error_struct(handle->image.get());
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


int heif_image_handle_has_alpha_channel(const struct heif_image_handle* handle)
{
  return handle->image->get_alpha_channel() != nullptr;
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
  if (out) {
    if (handle->image->has_depth_representation_info()) {
      auto info = new heif_depth_representation_info;
      *info = handle->image->get_depth_representation_info();
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

  if (count==0) {
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
  auto depth_image = handle->image->get_depth_channel();

  *out_depth_handle = new heif_image_handle();
  (*out_depth_handle)->image = depth_image;

  if (depth_image->get_id() != depth_id) {
    Error err(heif_error_Usage_error, heif_suberror_Nonexisting_item_referenced);
    return err.error_struct(handle->image.get());
  }

  return Error::Ok.error_struct(handle->image.get());
}


heif_decoding_options* heif_decoding_options_alloc()
{
  auto options = new heif_decoding_options;

  options->version = 1;

  options->ignore_transformations = false;

  options->start_progress = NULL;
  options->on_progress = NULL;
  options->end_progress = NULL;
  options->progress_user_data = NULL;

  return options;
}


void heif_decoding_options_free(heif_decoding_options* options)
{
  delete options;
}


struct heif_error heif_decode_image(const struct heif_image_handle* in_handle,
                                    struct heif_image** out_img,
                                    heif_colorspace colorspace,
                                    heif_chroma chroma,
                                    const struct heif_decoding_options* options)
{
  std::shared_ptr<HeifPixelImage> img;

  Error err = in_handle->image->decode_image(img,
                                             colorspace,
                                             chroma,
                                             options);
  if (err.error_code != heif_error_Ok) {
    return err.error_struct(in_handle->image.get());
  }

  *out_img = new heif_image();
  (*out_img)->image = std::move(img);

  return Error::Ok.error_struct(in_handle->image.get());
}


struct heif_error heif_image_create(int width, int height,
                                    heif_colorspace colorspace,
                                    heif_chroma chroma,
                                    struct heif_image** image)
{
  struct heif_image* img = new heif_image;
  img->image = std::make_shared<HeifPixelImage>();

  img->image->create(width, height, colorspace, chroma);

  *image = img;

  struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, Error::kSuccess };
  return err;
}

void heif_image_release(const struct heif_image* img)
{
  delete img;
}

void heif_image_handle_release(const struct heif_image_handle* handle)
{
  delete handle;
}


enum heif_colorspace heif_image_get_colorspace(const struct heif_image* img)
{
  return img->image->get_colorspace();
}

enum heif_chroma heif_image_get_chroma_format(const struct heif_image* img)
{
  return img->image->get_chroma_format();
}


int heif_image_get_width(const struct heif_image* img,enum heif_channel channel)
{
  return img->image->get_width(channel);
}

int heif_image_get_height(const struct heif_image* img,enum heif_channel channel)
{
  return img->image->get_height(channel);
}


int heif_image_get_bits_per_pixel(const struct heif_image* img,enum heif_channel channel)
{
  return img->image->get_bits_per_pixel(channel);
}


struct heif_error heif_image_add_plane(struct heif_image* image,
                                       heif_channel channel, int width, int height, int bit_depth)
{
  image->image->add_plane(channel, width, height, bit_depth);

  struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, Error::kSuccess };
  return err;
}



const uint8_t* heif_image_get_plane_readonly(const struct heif_image* image,
                                             enum heif_channel channel,
                                             int* out_stride)
{
  if (!image || !image->image) {
    *out_stride = 0;
    return nullptr;
  }

  return image->image->get_plane(channel, out_stride);
}


uint8_t* heif_image_get_plane(struct heif_image* image,
                              enum heif_channel channel,
                              int* out_stride)
{
  if (!image || !image->image) {
    *out_stride = 0;
    return nullptr;
  }

  return image->image->get_plane(channel, out_stride);
}


struct heif_error heif_image_scale_image(const struct heif_image* input,
                                         struct heif_image** output,
                                         int width, int height,
                                         const struct heif_scaling_options* options)
{
  std::shared_ptr<HeifPixelImage> out_img;

  Error err = input->image->scale_nearest_neighbor(out_img, width, height);
  if (err) {
    return err.error_struct(input->image.get());
  }

  *output = new heif_image;
  (*output)->image = out_img;

  return Error::Ok.error_struct(input->image.get());
}


int heif_image_handle_get_number_of_metadata_blocks(const struct heif_image_handle* handle,
                                                    const char* type_filter)
{
  auto metadata_list = handle->image->get_metadata();

  int cnt=0;
  for (const auto& metadata : metadata_list) {
    if (type_filter==nullptr ||
        metadata->item_type == type_filter) {
      cnt++;
    }
  }

  return cnt;
}


int heif_image_handle_get_list_of_metadata_block_IDs(const struct heif_image_handle* handle,
                                                     const char* type_filter,
                                                     heif_item_id* ids, int count)
{
  auto metadata_list = handle->image->get_metadata();

  int cnt=0;
  for (const auto& metadata : metadata_list) {
    if (type_filter==nullptr ||
        metadata->item_type == type_filter) {
      if (cnt < count) {
        ids[cnt] = metadata->item_id;
        cnt++;
      }
      else {
        break;
      }
    }
  }

  return cnt;
}


const char* heif_image_handle_get_metadata_type(const struct heif_image_handle* handle,
                                                heif_item_id metadata_id)
{
  auto metadata_list = handle->image->get_metadata();

  for (auto metadata : metadata_list) {
    if (metadata->item_id == metadata_id) {
      return metadata->item_type.c_str();
    }
  }

  return NULL;
}


size_t heif_image_handle_get_metadata_size(const struct heif_image_handle* handle,
                                           heif_item_id metadata_id)
{
  auto metadata_list = handle->image->get_metadata();

  for (auto metadata : metadata_list) {
    if (metadata->item_id == metadata_id) {
      return metadata->m_data.size();
    }
  }

  return 0;
}


struct heif_error heif_image_handle_get_metadata(const struct heif_image_handle* handle,
                                                 heif_item_id metadata_id,
                                                 void* out_data)
{
  if (out_data==nullptr) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(handle->image.get());
  }

  auto metadata_list = handle->image->get_metadata();

  for (auto metadata : metadata_list) {
    if (metadata->item_id == metadata_id) {
      memcpy(out_data,
             metadata->m_data.data(),
             metadata->m_data.size());

      return Error::Ok.error_struct(handle->image.get());
    }
  }

  Error err(heif_error_Usage_error,
            heif_suberror_Nonexisting_item_referenced);
  return err.error_struct(handle->image.get());
}



struct heif_error heif_register_decoder(heif_context* heif, const heif_decoder_plugin* decoder_plugin)
{
  if (decoder_plugin && decoder_plugin->plugin_api_version != 1) {
    Error err(heif_error_Usage_error, heif_suberror_Unsupported_plugin_version);
    return err.error_struct(heif->context.get());
  }

  heif->context->register_decoder(decoder_plugin);
  return Error::Ok.error_struct(heif->context.get());
}


struct heif_error heif_register_encoder(heif_context* heif, const heif_encoder_plugin* encoder_plugin)
{
  if (encoder_plugin && encoder_plugin->plugin_api_version != 1) {
    Error err(heif_error_Usage_error, heif_suberror_Unsupported_plugin_version);
    return err.error_struct(heif->context.get());
  }

  heif->context->register_encoder(encoder_plugin);
  return Error::Ok.error_struct(heif->context.get());
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
    const void* data, size_t size, void* userdata) {
  const char* filename = static_cast<const char*>(userdata);

  std::ofstream ostr(filename);
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
  return heif_context_write(ctx, &writer, (void*)filename);
}


struct heif_error heif_context_write(struct heif_context* ctx,
                                     struct heif_writer* writer,
                                     void* userdata)
{
  if (!writer) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(ctx->context.get());
  } else if (writer->writer_api_version != 1) {
    Error err(heif_error_Usage_error, heif_suberror_Unsupported_writer_version);
    return err.error_struct(ctx->context.get());
  }

  StreamWriter swriter;
  ctx->context->write(swriter);

  const auto& data = swriter.get_data();
  return writer->write(ctx, data.data(), data.size(), userdata);
}


int heif_context_get_encoders(struct heif_context* ctx,
                              enum heif_compression_format format,
                              const char* name,
                              struct heif_encoder** out_encoders,
                              int count)
{
  if (out_encoders == nullptr || count <= 0) {
    return 0;
  }

  std::vector<struct heif_encoder*> plugins;
  plugins = ctx->context->get_filtered_encoders(format, name);

  int i;
  for (i=0 ; i < count && static_cast<size_t>(i) < plugins.size() ; i++) {
    out_encoders[i] = plugins[i];
  }

  return i;
}


const char* heif_encoder_get_name(const struct heif_encoder* encoder)
{
  return encoder->plugin->get_plugin_name();
}


struct heif_encoder_options* heif_encoder_options_alloc(struct heif_encoder* encoder) {
  struct heif_encoder_options* options = new heif_encoder_options();
  options->encoder = encoder;
  options->options = encoder->plugin->alloc_options(encoder->encoder);
  if (!options->options) {
    delete(options);
    return nullptr;
  }

  return options;
}


void heif_encoder_options_free(struct heif_encoder_options* options) {
  options->encoder->plugin->free_options(options->encoder->encoder,
      options->options);
  delete options;
}


struct heif_error heif_encoder_options_set_int(struct heif_encoder_options* options,
                                               const char* name,
                                               int value) {
  return options->encoder->plugin->set_option_int(options->encoder->encoder,
      options->options, name, value);
}


struct heif_error heif_context_encode_image(struct heif_context* ctx,
                                            const struct heif_image* input_image,
                                            struct heif_encoder* encoder,
                                            struct heif_encoder_options* options,
                                            struct heif_image_handle** out_image_handle)
{
  if (!encoder || !options) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(ctx->context.get());
  }

  if (options->encoder != encoder) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Options_for_other_encoder).error_struct(ctx->context.get());
  }

  std::shared_ptr<HeifContext::Image> image;
  Error error(heif_error_Encoder_plugin_error, heif_suberror_Unsupported_codec);
  struct heif_error err;
  switch (encoder->plugin->compression_format) {
    case heif_compression_HEVC:
      err = encoder->alloc();
      if (err.code != heif_error_Ok) {
        return err;
      }

      image = ctx->context->add_new_hvc1_image();
      error = image->encode_image_as_hevc(input_image->image, encoder, options->options);
      encoder->release();
      break;

    default:
      // Will return "heif_suberror_Unsupported_codec" from above.
      break;
  }
  if (error != Error::Ok) {
    return error.error_struct(ctx->context.get());
  }
  ctx->context->set_primary_image(image);

  if (out_image_handle) {
    *out_image_handle = new heif_image_handle;
    (*out_image_handle)->image = image;
  }

  err = { heif_error_Ok, heif_suberror_Unspecified, kSuccess };
  return err;
}
