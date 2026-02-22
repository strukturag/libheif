/*
  libheif OMAF (ISO/IEC 23090-2) unit tests

  MIT License

  Copyright (c) 2026 Brad Hards <bradh@frogmouth.net>

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
#include "libheif/heif.h"
#include "api_structs.h"
#include "pixelimage.h"

#include "test_utils.h"

#include <string.h>

static heif_encoding_options * set_encoding_options()
{
  heif_encoding_options * options = heif_encoding_options_alloc();
  options->macOS_compatibility_workaround = false;
  options->macOS_compatibility_workaround_no_nclx_profile = true;
  options->image_orientation = heif_orientation_normal;
  return options;
}

static void do_encode(heif_image* input_image, const char* filename, heif_image_projection projection)
{
  REQUIRE(input_image != nullptr);
  heif_init(nullptr);
  heif_context *ctx = heif_context_alloc();
  heif_encoder *encoder;
  heif_error err;
  err = heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);
  REQUIRE(err.code == heif_error_Ok);

  heif_encoding_options *options = set_encoding_options();

  heif_image_handle *output_image_handle;

  err = heif_context_encode_image(ctx, input_image, encoder, options, &output_image_handle);
  REQUIRE(err.code == heif_error_Ok);
  heif_image_handle_set_image_projection(output_image_handle, projection);
  err = heif_context_write_to_file(ctx, filename);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle_release(output_image_handle);
  heif_encoding_options_free(options);
  heif_encoder_release(encoder);
  heif_image_release(input_image);

  heif_context_free(ctx);

  heif_context *readbackCtx = get_context_for_local_file(filename);
  heif_image_handle *readbackHandle = get_primary_image_handle(readbackCtx);
  heif_image_projection readbackProjection = heif_image_handle_get_image_projection(readbackHandle);
  REQUIRE(readbackProjection == projection);
  heif_image_handle_release(readbackHandle);
  heif_context_free(readbackCtx);
  
  heif_deinit();
}

TEST_CASE("Encode OMAF HEIC")
{
  heif_image *input_image = createImage_RGB_planar();
  do_encode(input_image, "encode_omaf_equirectangular.heic", heif_image_projection::equirectangular);
}

TEST_CASE("Encode OMAF HEIC Cubemap")
{
  heif_image *input_image = createImage_RGB_planar();
  do_encode(input_image, "encode_omaf_cubemap.heic", heif_image_projection::cube_map);
}