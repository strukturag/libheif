/*
  libheif unit tests

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

// Regression test: decoding an image that has an alpha channel into a YCbCr target
// while compositing the alpha onto a background (what heif-dec does when it writes
// JPEG) crashed with a null-pointer write in Op_flatten_alpha_plane. The operator
// allocated its composited R/G/B planes with the depth recorded for those channels
// in the target ColorState, which is 0 for a YCbCr target. Decoding to an RGB
// target was unaffected because the planner converts to RGB before it flattens.
//
// The companion test in conversion.cc drives the operator directly on synthesized
// images. This one goes through the public decoding API on the shipped alpha samples
// (HEVC and AV1), so it also covers the planner route a real decode takes.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

namespace {

struct Sample
{
  const char* filename;
  heif_compression_format codec;
};

const Sample samples[] = {
    {"simple_osm_tile_alpha.avif", heif_compression_AV1},
    {"with-alpha-512x512.heic", heif_compression_HEVC},
};

void decode_composited(const char* filename,
                       heif_colorspace colorspace, heif_chroma chroma,
                       heif_alpha_composition_mode mode)
{
  INFO(filename << " -> colorspace " << colorspace << ", chroma " << chroma << ", mode " << mode);

  heif_context* ctx = get_context_for_test_file(filename);
  heif_image_handle* handle = get_primary_image_handle(ctx);
  REQUIRE(heif_image_handle_has_alpha_channel(handle));

  heif_decoding_options* options = heif_decoding_options_alloc();
  REQUIRE(options != nullptr);

  heif_color_conversion_options_ext* ext = heif_color_conversion_options_ext_alloc();
  REQUIRE(ext != nullptr);
  ext->alpha_composition_mode = mode;
  ext->background_red = 0xFFFF;
  ext->background_green = 0xFFFF;
  ext->background_blue = 0xFFFF;
  ext->secondary_background_red = 0x8000;
  ext->secondary_background_green = 0x8000;
  ext->secondary_background_blue = 0x8000;
  ext->checkerboard_square_size = 16;
  options->color_conversion_options_ext = ext;

  heif_image* img = nullptr;
  heif_error err = heif_decode_image(handle, &img, colorspace, chroma, options);
  INFO("decode error (" << err.code << "/" << err.subcode << "): " << err.message);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(img != nullptr);

  CHECK(heif_image_get_colorspace(img) == colorspace);
  CHECK(heif_image_get_chroma_format(img) == chroma);

  // The alpha has been composited onto the colour planes, so none is left.
  CHECK(!heif_image_has_channel(img, heif_channel_Alpha));

  heif_channel size_channel = (colorspace == heif_colorspace_YCbCr) ? heif_channel_Y : heif_channel_interleaved;
  CHECK(heif_image_get_width(img, size_channel) == heif_image_handle_get_width(handle));
  CHECK(heif_image_get_height(img, size_channel) == heif_image_handle_get_height(handle));

  heif_image_release(img);
  heif_color_conversion_options_ext_free(ext);
  heif_decoding_options_free(options);
  heif_image_handle_release(handle);
  heif_context_free(ctx);
}

} // namespace

TEST_CASE("decode with alpha compositing into a YCbCr target")
{
  int samples_tested = 0;

  for (const Sample& sample : samples) {
    if (!heif_have_decoder_for_format(sample.codec)) {
      continue;
    }
    samples_tested++;

    // heif-dec's default when the output format cannot store alpha.
    decode_composited(sample.filename, heif_colorspace_YCbCr, heif_chroma_420, heif_alpha_composition_mode_checkerboard);
    decode_composited(sample.filename, heif_colorspace_YCbCr, heif_chroma_420, heif_alpha_composition_mode_solid_color);
    decode_composited(sample.filename, heif_colorspace_YCbCr, heif_chroma_444, heif_alpha_composition_mode_solid_color);

    // An RGB target takes the other planner route (convert first, flatten on RGB).
    decode_composited(sample.filename, heif_colorspace_RGB, heif_chroma_interleaved_RGB, heif_alpha_composition_mode_checkerboard);
  }

  if (samples_tested == 0) {
    SKIP("neither an AV1 nor a HEVC decoder is available");
  }
}
