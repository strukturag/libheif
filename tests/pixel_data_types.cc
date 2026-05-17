/*
  libheif unit tests

  MIT License

  Copyright (c) 2024 Dirk Farin <dirk.farin@gmail.com>

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

#include "image/pixelimage.h"
#include "catch_amalgamated.hpp"

TEST_CASE( "uint32_t" )
{
  HeifPixelImage image;

  auto* limits = heif_get_global_security_limits();
  image.create(3,2, heif_colorspace_custom, heif_chroma_planar);
  image.add_channel(heif_channel_Y, 3,2, 32, limits, heif_component_datatype_unsigned_integer);

  size_t stride;
  uint32_t* data = image.get_channel_memory<uint32_t>(heif_channel_Y, &stride);
  stride /= sizeof(uint32_t);

  REQUIRE(stride >= 3);
  REQUIRE(image.get_width(heif_channel_Y) == 3);
  REQUIRE(image.get_height(heif_channel_Y) == 2);
  REQUIRE(image.get_bits_per_pixel(heif_channel_Y) == 32);
  REQUIRE(image.get_storage_bits_per_pixel(heif_channel_Y) == 32);
  REQUIRE(image.get_datatype(heif_channel_Y) == heif_component_datatype_unsigned_integer);
  REQUIRE(image.get_number_of_interleaved_components(heif_channel_Y) == 1);

  data[0*stride + 0] = 0;
  data[0*stride + 1] = 0xFFFFFFFFu;
  data[0*stride + 2] = 1000;
  data[1*stride + 0] = 0xFFFFFFFFu;
  data[1*stride + 1] = 0;
  data[1*stride + 2] = 2000;

  REQUIRE(data[0*stride + 1] == 0xFFFFFFFFu);

  // --- rotate data

  std::shared_ptr<HeifPixelImage> rot;
  auto rotationResult = image.rotate_ccw(90, limits);
  REQUIRE(rotationResult.error().error_code == heif_error_Ok);
  rot = *rotationResult;

  data = rot->get_channel_memory<uint32_t>(heif_channel_Y, &stride);
  stride /= sizeof(uint32_t);

  REQUIRE(data[0*stride + 0] == 1000);
  REQUIRE(data[0*stride + 1] == 2000);
  REQUIRE(data[1*stride + 0] == 0xFFFFFFFFu);
  REQUIRE(data[1*stride + 1] == 0);
  REQUIRE(data[2*stride + 0] == 0);
  REQUIRE(data[2*stride + 1] == 0xFFFFFFFFu);

  // --- mirror

  rot->mirror_inplace(heif_transform_mirror_direction_horizontal, limits);

  REQUIRE(data[0*stride + 1] == 1000);
  REQUIRE(data[0*stride + 0] == 2000);
  REQUIRE(data[1*stride + 1] == 0xFFFFFFFFu);
  REQUIRE(data[1*stride + 0] == 0);
  REQUIRE(data[2*stride + 1] == 0);
  REQUIRE(data[2*stride + 0] == 0xFFFFFFFFu);

  rot->mirror_inplace(heif_transform_mirror_direction_vertical, limits);

  REQUIRE(data[2*stride + 1] == 1000);
  REQUIRE(data[2*stride + 0] == 2000);
  REQUIRE(data[1*stride + 1] == 0xFFFFFFFFu);
  REQUIRE(data[1*stride + 0] == 0);
  REQUIRE(data[0*stride + 1] == 0);
  REQUIRE(data[0*stride + 0] == 0xFFFFFFFFu);

  // --- crop

  std::shared_ptr<HeifPixelImage> crop;
  auto cropResult = image.crop(1,2,1,1, limits);
  REQUIRE(cropResult.error().error_code == heif_error_Ok);
  crop = *cropResult;

  REQUIRE(crop->get_width(heif_channel_Y) == 2);
  REQUIRE(crop->get_height(heif_channel_Y) == 1);

  data = crop->get_channel_memory<uint32_t>(heif_channel_Y, &stride);
  stride /= sizeof(uint32_t);

  REQUIRE(data[0*stride + 0] == 0);
  REQUIRE(data[0*stride + 1] == 2000);

  cropResult = image.crop(0,1,0,1, limits);
  REQUIRE(cropResult.error().error_code == heif_error_Ok);
  crop = *cropResult;

  REQUIRE(crop->get_width(heif_channel_Y) == 2);
  REQUIRE(crop->get_height(heif_channel_Y) == 2);

  data = crop->get_channel_memory<uint32_t>(heif_channel_Y, &stride);
  stride /= sizeof(uint32_t);

  REQUIRE(data[0*stride + 0] == 0);
  REQUIRE(data[0*stride + 1] == 0xFFFFFFFFu);
  REQUIRE(data[1*stride + 0] == 0xFFFFFFFFu);
  REQUIRE(data[1*stride + 1] == 0);
}


TEST_CASE( "complex64_t" )
{
  HeifPixelImage image;

  auto* limits = heif_get_global_security_limits();
  image.create(3,2, heif_colorspace_custom, heif_chroma_planar);
  image.add_channel(heif_channel_Y, 3,2, 128, limits, heif_component_datatype_complex_number);

  size_t stride;
  heif_complex64* data = image.get_channel_memory<heif_complex64>(heif_channel_Y, &stride);
  stride /= sizeof(heif_complex64);

  REQUIRE(stride >= 3);
  REQUIRE(image.get_width(heif_channel_Y) == 3);
  REQUIRE(image.get_height(heif_channel_Y) == 2);
  REQUIRE(image.get_bits_per_pixel(heif_channel_Y) == 128);
  REQUIRE(image.get_storage_bits_per_pixel(heif_channel_Y) == 128);
  REQUIRE(image.get_datatype(heif_channel_Y) == heif_component_datatype_complex_number);
  REQUIRE(image.get_number_of_interleaved_components(heif_channel_Y) == 1);

  data[0*stride + 0] = {0.0, -1.0};
  data[0*stride + 1] = {1.0, 2.0};
  data[0*stride + 2] = {2.0, -1.0};
  data[1*stride + 0] = {0.25, 0.5};
  data[1*stride + 1] = {0.0, 0.0};
  data[1*stride + 2] = {-0.75, 0.125};

  REQUIRE(data[0*stride + 1].real == 1.0);
  REQUIRE(data[0*stride + 1].imaginary == 2.0);
}


TEST_CASE( "image datatype through public API" )
{
  heif_image* image;
  heif_error error = heif_image_create(3,2,heif_colorspace_custom, heif_chroma_planar, &image);
  REQUIRE(!error.code);

  uint32_t comp_idx;
  error = heif_image_add_component(image, 3, 2, 0, heif_component_datatype_unsigned_integer, 32, &comp_idx);
  REQUIRE(!error.code);

  size_t stride;
  uint32_t* data = heif_image_get_component_uint32(image, comp_idx, &stride);
  REQUIRE(data != nullptr);
  stride /= sizeof(uint32_t);

  REQUIRE(stride >= 3);
  REQUIRE(heif_image_get_component_bits_per_pixel(image, comp_idx) == 32);

  data[stride*0 + 0] = 0xFFFFFFFFu;
  data[stride*0 + 1] = 0;
  data[stride*0 + 2] = 1000;
  data[stride*1 + 0] = 0xFFFFFFFFu;
  data[stride*1 + 1] = 200;
  data[stride*1 + 2] = 5;
}


// Verify that an interleaved RGB plane produces THREE ComponentDescription
// entries (one per cmpd component: red, green, blue) all sharing
// channel=heif_channel_interleaved, while m_storage has a single buffer.
TEST_CASE( "interleaved RGB component descriptions" )
{
  HeifPixelImage image;
  auto* limits = heif_get_global_security_limits();
  image.create(100, 100, heif_colorspace_RGB, heif_chroma_interleaved_RGB);
  REQUIRE(image.add_channel(heif_channel_interleaved, 100, 100, 8, limits).error_code == heif_error_Ok);

  REQUIRE(image.get_number_of_used_components() == 3);

  auto ids = image.get_used_component_ids();
  REQUIRE(ids.size() == 3);

  // ids should be three distinct, monotonically-increasing values starting
  // from m_next_component_id's initial value (1).
  REQUIRE(ids[0] == 1);
  REQUIRE(ids[1] == 2);
  REQUIRE(ids[2] == 3);

  REQUIRE(image.get_component_type(ids[0]) == heif_cmpd_component_type_red);
  REQUIRE(image.get_component_type(ids[1]) == heif_cmpd_component_type_green);
  REQUIRE(image.get_component_type(ids[2]) == heif_cmpd_component_type_blue);

  // All three descriptions share channel=interleaved, identical dims/bpp.
  for (uint32_t id : ids) {
    REQUIRE(image.get_component_channel(id) == heif_channel_interleaved);
    REQUIRE(image.get_component_width(id) == 100);
    REQUIRE(image.get_component_height(id) == 100);
    REQUIRE(image.get_component_bits_per_pixel(id) == 8);
    REQUIRE(image.get_component_datatype(id) == heif_component_datatype_unsigned_integer);
  }

  // Public C API surface: the same three ids, addressing the same single
  // interleaved buffer (one plane).
  REQUIRE(image.has_channel(heif_channel_interleaved));
  REQUIRE(image.get_number_of_interleaved_components(heif_channel_interleaved) == 3);
}


// Same but for RGBA: four ComponentDescription entries.
TEST_CASE( "interleaved RGBA component descriptions" )
{
  HeifPixelImage image;
  auto* limits = heif_get_global_security_limits();
  image.create(50, 50, heif_colorspace_RGB, heif_chroma_interleaved_RGBA);
  REQUIRE(image.add_channel(heif_channel_interleaved, 50, 50, 8, limits).error_code == heif_error_Ok);

  REQUIRE(image.get_number_of_used_components() == 4);
  auto ids = image.get_used_component_ids();
  REQUIRE(ids.size() == 4);
  REQUIRE(image.get_component_type(ids[0]) == heif_cmpd_component_type_red);
  REQUIRE(image.get_component_type(ids[1]) == heif_cmpd_component_type_green);
  REQUIRE(image.get_component_type(ids[2]) == heif_cmpd_component_type_blue);
  REQUIRE(image.get_component_type(ids[3]) == heif_cmpd_component_type_alpha);

  for (uint32_t id : ids) {
    REQUIRE(image.get_component_channel(id) == heif_channel_interleaved);
    REQUIRE(image.get_component_bits_per_pixel(id) == 8);
  }
}


// Verify that BayerPattern.pixels reuses the same component_id for two
// pattern positions that reference the same cmpd entry.  This exercises
// HeifPixelImage's data model directly (not the unci file -> ImageItem
// populate path, which would need a full HEIF round-trip and a fixture).
TEST_CASE( "Bayer pattern shares component id for same cmpd" )
{
  HeifPixelImage image;
  auto* limits = heif_get_global_security_limits();
  image.create(4, 4, heif_colorspace_filter_array, heif_chroma_planar);

  // 1) The data plane: a 14-bit filter-array component.
  auto fa_result = image.add_component(4, 4, heif_cmpd_component_type_filter_array,
                                       heif_component_datatype_unsigned_integer, 14, limits);
  REQUIRE(fa_result.error().error_code == heif_error_Ok);

  // 2) Reference (no-data-plane) components for R, G, B.
  uint32_t r_id = image.add_component_without_data(heif_cmpd_component_type_red);
  uint32_t g_id = image.add_component_without_data(heif_cmpd_component_type_green);
  uint32_t b_id = image.add_component_without_data(heif_cmpd_component_type_blue);

  REQUIRE(image.get_number_of_used_components() == 4);

  // 3) Build a 2x2 RGGB pattern: R, G, G, B with the two G positions sharing g_id.
  BayerPattern pattern;
  pattern.pattern_width = 2;
  pattern.pattern_height = 2;
  pattern.pixels = {
    { r_id, 1.0f },
    { g_id, 1.0f },
    { g_id, 1.0f },   // <-- same id as the previous G position
    { b_id, 1.0f },
  };
  image.set_bayer_pattern(pattern);

  REQUIRE(image.has_any_bayer_pattern());
  const auto& stored = image.get_any_bayer_pattern();
  REQUIRE(stored.pixels.size() == 4);
  REQUIRE(stored.pixels[0].component_id == r_id);
  REQUIRE(stored.pixels[1].component_id == g_id);
  REQUIRE(stored.pixels[2].component_id == g_id);   // shared
  REQUIRE(stored.pixels[3].component_id == b_id);
  REQUIRE(stored.pixels[1].component_id == stored.pixels[2].component_id);

  // The reference components should have has_data_plane=false on their
  // descriptions and not appear as image planes.
  REQUIRE(image.has_channel(heif_channel_filter_array));
  REQUIRE_FALSE(image.has_channel(heif_channel_R));
  REQUIRE_FALSE(image.has_channel(heif_channel_G));
  REQUIRE_FALSE(image.has_channel(heif_channel_B));
}
