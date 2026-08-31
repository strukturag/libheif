/*
  libheif decoding tests for images with a clean aperture (clap) crop

  MIT License

  Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>

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
#include "test_utils.h"

// Regression test for issue #1856. The per-decode tightened security limit
// (max_image_size_pixels clamped to just above the declared size, introduced
// for issue #1798) was computed from the post-transformation dimensions
// instead of the coded 'ispe' size. The codec has to decode the full coded
// frame before the crop is applied, so any image whose 'clap' removes more
// area than the coding-unit margin adds was rejected with "security limit
// exceeded" under the default limits.
//
// The test files carry a 256x256 coded frame with a centered 64x64 'clap';
// one variant adds 'irot'/'imir' like the MIAF007 conformance file.

TEST_CASE("decode cropped image within default security limits")
{
  const char* files[] = {
      "clap_cropped.avif",
      "clap_cropped.heic",
      "clap_cropped_irot_imir.avif",
  };

  for (const char* file : files) {
    INFO(file);

    heif_context* ctx = get_context_for_test_file(file);
    heif_image_handle* handle = get_primary_image_handle(ctx);

    // The handle reports the size after all transformations.
    REQUIRE(heif_image_handle_get_width(handle) == 64);
    REQUIRE(heif_image_handle_get_height(handle) == 64);

    heif_image* img = nullptr;
    heif_error err = heif_decode_image(handle, &img, heif_colorspace_undefined,
                                       heif_chroma_undefined, nullptr);
    INFO((err.message ? err.message : ""));
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(heif_image_get_primary_width(img) == 64);
    REQUIRE(heif_image_get_primary_height(img) == 64);
    heif_image_release(img);

    // Decoding without transformations must also fit within the limits and
    // yield the full coded frame.
    heif_decoding_options* options = heif_decoding_options_alloc();
    options->ignore_transformations = 1;
    img = nullptr;
    err = heif_decode_image(handle, &img, heif_colorspace_undefined,
                            heif_chroma_undefined, options);
    heif_decoding_options_free(options);
    INFO((err.message ? err.message : ""));
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(heif_image_get_primary_width(img) == 256);
    REQUIRE(heif_image_get_primary_height(img) == 256);
    heif_image_release(img);

    heif_image_handle_release(handle);
    heif_context_free(ctx);
  }
}


// Second regression from issue #1856: a conformant HEVC stream may code a frame
// far larger than the 'ispe' size and crop it away with the SPS conformance
// window. The test file (reported through ImageMagick) declares a 2x2 image but
// codes a 160x64 frame, exceeding the ispe + one-CTU budget. The tightened
// limit must allow at least MIN_TIGHTENED_CODED_IMAGE_PIXELS for the coded
// frame. The file also carries a 1x1 'clap'.

TEST_CASE("decode image with coded frame much larger than ispe")
{
  heif_context* ctx = get_context_for_test_file("conformance_window_padding.heic");
  heif_image_handle* handle = get_primary_image_handle(ctx);

  REQUIRE(heif_image_handle_get_width(handle) == 1);
  REQUIRE(heif_image_handle_get_height(handle) == 1);

  heif_image* img = nullptr;
  heif_error err = heif_decode_image(handle, &img, heif_colorspace_undefined,
                                     heif_chroma_undefined, nullptr);
  INFO((err.message ? err.message : ""));
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(heif_image_get_primary_width(img) == 1);
  REQUIRE(heif_image_get_primary_height(img) == 1);
  heif_image_release(img);

  heif_image_handle_release(handle);
  heif_context_free(ctx);
}
