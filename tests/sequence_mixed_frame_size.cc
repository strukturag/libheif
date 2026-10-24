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

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"

#include <cstdint>
#include <cstring>
#include <utility>

namespace {

heif_image* make_frame(uint32_t width, uint32_t height)
{
  heif_image* image = nullptr;
  REQUIRE(heif_image_create(width, height, heif_colorspace_YCbCr, heif_chroma_420, &image).code == heif_error_Ok);
  REQUIRE(image != nullptr);

  for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}) {
    const uint32_t plane_width = channel == heif_channel_Y ? width : width / 2;
    const uint32_t plane_height = channel == heif_channel_Y ? height : height / 2;
    REQUIRE(heif_image_add_plane(image, channel, plane_width, plane_height, 8).code == heif_error_Ok);

    size_t stride = 0;
    uint8_t* pixels = heif_image_get_plane2(image, channel, &stride);
    REQUIRE(pixels != nullptr);
    for (uint32_t y = 0; y < plane_height; y++) {
      std::memset(pixels + y * stride, 0x80, plane_width);
    }
  }

  heif_image_set_duration(image, 1000);
  return image;
}

heif_error append_frame(heif_track* track, heif_encoder* encoder, uint32_t width, uint32_t height)
{
  heif_image* image = make_frame(width, height);
  heif_error err = heif_track_encode_sequence_image(track, image, encoder, nullptr);
  heif_image_release(image);
  return err;
}

} // namespace

TEST_CASE("encoders configured from the first frame reject mixed frame sizes")
{
  bool tested_encoder = false;
  for (const auto& [format, encoder_name] : {
         std::pair{heif_compression_AVC, "x264"},
         std::pair{heif_compression_HEVC, "x265"},
         std::pair{heif_compression_HEVC, "kvazaar"},
         std::pair{heif_compression_VVC, "uvg266"},
         std::pair{heif_compression_VVC, "vvenc"},
         std::pair{heif_compression_AV1, "aom"},
         std::pair{heif_compression_AV1, "rav1e"},
         std::pair{heif_compression_AV1, "svt"}}) {
    const heif_encoder_descriptor* descriptor = nullptr;
    if (heif_get_encoder_descriptors(format, encoder_name, &descriptor, 1) == 0) {
      continue;
    }

    tested_encoder = true;
    INFO("encoder: " << encoder_name);
    heif_context* context = heif_context_alloc();
    REQUIRE(context != nullptr);

    heif_encoder* encoder = nullptr;
    REQUIRE(heif_context_get_encoder(context, descriptor, &encoder).code == heif_error_Ok);
    REQUIRE(encoder != nullptr);

    heif_track* track = nullptr;
    REQUIRE(heif_context_add_visual_sequence_track(context, 128, 64, heif_track_type_video,
                                                   nullptr, nullptr, &track).code == heif_error_Ok);
    REQUIRE(track != nullptr);

    heif_error err = append_frame(track, encoder, 128, 64);
    INFO("first frame: " << (err.message ? err.message : ""));
    REQUIRE(err.code == heif_error_Ok);

    for (const auto& [width, height] : {std::pair{64U, 64U}, std::pair{128U, 128U},
                                        std::pair{64U, 128U}}) {
      err = append_frame(track, encoder, width, height);
      REQUIRE(err.code == heif_error_Invalid_input);
      REQUIRE(err.message != nullptr);
      REQUIRE(std::strstr(err.message, "size of the first frame") != nullptr);
    }

    // A rejected frame must not prevent a later frame of the correct size.
    err = append_frame(track, encoder, 128, 64);
    INFO("matching frame after rejection: " << (err.message ? err.message : ""));
    REQUIRE(err.code == heif_error_Ok);

    REQUIRE(heif_track_encode_end_of_sequence(track, encoder).code == heif_error_Ok);
    heif_encoder_release(encoder);
    heif_context_free(context);
  }

  if (!tested_encoder) {
    SKIP("Skipping test because no fixed-size sequence encoder is available.");
  }
}
