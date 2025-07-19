/*
  libheif integration tests for text items

  MIT License

  Copyright (c) 2025 Brad Hards <bradh@frogmouth.net>

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

#include "catch_amalgamated.hpp"
#include "api_structs.h"
#include "libheif/heif.h"
#include "test-config.h"
#include "test_utils.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <libheif/heif_items.h>


TEST_CASE("no text") {
  // skip test if we do not have the uncompressed codec
  if (!heif_have_decoder_for_format(heif_compression_uncompressed)) {
    SKIP("Skipping test because uncompressed codec is not compiled.");
  }

  auto context = get_context_for_test_file("uncompressed_comp_RGB.heif");
  heif_image_handle *handle = get_primary_image_handle(context);
  int num_text_items = heif_image_handle_get_number_of_text_items(handle);
  REQUIRE(num_text_items == 0);
  heif_image_handle_release(handle);
  heif_context_free(context);
}

TEST_CASE("create text item") {
  // skip test if we do not have the uncompressed codec
  if (!heif_have_decoder_for_format(heif_compression_uncompressed)) {
    SKIP("Skipping test because uncompressed codec is not compiled.");
  }
  struct heif_error err;

  heif_image* img;
  uint32_t input_width = 1280;
  uint32_t input_height = 1024;
  heif_image_create(input_width,input_height, heif_colorspace_YCbCr, heif_chroma_420, &img);
  fill_new_plane(img, heif_channel_Y, input_width, input_height);
  fill_new_plane(img, heif_channel_Cb, (input_width+1)/2, (input_height+1)/2);
  fill_new_plane(img, heif_channel_Cr, (input_width+1)/2, (input_height+1)/2);

  heif_context* ctx = heif_context_alloc();
  heif_encoder* enc;
  err = heif_context_get_encoder_for_format(ctx, heif_compression_uncompressed, &enc);
  REQUIRE(err.code == heif_error_Ok);

  struct heif_encoding_options* options;
  options = heif_encoding_options_alloc();
  options->macOS_compatibility_workaround = false;
  options->macOS_compatibility_workaround_no_nclx_profile = false;
  options->image_orientation = heif_orientation_normal;

  heif_image_handle* handle;
  err = heif_context_encode_image(ctx, img, enc, options, &handle);
  REQUIRE(err.code == heif_error_Ok);

  struct heif_text_item* text_item1;
  std::string text_body1("first string");
  err = heif_image_handle_add_text_item(handle, "text/plain", text_body1.c_str(), &text_item1);
  REQUIRE(err.code == heif_error_Ok);

  struct heif_text_item* text_item2;
  std::string text_body2("a second string");
  err = heif_image_handle_add_text_item(handle, "text/plain", text_body2.c_str(), &text_item2);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_write_to_file(ctx, "text.heif");
  REQUIRE(err.code == heif_error_Ok);

  heif_text_item_release(text_item1);
  heif_text_item_release(text_item2);

  heif_image_handle_release(handle);
  heif_encoder_release(enc);
  heif_encoding_options_free(options);
  heif_context_free(ctx);
  heif_image_release(img);

  heif_context *readbackCtx = get_context_for_local_file("text.heif");
  heif_image_handle *readbackHandle = get_primary_image_handle(readbackCtx);
  int num_text_items = heif_image_handle_get_number_of_text_items(readbackHandle);
  REQUIRE(num_text_items == 2);

  std::vector<heif_item_id> text_item_ids_minus(1);
  int num_returned = heif_image_handle_get_list_of_text_item_ids(readbackHandle, text_item_ids_minus.data(), 1);
  REQUIRE(num_returned == 1);

  std::vector<heif_item_id> text_item_ids_extra(3);
  num_returned = heif_image_handle_get_list_of_text_item_ids(readbackHandle, text_item_ids_extra.data(), 3);
  REQUIRE(num_returned == num_text_items);

  std::vector<heif_item_id> text_item_ids(num_text_items);
  num_returned = heif_image_handle_get_list_of_text_item_ids(readbackHandle, text_item_ids.data(), num_text_items);
  REQUIRE(num_returned == 2);

  heif_text_item* text0;
  err = heif_context_get_text_item(readbackCtx, text_item_ids[0], &text0);
  heif_item_id id0 = heif_text_item_get_id(text0);
  REQUIRE(id0 == text_item_ids[0]);

  REQUIRE(heif_item_get_item_type(readbackCtx, text_item_ids[0]) == fourcc("mime"));
  const char* content_type0 = heif_item_get_mime_item_content_type(readbackCtx, text_item_ids[0]);
  REQUIRE(std::string(content_type0) == "text/plain");
  const char* body0 = heif_text_item_get_content(text0);
  REQUIRE(std::string(body0) == text_body1);

  heif_text_item* text1;
  err = heif_context_get_text_item(readbackCtx, text_item_ids[1], &text1);
  heif_item_id id1 = heif_text_item_get_id(text1);
  REQUIRE(id1 == text_item_ids[1]);

  REQUIRE(heif_item_get_item_type(readbackCtx, text_item_ids[1]) == fourcc("mime"));
  const char* content_type1 = heif_item_get_mime_item_content_type(readbackCtx, text_item_ids[0]);
  REQUIRE(std::string(content_type1) == "text/plain");
  const char* body1 = heif_text_item_get_content(text1);
  REQUIRE(std::string(body1) == text_body2);

  heif_text_item_release(text0);
  heif_text_item_release(text1);
  heif_image_handle_release(readbackHandle);
  heif_context_free(readbackCtx);
}
