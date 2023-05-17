/*
  libheif unit tests

  MIT License

  Copyright (c) 2019 struktur AG, Dirk Farin <farin@struktur.de>

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

#include "catch.hpp"
#include "libheif/heif.h"
#include "libheif/pixelimage.h"
#include "libheif/api_structs.h"


struct heif_image* createImage_RRGGBB_BE() {
  struct heif_image* image;
  struct heif_error err;
  err = heif_image_create(256,256,
                          heif_colorspace_RGB,
                          heif_chroma_interleaved_RRGGBB_BE,
                          &image);
  if (err.code) {
    return nullptr;
  }

  err = heif_image_add_plane(image,
                             heif_channel_interleaved,
                             256,256, 10);
  if (err.code) {
    heif_image_release(image);
    return nullptr;
  }

  return image;
}


struct heif_error encode_image(struct heif_image* img) {
  struct heif_context* ctx = heif_context_alloc();

  struct heif_encoder* enc;
  struct heif_error err { heif_error_Ok };

  err = heif_context_get_encoder_for_format(ctx,
                                            heif_compression_HEVC,
                                            &enc);
  if (err.code) {
    heif_context_free(ctx);
    return err;
  }


  struct heif_image_handle* hdl;
  err = heif_context_encode_image(ctx,
                                  img,
                                  enc,
                                  nullptr,
                                  &hdl);
  if (err.code) {
    heif_encoder_release(enc);
    heif_context_free(ctx);
    return err;
  }

  return err;
}


#if 0
TEST_CASE( "Create images", "[heif_image]" ) {
  auto img = createImage_RRGGBB_BE();
  REQUIRE( img != nullptr );

  heif_image_release(img);
}



TEST_CASE( "Encode HDR", "[heif_encoder]" ) {
  auto img = createImage_RRGGBB_BE();
  REQUIRE( img != nullptr );

  REQUIRE( encode_image(img).code == heif_error_Ok );

  heif_image_release(img);
}
#endif


static void test_ispe_size(heif_compression_format compression,
                           heif_orientation orientation,
                           int input_width, int input_height,
                           int expected_ispe_width, int expected_ispe_height)
{
  struct heif_error err;

  heif_image* img;
  heif_image_create(input_width,input_height, heif_colorspace_YCbCr, heif_chroma_420, &img);
  img->image->fill_new_plane(heif_channel_Y, 128, input_width,input_height, 8);
  img->image->fill_new_plane(heif_channel_Cb, 128, (input_width+1)/2,(input_height+1)/2, 8);
  img->image->fill_new_plane(heif_channel_Cr, 128, (input_width+1)/2,(input_height+1)/2, 8);

  heif_context* ctx = heif_context_alloc();
  heif_encoder* enc;
  err = heif_context_get_encoder_for_format(ctx, compression, &enc);
  REQUIRE(err.code == heif_error_Ok);

  struct heif_encoding_options* options;
  options = heif_encoding_options_alloc();
  options->macOS_compatibility_workaround = false;
  options->macOS_compatibility_workaround_no_nclx_profile = false;
  options->image_orientation = orientation;

  heif_image_handle* handle;
  heif_context_encode_image(ctx, img, enc, options, &handle);

  int ispe_width = heif_image_handle_get_ispe_width(handle);
  int ispe_height = heif_image_handle_get_ispe_height(handle);

  REQUIRE(ispe_width == expected_ispe_width);
  REQUIRE(ispe_height == expected_ispe_height);

  heif_image_handle_release(handle);
  heif_encoder_release(enc);
  heif_encoding_options_free(options);
  heif_context_free(ctx);
  heif_image_release(img);
}


TEST_CASE( "ispe odd size", "[heif_context]" ) {

  // HEVC encoders typically encode with even dimensions only
  test_ispe_size(heif_compression_HEVC, heif_orientation_normal, 121,99, 122,100);
  test_ispe_size(heif_compression_HEVC, heif_orientation_rotate_180, 121,99, 122,100);
  test_ispe_size(heif_compression_HEVC, heif_orientation_rotate_90_cw, 121,99, 122,100);
  test_ispe_size(heif_compression_HEVC, heif_orientation_rotate_90_cw, 120,100, 120,100);

  // AVIF encoders typically encode with odd dimensions
  test_ispe_size(heif_compression_AV1, heif_orientation_normal, 121,99, 121,99);
  test_ispe_size(heif_compression_AV1, heif_orientation_rotate_180, 121,99, 121,99);
  test_ispe_size(heif_compression_AV1, heif_orientation_rotate_90_cw, 121,99, 121,99);
  test_ispe_size(heif_compression_AV1, heif_orientation_rotate_90_cw, 120,100, 120,100);
}
