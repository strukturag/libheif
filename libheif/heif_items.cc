/*
 * HEIF codec.
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
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

#include "libheif/heif_items.h"
#include "context.h"
#include "api_structs.h"
#include "file.h"

#include <cstring>
#include <memory>
#include <vector>
#include <string>


struct heif_error heif_context_add_item(struct heif_context* ctx,
                                        const char* item_type,
                                        const void* data, int size,
                                        heif_item_id* out_item_id)
{
  Result<heif_item_id> result = ctx->context->get_heif_file()->add_infe(item_type, (const uint8_t*) data, size);

  if (result && out_item_id) {
    *out_item_id = result.value;
    return heif_error_success;
  }
  else {
    return result.error.error_struct(ctx->context.get());
  }
}

struct heif_error heif_context_add_mime_item(struct heif_context* ctx,
                                             const char* content_type,
                                             heif_metadata_compression content_encoding,
                                             const void* data, int size,
                                             heif_item_id* out_item_id)
{
  Result<heif_item_id> result = ctx->context->get_heif_file()->add_infe_mime(content_type, content_encoding, (const uint8_t*) data, size);

  if (result && out_item_id) {
    *out_item_id = result.value;
    return heif_error_success;
  }
  else {
    return result.error.error_struct(ctx->context.get());
  }
}

struct heif_error heif_context_add_uri_item(struct heif_context* ctx,
                                            const char* item_uri_type,
                                            const void* data, int size,
                                            heif_item_id* out_item_id)
{
  Result<heif_item_id> result = ctx->context->get_heif_file()->add_infe_uri(item_uri_type, (const uint8_t*) data, size);

  if (result && out_item_id) {
    *out_item_id = result.value;
    return heif_error_success;
  }
  else {
    return result.error.error_struct(ctx->context.get());
  }
}


struct heif_error heif_context_add_item_reference(struct heif_context* ctx,
                                                  const char* reference_type,
                                                  heif_item_id from_item,
                                                  heif_item_id to_item)
{
  ctx->context->get_heif_file()->add_iref_reference(from_item,
                                  fourcc(reference_type), {to_item});

  return heif_error_success;
}

struct heif_error heif_context_add_item_references(struct heif_context* ctx,
                                                   const char* reference_type,
                                                   heif_item_id from_item,
                                                   const heif_item_id* to_item,
                                                   int num_to_items)
{
  std::vector<heif_item_id> to_refs(to_item, to_item + num_to_items);

  ctx->context->get_heif_file()->add_iref_reference(from_item,
                                                    fourcc(reference_type), to_refs);

  return heif_error_success;
}

struct heif_error heif_context_add_item_name(struct heif_context* ctx,
                                             heif_item_id item,
                                             const char* item_name)
{
  auto infe = ctx->context->get_heif_file()->get_infe_box(item);
  if (!infe) {
    return heif_error{heif_error_Input_does_not_exist, heif_suberror_Nonexisting_item_referenced, "Item does not exist"};
  }

  infe->set_item_name(item_name);

  return heif_error_success;
}
