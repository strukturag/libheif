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

#ifndef LIBHEIF_HEIF_ITEMS_H
#define LIBHEIF_HEIF_ITEMS_H

#include "libheif/heif.h"

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------- items -------------------------

LIBHEIF_API
struct heif_error heif_context_add_item(struct heif_context* ctx,
                                        const char* item_type,
                                        const void* data, int size,
                                        heif_item_id* out_item_id);

LIBHEIF_API
struct heif_error heif_context_add_mime_item(struct heif_context* ctx,
                                             const char* content_type,
                                             heif_metadata_compression content_encoding,
                                             const void* data, int size,
                                             heif_item_id* out_item_id);

LIBHEIF_API
struct heif_error heif_context_add_uri_item(struct heif_context* ctx,
                                            const char* item_uri_type,
                                            const void* data, int size,
                                            heif_item_id* out_item_id);

LIBHEIF_API
struct heif_error heif_context_add_item_reference(struct heif_context* ctx,
                                                  const char* reference_type,
                                                  heif_item_id from_item,
                                                  heif_item_id to_item);

LIBHEIF_API
struct heif_error heif_context_add_item_references(struct heif_context* ctx,
                                                   const char* reference_type,
                                                   heif_item_id from_item,
                                                   const heif_item_id* to_item,
                                                   int num_to_items);

LIBHEIF_API
struct heif_error heif_context_add_item_name(struct heif_context* ctx,
                                             heif_item_id item,
                                             const char* item_name);

#ifdef __cplusplus
}
#endif

#endif
