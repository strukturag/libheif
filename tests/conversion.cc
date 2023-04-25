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

#include <iomanip>
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
  return (uint16_t)((lo << 8) | hi);
}

uint16_t SwapBytesIfNeeded(uint16_t v, heif_chroma chroma) {
  return EndiannessMatchesPlatform(chroma) ? v : SwapBytes(v);
}

template <typename T>
std::string PrintChannel(const HeifPixelImage& image, heif_channel channel) {
  heif_chroma chroma = image.get_chroma_format();
  int num_interleaved = num_interleaved_pixels_per_plane(chroma);
  bool is_interleaved = num_interleaved > 1;
  int max_cols = is_interleaved ? 3 : 10;
  int max_rows = 10;
  int width = std::min(image.get_width(channel), max_cols);
  int height = std::min(image.get_height(channel), max_rows);
  int stride;
  const T* p = (T*)image.get_plane(channel, &stride);
  stride /= sizeof(T);
  int digits = (int)std::ceil(std::log10(1 << image.get_bits_per_pixel(channel))) + 1;

  std::ostringstream os;
  os << std::string(digits, ' ');
  int header_width = digits * num_interleaved - 1 + (is_interleaved ? 3 : 0);
  for (int x = 0; x < width; ++x) {
    os << "|" << std::left << std::setw(header_width) << std::to_string(x);
  }
  os << "\n";
  for (int y = 0; y < height; ++y) {
    os << std::left << std::setw(digits) << std::to_string(y) << "|";
    for (int x = 0; x < width; ++x) {
      if (is_interleaved) os << "(";
      for (int k = 0; k < num_interleaved; ++k) {
        int v = SwapBytesIfNeeded(p[y * stride + x * num_interleaved + k], chroma);
        os << std::left << std::setw(digits) << std::to_string(v);
      }
      if (is_interleaved) os << ") ";
    }
    os << "\n";
  }
  return os.str();
}

std::string PrintChannel(const HeifPixelImage& image, heif_channel channel) {
 if (image.get_bits_per_pixel(channel) <= 8) {
    return PrintChannel<uint8_t>(image, channel);
 } else {
    return PrintChannel<uint16_t>(image, channel);
 }
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
    uint16_t value = SwapBytesIfNeeded(
        static_cast<uint16_t>(half_max + i * 10 + i), state.chroma);
    image->fill_new_plane(plane.channel, value, plane.width, plane.height,
                          plane.bit_depth);
  }
  return true;
}

void TestConversion(const std::string& test_name, const ColorState& input_state,
                    const ColorState& target_state,
                    const heif_color_conversion_options& options) {
  INFO(test_name);

  ColorConversionPipeline pipeline;
  REQUIRE(pipeline.construct_pipeline(input_state, target_state, options));
  INFO(pipeline.debug_dump_pipeline());

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
    INFO("Channel: " << plane.channel);
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
    bool expect_lossless =
        input_state.colorspace == target_state.colorspace &&
        input_state.bits_per_pixel == target_state.bits_per_pixel &&
        (input_state.chroma == target_state.chroma ||
         (input_state.chroma != heif_chroma_420 &&
          input_state.chroma != heif_chroma_422 &&
          target_state.chroma != heif_chroma_420 &&
          target_state.chroma != heif_chroma_422));
    double expected_psnr = expect_lossless ? 100. : 40.;

    for (const Plane& plane : GetPlanes(input_state, width, height)) {
      if (skip_alpha && plane.channel == heif_channel_Alpha) continue;
      INFO("Channel: " << plane.channel);
      INFO("Original:\n" << PrintChannel(*in_image, plane.channel));
      INFO("Recovered:\n" << PrintChannel(*recovered_image, plane.channel));
      for (const Plane& converted_plane :
           GetPlanes(target_state, width, height)) {
        UNSCOPED_INFO("Converted channel "
                      << converted_plane.channel << ":\n"
                      << PrintChannel(*out_image, converted_plane.channel));
      }
      double psnr = GetPsnr(*in_image, *recovered_image, plane.channel, skip_alpha);
      CHECK(psnr >= expected_psnr);
    }
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

// Returns the list of valid heif_chroma values for a given colorspace.
std::vector<heif_chroma> GetValidChroma(heif_colorspace colorspace) {
  switch (colorspace) {
    case heif_colorspace_YCbCr:
      return {heif_chroma_420, heif_chroma_422, heif_chroma_444};
    case heif_colorspace_RGB:
      return {heif_chroma_444,
              heif_chroma_interleaved_RGB,
              heif_chroma_interleaved_RGBA,
              heif_chroma_interleaved_RRGGBB_BE,
              heif_chroma_interleaved_RRGGBBAA_BE,
              heif_chroma_interleaved_RRGGBB_LE,
              heif_chroma_interleaved_RRGGBBAA_LE};
    case heif_colorspace_monochrome:
      return {heif_chroma_monochrome};
    default:
      return {};
  }
}

// Returns the list of valid has_alpha values for a given heif_chroma.
std::vector<bool> GetValidHasAlpha(heif_chroma chroma) {
  switch (chroma) {
    case heif_chroma_monochrome:
    case heif_chroma_420:
    case heif_chroma_422:
    case heif_chroma_444:
      return {false, true};
    case heif_chroma_interleaved_RGB:
    case heif_chroma_interleaved_RRGGBB_BE:
    case heif_chroma_interleaved_RRGGBB_LE:
      return {false};
    case heif_chroma_interleaved_RGBA:
    case heif_chroma_interleaved_RRGGBBAA_BE:
    case heif_chroma_interleaved_RRGGBBAA_LE:
      return {true};
    default:
      return {};
  }
}

// Returns some valid bits per pixels values for a given heif_chroma.
std::vector<int> GetValidBitsPerPixel(heif_chroma chroma) {
  const std::vector<int> sdr = {8};
  const std::vector<int> hdr = {12};
  const std::vector<int> both = {8, 12};
  switch (chroma) {
    case heif_chroma_monochrome:
    case heif_chroma_420:
    case heif_chroma_422:
    case heif_chroma_444:
      return both;
    case heif_chroma_interleaved_RGB:
    case heif_chroma_interleaved_RGBA:
      return sdr;
    case heif_chroma_interleaved_RRGGBB_BE:
    case heif_chroma_interleaved_RRGGBB_LE:
    case heif_chroma_interleaved_RRGGBBAA_BE:
    case heif_chroma_interleaved_RRGGBBAA_LE:
      return hdr;
    default:
      return {};
  }
}

// Returns of list of all valid ColorState (valid combinations
// of a heif_colorspace/heif_chroma/has_alpha/bpp).
std::vector<ColorState> GetAllColorStates() {
  std::vector<ColorState> color_states;
  for (heif_colorspace colorspace : {heif_colorspace_YCbCr, heif_colorspace_RGB, heif_colorspace_monochrome}) {
    for (heif_chroma chroma : GetValidChroma(colorspace)) {
      for (bool has_alpha : GetValidHasAlpha(chroma)) {
        for (int bits_per_pixel : GetValidBitsPerPixel(chroma)) {
          ColorState color_state(colorspace, chroma, has_alpha, bits_per_pixel);
          color_states.push_back(color_state);
        }
      }
    }
  }
  return color_states;
}

TEST_CASE("All conversions", "[heif_image]") {
  heif_color_conversion_options basic_options = {
      .preferred_chroma_downsampling_algorithm =
          heif_chroma_downsampling_average,
      .preferred_chroma_upsampling_algorithm = heif_chroma_upsampling_bilinear,
      .only_use_preferred_chroma_algorithm = false};

  // Test all source and destination state combinations.
  ColorState src_state = GENERATE(from_range(GetAllColorStates()));
  ColorState dst_state = GENERATE(from_range(GetAllColorStates()));
  // To debug a particular combination, hardcoe the ColorState values
  // instead:
  // ColorState src_state(heif_colorspace_YCbCr, heif_chroma_420, false, 8);
  // ColorState dst_state(...);

  // Converting to monochrome is not supported at the moment.
  if (dst_state.colorspace == heif_colorspace_monochrome ||
      dst_state.chroma == heif_chroma_monochrome)
    return;

  std::ostringstream os;
  os << "from: " << src_state << "\nto:   " << dst_state;
  TestConversion(os.str(), src_state, dst_state, basic_options);
}

TEST_CASE("Sharp yuv conversion", "[heif_image]") {
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
}
