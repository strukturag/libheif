/*
  libheif test support utilities

  MIT License

  Copyright (c) 2023 Brad Hards <bradh@frogmouth.net>

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

#include "test_utils.h"
#include "test-config.h"
#include <cstring>
#include "catch.hpp"

struct heif_context * get_context_for_test_file(std::string filename)
{
  return get_context_for_local_file(tests_data_directory + "/" + filename);
}

struct heif_context * get_context_for_local_file(std::string filename)
{
  struct heif_context* context;
  struct heif_error err;
  context = heif_context_alloc();
  err = heif_context_read_from_file(context, filename.c_str(), NULL);
  INFO(filename);
  REQUIRE(err.code == heif_error_Ok);
  return context;
}

struct heif_image_handle * get_primary_image_handle(heif_context *context)
{
  struct heif_error err;
  struct heif_image_handle * image_handle;
  int num_images = heif_context_get_number_of_top_level_images(context);
  REQUIRE(num_images == 1);
  err =  heif_context_get_primary_image_handle(context, &image_handle);
  REQUIRE(err.code == heif_error_Ok);
  return image_handle;
}

struct heif_image * get_primary_image(heif_image_handle * handle)
{
  struct heif_error err;
  struct heif_image* img;
  err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_444, NULL);
  REQUIRE(err.code == heif_error_Ok);
  return img;
}

void fill_new_plane(heif_image* img, heif_channel channel, int w, int h)
{
  struct heif_error err;

  err = heif_image_add_plane(img, channel, w, h, 8);
  REQUIRE(err.code == heif_error_Ok);

  int stride;
  uint8_t* p = heif_image_get_plane(img, channel, &stride);

  for (int y = 0; y < h; y++) {
    memset(p + y * stride, 128, w);
  }
}