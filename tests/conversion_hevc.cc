/*
  libheif HEVC conversion tests

  MIT License

  Copyright (c) 2026 OlJohnny

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

#include "api_structs.h"
#include "catch_amalgamated.hpp"
#include "color-conversion/hdr_sdr.h"
#include "color-conversion/rgb2rgb.h"
#include "color-conversion/yuv2rgb.h"
#include "image/pixelimage.h"
#include "test_utils.h"
#include <cstdint>
#include <cstring>
#include <memory>

TEST_CASE("Generated 10-bit HEVC fixtures decode like the planar conversion", "[heif_image][fused_rgba][hevc_fixture]")
{
  if (!heif_have_decoder_for_format(heif_compression_HEVC)) {
    SKIP("no HEVC decoder available");
  }
  const heif_chroma chroma = GENERATE(heif_chroma_422, heif_chroma_444);
  const char* filename = chroma == heif_chroma_422 ? "fused-rgba10-422.heic" : "fused-rgba10-444.heic";
  const uint32_t width = chroma == heif_chroma_422 ? 258 : 257;
  const uint32_t height = 129;
  INFO(filename);
  auto context = std::unique_ptr<heif_context, decltype(&heif_context_free)>(
      get_context_for_test_file(filename), heif_context_free);
  auto handle = std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)>(
      get_primary_image_handle(context.get()), heif_image_handle_release);
  REQUIRE(heif_image_handle_get_width(handle.get()) == static_cast<int>(width));
  REQUIRE(heif_image_handle_get_height(handle.get()) == static_cast<int>(height));
  REQUIRE(!heif_image_handle_has_alpha_channel(handle.get()));
  auto options = std::unique_ptr<heif_decoding_options, decltype(&heif_decoding_options_free)>(
      heif_decoding_options_alloc(), heif_decoding_options_free);
  options->output_image_nclx_profile_passthrough = true;
  heif_image* raw_native = nullptr;
  heif_error error = heif_decode_image(handle.get(), &raw_native, heif_colorspace_undefined,
                                      heif_chroma_undefined, options.get());
  INFO(error.message);
  REQUIRE(error.code == heif_error_Ok);
  auto native = std::unique_ptr<heif_image, decltype(&heif_image_release)>(raw_native, heif_image_release);
  REQUIRE(native->image->get_colorspace() == heif_colorspace_YCbCr);
  REQUIRE(native->image->get_chroma_format() == chroma);
  REQUIRE(native->image->get_width() == width);
  REQUIRE(native->image->get_height() == height);
  REQUIRE(native->image->has_nclx_color_profile());
  auto profile = native->image->get_color_profile_nclx();
  REQUIRE(profile.get_colour_primaries() == 1);
  REQUIRE(profile.get_transfer_characteristics() == 13);
  REQUIRE(profile.get_matrix_coefficients() == 1);
  REQUIRE(profile.get_full_range_flag() == (chroma == heif_chroma_444));
  for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}) {
    REQUIRE(native->image->get_bits_per_pixel(channel) == 10);
    size_t stride;
    const uint8_t* data = native->image->get_channel_memory(channel, &stride);
    bool has_low_bits = false;
    for (uint32_t y = 0; y < height && !has_low_bits; y++) {
      auto row = reinterpret_cast<const uint16_t*>(data + y * stride);
      for (uint32_t x = 0; x < native->image->get_width(channel); x++) {
        has_low_bits |= (row[x] & 3) != 0;
      }
    }
    REQUIRE(has_low_bits);
  }

  // Build the reference explicitly with the operators used before the fused path.
  auto options_ext = std::unique_ptr<heif_color_conversion_options_ext,
                                    void (*)(heif_color_conversion_options_ext*)>(
      heif_color_conversion_options_ext_alloc(), heif_color_conversion_options_ext_free);
  ColorState input = ColorState::from_image_planes(*native->image);
  input.nclx = profile;
  ColorState rgb10(heif_colorspace_RGB, heif_chroma_444, false, 10);
  ColorState rgb8(heif_colorspace_RGB, heif_chroma_444, false, 8);
  ColorState rgba8(heif_colorspace_RGB, heif_chroma_interleaved_RGBA, true, 8);
  auto planar10 = Op_YCbCr_to_RGB<uint16_t>().convert_colorspace(
      native->image, input, rgb10, options->color_conversion_options, *options_ext, nullptr);
  REQUIRE(planar10);
  auto planar8 = Op_to_sdr_planes().convert_colorspace(
      *planar10, rgb10, rgb8, options->color_conversion_options, *options_ext, nullptr);
  REQUIRE(planar8);
  auto reference = Op_RGB_to_RGB24_32().convert_colorspace(
      *planar8, rgb8, rgba8, options->color_conversion_options, *options_ext, nullptr);
  REQUIRE(reference);
  heif_image* raw_rgba = nullptr;
  error = heif_decode_image(handle.get(), &raw_rgba, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, nullptr);
  INFO(error.message);
  REQUIRE(error.code == heif_error_Ok);
  auto rgba = std::unique_ptr<heif_image, decltype(&heif_image_release)>(raw_rgba, heif_image_release);
  REQUIRE(rgba->image->get_width() == width);
  REQUIRE(rgba->image->get_height() == height);
  REQUIRE(rgba->image->get_chroma_format() == heif_chroma_interleaved_RGBA);
  size_t reference_stride, rgba_stride;
  const uint8_t* reference_data = (*reference)->get_channel_memory(heif_channel_interleaved, &reference_stride);
  const uint8_t* rgba_data = rgba->image->get_channel_memory(heif_channel_interleaved, &rgba_stride);
  for (uint32_t y = 0; y < height; y++) {
    REQUIRE(std::memcmp(reference_data + y * reference_stride, rgba_data + y * rgba_stride, 4 * width) == 0);
  }
}
