/*
  libheif unit tests for component-description ID consistency between
  heif_image_handle (pre-decode) and heif_image (post-decode).

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
#include "libheif/heif_uncompressed.h"
#include "test_utils.h"
#include <vector>


// Verifies the contract that handle-side and decoded-image-side per-component
// queries return the same component IDs and the same per-component metadata.
// The image is decoded with native colorspace/chroma so no conversion takes
// place that would replace the planes.
static void check_handle_matches_decoded_image(heif_image_handle* handle,
                                               heif_compression_format codec)
{
  uint32_t handle_n = heif_image_handle_get_number_of_components(handle);
  REQUIRE(handle_n > 0);

  std::vector<uint32_t> handle_ids(handle_n);
  heif_image_handle_get_used_component_ids(handle, handle_ids.data());

  // Decode with native colorspace to avoid the conversion path.
  heif_image* image = nullptr;
  heif_decoding_options* options = heif_decoding_options_alloc();
  options->ignore_transformations = 1; // avoid mirror/rotate fixups changing dims
  heif_error err = heif_decode_image(handle, &image, heif_colorspace_undefined,
                                     heif_chroma_undefined, options);
  heif_decoding_options_free(options);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(image != nullptr);

  uint32_t img_n = heif_image_get_number_of_used_components(image);

  // Handle and decoded image now agree exactly on component count, including
  // alpha (which set_alpha_channel() appends to the main item's description
  // list at file-load time).
  REQUIRE(img_n == handle_n);

  std::vector<uint32_t> img_ids(img_n);
  heif_image_get_used_component_ids(image, img_ids.data());

  // The first handle_n IDs in the decoded image must equal the handle's IDs
  // and refer to the same channel/type/bit-depth/datatype.
  for (uint32_t i = 0; i < handle_n; i++) {
    INFO("component index " << i);
    REQUIRE(img_ids[i] == handle_ids[i]);

    uint16_t handle_type = heif_image_handle_get_component_type(handle, handle_ids[i]);
    uint16_t img_type    = heif_image_get_component_type(image, img_ids[i]);
    REQUIRE(handle_type == img_type);

    int handle_bpp = heif_image_handle_get_component_bits_per_pixel(handle, handle_ids[i]);
    int img_bpp    = heif_image_get_component_bits_per_pixel(image, img_ids[i]);
    REQUIRE(handle_bpp == img_bpp);

    heif_component_datatype handle_dt = heif_image_handle_get_component_datatype(handle, handle_ids[i]);
    heif_component_datatype img_dt    = heif_image_get_component_datatype(image, img_ids[i]);
    REQUIRE(handle_dt == img_dt);
  }

  heif_image_release(image);
  (void)codec;
}


// Build an RGGB Bayer filter-array image, encode as unci, write to a temp
// file, read it back, and verify that the populate-side cpat dedup matches
// the decoded-side IDs:
//   - cmpd has 4 entries (filter_array, R, G, B); only filter_array carries
//     pixel data, R/G/B are reference components.
//   - cpat is a 2x2 RGGB pattern; the two G positions reference the same
//     cmpd index, so populate_component_descriptions() emits ONE G
//     description, not two.
//   - heif_image_handle_get_number_of_components() returns 4.
//   - The decoded image's m_components has the same 4 ids in the same order.
TEST_CASE("unci with cpat: handle and decoded image agree on component IDs")
{
  if (!heif_have_decoder_for_format(heif_compression_uncompressed)) {
    SKIP("Skipping test because uncompressed codec is not compiled.");
  }
  if (!heif_have_encoder_for_format(heif_compression_uncompressed)) {
    SKIP("Skipping test because uncompressed encoder is not compiled.");
  }

  constexpr uint32_t W = 8;
  constexpr uint32_t H = 8;

  // --- 1. Build the image programmatically.
  heif_image* image = nullptr;
  heif_error err = heif_image_create(W, H, heif_colorspace_filter_array,
                                     heif_chroma_planar, &image);
  REQUIRE(err.code == heif_error_Ok);

  uint32_t fa_id = 0;
  err = heif_image_add_component(image, W, H,
                                 heif_cmpd_component_type_filter_array,
                                 heif_component_datatype_unsigned_integer, 8,
                                 &fa_id);
  REQUIRE(err.code == heif_error_Ok);

  // Fill the filter-array plane with arbitrary data.
  size_t fa_stride = 0;
  uint8_t* fa = heif_image_get_component(image, fa_id, &fa_stride);
  REQUIRE(fa != nullptr);
  for (uint32_t y = 0; y < H; y++) {
    for (uint32_t x = 0; x < W; x++) {
      fa[y * fa_stride + x] = static_cast<uint8_t>((x * 31 + y * 17) & 0xFF);
    }
  }

  // Reference components for R, G, B (one each — G appears twice in the
  // pattern but they share a single cmpd entry).
  uint32_t r_id = 0, g_id = 0, b_id = 0;
  err = heif_image_add_bayer_component(image, heif_cmpd_component_type_red,   &r_id); REQUIRE(err.code == heif_error_Ok);
  err = heif_image_add_bayer_component(image, heif_cmpd_component_type_green, &g_id); REQUIRE(err.code == heif_error_Ok);
  err = heif_image_add_bayer_component(image, heif_cmpd_component_type_blue,  &b_id); REQUIRE(err.code == heif_error_Ok);

  // 2x2 RGGB pattern; pixels[1] and pixels[2] both point at g_id.
  heif_bayer_pattern_pixel pattern[4] = {
      { r_id, 1.0f },
      { g_id, 1.0f },
      { g_id, 1.0f },
      { b_id, 1.0f },
  };
  err = heif_image_set_bayer_pattern(image, fa_id, /*pattern_w=*/2, /*pattern_h=*/2, pattern);
  REQUIRE(err.code == heif_error_Ok);

  // --- 2. Encode + write.
  heif_context* ctx = heif_context_alloc();
  heif_encoder* encoder = nullptr;
  err = heif_context_get_encoder_for_format(ctx, heif_compression_uncompressed, &encoder);
  REQUIRE(err.code == heif_error_Ok);

  heif_encoding_options* enc_options = heif_encoding_options_alloc();
  err = heif_context_encode_image(ctx, image, encoder, enc_options, nullptr);
  REQUIRE(err.code == heif_error_Ok);
  heif_encoding_options_free(enc_options);

  std::string out_path = get_tests_output_file_path("cpat_rggb.heif");
  err = heif_context_write_to_file(ctx, out_path.c_str());
  REQUIRE(err.code == heif_error_Ok);

  heif_encoder_release(encoder);
  heif_image_release(image);
  heif_context_free(ctx);

  // --- 3. Read back and verify the handle's component descriptions.
  heif_context* ctx2 = heif_context_alloc();
  err = heif_context_read_from_file(ctx2, out_path.c_str(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx2, &handle);
  REQUIRE(err.code == heif_error_Ok);

  // 4 components: filter_array, R, G, B (G shared between the two pattern
  // positions — populate dedups by cmpd index).
  REQUIRE(heif_image_handle_get_number_of_components(handle) == 4);

  uint32_t handle_ids[4];
  heif_image_handle_get_used_component_ids(handle, handle_ids);

  REQUIRE(heif_image_handle_get_component_type(handle, handle_ids[0]) == heif_cmpd_component_type_filter_array);
  REQUIRE(heif_image_handle_get_component_type(handle, handle_ids[1]) == heif_cmpd_component_type_red);
  REQUIRE(heif_image_handle_get_component_type(handle, handle_ids[2]) == heif_cmpd_component_type_green);
  REQUIRE(heif_image_handle_get_component_type(handle, handle_ids[3]) == heif_cmpd_component_type_blue);

  REQUIRE(heif_image_handle_get_component_bits_per_pixel(handle, handle_ids[0]) == 8);

  // --- 4. Decode and confirm that ids agree end-to-end.
  heif_image* decoded = nullptr;
  err = heif_decode_image(handle, &decoded, heif_colorspace_undefined,
                          heif_chroma_undefined, nullptr);
  REQUIRE(err.code == heif_error_Ok);

  REQUIRE(heif_image_get_number_of_used_components(decoded) == 4);
  uint32_t img_ids[4];
  heif_image_get_used_component_ids(decoded, img_ids);
  for (uint32_t i = 0; i < 4; i++) {
    INFO("component index " << i);
    REQUIRE(img_ids[i] == handle_ids[i]);
    REQUIRE(heif_image_handle_get_component_type(handle, handle_ids[i]) ==
            heif_image_get_component_type(decoded, img_ids[i]));
  }

  heif_image_release(decoded);
  heif_image_handle_release(handle);
  heif_context_free(ctx2);
}


// The following three test cases load files in the low-overhead MINI box format
// (lightning_mini.heif, simple_osm_tile_meta.avif, simple_osm_tile_alpha.avif).

TEST_CASE("HEVC YUV 4:2:0 component IDs match between handle and decoded image")
{
  if (!heif_have_decoder_for_format(heif_compression_HEVC)) {
    SKIP("Skipping test because HEVC decoder is not available.");
  }

  // lightning_mini.heif: 256x256, YCbCr 4:2:0, 8-bit, no alpha.
  auto* context = get_context_for_test_file("lightning_mini.heif");
  auto* handle = get_primary_image_handle(context);

  // The handle should report exactly 3 components for a YUV 4:2:0 image.
  REQUIRE(heif_image_handle_get_number_of_components(handle) == 3);

  uint32_t ids[3];
  heif_image_handle_get_used_component_ids(handle, ids);

  // Canonical ordering.
  REQUIRE(heif_image_handle_get_component_type(handle, ids[0]) == heif_cmpd_component_type_Y);
  REQUIRE(heif_image_handle_get_component_type(handle, ids[1]) == heif_cmpd_component_type_Cb);
  REQUIRE(heif_image_handle_get_component_type(handle, ids[2]) == heif_cmpd_component_type_Cr);

  // 8 bits per channel.
  for (uint32_t id : ids) {
    REQUIRE(heif_image_handle_get_component_bits_per_pixel(handle, id) == 8);
    REQUIRE(heif_image_handle_get_component_datatype(handle, id) == heif_component_datatype_unsigned_integer);
  }

  // IDs and metadata must match what the decoded image reports.
  check_handle_matches_decoded_image(handle, heif_compression_HEVC);

  heif_image_handle_release(handle);
  heif_context_free(context);
}


TEST_CASE("AVIF YUV 4:4:4 component IDs match between handle and decoded image")
{
  if (!heif_have_decoder_for_format(heif_compression_AV1)) {
    SKIP("Skipping test because AV1 decoder is not available.");
  }

  // simple_osm_tile_meta.avif: 256x256, YCbCr 4:4:4, 8-bit, no alpha.
  auto* context = get_context_for_test_file("simple_osm_tile_meta.avif");
  auto* handle = get_primary_image_handle(context);

  REQUIRE(heif_image_handle_get_number_of_components(handle) == 3);

  uint32_t ids[3];
  heif_image_handle_get_used_component_ids(handle, ids);

  REQUIRE(heif_image_handle_get_component_type(handle, ids[0]) == heif_cmpd_component_type_Y);
  REQUIRE(heif_image_handle_get_component_type(handle, ids[1]) == heif_cmpd_component_type_Cb);
  REQUIRE(heif_image_handle_get_component_type(handle, ids[2]) == heif_cmpd_component_type_Cr);

  for (uint32_t id : ids) {
    REQUIRE(heif_image_handle_get_component_bits_per_pixel(handle, id) == 8);
    REQUIRE(heif_image_handle_get_component_datatype(handle, id) == heif_component_datatype_unsigned_integer);
  }

  check_handle_matches_decoded_image(handle, heif_compression_AV1);

  heif_image_handle_release(handle);
  heif_context_free(context);
}


TEST_CASE("HEIC YUV 4:2:0 (rainbow-451x461) component IDs match")
{
  if (!heif_have_decoder_for_format(heif_compression_HEVC)) {
    SKIP("Skipping test because HEVC decoder is not available.");
  }

  // rainbow-451x461.heic: 451x461 displayed dims, ispe 452x462 (HEVC
  // macroblock-aligned, with a clap producing the displayed size).
  // Test the handle-side queries and confirm IDs match post-decode.
  auto* context = get_context_for_test_file("rainbow-451x461.heic");
  auto* handle = get_primary_image_handle(context);

  // Displayed dims (post-clap).
  REQUIRE(heif_image_handle_get_width(handle) == 451);
  REQUIRE(heif_image_handle_get_height(handle) == 461);
  // Coded ispe dims.
  REQUIRE(heif_image_handle_get_ispe_width(handle) == 452);
  REQUIRE(heif_image_handle_get_ispe_height(handle) == 462);

  REQUIRE(heif_image_handle_get_number_of_components(handle) == 3);
  uint32_t ids[3];
  heif_image_handle_get_used_component_ids(handle, ids);

  REQUIRE(heif_image_handle_get_component_type(handle, ids[0]) == heif_cmpd_component_type_Y);
  REQUIRE(heif_image_handle_get_component_type(handle, ids[1]) == heif_cmpd_component_type_Cb);
  REQUIRE(heif_image_handle_get_component_type(handle, ids[2]) == heif_cmpd_component_type_Cr);

  for (uint32_t id : ids) {
    REQUIRE(heif_image_handle_get_component_bits_per_pixel(handle, id) == 8);
    REQUIRE(heif_image_handle_get_component_datatype(handle, id) == heif_component_datatype_unsigned_integer);
  }

  // Decoded image's component IDs and per-id metadata must match.
  check_handle_matches_decoded_image(handle, heif_compression_HEVC);

  heif_image_handle_release(handle);
  heif_context_free(context);
}


// Common shape for "image has alpha" tests: handle reports four components
// (Y/Cb/Cr/Alpha), and the decoded image returns the same four IDs in the
// same order. The Alpha description on the handle is added by
// ImageItem::set_alpha_channel(), populated when the iref/auxl reference
// is resolved at file-load time.
static void check_alpha_handle_matches_decoded_image(const char* fixture,
                                                     heif_compression_format codec)
{
  auto* context = get_context_for_test_file(fixture);
  auto* handle = get_primary_image_handle(context);
  REQUIRE(heif_image_handle_has_alpha_channel(handle) == 1);

  REQUIRE(heif_image_handle_get_number_of_components(handle) == 4);
  uint32_t handle_ids[4];
  heif_image_handle_get_used_component_ids(handle, handle_ids);

  REQUIRE(heif_image_handle_get_component_type(handle, handle_ids[0]) == heif_cmpd_component_type_Y);
  REQUIRE(heif_image_handle_get_component_type(handle, handle_ids[1]) == heif_cmpd_component_type_Cb);
  REQUIRE(heif_image_handle_get_component_type(handle, handle_ids[2]) == heif_cmpd_component_type_Cr);
  REQUIRE(heif_image_handle_get_component_type(handle, handle_ids[3]) == heif_cmpd_component_type_alpha);

  // The whole point of this round of fixes: the post-decode image carries
  // the same four IDs the handle reports, including the alpha id.
  check_handle_matches_decoded_image(handle, codec);

  heif_image_handle_release(handle);
  heif_context_free(context);
}


TEST_CASE("HEIC YUV 4:2:0 with alpha: handle exposes alpha as the 4th component")
{
  if (!heif_have_decoder_for_format(heif_compression_HEVC)) {
    SKIP("Skipping test because HEVC decoder is not available.");
  }
  // with-alpha-512x512.heic: HEIC with alpha-from-aux item.
  check_alpha_handle_matches_decoded_image("with-alpha-512x512.heic",
                                           heif_compression_HEVC);
}


TEST_CASE("AVIF with alpha: handle exposes alpha as the 4th component")
{
  if (!heif_have_decoder_for_format(heif_compression_AV1)) {
    SKIP("Skipping test because AV1 decoder is not available.");
  }
  // simple_osm_tile_alpha.avif: AVIF with alpha-from-aux item.
  check_alpha_handle_matches_decoded_image("simple_osm_tile_alpha.avif",
                                           heif_compression_AV1);
}

