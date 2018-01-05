/*
 * libheif example application "heif".
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of heif, an example application using libheif.
 *
 * heif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * heif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with heif.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <string.h>

#include "config.h"
#include "heif.h"
#include "heif_api_structs.h"
#include "libde265/de265.h"

#include <fstream>
#include <iostream>

class ContextReleaser {
 public:
  ContextReleaser(struct heif_context* ctx) : ctx_(ctx) {}
  ~ContextReleaser() {
    heif_context_free(ctx_);
  }

 private:
  struct heif_context* ctx_;
};

int main(int argc, char** argv)
{
  if (argc < 2) {
    fprintf(stderr, "USAGE: %s <filename> [output]\n", argv[0]);
    return 1;
  }

  const char* input_filename = argv[1];

#if 0
  const char* output_filename = nullptr;
  if (argc >= 3) {
    output_filename = argv[2];
  }
#endif

  // ==============================================================================

  struct heif_context* ctx = heif_context_alloc();
  if (!ctx) {
    fprintf(stderr, "Could not create HEIF context\n");
    return 1;
  }

  ContextReleaser cr(ctx);
  struct heif_error err;
  err = heif_context_read_from_file(ctx, input_filename);
  if (err.code != 0) {
    std::cerr << "Could not read HEIF file: " << err.message << "\n";
    return 1;
  }

#ifndef HAVE_VISIBILITY
  std::cout << ctx->context->debug_dump_boxes();
#endif

  std::cout << "----------------------------------------------------------\n";

  std::cout << "num images: " << heif_context_get_number_of_images(ctx) << "\n";

  struct heif_image_handle* handle;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  if (err.code != 0) {
    std::cerr << "Could not get primage image handle: " << err.message << "\n";
    return 1;
  }

  struct heif_image* image;
  err = heif_decode_image(handle, &image, heif_colorspace_YCbCr,
      heif_chroma_420);
  if (err.code != 0) {
    heif_image_handle_release(handle);
    std::cerr << "Could not decode primage image: " << err.message << "\n";
    return 1;
  }

  heif_image_release(image);
  heif_image_handle_release(handle);
  return 0;
}
