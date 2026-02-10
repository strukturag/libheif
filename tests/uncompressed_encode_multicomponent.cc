/*
  libheif integration tests for uncompressed multi-component images

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
#include "libheif/heif_experimental.h"
#include "test_utils.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>

static constexpr int kWidth = 8;
static constexpr int kHeight = 8;
static constexpr int kNumComponents = 4;
static constexpr uint16_t kMonoComponentType = 0;


template <typename T>
static T compute_fill_value(uint32_t comp, uint32_t y, uint32_t x)
{
  assert(false && "compute_fill_value not specialized for this type");
  return T{};
}

template <>
float compute_fill_value<float>(uint32_t comp, uint32_t y, uint32_t x)
{
  return static_cast<float>(comp * 37 + y * kWidth + x + 1) * 0.1f;
}

template <>
double compute_fill_value<double>(uint32_t comp, uint32_t y, uint32_t x)
{
  return static_cast<double>(comp * 37 + y * kWidth + x + 1) * 0.1;
}

template <>
heif_complex32 compute_fill_value<heif_complex32>(uint32_t comp, uint32_t y, uint32_t x)
{
  float base = static_cast<float>(comp * 37 + y * kWidth + x + 1);
  return {base * 0.1f, base * 0.2f};
}

template <>
heif_complex64 compute_fill_value<heif_complex64>(uint32_t comp, uint32_t y, uint32_t x)
{
  double base = static_cast<double>(comp * 37 + y * kWidth + x + 1);
  return {base * 0.1, base * 0.2};
}

template <>
int8_t compute_fill_value<int8_t>(uint32_t comp, uint32_t y, uint32_t x)
{
  return static_cast<int8_t>(((comp * 37 + y * kWidth + x + 1) % 256) - 128);
}

template <>
int16_t compute_fill_value<int16_t>(uint32_t comp, uint32_t y, uint32_t x)
{
  return static_cast<int16_t>(comp * 37 + y * kWidth + x + 1);
}

template <>
int32_t compute_fill_value<int32_t>(uint32_t comp, uint32_t y, uint32_t x)
{
  return static_cast<int32_t>(comp * 37 + y * kWidth + x + 1);
}

template <>
uint8_t compute_fill_value<uint8_t>(uint32_t comp, uint32_t y, uint32_t x)
{
  return static_cast<uint8_t>((comp * 37 + y * kWidth + x + 1) & 0xFF);
}

template <>
uint16_t compute_fill_value<uint16_t>(uint32_t comp, uint32_t y, uint32_t x)
{
  return static_cast<uint16_t>(comp * 37 + y * kWidth + x + 1);
}

template <>
uint32_t compute_fill_value<uint32_t>(uint32_t comp, uint32_t y, uint32_t x)
{
  return comp * 37 + y * kWidth + x + 1;
}


// Typed accessor helpers: get mutable pointer for filling
template <typename T>
T* get_component_ptr(heif_image* image, uint32_t idx, size_t* out_stride);

template <>
uint8_t* get_component_ptr<uint8_t>(heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component(image, idx, out_stride);
}

template <>
uint16_t* get_component_ptr<uint16_t>(heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_uint16(image, idx, out_stride);
}

template <>
uint32_t* get_component_ptr<uint32_t>(heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_uint32(image, idx, out_stride);
}

template <>
int16_t* get_component_ptr<int16_t>(heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_int16(image, idx, out_stride);
}

template <>
int32_t* get_component_ptr<int32_t>(heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_int32(image, idx, out_stride);
}

template <>
float* get_component_ptr<float>(heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_float32(image, idx, out_stride);
}

template <>
double* get_component_ptr<double>(heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_float64(image, idx, out_stride);
}

template <>
heif_complex32* get_component_ptr<heif_complex32>(heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_complex32(image, idx, out_stride);
}

template <>
heif_complex64* get_component_ptr<heif_complex64>(heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_complex64(image, idx, out_stride);
}


// Typed accessor helpers: get const pointer for reading
template <typename T>
const T* get_component_ptr_readonly(const heif_image* image, uint32_t idx, size_t* out_stride);

template <>
const uint8_t* get_component_ptr_readonly<uint8_t>(const heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_readonly(image, idx, out_stride);
}

template <>
const uint16_t* get_component_ptr_readonly<uint16_t>(const heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_uint16_readonly(image, idx, out_stride);
}

template <>
const uint32_t* get_component_ptr_readonly<uint32_t>(const heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_uint32_readonly(image, idx, out_stride);
}

template <>
const int16_t* get_component_ptr_readonly<int16_t>(const heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_int16_readonly(image, idx, out_stride);
}

template <>
const int32_t* get_component_ptr_readonly<int32_t>(const heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_int32_readonly(image, idx, out_stride);
}

template <>
const float* get_component_ptr_readonly<float>(const heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_float32_readonly(image, idx, out_stride);
}

template <>
const double* get_component_ptr_readonly<double>(const heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_float64_readonly(image, idx, out_stride);
}

template <>
const heif_complex32* get_component_ptr_readonly<heif_complex32>(const heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_complex32_readonly(image, idx, out_stride);
}

template <>
const heif_complex64* get_component_ptr_readonly<heif_complex64>(const heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_complex64_readonly(image, idx, out_stride);
}


template <>
int8_t* get_component_ptr<int8_t>(heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_int8(image, idx, out_stride);
}

template <>
const int8_t* get_component_ptr_readonly<int8_t>(const heif_image* image, uint32_t idx, size_t* out_stride)
{
  return heif_image_get_component_int8_readonly(image, idx, out_stride);
}


template <typename T>
static heif_image* create_and_fill_image(heif_channel_datatype datatype, int bit_depth)
{
  heif_image* image = nullptr;
  heif_error err;

  err = heif_image_create(kWidth, kHeight, heif_colorspace_nonvisual,
                          heif_chroma_undefined, &image);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(image != nullptr);

  for (uint32_t c = 0; c < kNumComponents; c++) {
    uint32_t idx = 0;
    err = heif_image_add_component(image, kWidth, kHeight,
                                   kMonoComponentType, datatype, bit_depth, &idx);
    REQUIRE(err.code == heif_error_Ok);
    REQUIRE(idx == c);

    size_t stride = 0;
    T* data = get_component_ptr<T>(image, idx, &stride);
    REQUIRE(data != nullptr);
    REQUIRE(stride >= kWidth);

    for (uint32_t y = 0; y < kHeight; y++) {
      for (uint32_t x = 0; x < kWidth; x++) {
        data[y * stride + x] = compute_fill_value<T>(c, y, x);
      }
    }
  }

  return image;
}


template <typename T>
static bool values_equal(T a, T b)
{
  return a == b;
}

template <>
bool values_equal<float>(float a, float b)
{
  return std::abs(a - b) < 1e-5f;
}

template <>
bool values_equal<double>(double a, double b)
{
  return std::abs(a - b) < 1e-10;
}

template <>
bool values_equal<heif_complex32>(heif_complex32 a, heif_complex32 b)
{
  return std::abs(a.real - b.real) < 1e-5f && std::abs(a.imaginary - b.imaginary) < 1e-5f;
}

template <>
bool values_equal<heif_complex64>(heif_complex64 a, heif_complex64 b)
{
  return std::abs(a.real - b.real) < 1e-10 && std::abs(a.imaginary - b.imaginary) < 1e-10;
}


template <typename T>
static void verify_image_data(const heif_image* image)
{
  uint32_t num_components = heif_image_get_number_of_components(image);
  REQUIRE(num_components == kNumComponents);

  for (uint32_t c = 0; c < kNumComponents; c++) {
    REQUIRE(heif_image_get_component_width(image, c) == kWidth);
    REQUIRE(heif_image_get_component_height(image, c) == kHeight);
    REQUIRE(heif_image_get_component_type(image, c) == kMonoComponentType);

    size_t stride = 0;
    const T* data = get_component_ptr_readonly<T>(image, c, &stride);
    REQUIRE(data != nullptr);

    for (uint32_t y = 0; y < kHeight; y++) {
      for (uint32_t x = 0; x < kWidth; x++) {
        T expected = compute_fill_value<T>(c, y, x);
        T actual = data[y * stride + x];
        REQUIRE(values_equal(expected, actual));
      }
    }
  }
}


template <typename T>
static void test_multi_mono(heif_channel_datatype datatype, int bit_depth, const char* output_filename)
{
  heif_image* image = create_and_fill_image<T>(datatype, bit_depth);

  // Verify that data was written correctly before encode
  verify_image_data<T>(image);

  // Encode
  heif_context* ctx = heif_context_alloc();
  heif_encoder* encoder = nullptr;
  heif_error err = heif_context_get_encoder_for_format(ctx, heif_compression_uncompressed, &encoder);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_encode_image(ctx, image, encoder, nullptr, nullptr);
  REQUIRE(err.code == heif_error_Ok);

  // Write to file
  std::string output_path = get_tests_output_file_path(output_filename);
  err = heif_context_write_to_file(ctx, output_path.c_str());
  REQUIRE(err.code == heif_error_Ok);

  heif_encoder_release(encoder);
  heif_image_release(image);
  heif_context_free(ctx);

  // Read back
  heif_context* ctx2 = heif_context_alloc();
  err = heif_context_read_from_file(ctx2, output_path.c_str(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx2, &handle);
  REQUIRE(err.code == heif_error_Ok);

  heif_image* decoded = nullptr;
  err = heif_decode_image(handle, &decoded, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
  REQUIRE(err.code == heif_error_Ok);

  verify_image_data<T>(decoded);

  heif_image_release(decoded);
  heif_image_handle_release(handle);
  heif_context_free(ctx2);
}


TEST_CASE("Multi-mono uint8")
{
  test_multi_mono<uint8_t>(heif_channel_datatype_unsigned_integer, 8, "multi_mono_uint8.heif");
}

TEST_CASE("Multi-mono uint16")
{
  test_multi_mono<uint16_t>(heif_channel_datatype_unsigned_integer, 16, "multi_mono_uint16.heif");
}

TEST_CASE("Multi-mono uint32")
{
  test_multi_mono<uint32_t>(heif_channel_datatype_unsigned_integer, 32, "multi_mono_uint32.heif");
}

TEST_CASE("Multi-mono int8")
{
  test_multi_mono<int8_t>(heif_channel_datatype_signed_integer, 8, "multi_mono_int8.heif");
}

TEST_CASE("Multi-mono int16")
{
  test_multi_mono<int16_t>(heif_channel_datatype_signed_integer, 16, "multi_mono_int16.heif");
}

TEST_CASE("Multi-mono int32")
{
  test_multi_mono<int32_t>(heif_channel_datatype_signed_integer, 32, "multi_mono_int32.heif");
}

TEST_CASE("Multi-mono float32")
{
  test_multi_mono<float>(heif_channel_datatype_floating_point, 32, "multi_mono_float32.heif");
}

TEST_CASE("Multi-mono float64")
{
  test_multi_mono<double>(heif_channel_datatype_floating_point, 64, "multi_mono_float64.heif");
}

TEST_CASE("Multi-mono complex32")
{
  test_multi_mono<heif_complex32>(heif_channel_datatype_complex_number, 64, "multi_mono_complex32.heif");
}

TEST_CASE("Multi-mono complex64")
{
  test_multi_mono<heif_complex64>(heif_channel_datatype_complex_number, 128, "multi_mono_complex64.heif");
}
