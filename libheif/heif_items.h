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

// ------------------------- reading -------------------------

LIBHEIF_API
int heif_context_get_number_of_items(const struct heif_context* ctx);

// Fills in file level metadata IDs into the user-supplied int-array 'ID_array',
// preallocated with 'count' entries.
// Function returns the total number of IDs filled into the array.
LIBHEIF_API
int heif_context_get_list_of_item_IDs(const struct heif_context* ctx,
                                      heif_item_id* ID_array,
                                      int count);

/**
 * Gets the metadata type for a top-level metadata item.
 *
 * This is the 4CC code (e.g. `mime` or `uri `) for the item.
 *
 * @param ctx the file context
 * @param item_id the item identifier for the item
 * @return the item type
 */
LIBHEIF_API
uint32_t heif_context_get_item_type(const struct heif_context* ctx, heif_item_id item_id);

#define heif_item_type_mime   heif_fourcc('m','i','m','e')
#define heif_item_type_uri    heif_fourcc('u','r','i',' ')


/**
 * Gets the content_type for an item.
 *
 * This is the MIME for the metadata item. Only valid if the item type is `mime`.
 * If the item does not exist, or if it is no 'mime' item, NULL is returned.
 *
 * @param ctx the file context
 * @param item_id the item identifier for the item
 * @return the item content_type
 */
LIBHEIF_API
const char* heif_context_get_mime_item_content_type(const struct heif_context* ctx, heif_item_id item_id);

/**
 * Gets the item_uri_type for an item.
 *
 * Only valid if the item type is `uri `.
 * If the item does not exist, or if it is no 'uri ' item, NULL is returned.
 *
 * @param ctx the file context
 * @param item_id the item identifier for the item
 * @return the item item_uri_type
 */
LIBHEIF_API
const char* heif_context_get_uri_item_uri_type(const struct heif_context* ctx, heif_item_id item_id);

LIBHEIF_API
const char* heif_context_get_item_name(const struct heif_context* ctx, heif_item_id item_id);


// Get the size of the raw metadata, as stored in the HEIF file.
// Returns 0 if no data is stored for this item.
// If the data is compressed (in the sense of a "mime" item with "content_encoding"), the uncompressed data is returned.
// It is legal to set 'out_data' to NULL. In that case, only the 'out_data_size' is filled.
LIBHEIF_API
struct heif_error heif_context_get_item_data(const struct heif_context* ctx,
                                             heif_item_id item_id,
                                             uint8_t** out_data, size_t* out_data_size);

// Free the memory returned by heif_context_get_item_data().
LIBHEIF_API
void heif_release_item_data(const struct heif_context* ctx, uint8_t** item_data);

// ------------------------- writing -------------------------

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
