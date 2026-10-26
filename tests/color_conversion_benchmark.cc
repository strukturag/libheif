/*
  libheif benchmarks

  MIT License

  Copyright (c) 2026 OlJohnny

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

// Portable benchmark, not a test. Build with internal symbols visible:
//
//   cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DWITH_REDUCED_VISIBILITY=OFF -DBUILD_TESTING=ON
//   cmake --build build-bench --target color_conversion_benchmark
//   build-bench/tests/color_conversion_benchmark synthetic [rounds] [RGBA output directory]
//   build-bench/tests/color_conversion_benchmark full filename [rounds] [RGBA output file]
//   build-bench/tests/color_conversion_benchmark convert filename [rounds] [RGBA output file]
//
// Prints individual timings as CSV and median/sample SD to stderr. One warmup is excluded.
// Synthetic inputs need no decoder or sample files. 'full' measures HEVC decoding, transforms
// and RGBA conversion; 'convert' uses already decoded, oriented YUV. Reads, parsing, input
// preparation and optional packed RGBA output are outside timing. Output files allow byte
// comparisons without a hashing dependency. Alternate builds between blocks of measurements.

#include "api_structs.h"
#include "color-conversion/colorconversion.h"
#include "image/pixelimage.h"
#include "libheif/heif.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>


static void check(heif_error error)
{
  if (error.code != heif_error_Ok) {
    throw std::runtime_error(error.message);
  }
}


static std::shared_ptr<HeifPixelImage> make_image(uint32_t width, uint32_t height,
                                                 heif_chroma chroma, bool full_range)
{
  auto image = std::make_shared<HeifPixelImage>();
  image->create(width, height, heif_colorspace_YCbCr, chroma);
  nclx_profile profile = nclx_profile::defaults();
  profile.set_matrix_coefficients(5);
  profile.set_full_range_flag(full_range);
  image->set_color_profile_nclx(profile);
  uint32_t random = 1234567;
  for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}) {
    uint32_t plane_width = channel == heif_channel_Y || chroma == heif_chroma_444
                               ? width : width / 2 + width % 2;
    uint32_t plane_height = channel != heif_channel_Y && chroma == heif_chroma_420
                                ? height / 2 + height % 2 : height;
    if (auto error = image->add_channel(channel, plane_width, plane_height, 10, nullptr)) {
      throw std::runtime_error(error.message);
    }
    size_t stride;
    uint16_t* data = image->get_channel_memory<uint16_t>(channel, &stride);
    stride /= sizeof(uint16_t);
    for (uint32_t y = 0; y < plane_height; y++) {
      for (uint32_t x = 0; x < plane_width; x++) {
        random = random * 1664525U + 1013904223U;
        data[y * stride + x] = static_cast<uint16_t>((random >> 16) & 1023);
      }
    }
  }
  return image;
}


static void write_rgba(const HeifPixelImage& image, const std::filesystem::path& filename)
{
  std::ofstream file(filename, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Cannot open RGBA output");
  }
  size_t stride;
  const uint8_t* data = image.get_channel_memory(heif_channel_interleaved, &stride);
  size_t row_bytes = static_cast<size_t>(image.get_width()) * 4;
  for (uint32_t y = 0; y < image.get_height(); y++) {
    file.write(reinterpret_cast<const char*>(data + y * stride), static_cast<std::streamsize>(row_bytes));
  }
  if (!file) {
    throw std::runtime_error("Cannot write RGBA output");
  }
}


template<class Convert>
static void measure(const std::string& label, const char* mode, int rounds, Convert convert,
                     const std::filesystem::path& output)
{
  std::vector<double> times;
  times.reserve(static_cast<size_t>(rounds));
  for (int round = -1; round < rounds; round++) {
    auto start = std::chrono::steady_clock::now();
    auto image = convert();
    auto end = std::chrono::steady_clock::now();
    if (round >= 0) {
      times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    if (round == -1) {
      fprintf(stderr, "%s: %ux%u RGBA\n", label.c_str(), image->get_width(), image->get_height());
    }
    if (round == rounds - 1 && !output.empty()) {
      write_rgba(*image, output);
    }
  }
  double mean = 0;
  for (double time : times) {
    mean += time;
  }
  mean /= rounds;
  double sum_squares = 0;
  for (double time : times) {
    sum_squares += (time - mean) * (time - mean);
  }
  double sd = std::sqrt(sum_squares / (rounds - 1));
  for (int round = 0; round < rounds; round++) {
    printf("%s,%s,%d,%.9f\n", label.c_str(), mode, round, times[static_cast<size_t>(round)]);
  }
  std::sort(times.begin(), times.end());
  size_t middle = times.size() / 2;
  double median = rounds % 2 ? times[middle] : (times[middle - 1] + times[middle]) / 2;
  fprintf(stderr, "%s %s: median %.3f ms, sample SD %.3f ms, n=%d\n",
          label.c_str(), mode, median, sd, rounds);
}


static void synthetic(int rounds, const std::filesystem::path& output_directory)
{
  struct Sample {
    const char* label;
    uint32_t width, height;
    heif_chroma chroma;
    bool full_range;
  };
  const Sample samples[] = {
      {"422-full", 6192, 4128, heif_chroma_422, true},
      {"422-limited", 6192, 4128, heif_chroma_422, false},
      {"444-full", 4128, 6192, heif_chroma_444, true},
      {"444-limited", 4128, 6192, heif_chroma_444, false},
      {"422-odd", 6193, 4129, heif_chroma_422, true},
      {"420-control", 6192, 4128, heif_chroma_420, true},
      {"420-limited-control", 6192, 4128, heif_chroma_420, false}
  };
  heif_color_conversion_options options{};
  heif_color_conversion_options_set_defaults(&options);
  options.only_use_preferred_chroma_algorithm = false;
  for (const auto& sample : samples) {
    auto input = make_image(sample.width, sample.height, sample.chroma, sample.full_range);
    std::filesystem::path output;
    if (!output_directory.empty()) {
      output = output_directory / (std::string(sample.label) + ".rgba");
    }
    measure(sample.label, "synthetic", rounds, [&]() {
      auto result = convert_colorspace(input, heif_colorspace_RGB, heif_chroma_interleaved_RGBA,
                                       input->get_color_profile_nclx(), 8, options, nullptr, nullptr);
      if (!result) {
        throw std::runtime_error(result.error().message);
      }
      return *result;
    }, output);
  }
}


static void from_file(const char* mode, const std::filesystem::path& filename, int rounds,
                      const std::filesystem::path& output)
{
  std::ifstream file(filename, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Cannot open input");
  }
  std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
  auto context = std::unique_ptr<heif_context, decltype(&heif_context_free)>(heif_context_alloc(), heif_context_free);
  check(heif_context_read_from_memory_without_copy(context.get(), bytes.data(), bytes.size(), nullptr));
  heif_image_handle* raw_handle;
  check(heif_context_get_primary_image_handle(context.get(), &raw_handle));
  auto handle = std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)>(
      raw_handle, heif_image_handle_release);
  std::string label = filename.filename().string();
  // Filenames remain a single CSV field, without imposing a CSV library dependency.
  std::replace(label.begin(), label.end(), ',', '_');
  std::replace(label.begin(), label.end(), '\n', '_');
  std::replace(label.begin(), label.end(), '\r', '_');
  std::replace(label.begin(), label.end(), '"', '_');
  const heif_decoder_descriptor* descriptors[8];
  int count = heif_get_decoder_descriptors(heif_compression_HEVC, descriptors, 8);
  for (int i = 0; i < count; i++) {
    fprintf(stderr, "decoder: %s\n", heif_decoder_descriptor_get_name(descriptors[i]));
  }
  if (std::string(mode) == "full") {
    measure(label, mode, rounds, [&]() {
      heif_image* decoded;
      check(heif_decode_image(handle.get(), &decoded, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, nullptr));
      auto image = decoded->image;
      heif_image_release(decoded);
      return image;
    }, output);
  }
  else {
    auto options = std::unique_ptr<heif_decoding_options, decltype(&heif_decoding_options_free)>(
        heif_decoding_options_alloc(), heif_decoding_options_free);
    options->output_image_nclx_profile_passthrough = true;
    heif_image* raw_native;
    check(heif_decode_image(handle.get(), &raw_native, heif_colorspace_undefined, heif_chroma_undefined, options.get()));
    auto native = std::unique_ptr<heif_image, decltype(&heif_image_release)>(raw_native, heif_image_release);
    options->output_image_nclx_profile_passthrough = false;
    fprintf(stderr, "native: chroma %d, depth %d\n", static_cast<int>(native->image->get_chroma_format()),
            native->image->get_bits_per_pixel(heif_channel_Y));
    measure(label, mode, rounds, [&]() {
      auto result = context->context->convert_to_output_colorspace(
          native->image, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, *options);
      if (!result) {
        throw std::runtime_error(result.error().message);
      }
      return *result;
    }, output);
  }
}


int main(int argc, char** argv)
{
  bool initialized = false;
  try {
    if (argc < 2) {
      throw std::runtime_error("Usage: synthetic [rounds] [output directory] | full/convert filename [rounds] [output file]");
    }
    const std::string mode = argv[1];
    bool generated = mode == "synthetic";
    if ((!generated && mode != "full" && mode != "convert") ||
        (!generated && argc < 3) || argc > (generated ? 4 : 5)) {
      throw std::runtime_error("Invalid mode or arguments");
    }
    int rounds_arg = generated ? 2 : 3;
    int rounds = argc > rounds_arg ? std::stoi(argv[rounds_arg]) : 12;
    if (rounds < 2) {
      throw std::runtime_error("At least two rounds are required for sample standard deviation");
    }
    std::filesystem::path output = argc > rounds_arg + 1 ? argv[rounds_arg + 1] : "";
    check(heif_init(nullptr));
    initialized = true;
    fprintf(stderr, "libheif %s\n", heif_get_version());
    printf("sample,mode,round,milliseconds\n");
    if (generated) {
      synthetic(rounds, output);
    }
    else {
      from_file(argv[1], argv[2], rounds, output);
    }
    heif_deinit();
    return 0;
  }
  catch (const std::exception& error) {
    fprintf(stderr, "%s\n", error.what());
    if (initialized) {
      heif_deinit();
    }
    return 1;
  }
}
