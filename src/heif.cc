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


#include <memory>
#include <utility>
#include <vector>
#include <assert.h>

using namespace heif;


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

// NOTE: data types will change ! (TODO)
heif_error heif_context_get_primary_image_handle(heif_context* ctx, heif_image_handle** img)
{
  if (!img) {
    // TODO
  }

  std::shared_ptr<HeifContext::Image> primary_image = ctx->context->get_primary_image();
  if (!primary_image) {
    Error err(heif_error_Usage_error, heif_suberror_Nonexisting_image_referenced);
    return err.error_struct(ctx->context.get());
  }

  //uint16_t primary_ID = ctx->context->get_primary_image_ID();

  *img = new heif_image_handle();
  (*img)->image = std::move(primary_image);

  /*
  Error err = ctx->context->decode_image(primary_ID, (*img)->image);
  assert((*img)->image);

  return err.error_struct();
  */

  return Error::Ok.error_struct(ctx->context.get());
}


size_t heif_context_get_number_of_images(heif_context* ctx)
{
  return ctx->context->get_top_level_images().size();
}


// NOTE: data types will change ! (TODO)
heif_error heif_context_get_image_handle(heif_context* ctx, size_t image_idx, heif_image_handle** img)
{
  if (!img) {
    // TODO
  }

  //const auto& IDs = ctx->context->get_image_IDs();

  const std::vector<std::shared_ptr<HeifContext::Image>> images = ctx->context->get_top_level_images();
  if (image_idx >= images.size()) {
    Error err(heif_error_Usage_error, heif_suberror_Nonexisting_image_referenced);
    return err.error_struct(ctx->context.get());
  }

  *img = new heif_image_handle();
  //(*img)->image_ID = IDs[image_ID];
  (*img)->image = images[image_idx];

  /*
  Error err = ctx->context->decode_image(image_ID, (*img)->image);
  return err.error_struct();
  */

  return Error::Ok.error_struct(ctx->context.get());
}


int heif_image_handle_is_primary_image(const struct heif_context* h,
                                       const struct heif_image_handle* handle)
{
  return handle->image->is_primary();
}


size_t heif_image_handle_get_number_of_thumbnails(const struct heif_context* h,
                                                  const struct heif_image_handle* handle)
{
  return handle->image->get_thumbnails().size();
}


heif_error heif_image_handle_get_thumbnail(const struct heif_context* h,
                                           const struct heif_image_handle* handle,
                                           size_t thumbnail_idx,
                                           struct heif_image_handle** out_thumbnail_handle)
{
  assert(out_thumbnail_handle);

  auto thumbnails = handle->image->get_thumbnails();
  if (thumbnail_idx >= thumbnails.size()) {
    Error err(heif_error_Usage_error, heif_suberror_Nonexisting_image_referenced);
    return err.error_struct(h->context.get());
  }

  *out_thumbnail_handle = new heif_image_handle();
  (*out_thumbnail_handle)->image = thumbnails[thumbnail_idx];

  return Error::Ok.error_struct(h->context.get());
}


struct heif_error heif_image_handle_get_resolution(const struct heif_context* h,
                                                   const struct heif_image_handle* handle,
                                                   int* width, int* height)
{
  if (width) *width = handle->image->get_width();
  if (height) *height = handle->image->get_height();
  return Error::Ok.error_struct(h->context.get());
}

struct heif_error heif_decode_image(struct heif_context* ctx,
                                    const struct heif_image_handle* in_handle,
                                    struct heif_image** out_img,
                                    heif_colorspace colorspace,
                                    heif_chroma chroma)
{
  *out_img = new heif_image();

  //Error err = ctx->context->decode_image(in_handle->image_ID, (*out_img)->image);
  Error err = in_handle->image->decode_image(ctx, (*out_img)->image,
                                             colorspace,
                                             chroma,
                                             nullptr);
  if (err.error_code != heif_error_Ok) {
    delete *out_img;
    *out_img = nullptr;
  }

  // TODO: colorspace conversion

  return err.error_struct(ctx->context.get());
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
  return img->image->get_width();
}

int heif_image_get_height(const struct heif_image* img,enum heif_channel channel)
{
  return img->image->get_height();
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
  assert(image->image);
  return image->image->get_plane(channel, out_stride);
}


uint8_t* heif_image_get_plane(struct heif_image* image,
                              enum heif_channel channel,
                              int* out_stride)
{
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
