/*
 * libheif example application "convert".
 * Copyright (c) 2018 struktur AG, Joachim Bauch <bauch@struktur.de>
 *
 * This file is part of convert, an example application using libheif.
 *
 * convert is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * convert is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with convert.  If not, see <http://www.gnu.org/licenses/>.
 */
#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <stdlib.h>

#if defined(HAVE_STRINGS_H)
#include <strings.h>
#endif

#include "encoder.h"

#if defined(_MSC_VER)
#define strcasecmp _stricmp
#endif

static const char kMetadataTypeExif[] = "Exif";

// static
bool Encoder::HasExifMetaData(const struct heif_image_handle* handle) {
  heif_item_id metadata_id;
  int count = heif_image_handle_get_list_of_metadata_block_IDs(
      handle, kMetadataTypeExif, &metadata_id, 1);
  return count > 0;
}

// static
uint8_t* Encoder::GetExifMetaData(const struct heif_image_handle* handle,
                                  size_t* size) {
  heif_item_id metadata_id;
  int count = heif_image_handle_get_list_of_metadata_block_IDs(
      handle, kMetadataTypeExif, &metadata_id, 1);

  for (int i = 0; i < count; i++) {
    size_t datasize = heif_image_handle_get_metadata_size(handle, metadata_id);
    uint8_t* data = static_cast<uint8_t*>(malloc(datasize));
    if (!data) {
      continue;
    }

    heif_error error =
        heif_image_handle_get_metadata(handle, metadata_id, data);
    if (error.code != heif_error_Ok) {
      free(data);
      continue;
    }

    *size = datasize;
    return data;
  }

  return nullptr;
}
