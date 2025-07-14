/*
 * HEIF codec.
 * Copyright (c) 2024 Dirk Farin <dirk.farin@gmail.com>
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

#include "heif_uncompressed.h"
#include "context.h"
#include "api_structs.h"
#include "image-items/unc_image.h"

#include <array>
#include <memory>
#include <algorithm>


struct heif_unci_image_parameters* heif_unci_image_parameters_alloc()
{
  auto* params = new heif_unci_image_parameters();

  params->version = 1;

  // --- version 1

  params->image_width = 0;
  params->image_height = 0;

  // TODO: should we define that tile size = 0 means no tiling?
  params->tile_width = 0;
  params->tile_height = 0;

  params->compression = heif_unci_compression_off;

  return params;
}


void heif_unci_image_parameters_copy(struct heif_unci_image_parameters* dst,
                                     const struct heif_unci_image_parameters* src)
{
  if (src == nullptr || dst == nullptr) {
    return;
  }

  int min_version = std::min(src->version, dst->version);

  switch (min_version) {
    case 1:
      dst->image_width = src->image_width;
      dst->image_height = src->image_height;
      dst->tile_width = src->tile_width;
      dst->tile_height = src->tile_height;
      dst->compression = src->compression;
      break;
  }
}


void heif_unci_image_parameters_release(struct heif_unci_image_parameters* params)
{
  delete params;
}


struct heif_error heif_context_add_empty_unci_image(struct heif_context* ctx,
                                                    const struct heif_unci_image_parameters* parameters,
                                                    const struct heif_encoding_options* encoding_options,
                                                    const heif_image* prototype,
                                                    struct heif_image_handle** out_unci_image_handle)
{
#if WITH_UNCOMPRESSED_CODEC
  if (prototype == nullptr) {
    return {heif_error_Usage_error,
            heif_suberror_Null_pointer_argument,
            "prototype image is NULL"};
  }

  if (out_unci_image_handle == nullptr) {
    return {heif_error_Usage_error,
            heif_suberror_Null_pointer_argument,
            "out_unci_image_handle image is NULL"};
  }

  Result<std::shared_ptr<ImageItem_uncompressed>> unciImageResult;
  unciImageResult = ImageItem_uncompressed::add_unci_item(ctx->context.get(), parameters, encoding_options, prototype->image);

  if (unciImageResult.error != Error::Ok) {
    return unciImageResult.error.error_struct(ctx->context.get());
  }

  assert(out_unci_image_handle);
  *out_unci_image_handle = new heif_image_handle;
  (*out_unci_image_handle)->image = unciImageResult.value;
  (*out_unci_image_handle)->context = ctx->context;

  return heif_error_success;
#else
  return {heif_error_Unsupported_feature,
          heif_suberror_Unspecified,
          "support for uncompressed images (ISO23001-17) has been disabled."};
#endif
}
