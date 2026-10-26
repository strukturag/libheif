/*
  libheifio PNG decode unit tests

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

// All test images are 8x4 pixels.

#include "catch_amalgamated.hpp"
#include "heifio/decoder.h"
#include "heifio/decoder_png.h"
#include "test_utils.h"
#include "libheif/heif.h"

#include <cstdint>
#include <string>

static constexpr int width = 8;
static constexpr int height = 4;

static InputImage load(const char* filename, int output_bit_depth)
{
  InputImage input_image;
  std::string path = get_path_for_heifio_test_file(filename);
  heif_error err = loadPNG(path.c_str(), output_bit_depth, &input_image);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(input_image.image != nullptr);
  REQUIRE(heif_image_get_primary_width(input_image.image.get()) == width);
  REQUIRE(heif_image_get_primary_height(input_image.image.get()) == height);
  return input_image;
}


// 16 bit gray+alpha used to be decoded as if it had four channels.
// Pixel (x,y) has gray value 0x1000*x+y and alpha 0x2000*x+0x100*y.
TEST_CASE("Grayscale+alpha 16 bit") {
  InputImage input_image = load("gray_alpha16.png", 16);
  const heif_image* image = input_image.image.get();
  REQUIRE(heif_image_get_colorspace(image) == heif_colorspace_monochrome);
  REQUIRE(heif_image_get_chroma_format(image) == heif_chroma_monochrome);
  REQUIRE(heif_image_has_channel(image, heif_channel_Alpha));

  size_t y_stride, a_stride;
  const uint8_t* py = heif_image_get_plane_readonly2(image, heif_channel_Y, &y_stride);
  const uint8_t* pa = heif_image_get_plane_readonly2(image, heif_channel_Alpha, &a_stride);
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++) {
      INFO("x=" << x << " y=" << y);
      REQUIRE(*(const uint16_t*) (py + y * y_stride + x * 2) == 0x1000 * x + y);
      REQUIRE(*(const uint16_t*) (pa + y * a_stride + x * 2) == 0x2000 * x + 0x100 * y);
    }
}
