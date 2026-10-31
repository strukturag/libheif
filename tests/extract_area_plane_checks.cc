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

#include "image/pixelimage.h"
#include "catch_amalgamated.hpp"

#include <string>

TEST_CASE("extract_image_area preserves every component sharing a channel") {
  const bool padded = GENERATE(false, true);
  const auto type = GENERATE(heif_cmpd_component_type_monochrome, heif_cmpd_component_type_Y);
  auto image = std::make_shared<HeifPixelImage>();
  image->create(4, 4, heif_colorspace_custom, heif_chroma_planar);
  struct Format {
    int bits;
    heif_component_datatype datatype;
  };
  const Format formats[] = {{8, heif_component_datatype_unsigned_integer},
                            {16, heif_component_datatype_signed_integer},
                            {32, heif_component_datatype_floating_point}};
  for (const auto& format : formats) {
    auto id = image->add_component(4, 4, type, format.datatype, format.bits, nullptr);
    REQUIRE(id);
    image->find_component_description(*id)->gimi_content_id = "band-" + std::to_string(*id);
    size_t stride;
    auto* data = image->get_component(*id, &stride);
    const int bytes = format.bits / 8;
    for (uint32_t y = 0; y < 4; y++) {
      for (uint32_t x = 0; x < 4; x++) {
        for (int b = 0; b < bytes; b++) {
          data[y * stride + x * bytes + b] = static_cast<uint8_t>(*id * 41 + y * 13 + x * 3 + b);
        }
      }
    }
  }

  const uint32_t width = padded ? 67 : 2;
  const uint32_t height = padded ? 65 : 2;
  auto result = image->extract_image_area(1, 1, width, height, nullptr);
  REQUIRE(result);
  auto area = *result;
  REQUIRE(area->get_used_component_ids() == image->get_used_component_ids());
  REQUIRE(area->get_colorspace() == image->get_colorspace());
  REQUIRE(area->get_chroma_format() == image->get_chroma_format());
  for (const auto& original : image->get_component_descriptions()) {
    CAPTURE(original.component_id, padded);
    const auto* desc = area->find_component_description(original.component_id);
    REQUIRE(desc);
    REQUIRE(desc->component_type == original.component_type);
    REQUIRE(desc->channel == original.channel);
    REQUIRE(desc->datatype == original.datatype);
    REQUIRE(desc->bit_depth == original.bit_depth);
    REQUIRE(desc->gimi_content_id == original.gimi_content_id);
    REQUIRE(desc->width == width);
    REQUIRE(desc->height == height);
    size_t stride;
    const auto* data = area->get_component(original.component_id, &stride);
    REQUIRE(data);
    const int bytes = original.bit_depth / 8;
    for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
        for (int b = 0; b < bytes; b++) {
          const uint8_t expected = x < 3 && y < 3
                                     ? static_cast<uint8_t>(original.component_id * 41 + (y + 1) * 13 + (x + 1) * 3 + b)
                                     : 0;
          REQUIRE(data[y * stride + x * bytes + b] == expected);
        }
      }
    }
  }
}

// Regression tests for HeifPixelImage::extract_image_area().
//
// The per-plane copy window is clamped against the image's declared size
// (chroma-mapped), not each plane's own actual stored size, and the offsets
// (xs/ys) it subtracts from that clamp are themselves derived from the
// declared size too. A plane smaller than the image's declared size implies
// (the same enabling condition as crop()/scale_nearest_neighbor()) is
// therefore both read past its end and, once xs/ys exceed the plane's real
// bounds, underflows the unsigned subtraction feeding the memcpy length --
// worse than a plain OOB read. extract_image_area() now calls
// has_standard_plane_sizes() up front to reject that, the same way crop()
// does. This is a deliberate policy choice: a "requested region exceeds the
// image" is a different, legitimate case this function already handles by
// clamping and zero-padding (extend_to_size_with_zero()), and that behavior
// is unaffected -- only a plane that disagrees with the image's own declared
// geometry is now rejected.

TEST_CASE("extract_image_area rejects a plane smaller than the image's declared size") {
  auto* limits = heif_get_global_security_limits();

  auto image = std::make_shared<HeifPixelImage>();
  image->create(8, 8, heif_colorspace_monochrome, heif_chroma_monochrome);

  // Y plane smaller than the image's declared 8x8 -- add_channel() doesn't
  // check this against m_width/m_height.
  REQUIRE(image->add_channel(heif_channel_Y, 4, 4, 8, limits).error_code == heif_error_Ok);

  auto result = image->extract_image_area(0, 0, 4, 4, limits);
  REQUIRE(result.is_error());
  REQUIRE(result.error().error_code == heif_error_Unsupported_feature);
}

TEST_CASE("extract_image_area extracts a region from a well-formed image correctly") {
  auto* limits = heif_get_global_security_limits();

  auto image = std::make_shared<HeifPixelImage>();
  image->create(4, 4, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(image->add_channel(heif_channel_Y, 4, 4, 8, limits).error_code == heif_error_Ok);

  size_t stride;
  uint8_t* y = image->get_channel_memory(heif_channel_Y, &stride);
  for (uint32_t row = 0; row < 4; row++) {
    for (uint32_t col = 0; col < 4; col++) {
      y[row * stride + col] = static_cast<uint8_t>(row * 4 + col);
    }
  }

  // Extract the 2x2 region starting at (1,1) -- fully inside the image.
  auto result = image->extract_image_area(1, 1, 2, 2, limits);
  REQUIRE(!result.is_error());

  auto area = *result;
  REQUIRE(area->get_width(heif_channel_Y) == 2);
  REQUIRE(area->get_height(heif_channel_Y) == 2);

  const uint8_t* out = area->get_channel_memory(heif_channel_Y, &stride);
  REQUIRE(out[0 * stride + 0] == 5);  // (1,1)
  REQUIRE(out[0 * stride + 1] == 6);  // (2,1)
  REQUIRE(out[1 * stride + 0] == 9);  // (1,2)
  REQUIRE(out[1 * stride + 1] == 10); // (2,2)
}

TEST_CASE("extract_image_area zero-pads a well-formed image when the requested region exceeds it") {
  auto* limits = heif_get_global_security_limits();

  auto image = std::make_shared<HeifPixelImage>();
  image->create(2, 2, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(image->add_channel(heif_channel_Y, 2, 2, 8, limits).error_code == heif_error_Ok);

  size_t stride;
  uint8_t* y = image->get_channel_memory(heif_channel_Y, &stride);
  y[0 * stride + 0] = 1;
  y[0 * stride + 1] = 2;
  y[1 * stride + 0] = 3;
  y[1 * stride + 1] = 4;

  // Request a 4x4 area starting at (1,1) -- extends past the 2x2 image. This
  // is unrelated to plane-size correctness (the plane matches the image's
  // declared size); has_standard_plane_sizes() must not reject it.
  auto result = image->extract_image_area(1, 1, 4, 4, limits);
  REQUIRE(!result.is_error());

  auto area = *result;
  REQUIRE(area->get_width(heif_channel_Y) == 4);
  REQUIRE(area->get_height(heif_channel_Y) == 4);

  const uint8_t* out = area->get_channel_memory(heif_channel_Y, &stride);
  REQUIRE(out[0 * stride + 0] == 4);  // source pixel (1,1)
  REQUIRE(out[0 * stride + 1] == 0);  // past the source edge -> zero pad
  REQUIRE(out[1 * stride + 0] == 0);
  REQUIRE(out[3 * stride + 3] == 0);
}
