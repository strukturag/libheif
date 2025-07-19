/*
 * HEIF codec.
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
 * Copyright (c) 2025 Brad Hards <bradh@frogmouth.net>
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

#include "heif_text.h"
#include "api_structs.h"
#include "text.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

struct heif_error heif_image_handle_add_text_item(heif_image_handle *image_handle,
                                           const char *content_type,
                                           const char *text,
                                           heif_text_item** out_text_item)
{

  std::shared_ptr<TextItem> textItem = image_handle->context->add_text_item(content_type, text);
  image_handle->image->add_text_item_id(textItem->get_item_id());
  if (out_text_item) {
    heif_text_item* item = new heif_text_item();
    item->context = image_handle->context;
    item->text_item = std::move(textItem);
    *out_text_item = item;
  }
  return heif_error_success;
}

void heif_text_item_release(struct heif_text_item* text_item)
{
  delete text_item;
}

int heif_image_handle_get_number_of_text_items(const struct heif_image_handle* handle)
{
  return (int) handle->image->get_text_item_ids().size();
}

int heif_image_handle_get_list_of_text_item_ids(const struct heif_image_handle* handle,
                                                  heif_item_id* item_ids,
                                                  int max_count)
{
  auto text_item_ids = handle->image->get_text_item_ids();
  int num = std::min((int) text_item_ids.size(), max_count);

  memcpy(item_ids, text_item_ids.data(), num * sizeof(heif_item_id));

  return num;
}


struct heif_error heif_context_get_text_item(const struct heif_context* context,
                                             heif_item_id text_item_id,
                                             struct heif_text_item** out)
{
  if (out==nullptr) {
    return {heif_error_Usage_error, heif_suberror_Null_pointer_argument, "NULL argument"};
  }

  auto r = context->context->get_text_item(text_item_id);

  if (r==nullptr) {
    return {heif_error_Usage_error, heif_suberror_Nonexisting_item_referenced, "Text item does not exist"};
  }

  heif_text_item* item = new heif_text_item();
  item->context = context->context;
  item->text_item = std::move(r);
  *out = item;

  return heif_error_success;
}

heif_item_id heif_text_item_get_id(struct heif_text_item* text_item)
{
  if (text_item == nullptr) {
    return 0;
  }

  return text_item->text_item->get_item_id();
}

const char* heif_text_item_get_content(struct heif_text_item* text_item)
{
  if (text_item == nullptr) {
    return NULL;
  }
  return text_item->text_item->get_item_text();
}