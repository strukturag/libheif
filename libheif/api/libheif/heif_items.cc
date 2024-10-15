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

#include "heif_items.h"
#include "context.h"
#include "api_structs.h"
#include "file.h"

#include <cstring>
#include <memory>
#include <vector>
#include <string>



// ------------------------- reading -------------------------

int heif_context_get_number_of_items(const struct heif_context* ctx)
{
  return (int) ctx->context->get_heif_file()->get_number_of_items();
}


int heif_context_get_list_of_item_IDs(const struct heif_context* ctx,
                                      heif_item_id* ID_array,
                                      int count)
{
  if (!ID_array) {
    return 0;
  }

  auto ids = ctx->context->get_heif_file()->get_item_IDs();
  for (int i = 0; i < (int) ids.size(); i++) {
    if (i == count) {
      return count;
    }

    ID_array[i] = ids[i];
  }

  return (int) ids.size();
}


uint32_t heif_context_get_item_type(const struct heif_context* ctx, heif_item_id item_id)
{
  return ctx->context->get_heif_file()->get_item_type_4cc(item_id);
}


int heif_context_is_item_hidden(const struct heif_context* ctx, heif_item_id item_id)
{
  auto infe = ctx->context->get_heif_file()->get_infe_box(item_id);
  if (infe == nullptr) {
    return true;
  }
  else {
    return infe->is_hidden_item();
  }
}


const char* heif_context_get_mime_item_content_type(const struct heif_context* ctx, heif_item_id item_id)
{
  auto infe = ctx->context->get_heif_file()->get_infe_box(item_id);
  if (!infe) { return nullptr; }

  if (infe->get_item_type_4cc() != fourcc("mime")) {
    return nullptr;
  }

  return infe->get_content_type().c_str();
}

const char* heif_context_get_mime_item_content_encoding(const struct heif_context* ctx, heif_item_id item_id)
{
  auto infe = ctx->context->get_heif_file()->get_infe_box(item_id);
  if (!infe) { return nullptr; }

  if (infe->get_item_type_4cc() != fourcc("mime")) {
    return nullptr;
  }

  return infe->get_content_encoding().c_str();
}


const char* heif_context_get_uri_item_uri_type(const struct heif_context* ctx, heif_item_id item_id)
{
  auto infe = ctx->context->get_heif_file()->get_infe_box(item_id);
  if (!infe) { return nullptr; }

  if (infe->get_item_type_4cc() != fourcc("uri ")) {
    return nullptr;
  }

  return infe->get_item_uri_type().c_str();
}

const char* heif_context_get_item_name(const struct heif_context* ctx, heif_item_id item_id)
{
  auto infe = ctx->context->get_heif_file()->get_infe_box(item_id);
  if (!infe) { return nullptr; }

  return infe->get_item_name().c_str();
}


struct heif_error heif_context_get_item_data(const struct heif_context* ctx,
                                             heif_item_id item_id,
                                             heif_metadata_compression* out_compression_format,
                                             uint8_t** out_data, size_t* out_data_size)
{
  if (out_data && !out_data_size) {
      return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "cannot return data with out_data_size==NULL"};
  }

  std::vector<uint8_t> data;
  Error err = ctx->context->get_heif_file()->get_item_data(item_id, &data, out_compression_format);
  if (err) {
    *out_data_size = 0;
    if (out_data) {
      *out_data = 0;
    }

    return err.error_struct(ctx->context.get());
  }

  if (out_data_size) {
    *out_data_size = data.size();
  }

  if (out_data) {
    *out_data = new uint8_t[data.size()];
    memcpy(*out_data, data.data(), data.size());
  }

  return heif_error_success;
}


void heif_release_item_data(const struct heif_context* ctx, uint8_t** item_data)
{
  (void) ctx;

  if (item_data) {
    delete[] *item_data;
    *item_data = nullptr;
  }
}


struct heif_error heif_context_get_item_data(struct heif_context* ctx,
                                             heif_item_id item_id,
                                             void* out_data)
{
  std::vector<uint8_t> data;
  Error err = ctx->context->get_heif_file()->get_uncompressed_item_data(item_id, &data);

  if (err) {
    return err.error_struct(ctx->context.get());
  }
  else {
    memcpy(out_data, data.data(), data.size());
    return heif_error_success;
  }
}


size_t heif_context_get_item_references(const struct heif_context* ctx,
                                        heif_item_id from_item_id,
                                        int index,
                                        uint32_t* out_reference_type_4cc,
                                        heif_item_id** out_references_to)
{
  if (index < 0) {
    return 0;
  }

  auto iref = ctx->context->get_heif_file()->get_iref_box();
  if (!iref) {
    return 0;
  }

  auto refs = iref->get_references_from(from_item_id);
  if (index >= (int) refs.size()) {
    return 0;
  }

  const auto& ref = refs[index];
  if (out_reference_type_4cc) {
    *out_reference_type_4cc = ref.header.get_short_type();
  }

  if (out_references_to) {
    *out_references_to = new heif_item_id[ref.to_item_ID.size()];
    for (size_t i = 0; i < ref.to_item_ID.size(); i++) {
      (*out_references_to)[i] = ref.to_item_ID[i];
    }
  }

  return ref.to_item_ID.size();
}


void heif_release_item_references(const struct heif_context* ctx, heif_item_id** references)
{
  (void) ctx;

  if (references) {
    delete[] *references;
    *references = nullptr;
  }
}


// ------------------------- writing -------------------------

struct heif_error heif_context_add_item(struct heif_context* ctx,
                                        const char* item_type,
                                        const void* data, int size,
                                        heif_item_id* out_item_id)
{
  if (item_type == nullptr || strlen(item_type) != 4) {
    return {heif_error_Usage_error,
            heif_suberror_Invalid_parameter_value,
            "called heif_context_add_item() with invalid 'item_type'."};
  }

  Result<heif_item_id> result = ctx->context->get_heif_file()->add_infe(fourcc(item_type), (const uint8_t*) data, size);

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

LIBHEIF_API
struct heif_error heif_context_add_precompressed_mime_item(struct heif_context* ctx,
                                                           const char* content_type,
                                                           const char* content_encoding,
                                                           const void* data, int size,
                                                           heif_item_id* out_item_id)
{
  Result<heif_item_id> result = ctx->context->get_heif_file()->add_precompressed_infe_mime(content_type, content_encoding, (const uint8_t*) data, size);

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
                                                  uint32_t reference_type,
                                                  heif_item_id from_item,
                                                  heif_item_id to_item)
{
  ctx->context->get_heif_file()->add_iref_reference(from_item,
                                                    reference_type, {to_item});

  return heif_error_success;
}

struct heif_error heif_context_add_item_references(struct heif_context* ctx,
                                                   uint32_t reference_type,
                                                   heif_item_id from_item,
                                                   const heif_item_id* to_item,
                                                   int num_to_items)
{
  std::vector<heif_item_id> to_refs(to_item, to_item + num_to_items);

  ctx->context->get_heif_file()->add_iref_reference(from_item,
                                                    reference_type, to_refs);

  return heif_error_success;
}

struct heif_error heif_context_set_item_name(struct heif_context* ctx,
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
