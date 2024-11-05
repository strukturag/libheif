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

#include "pixelimage.h"
#include "catch_amalgamated.hpp"

TEST_CASE( "uint32_t" )
{
  HeifPixelImage image;

  image.create(3,2, heif_colorspace_nonvisual, heif_chroma_undefined);
  image.add_channel(heif_channel_Y, 3,2, heif_channel_datatype_unsigned_integer, 32);

  uint32_t stride;
  uint32_t* data = image.get_channel<uint32_t>(heif_channel_Y, &stride);

  REQUIRE(stride >= 3);
  REQUIRE(image.get_width(heif_channel_Y) == 3);
  REQUIRE(image.get_height(heif_channel_Y) == 2);
  REQUIRE(image.get_bits_per_pixel(heif_channel_Y) == 32);
  REQUIRE(image.get_storage_bits_per_pixel(heif_channel_Y) == 32);
  REQUIRE(image.get_datatype(heif_channel_Y) == heif_channel_datatype_unsigned_integer);
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
  auto rotationResult = image.rotate_ccw(90);
  REQUIRE(rotationResult.error.error_code == heif_error_Ok);
  rot = rotationResult.value;

  data = rot->get_channel<uint32_t>(heif_channel_Y, &stride);

  REQUIRE(data[0*stride + 0] == 1000);
  REQUIRE(data[0*stride + 1] == 2000);
  REQUIRE(data[1*stride + 0] == 0xFFFFFFFFu);
  REQUIRE(data[1*stride + 1] == 0);
  REQUIRE(data[2*stride + 0] == 0);
  REQUIRE(data[2*stride + 1] == 0xFFFFFFFFu);

  // --- mirror

  rot->mirror_inplace(heif_transform_mirror_direction_horizontal);

  REQUIRE(data[0*stride + 1] == 1000);
  REQUIRE(data[0*stride + 0] == 2000);
  REQUIRE(data[1*stride + 1] == 0xFFFFFFFFu);
  REQUIRE(data[1*stride + 0] == 0);
  REQUIRE(data[2*stride + 1] == 0);
  REQUIRE(data[2*stride + 0] == 0xFFFFFFFFu);

  rot->mirror_inplace(heif_transform_mirror_direction_vertical);

  REQUIRE(data[2*stride + 1] == 1000);
  REQUIRE(data[2*stride + 0] == 2000);
  REQUIRE(data[1*stride + 1] == 0xFFFFFFFFu);
  REQUIRE(data[1*stride + 0] == 0);
  REQUIRE(data[0*stride + 1] == 0);
  REQUIRE(data[0*stride + 0] == 0xFFFFFFFFu);

  // --- crop

  std::shared_ptr<HeifPixelImage> crop;
  auto cropResult = image.crop(1,2,1,1);
  REQUIRE(cropResult.error.error_code == heif_error_Ok);
  crop = cropResult.value;

  REQUIRE(crop->get_width(heif_channel_Y) == 2);
  REQUIRE(crop->get_height(heif_channel_Y) == 1);

  data = crop->get_channel<uint32_t>(heif_channel_Y, &stride);

  REQUIRE(data[0*stride + 0] == 0);
  REQUIRE(data[0*stride + 1] == 2000);

  cropResult = image.crop(0,1,0,1);
  REQUIRE(cropResult.error.error_code == heif_error_Ok);
  crop = cropResult.value;

  REQUIRE(crop->get_width(heif_channel_Y) == 2);
  REQUIRE(crop->get_height(heif_channel_Y) == 2);

  data = crop->get_channel<uint32_t>(heif_channel_Y, &stride);

  REQUIRE(data[0*stride + 0] == 0);
  REQUIRE(data[0*stride + 1] == 0xFFFFFFFFu);
  REQUIRE(data[1*stride + 0] == 0xFFFFFFFFu);
  REQUIRE(data[1*stride + 1] == 0);
}


TEST_CASE( "complex64_t" )
{
  HeifPixelImage image;

  image.create(3,2, heif_colorspace_nonvisual, heif_chroma_undefined);
  image.add_channel(heif_channel_Y, 3,2, heif_channel_datatype_complex_number, 128);

  uint32_t stride;
  heif_complex64* data = image.get_channel<heif_complex64>(heif_channel_Y, &stride);

  REQUIRE(stride >= 3);
  REQUIRE(image.get_width(heif_channel_Y) == 3);
  REQUIRE(image.get_height(heif_channel_Y) == 2);
  REQUIRE(image.get_bits_per_pixel(heif_channel_Y) == 128);
  REQUIRE(image.get_storage_bits_per_pixel(heif_channel_Y) == 128);
  REQUIRE(image.get_datatype(heif_channel_Y) == heif_channel_datatype_complex_number);
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
  heif_error error = heif_image_create(3,2,heif_colorspace_nonvisual, heif_chroma_undefined, &image);
  REQUIRE(!error.code);

  heif_image_add_channel(image, heif_channel_Y, 3,2, heif_channel_datatype_unsigned_integer, 32);

  uint32_t stride;
  uint32_t* data = heif_image_get_channel_uint32(image, heif_channel_Y, &stride);
  REQUIRE(data != nullptr);

  REQUIRE(stride >= 3);
  REQUIRE(heif_image_get_datatype(image, heif_channel_Y) == heif_channel_datatype_unsigned_integer);
  REQUIRE(heif_image_get_bits_per_pixel_range(image, heif_channel_Y) == 32);

  data[stride*0 + 0] = 0xFFFFFFFFu;
  data[stride*0 + 1] = 0;
  data[stride*0 + 2] = 1000;
  data[stride*1 + 0] = 0xFFFFFFFFu;
  data[stride*1 + 1] = 200;
  data[stride*1 + 2] = 5;
}
