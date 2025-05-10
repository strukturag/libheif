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


struct heif_error heif_context_add_unci_image(struct heif_context* ctx,
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
