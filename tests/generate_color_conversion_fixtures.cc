/*
  libheif test fixture generator

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

#include "libheif/heif.h"
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>

static void check(heif_error error)
{
  if (error.code != heif_error_Ok) {
    throw std::runtime_error(error.message);
  }
}


static void generate(const std::filesystem::path& directory, heif_chroma chroma, bool full_range)
{
  const int width = chroma == heif_chroma_422 ? 258 : 257;
  const int height = 129;
  auto context = std::unique_ptr<heif_context, decltype(&heif_context_free)>(heif_context_alloc(), heif_context_free);
  const heif_encoder_descriptor* descriptor = nullptr;
  if (heif_get_encoder_descriptors(heif_compression_HEVC, "x265", &descriptor, 1) != 1) {
    throw std::runtime_error("An x265 encoder with 10-bit support is required");
  }
  heif_encoder* raw_encoder;
  check(heif_context_get_encoder(context.get(), descriptor, &raw_encoder));
  auto encoder = std::unique_ptr<heif_encoder, decltype(&heif_encoder_release)>(raw_encoder, heif_encoder_release);
  check(heif_encoder_set_lossless(encoder.get(), 1));
  check(heif_encoder_set_logging_level(encoder.get(), 0));
  check(heif_encoder_set_parameter_string(encoder.get(), "chroma", chroma == heif_chroma_422 ? "422" : "444"));
  check(heif_encoder_set_parameter_string(encoder.get(), "preset", "medium"));
  check(heif_encoder_set_parameter_string(encoder.get(), "x265:pools", "none"));
  check(heif_encoder_set_parameter_string(encoder.get(), "x265:frame-threads", "1"));
  check(heif_encoder_set_parameter_string(encoder.get(), "x265:wpp", "0"));

  heif_image* raw_image;
  check(heif_image_create(width, height, heif_colorspace_YCbCr, chroma, &raw_image));
  auto image = std::unique_ptr<heif_image, decltype(&heif_image_release)>(raw_image, heif_image_release);
  int plane_index = 0;
  for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}) {
    const int plane_width = channel != heif_channel_Y && chroma == heif_chroma_422 ? width / 2 : width;
    check(heif_image_add_plane(image.get(), channel, plane_width, height, 10));
    size_t stride;
    uint8_t* data = heif_image_get_plane2(image.get(), channel, &stride);
    for (int y = 0; y < height; y++) {
      auto row = reinterpret_cast<uint16_t*>(data + y * stride);
      for (int x = 0; x < plane_width; x++) {
        const int pattern = x * 29 + y * 53 + plane_index * 211 + ((x / 17 + y / 13) % 2) * 401;
        const int span = full_range ? 1024 : (channel == heif_channel_Y ? 877 : 897);
        row[x] = static_cast<uint16_t>((full_range ? 0 : 64) + pattern % span);
      }
    }
    plane_index++;
  }

  auto profile = std::unique_ptr<heif_color_profile_nclx, decltype(&heif_nclx_color_profile_free)>(
      heif_nclx_color_profile_alloc(), heif_nclx_color_profile_free);
  check(heif_nclx_color_profile_set_color_primaries(profile.get(), 1));
  check(heif_nclx_color_profile_set_transfer_characteristics(profile.get(), 13));
  check(heif_nclx_color_profile_set_matrix_coefficients(profile.get(), 1));
  profile->full_range_flag = full_range;
  check(heif_image_set_nclx_color_profile(image.get(), profile.get()));
  auto options = std::unique_ptr<heif_encoding_options, decltype(&heif_encoding_options_free)>(
      heif_encoding_options_alloc(), heif_encoding_options_free);
  options->output_nclx_profile = profile.get();
  options->macOS_compatibility_workaround_no_nclx_profile = false;
  check(heif_context_encode_image(context.get(), image.get(), encoder.get(), options.get(), nullptr));
  const char* name = chroma == heif_chroma_422 ? "fused-rgba10-422.heic" : "fused-rgba10-444.heic";
  check(heif_context_write_to_file(context.get(), (directory / name).string().c_str()));
  printf("Generated %s, %dx%d, 10-bit %s-range\n", name, width, height, full_range ? "full" : "limited");
}


int main(int argc, char** argv)
{
  if (argc != 2) {
    fprintf(stderr, "Usage: generate_color_conversion_fixtures output-directory\n");
    return 1;
  }
  bool initialized = false;
  try {
    check(heif_init(nullptr));
    initialized = true;
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    generate(directory, heif_chroma_422, false);
    generate(directory, heif_chroma_444, true);
    heif_deinit();
    return 0;
  }
  catch (const std::exception& error) {
    fprintf(stderr, "%s\n", error.what());
    if (initialized) {
      heif_deinit();
    }
    return 1;
  }
}
