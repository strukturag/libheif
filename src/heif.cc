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

#include "heif.h"
#include "heif_image.h"
#include "heif_api_structs.h"
#include "heif_context.h"
#include "error.h"

#if defined(__EMSCRIPTEN__)
#include "heif-emscripten.h"
#endif

#include <memory>
#include <utility>
#include <vector>
#include <assert.h>

using namespace heif;

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

heif_error heif_context_read_from_file(heif_context* ctx, const char* filename)
{
  Error err = ctx->context->read_from_file(filename);
  return err.error_struct(ctx->context.get());
}

heif_error heif_context_read_from_memory(heif_context* ctx, const void* mem, size_t size)
{
  Error err = ctx->context->read_from_memory(mem, size);
  return err.error_struct(ctx->context.get());
}

// TODO
//heif_error heif_context_read_from_file_descriptor(heif_context*, int fd);

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
              heif_suberror_Nonexisting_image_referenced);
    return err.error_struct(ctx->context.get());
  }

  assert(primary_image);


  *img = new heif_image_handle();
  (*img)->image = std::move(primary_image);
  (*img)->context = ctx->context;

  return Error::Ok.error_struct(ctx->context.get());
}


int heif_context_get_number_of_images(heif_context* ctx)
{
  return (int)ctx->context->get_top_level_images().size();
}


heif_error heif_context_get_image_handle(heif_context* ctx, int image_idx, heif_image_handle** img)
{
  if (!img) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(ctx->context.get());
  }

  const std::vector<std::shared_ptr<HeifContext::Image>> images = ctx->context->get_top_level_images();
  if (image_idx<0 || (size_t)image_idx >= images.size()) {
    Error err(heif_error_Usage_error, heif_suberror_Nonexisting_image_referenced);
    return err.error_struct(ctx->context.get());
  }

  *img = new heif_image_handle();
  (*img)->image = images[image_idx];
  (*img)->context = ctx->context;

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


heif_error heif_image_handle_get_thumbnail(const struct heif_image_handle* handle,
                                           int thumbnail_idx,
                                           struct heif_image_handle** out_thumbnail_handle)
{
  assert(out_thumbnail_handle);

  auto thumbnails = handle->image->get_thumbnails();
  if (thumbnail_idx<0 || (size_t)thumbnail_idx >= thumbnails.size()) {
    Error err(heif_error_Usage_error, heif_suberror_Nonexisting_image_referenced);
    return err.error_struct(handle->image.get());
  }

  *out_thumbnail_handle = new heif_image_handle();
  (*out_thumbnail_handle)->image = thumbnails[thumbnail_idx];

  return Error::Ok.error_struct(handle->image.get());
}


void heif_image_handle_get_resolution(const struct heif_image_handle* handle,
                                      int* width, int* height)
{
  int w, h;

  if (handle && handle->image) {
    w = handle->image->get_width();
    h = handle->image->get_height();
  }
  else {
    w = 0;
    h = 0;
  }

  if (width)  *width = w;
  if (height) *height = h;
}

struct heif_error heif_decode_image(const struct heif_image_handle* in_handle,
                                    heif_colorspace colorspace,
                                    heif_chroma chroma,
                                    struct heif_image** out_img)
{
  std::shared_ptr<HeifPixelImage> img;

  //Error err = ctx->context->decode_image(in_handle->image_ID, (*out_img)->image);
  Error err = in_handle->image->decode_image(img,
                                             colorspace,
                                             chroma,
                                             nullptr);
  if (err.error_code != heif_error_Ok) {
    return err.error_struct(in_handle->image.get());
  }

  *out_img = new heif_image();
  (*out_img)->image = std::move(img);

  // TODO: colorspace conversion

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


struct heif_error heif_register_decoder(heif_context* heif, uint32_t type, const heif_decoder_plugin* decoder_plugin)
{
  heif->context->register_decoder(type, decoder_plugin);
  return Error::Ok.error_struct(heif->context.get());
}




/*
int  heif_image_get_number_of_data_chunks(heif_image* img);

void heif_image_get_data_chunk(heif_image* img, int chunk_index,
                               uint8_t const*const* dataptr,
                               int const* data_size);

void heif_image_free_data_chunk(heif_image* img, int chunk_index);
*/
