/*
  libheif example application.
  This file is part of heif-dec, an example application using libheif.

  MIT License

  Copyright (c) 2018 struktur AG, Joachim Bauch <bauch@struktur.de>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include <stdlib.h>
#include <cstring>

#include "encoder.h"

static const char kMetadataTypeExif[] = "Exif";

// static
bool Encoder::HasExifMetaData(const struct heif_image_handle* handle)
{

  heif_item_id metadata_id;
  int count = heif_image_handle_get_list_of_metadata_block_IDs(handle, kMetadataTypeExif,
                                                               &metadata_id, 1);
  return count > 0;
}

// static
uint8_t* Encoder::GetExifMetaData(const struct heif_image_handle* handle, size_t* size)
{
  heif_item_id metadata_id;
  int count = heif_image_handle_get_list_of_metadata_block_IDs(handle, kMetadataTypeExif,
                                                               &metadata_id, 1);

  for (int i = 0; i < count; i++) {
    size_t datasize = heif_image_handle_get_metadata_size(handle, metadata_id);
    uint8_t* data = static_cast<uint8_t*>(malloc(datasize));
    if (!data) {
      continue;
    }

    heif_error error = heif_image_handle_get_metadata(handle, metadata_id, data);
    if (error.code != heif_error_Ok) {
      free(data);
      continue;
    }

    *size = datasize;
    return data;
  }

  return nullptr;
}


std::vector<uint8_t> Encoder::get_xmp_metadata(const struct heif_image_handle* handle)
{
  std::vector<uint8_t> xmp;

  heif_item_id metadata_ids[16];
  int count = heif_image_handle_get_list_of_metadata_block_IDs(handle, nullptr, metadata_ids, 16);

  for (int i = 0; i < count; i++) {
    if (strcmp(heif_image_handle_get_metadata_type(handle, metadata_ids[i]), "mime") == 0 &&
        strcmp(heif_image_handle_get_metadata_content_type(handle, metadata_ids[i]), "application/rdf+xml") == 0) {

      size_t datasize = heif_image_handle_get_metadata_size(handle, metadata_ids[i]);
      xmp.resize(datasize);

      heif_error error = heif_image_handle_get_metadata(handle, metadata_ids[i], xmp.data());
      if (error.code != heif_error_Ok) {
        // TODO: return error
        return {};
      }

      return xmp;
    }
  }

  return {};
}
