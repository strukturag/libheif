/*
  libheif decoder output contract tests

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
#include "codecs/decoder.h"
#include "image/pixelimage.h"
#include "libheif/heif_plugin.h"
#include "test_utils.h"

#include <array>
#include <cstring>
#include <utility>

namespace {

heif_image* next_image = nullptr;
int called_callback = -1;
const heif_error success = {heif_error_Ok, heif_suberror_Unspecified, "Success"};

const char* plugin_name() { return "planar output test decoder"; }
int supports(heif_compression_format format) { return format == heif_compression_HEVC ? 1 : 0; }
int supports2(const heif_decoder_plugin_compressed_format_description* format) { return supports(format->format); }
heif_error new_decoder(void** decoder) { *decoder = &called_callback; return success; }
heif_error new_decoder2(void** decoder, const heif_decoder_plugin_options*) { return new_decoder(decoder); }
void free_decoder(void*) {}
heif_error push_data2(void*, const void*, size_t, uintptr_t) { return success; }
heif_error flush_data(void*) { return success; }

heif_error decode_image(void*, heif_image** output)
{
  called_callback = 0;
  *output = std::exchange(next_image, nullptr);
  return success;
}

heif_error decode_next_image(void* decoder, heif_image** output, const heif_security_limits*)
{
  auto error = decode_image(decoder, output);
  called_callback = 1;
  return error;
}

heif_error decode_next_image2(void* decoder, heif_image** output, uintptr_t* user_data,
                              const heif_security_limits* limits)
{
  auto error = decode_next_image(decoder, output, limits);
  called_callback = 2;
  if (user_data) *user_data = 42;
  return error;
}

const std::array<heif_decoder_plugin, 3>& plugins()
{
  static const auto instances = [] {
    std::array<heif_decoder_plugin, 3> result{};
    const char* ids[] = {"planar-test-image", "planar-test-next", "planar-test-next2"};
    for (int i = 0; i < 3; ++i) {
      auto& plugin = result[i];
      plugin.plugin_api_version = 6;
      plugin.get_plugin_name = plugin_name;
      plugin.does_support_format = supports;
      plugin.new_decoder = new_decoder;
      plugin.free_decoder = free_decoder;
      plugin.decode_image = decode_image;
      plugin.id_name = ids[i];
      plugin.decode_next_image = i >= 1 ? decode_next_image : nullptr;
      plugin.minimum_required_libheif_version = LIBHEIF_MAKE_VERSION(1, 22, 0);
      plugin.does_support_format2 = supports2;
      plugin.new_decoder2 = new_decoder2;
      plugin.push_data2 = push_data2;
      plugin.flush_data = flush_data;
      plugin.decode_next_image2 = i == 2 ? decode_next_image2 : nullptr;
    }
    return result;
  }();
  static const bool registered = [&] {
    for (const auto& plugin : instances) {
      REQUIRE(heif_register_decoder_plugin(&plugin).code == heif_error_Ok);
    }
    return true;
  }();
  (void) registered;
  return instances;
}

// Exercise the real plugin dispatch without depending on any codec's bitstream parser.
class TestDecoder : public Decoder
{
public:
  TestDecoder()
  {
    DataExtent data;
    data.m_raw = {1};
    set_data_extent(std::move(data));
  }
  heif_compression_format get_compression_format() const override { return heif_compression_HEVC; }
  int get_luma_bits_per_pixel() const override { return 8; }
  int get_chroma_bits_per_pixel() const override { return 8; }
  Error get_coded_image_colorspace(heif_colorspace* colorspace, heif_chroma* chroma) const override
  {
    *colorspace = heif_colorspace_RGB;
    *chroma = heif_chroma_444;
    return Error::Ok;
  }
  Result<std::vector<uint8_t>> read_bitstream_configuration_data() const override { return std::vector<uint8_t>{}; }
};

heif_image* make_image(heif_colorspace colorspace, heif_chroma chroma, int size = 4)
{
  heif_image* image = nullptr;
  REQUIRE(heif_image_create(size, size, colorspace, chroma, &image).code == heif_error_Ok);
  const bool interleaved = num_interleaved_components_per_plane(chroma) > 1;
  const int depth = interleaved && chroma != heif_chroma_interleaved_RGB &&
                    chroma != heif_chroma_interleaved_RGBA ? 10 : 8;
  std::vector<heif_channel> channels;
  if (interleaved) channels = {heif_channel_interleaved};
  else if (colorspace == heif_colorspace_RGB) channels = {heif_channel_R, heif_channel_G, heif_channel_B, heif_channel_Alpha};
  else if (colorspace == heif_colorspace_monochrome) channels = {heif_channel_Y};
  else channels = {heif_channel_Y, heif_channel_Cb, heif_channel_Cr};
  for (auto channel : channels) {
    const bool subsampled = chroma == heif_chroma_420 && channel != heif_channel_Y;
    const int dimension = subsampled ? size / 2 : size;
    REQUIRE(heif_image_add_plane(image, channel, dimension, dimension, depth).code == heif_error_Ok);
    size_t stride;
    auto* plane = heif_image_get_plane2(image, channel, &stride);
    std::memset(plane, channel == heif_channel_Alpha ? 255 : 64, stride * dimension);
  }
  return image;
}

} // namespace

TEST_CASE("decoder callbacks reject interleaved output regardless of strict decoding")
{
  const int callback = GENERATE(0, 1, 2);
  const bool strict = GENERATE(false, true);
  const auto chroma = GENERATE(heif_chroma_interleaved_RGB, heif_chroma_interleaved_RGBA,
                               heif_chroma_interleaved_RRGGBB_BE, heif_chroma_interleaved_RRGGBB_LE,
                               heif_chroma_interleaved_RRGGBBAA_BE, heif_chroma_interleaved_RRGGBBAA_LE);
  auto* options = heif_decoding_options_alloc();
  options->decoder_id = plugins()[callback].id_name;
  options->strict_decoding = strict;
  TestDecoder decoder;
  REQUIRE_FALSE(decoder.decode_sequence_frame_from_compressed_data(false, *options, 0, nullptr));
  next_image = make_image(heif_colorspace_RGB, chroma);
  auto result = decoder.get_decoded_frame(*options, nullptr, nullptr);
  REQUIRE(called_callback == callback);
  REQUIRE_FALSE(result);
  CHECK(result.error().error_code == heif_error_Decoder_plugin_error);
  CHECK(result.error().sub_error_code == heif_suberror_Unspecified);
  CHECK(result.error().message == "Decoder returned an interleaved image; planar output is required");
  heif_decoding_options_free(options);
}

TEST_CASE("decoder callbacks accept planar output and preserve buffering")
{
  const int callback = GENERATE(0, 1, 2);
  const auto colorspace = GENERATE(heif_colorspace_RGB, heif_colorspace_YCbCr, heif_colorspace_monochrome);
  const auto chroma = colorspace == heif_colorspace_RGB ? heif_chroma_444 :
                      colorspace == heif_colorspace_YCbCr ? heif_chroma_420 : heif_chroma_monochrome;
  auto* options = heif_decoding_options_alloc();
  options->decoder_id = plugins()[callback].id_name;
  TestDecoder decoder;
  // No decoder context has been created yet.
  auto empty = decoder.get_decoded_frame(*options, nullptr, nullptr);
  REQUIRE(empty);
  CHECK(*empty == nullptr);
  REQUIRE_FALSE(decoder.decode_sequence_frame_from_compressed_data(false, *options, 0, nullptr));
  next_image = make_image(colorspace, chroma);
  uintptr_t user_data = 0;
  auto result = decoder.get_decoded_frame(*options, &user_data, nullptr);
  REQUIRE(called_callback == callback);
  REQUIRE(result);
  REQUIRE(*result);
  CHECK((*result)->get_colorspace() == colorspace);
  CHECK((*result)->get_chroma_format() == chroma);
  CHECK_FALSE((*result)->has_channel(heif_channel_interleaved));
  if (colorspace == heif_colorspace_RGB) CHECK((*result)->has_channel(heif_channel_Alpha));
  if (callback == 2) CHECK(user_data == 42);
  empty = decoder.get_decoded_frame(*options, nullptr, nullptr);
  REQUIRE(empty);
  CHECK(*empty == nullptr);
  heif_decoding_options_free(options);
}

TEST_CASE("public decoding can request interleaved RGBA from a planar decoder")
{
  const int callback = GENERATE(0, 1, 2);
  auto* context = get_context_for_test_file("clap_cropped.heic");
  auto* handle = get_primary_image_handle(context);
  auto* options = heif_decoding_options_alloc();
  options->decoder_id = plugins()[callback].id_name;
  next_image = make_image(heif_colorspace_RGB, heif_chroma_444, 256);
  heif_image* image = nullptr;
  auto error = heif_decode_image(handle, &image, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, options);
  INFO(error.message);
  REQUIRE(error.code == heif_error_Ok);
  REQUIRE(image != nullptr);
  CHECK(called_callback == callback);
  CHECK(heif_image_get_chroma_format(image) == heif_chroma_interleaved_RGBA);
  CHECK(heif_image_get_width(image, heif_channel_interleaved) == 64);
  CHECK(heif_image_get_height(image, heif_channel_interleaved) == 64);
  size_t stride;
  const auto* pixels = heif_image_get_plane_readonly2(image, heif_channel_interleaved, &stride);
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      const auto* pixel = pixels + size_t(y) * stride + x * 4;
      CHECK(pixel[0] == 64);
      CHECK(pixel[1] == 64);
      CHECK(pixel[2] == 64);
      CHECK(pixel[3] == 255);
    }
  }
  heif_image_release(image);
  heif_decoding_options_free(options);
  heif_image_handle_release(handle);
  heif_context_free(context);
}
