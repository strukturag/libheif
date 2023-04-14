/*
  libheif unit tests

  MIT License

  Copyright (c) 2019 struktur AG, Dirk Farin <farin@struktur.de>

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

#include "catch.hpp"
#include "libheif/color-conversion/colorconversion.h"
#include "libheif/heif_image.h"


using namespace heif;

struct Plane {
  heif_channel channel;
  int width;
  int height;
  int bit_depth;
};

bool PlatformIsBigEndian() {
  int i = 1;
  return !*((char*)&i);
}

bool EndiannessMatchesPlatform(heif_chroma chroma) {
  if (chroma == heif_chroma_interleaved_RRGGBB_BE ||
      chroma == heif_chroma_interleaved_RRGGBBAA_BE) {
    return PlatformIsBigEndian();
  } else if (chroma == heif_chroma_interleaved_RRGGBB_LE ||
             chroma == heif_chroma_interleaved_RRGGBBAA_LE) {
    return !PlatformIsBigEndian();
  }
  return true;
}

uint16_t SwapBytes(uint16_t v) {
  const uint8_t hi = static_cast<uint8_t>((v & 0xff00) >> 8);
  const uint8_t lo = static_cast<uint8_t>(v & 0x00ff);
  return (lo << 8) | hi;
}

uint16_t SwapBytesIfNeeded(uint16_t v, heif_chroma chroma) {
  return EndiannessMatchesPlatform(chroma) ? v : SwapBytes(v);
}

template <typename T>
double GetPsnr(const HeifPixelImage& original, const HeifPixelImage& compressed,
               heif_channel channel, bool skip_alpha) {
  int w = original.get_width(channel);
  int h = original.get_height(channel);
  heif_chroma chroma = original.get_chroma_format();

  int orig_stride;
  int compressed_stride;
  const T* orig_p = (T*)original.get_plane(channel, &orig_stride);
  const T* compressed_p = (T*)compressed.get_plane(channel, &compressed_stride);
  orig_stride /= sizeof(T);
  compressed_stride /= sizeof(T);
  double mse = 0.0;

  int num_interleaved = num_interleaved_pixels_per_plane(chroma);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w * num_interleaved; x++) {
      if (skip_alpha && num_interleaved > 1 && x % 4 == 3) continue;
      int orig_v = SwapBytesIfNeeded(orig_p[y * orig_stride + x], chroma);
      int compressed_v = SwapBytesIfNeeded(compressed_p[y * compressed_stride + x], chroma);
      int d = orig_v - compressed_v;
      mse += d * d;
    }
  }
  mse /= w * h;
  int max = (1 << original.get_bits_per_pixel(channel)) - 1;
  double psnr = 10 * log10(max * max / mse);
  if (psnr < 0.) return 0.;
  if (psnr > 100.) return 100.;
 return psnr;
}

double GetPsnr(const HeifPixelImage& original, const HeifPixelImage& compressed,
            heif_channel channel, bool skip_alpha) {
 if (original.get_bits_per_pixel(channel) <= 8) {
    return GetPsnr<uint8_t>(original, compressed, channel, skip_alpha);
 } else {
    return GetPsnr<uint16_t>(original, compressed, channel, skip_alpha);
 }
}

std::vector<Plane> GetPlanes(const ColorState& state, int width, int height) {
  std::vector<Plane> planes;
  if (state.colorspace == heif_colorspace_monochrome) {
    if (state.chroma != heif_chroma_monochrome) return {};
    planes.push_back({heif_channel_Y, width, height, state.bits_per_pixel});
    if (state.has_alpha) {
      planes.push_back(
          {heif_channel_Alpha, width, height, state.bits_per_pixel});
    }
  } else if (state.colorspace == heif_colorspace_YCbCr) {
    if (state.chroma != heif_chroma_444 && state.chroma != heif_chroma_422 &&
        state.chroma != heif_chroma_420 && state.chroma != heif_chroma_monochrome) {
      return {};
    }
    planes.push_back({heif_channel_Y, width, height, state.bits_per_pixel});
    if (state.chroma != heif_chroma_monochrome) {
      int chroma_width = state.chroma == heif_chroma_444 ? width : width / 2;
      int chroma_height =
          state.chroma == heif_chroma_444 || state.chroma == heif_chroma_422
              ? height
              : height / 2;
      planes.push_back(
          {heif_channel_Cb, chroma_width, chroma_height, state.bits_per_pixel});
      planes.push_back(
          {heif_channel_Cr, chroma_width, chroma_height, state.bits_per_pixel});
    }
    if (state.has_alpha) {
      planes.push_back(
          {heif_channel_Alpha, width, height, state.bits_per_pixel});
    }
  } else if (state.colorspace == heif_colorspace_RGB) {
    if (state.chroma != heif_chroma_444 &&
        state.chroma != heif_chroma_interleaved_RGB &&
        state.chroma != heif_chroma_interleaved_RGBA &&
        state.chroma != heif_chroma_interleaved_RRGGBB_BE &&
        state.chroma != heif_chroma_interleaved_RRGGBBAA_BE &&
        state.chroma != heif_chroma_interleaved_RRGGBB_LE &&
        state.chroma != heif_chroma_interleaved_RRGGBBAA_LE) {
      return {};
    }
    if (state.chroma == heif_chroma_444) {
      planes.push_back({heif_channel_R, width, height, state.bits_per_pixel});
      planes.push_back({heif_channel_G, width, height, state.bits_per_pixel});
      planes.push_back({heif_channel_B, width, height, state.bits_per_pixel});
      if (state.has_alpha) {
        planes.push_back(
            {heif_channel_Alpha, width, height, state.bits_per_pixel});
      }
    } else {
      planes.push_back(
          {heif_channel_interleaved, width, height, state.bits_per_pixel});
    }
  } else {
    return {};  // Unsupported colorspace.
  }
  return planes;
}

bool MakeTestImage(const ColorState& state, int width, int height,
                   HeifPixelImage* image) {
  image->create(width, height, state.colorspace, state.chroma);
  std::vector<Plane> planes = GetPlanes(state, width, height);
  if (planes.empty()) return false;
  for (size_t i = 0; i < planes.size(); ++i) {
    const Plane& plane = planes[i];
    int half_max = (1 << (plane.bit_depth -1));
    uint16_t value = SwapBytesIfNeeded(static_cast<uint16_t>(half_max + i * 10 + i), state.chroma);
    image->fill_new_plane(plane.channel, value, plane.width, plane.height,
                          plane.bit_depth);
  }
  return true;
}

void TestConversion(const std::string& test_name, const ColorState& input_state,
                    const ColorState& target_state,
                    const heif_color_conversion_options& options) {
  INFO(test_name);
  printf("\n\n%s\n", test_name.c_str());

  ColorConversionPipeline pipeline;
  REQUIRE(pipeline.construct_pipeline(input_state, target_state, options));

  auto in_image = std::make_shared<heif::HeifPixelImage>();
  // Width and height are multiples of 4.
  int width = 12;
  int height = 8; 
  REQUIRE(MakeTestImage(input_state, width, height, in_image.get()));

  std::shared_ptr<HeifPixelImage> out_image =
      pipeline.convert_image(in_image);

  REQUIRE(out_image != nullptr);
  CHECK(out_image->get_colorspace() == target_state.colorspace);
  CHECK(out_image->get_chroma_format() == target_state.chroma);
  CHECK(out_image->has_alpha() == target_state.has_alpha);
  for (const Plane& plane : GetPlanes(target_state, width, height)) {
    INFO("Plane " << plane.channel);
    int stride;
    CHECK(out_image->get_plane(plane.channel, &stride) != nullptr);
    CHECK(out_image->get_bits_per_pixel(plane.channel) ==
          target_state.bits_per_pixel);
  }

  // Convert back in the other direction (if supported).
  ColorConversionPipeline reverse_pipeline;
  if (reverse_pipeline.construct_pipeline(target_state, input_state,
                                          options)) {
    std::shared_ptr<HeifPixelImage> recovered_image =
        reverse_pipeline.convert_image(out_image);
    REQUIRE(recovered_image != nullptr);
    bool skip_alpha = !input_state.has_alpha || !target_state.has_alpha;
    double expected_psnr = input_state.colorspace == target_state.colorspace ? 100. : 45.;
    for (const Plane& plane : GetPlanes(input_state, width, height)) {
      if (skip_alpha && plane.channel == heif_channel_Alpha) continue;
      INFO("Plane " << plane.channel);
      double psnr = GetPsnr(*in_image, *recovered_image, plane.channel, skip_alpha);
      CHECK(psnr >= expected_psnr);
    }
  } else {
    WARN("no reverse conversion available to test round trip");
  }
}

void TestFailingConversion(const std::string& test_name,
                           const ColorState& input_state,
                           const ColorState& target_state,
                           const heif_color_conversion_options& options = {}) {
  ColorConversionPipeline pipeline;
  REQUIRE_FALSE(
      pipeline.construct_pipeline(input_state, target_state, options));
}

TEST_CASE("Color conversion", "[heif_image]") {
  heif_color_conversion_options basic_options = {
      .preferred_chroma_downsampling_algorithm =
          heif_chroma_downsampling_average,
      .preferred_chroma_upsampling_algorithm = heif_chroma_upsampling_bilinear,
      .only_use_preferred_chroma_algorithm = false};

  TestConversion("### RGB planes -> interleaved",
                 {heif_colorspace_RGB, heif_chroma_444, false, 8},
                 {heif_colorspace_RGB, heif_chroma_interleaved_RGB, false, 8},
                 basic_options);

  TestConversion("### YCbCr420 -> RGB",
                 {heif_colorspace_YCbCr, heif_chroma_420, false, 8},
                 {heif_colorspace_RGB, heif_chroma_interleaved_RGB, false, 8},
                 basic_options);

  TestConversion("### YCbCr420 -> RGBA",
                 {heif_colorspace_YCbCr, heif_chroma_420, false, 8},
                 {heif_colorspace_RGB, heif_chroma_interleaved_RGBA, true, 8},
                 basic_options);

  TestConversion("### YCbCr420 10bit -> RGB planes 10bit",
                 {heif_colorspace_YCbCr, heif_chroma_420, false, 10},
                 {heif_colorspace_RGB, heif_chroma_444, false, 10},
                 basic_options);

  TestConversion(
      "### RGB planes 10bit -> interleaved big endian",
      {heif_colorspace_RGB, heif_chroma_444, false, 10},
      {heif_colorspace_RGB, heif_chroma_interleaved_RRGGBBAA_BE, true, 10},
      basic_options);

  TestConversion(
      "### RGB planes 10bit -> interleaved little endian",
      {heif_colorspace_RGB, heif_chroma_444, false, 10},
      {heif_colorspace_RGB, heif_chroma_interleaved_RRGGBBAA_LE, true, 10},
      basic_options);

  TestConversion("### monochrome colorspace -> interleaved RGB",
                 {heif_colorspace_monochrome, heif_chroma_monochrome, false, 8},
                 {heif_colorspace_RGB, heif_chroma_interleaved_RGB, false, 8},
                 basic_options);

  TestConversion("### monochrome colorspace -> RGBA",
                 {heif_colorspace_monochrome, heif_chroma_monochrome, false, 8},
                 {heif_colorspace_RGB, heif_chroma_interleaved_RGBA, true, 8},
                 basic_options);

  TestConversion("### interleaved RGBA -> YCbCr 420",
                 {heif_colorspace_RGB, heif_chroma_interleaved_RGBA, true, 8},
                 {heif_colorspace_YCbCr, heif_chroma_420, false, 8},
                 basic_options);

  heif_color_conversion_options sharp_yuv_options{
      .preferred_chroma_downsampling_algorithm =
          heif_chroma_downsampling_sharp_yuv,
      .preferred_chroma_upsampling_algorithm = heif_chroma_upsampling_bilinear,
      .only_use_preferred_chroma_algorithm = true};

#ifdef HAVE_LIBSHARPYUV
  TestConversion("### interleaved RGB -> YCbCr 420 with sharp yuv",
                 {heif_colorspace_RGB, heif_chroma_interleaved_RGBA, false, 8},
                 {heif_colorspace_YCbCr, heif_chroma_420, false, 8},
                 sharp_yuv_options);
  TestConversion("### interleaved RGBA -> YCbCr 420 with sharp yuv",
                 {heif_colorspace_RGB, heif_chroma_interleaved_RGBA, true, 8},
                 {heif_colorspace_YCbCr, heif_chroma_420, true, 8},
                 sharp_yuv_options);
  TestConversion("### interleaved RGB 10bit -> YCbCr 420 10bit with sharp yuv",
                 {heif_colorspace_RGB, heif_chroma_interleaved_RGB, false, 8},
                 {heif_colorspace_YCbCr, heif_chroma_420, false, 8},
                 sharp_yuv_options);

  TestConversion("### interleaved RGBA 12bit big endian -> YCbCr 420 12bit with sharp yuv",
                 {heif_colorspace_RGB, heif_chroma_interleaved_RRGGBBAA_BE, true, 12},
                 {heif_colorspace_YCbCr, heif_chroma_420, true, 12},
                 sharp_yuv_options);
  TestConversion("### interleaved RGBA 12bit little endian -> YCbCr 420 12bit with sharp yuv",
                 {heif_colorspace_RGB, heif_chroma_interleaved_RRGGBBAA_LE, true, 12},
                 {heif_colorspace_YCbCr, heif_chroma_420, true, 12},
                 sharp_yuv_options);
  TestConversion("### planar RGB -> YCbCr 420 with sharp yuv",
                 {heif_colorspace_RGB, heif_chroma_444, false, 8},
                 {heif_colorspace_YCbCr, heif_chroma_420, false, 8},
                 sharp_yuv_options);
  TestConversion("### planar RGBA -> YCbCr 420 with sharp yuv",
                 {heif_colorspace_RGB, heif_chroma_444, true, 8},
                 {heif_colorspace_YCbCr, heif_chroma_420, true, 8},
                 sharp_yuv_options);
  TestConversion("### planar RGB 10bit -> YCbCr 420 10bit with sharp yuv",
                 {heif_colorspace_RGB, heif_chroma_444, false, 10},
                 {heif_colorspace_YCbCr, heif_chroma_420, false, 10},
                 sharp_yuv_options);
  TestConversion("### planar RGBA 10bit -> YCbCr 420 10bit with sharp yuv",
                 {heif_colorspace_RGB, heif_chroma_444, true, 10},
                 {heif_colorspace_YCbCr, heif_chroma_420, true, 10},
                 sharp_yuv_options);
#else
  // Should fail if libsharpyuv is not compiled in.
  TestFailingConversion(
      "### interleaved RGBA -> YCbCr 420 with sharp yuv NOT COMPILED IN",
      {heif_colorspace_RGB, heif_chroma_interleaved_RGBA, true, 8},
      {heif_colorspace_YCbCr, heif_chroma_420, false, 8}, sharp_yuv_options);
#endif

  TestFailingConversion(
      "### interleaved RGBA -> YCbCr 422 with sharp yuv (not supported!)",
      {heif_colorspace_RGB, heif_chroma_interleaved_RGBA, true, 8},
      {heif_colorspace_YCbCr, heif_chroma_422, false, 8}, sharp_yuv_options);

  TestConversion(
      "### RGB planes 10bit -> interleaved RGB 10bit little endian",
      {heif_colorspace_RGB, heif_chroma_444, false, 10},
      {heif_colorspace_RGB, heif_chroma_interleaved_RRGGBB_LE, false, 10},
      basic_options);

  TestConversion(
      "### interleaved RGB 12bit little endian -> RGB planes 12bit",
      {heif_colorspace_RGB, heif_chroma_interleaved_RRGGBB_LE, false, 12},
      {heif_colorspace_RGB, heif_chroma_444, false, 12}, basic_options);

  TestConversion("### RGB planes 12bit -> YCbCr 420 12bit",
                 {heif_colorspace_RGB, heif_chroma_444, false, 12},
                 {heif_colorspace_YCbCr, heif_chroma_420, false, 12},
                 basic_options);

  TestConversion(
      "### interleaved RGB 12bit big endian -> YCbCr 420 12bit",
      {heif_colorspace_RGB, heif_chroma_interleaved_RRGGBB_BE, false, 12},
      {heif_colorspace_YCbCr, heif_chroma_420, false, 12}, basic_options);

  TestConversion(
      "### interleaved RGB 12bit little endian -> YCbCr 420 12bit",
      {heif_colorspace_RGB, heif_chroma_interleaved_RRGGBB_LE, false, 12},
      {heif_colorspace_YCbCr, heif_chroma_420, false, 12}, basic_options);

  TestConversion("### monochrome YCbCr -> interleaved RGB",
                 {heif_colorspace_YCbCr, heif_chroma_monochrome, false, 8},
                 {heif_colorspace_RGB, heif_chroma_interleaved_RGB, false, 8},
                 basic_options);

  TestConversion("### monochrome YCbCr -> YCbCr with alpha",
                 {heif_colorspace_monochrome, heif_chroma_monochrome, false, 8},
                 {heif_colorspace_YCbCr, heif_chroma_444, true, 8},
                 basic_options);

  TestConversion("### monochrome YCbCr -> YCbCr 420",
                 {heif_colorspace_monochrome, heif_chroma_monochrome, false, 8},
                 {heif_colorspace_YCbCr, heif_chroma_420, false, 8},
                 basic_options);

  TestConversion("### monochrome YCbCr -> YCbCr 420 with alpha",
                 {heif_colorspace_monochrome, heif_chroma_monochrome, false, 8},
                 {heif_colorspace_YCbCr, heif_chroma_420, true, 8},
                 basic_options);
}
