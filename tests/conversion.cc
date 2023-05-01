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
#include "libheif/pixelimage.h"

// Enable for more verbose test output.
#define DEBUG_ME 0

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
  int bpp = image.get_bits_per_pixel(channel);
  int digits = (int)std::ceil(std::log10(1 << bpp)) + 1;

  std::ostringstream os;
  os << "channel=" << channel << " width=" << width << " height=" << height
     << " bpp=" << bpp << "\n";
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

// Returns the PSNR between 'original' and 'compressed' images.
// If expect_alpha_max is true, then alpha values in 'compressed' are expected
// to be equal to (1<<bpp)-1 rather than the alpha value in the original image.
template <typename T>
double GetPsnr(const HeifPixelImage& original, const HeifPixelImage& compressed,
               heif_channel channel, bool expect_alpha_max) {
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
  int alpha_max = (1 << original.get_bits_per_pixel(channel)) - 1;
  CAPTURE(expect_alpha_max);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w * num_interleaved; x++) {
      int orig_v = SwapBytesIfNeeded(orig_p[y * orig_stride + x], chroma);
      if (expect_alpha_max && (channel == heif_channel_Alpha ||
                               ((num_interleaved == 4) && x % 4 == 3))) {
        orig_v = alpha_max;
      }
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
            heif_channel channel, bool expect_alpha_max) {
 if (original.get_bits_per_pixel(channel) <= 8) {
    return GetPsnr<uint8_t>(original, compressed, channel, expect_alpha_max);
 } else {
    return GetPsnr<uint16_t>(original, compressed, channel, expect_alpha_max);
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

// Converts from 'input_state' to 'target_state' and checks that the resulting
// image has the expected shape. If the reverse conversion is also supported,
// does a round-trip back to the original state and checks the PSNR.
// If 'require_supported' is true, checks that the conversion from 'input_state'
// to 'target_state' is supported, otherwise, exists silently for unsupported
// conversions.
void TestConversion(const std::string& test_name, const ColorState& input_state,
                    const ColorState& target_state,
                    const heif_color_conversion_options& options,
                    bool require_supported = true) {
  INFO(test_name);
  INFO("downsampling=" << options.preferred_chroma_downsampling_algorithm
                       << " upsampling="
                       << options.preferred_chroma_upsampling_algorithm
                       << " only_used_preferred="
                       << (bool)options.only_use_preferred_chroma_algorithm);

  ColorConversionPipeline pipeline;
  bool supported = pipeline.construct_pipeline(input_state, target_state, options);
  if (require_supported) REQUIRE(supported);
  if (!supported) return;
  INFO("conversion pipeline: " << pipeline.debug_dump_pipeline());

  auto in_image = std::make_shared<HeifPixelImage>();
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
    // If an alpha plane was created from nothing, check that it's filled
    // with the max alpha value.
    if (plane.channel == heif_channel_Alpha && !input_state.has_alpha) {
      double alpha_psnr = GetPsnr(*out_image, *out_image, heif_channel_Alpha,
                                  /*expect_alpha_max=*/true);
      REQUIRE(alpha_psnr == 100.f);
    }
  }


  // Convert back in the other direction (if supported).
  ColorConversionPipeline reverse_pipeline;
  if (reverse_pipeline.construct_pipeline(target_state, input_state,
                                          options)) {
    INFO("reverse pipeline: " << reverse_pipeline.debug_dump_pipeline());
    std::shared_ptr<HeifPixelImage> recovered_image =
        reverse_pipeline.convert_image(out_image);
    REQUIRE(recovered_image != nullptr);
    // If the alpha plane was lost in the target state, it should come back
    // as the max value for the given bpp, i.e. (1<<bpp)-1
    bool expect_alpha_max = !target_state.has_alpha;
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
      INFO("Channel: " << plane.channel << " (set DEBUG_ME to 1 in the code for more info)");
#if DEBUG_ME
      INFO("Original:\n" << PrintChannel(*in_image, plane.channel));
      INFO("Recovered:\n" << PrintChannel(*recovered_image, plane.channel));
      for (const Plane& converted_plane :
           GetPlanes(target_state, width, height)) {
        UNSCOPED_INFO("Converted channel "
                      << converted_plane.channel << ":\n"
                      << PrintChannel(*out_image, converted_plane.channel));
      }
#endif
      double psnr = GetPsnr(*in_image, *recovered_image, plane.channel, expect_alpha_max);
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
    for (heif_chroma chroma : get_valid_chroma_values_for_colorspace(colorspace)) {
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
  bool only_use_preferred_chroma_algorithm = GENERATE(false, true);
  heif_chroma_downsampling_algorithm downsampling =
      heif_chroma_downsampling_nearest_neighbor;
  heif_chroma_upsampling_algorithm upsampling =
      heif_chroma_upsampling_nearest_neighbor;
  if (only_use_preferred_chroma_algorithm) {
    downsampling = GENERATE(heif_chroma_downsampling_nearest_neighbor,
                            heif_chroma_downsampling_average,
                            heif_chroma_downsampling_sharp_yuv);
    upsampling = GENERATE(heif_chroma_upsampling_nearest_neighbor,
                          heif_chroma_upsampling_bilinear);
  }
  heif_color_conversion_options options = {
      .preferred_chroma_downsampling_algorithm = downsampling,
      .preferred_chroma_upsampling_algorithm = upsampling,
      .only_use_preferred_chroma_algorithm = only_use_preferred_chroma_algorithm};

  // Test all source and destination state combinations.
  ColorState src_state = GENERATE(from_range(GetAllColorStates()));
  ColorState dst_state = GENERATE(from_range(GetAllColorStates()));
  // To debug a particular combination, hardcoe the ColorState values
  // instead:
  // ColorState src_state(heif_colorspace_YCbCr, heif_chroma_420, false, 8);
  // ColorState dst_state(...);

  bool require_supported = true;
  // Converting to monochrome is not supported at the moment.
  if (dst_state.colorspace == heif_colorspace_monochrome ||
      dst_state.chroma == heif_chroma_monochrome) {
    require_supported = false;
  }
  // Some conversions might not be supported when the exact upsampling
  // or downsampling algorithm is specified.
  if (only_use_preferred_chroma_algorithm) {
    require_supported = false;
  }

  std::ostringstream os;
  os << "from: " << src_state << "\nto:   " << dst_state;
  TestConversion(os.str(), src_state, dst_state, options, require_supported);
}

TEST_CASE("Sharp yuv conversion", "[heif_image]") {
  heif_color_conversion_options sharp_yuv_options{
      .preferred_chroma_downsampling_algorithm =
          heif_chroma_downsampling_sharp_yuv,
      .preferred_chroma_upsampling_algorithm = heif_chroma_upsampling_bilinear,
      .only_use_preferred_chroma_algorithm = true};

#ifdef HAVE_LIBSHARPYUV
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
  WARN("Tests built without sharp yuv");
#endif

  TestFailingConversion(
      "### interleaved RGBA -> YCbCr 422 with sharp yuv (not supported!)",
      {heif_colorspace_RGB, heif_chroma_interleaved_RGBA, true, 8},
      {heif_colorspace_YCbCr, heif_chroma_422, false, 8}, sharp_yuv_options);
}


static void fill_plane(std::shared_ptr<HeifPixelImage>& img, heif_channel channel, int w, int h, const std::vector<uint8_t>& pixels)
{
  img->add_plane(channel, w, h, 8);

  int stride;
  uint8_t* p = img->get_plane(channel, &stride);

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      p[y * stride + x] = pixels[y * w + x];
    }
  }
}


static void assert_plane(std::shared_ptr<HeifPixelImage>& img, heif_channel channel, const std::vector<uint8_t>& pixels)
{
  int w = img->get_width(channel);
  int h = img->get_height(channel);

  int stride;
  uint8_t* p = img->get_plane(channel, &stride);

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      REQUIRE((int)p[y * stride + x] == (int)pixels[y * w + x]);
    }
  }
}


TEST_CASE("Bilinear upsampling", "[heif_image]")
{
  heif_color_conversion_options options = {
      .preferred_chroma_upsampling_algorithm = heif_chroma_upsampling_bilinear,
      .only_use_preferred_chroma_algorithm = true};

  std::shared_ptr<HeifPixelImage> img = std::make_shared<HeifPixelImage>();
  img->create(4, 4, heif_colorspace_YCbCr, heif_chroma_420);

  img->fill_new_plane(heif_channel_Y, 128, 4,4, 8);
  fill_plane(img, heif_channel_Cb, 2,2,
             {10, 40,
              100, 240});
  fill_plane(img, heif_channel_Cr, 2, 2,
             {255, 200,
              50, 0});

  std::shared_ptr<HeifPixelImage> out = convert_colorspace(img, heif_colorspace_YCbCr, heif_chroma_444, nullptr, 8, options);

  assert_plane(out, heif_channel_Cb,
               {
                   10, 18, 33, 40,
                   33, 47, 76, 90,
                   78, 106, 162, 190,
                   100, 135, 205, 240
               });


  assert_plane(out, heif_channel_Cr,
               {
                   255, 241, 214, 200,
                   204, 190, 163, 150,
                   101, 88, 63, 50,
                   50, 38, 13, 0
               });
}
