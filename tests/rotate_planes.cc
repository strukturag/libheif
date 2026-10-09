/*
  libheif unit tests

  MIT License

  Copyright (c) 2026 Michael Wallner <mike@php.net>

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
#include "image/pixelimage.h"

#include <cstdint>
#include <memory>


// A sample value that differs from its neighbors', so that one moved to the wrong place shows.
static uint32_t pattern(uint32_t x, uint32_t y)
{
  return (x * 2654435761U) ^ (y * 40503U) ^ (x * y);
}


// Rotates a plane of w x h samples of T by angle degrees counter-clockwise and checks every sample
// of the result against the definition of the rotation.
template <typename T>
static void check_rotation(uint32_t w, uint32_t h, int angle)
{
  CAPTURE(sizeof(T) * 8, w, h, angle);
  auto* limits = heif_get_global_security_limits();

  // rotate_ccw() calls shared_from_this() for some inputs, so the image must be owned by a
  // shared_ptr.
  auto image = std::make_shared<HeifPixelImage>();
  image->create(w, h, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(image->add_channel(heif_channel_Y, w, h, sizeof(T) * 8, limits,
                             heif_component_datatype_unsigned_integer).error_code == heif_error_Ok);

  size_t in_stride;
  T* in = image->get_channel_memory<T>(heif_channel_Y, &in_stride);
  in_stride /= sizeof(T);
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      in[y * in_stride + x] = static_cast<T>(pattern(x, y));
    }
  }

  auto rotated = image->rotate_ccw(angle, limits);
  REQUIRE(rotated.error().error_code == heif_error_Ok);
  std::shared_ptr<HeifPixelImage> out_image = *rotated;

  uint32_t out_w = angle == 180 ? w : h;
  uint32_t out_h = angle == 180 ? h : w;
  REQUIRE(out_image->get_width(heif_channel_Y) == out_w);
  REQUIRE(out_image->get_height(heif_channel_Y) == out_h);

  size_t out_stride;
  const T* out = out_image->get_channel_memory<T>(heif_channel_Y, &out_stride);
  out_stride /= sizeof(T);

  uint32_t wrong = 0;
  uint32_t first_wrong_x = 0;
  uint32_t first_wrong_y = 0;
  for (uint32_t y = 0; y < out_h; y++) {
    for (uint32_t x = 0; x < out_w; x++) {
      // Where output sample (x, y) comes from in the input.
      uint32_t sx = angle == 90 ? w - 1 - y : angle == 180 ? w - 1 - x : y;
      uint32_t sy = angle == 90 ? x : angle == 180 ? h - 1 - y : h - 1 - x;
      if (out[y * out_stride + x] != static_cast<T>(pattern(sx, sy))) {
        if (wrong == 0) {
          first_wrong_x = x;
          first_wrong_y = y;
        }
        wrong++;
      }
    }
  }
  CAPTURE(first_wrong_x, first_wrong_y);
  REQUIRE(wrong == 0);
}


TEST_CASE("rotate_ccw moves every sample where the rotation puts it")
{
  // Sizes below, at and above the 8x8 tiles of 8-bit planes, the 4x4 tiles of 16-bit planes and
  // the 64x64 blocks, with edges that do not fill a tile.
  const uint32_t sizes[][2] = {{1, 1}, {7, 9}, {9, 7}, {8, 8}, {64, 64}, {65, 130}, {130, 65}, {200, 123}};

  for (auto& size : sizes) {
    for (int angle : {90, 180, 270}) {
      check_rotation<uint8_t>(size[0], size[1], angle);
      check_rotation<uint16_t>(size[0], size[1], angle);
    }
  }
}
