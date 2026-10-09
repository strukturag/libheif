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

// Benchmark for HeifPixelImage::rotate_ccw(). Rotates whole 4:2:0 images at 8 and 10 bit,
// including the allocation of the output image, with the planes evicted from the caches before
// every run, and prints the median and the minimum time per configuration as a Markdown table.
//
// This is not a test. It needs a Release build with the internal symbols visible:
//
//   cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DWITH_REDUCED_VISIBILITY=OFF -DBUILD_TESTING=ON
//   cmake --build build-bench --target rotate_benchmark
//   build-bench/tests/rotate_benchmark [rounds] [eviction buffer in MB]
//
// Whole-image timings drift by a few tenths of a millisecond between runs. To compare two versions
// of the library, build both and run their benchmarks alternately a few times rather than one
// after the other.

#include "image/pixelimage.h"
#include "libheif/heif.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>


template<typename T>
static void fill_plane(HeifPixelImage& img, heif_channel ch)
{
  uint32_t w = img.get_width(ch);
  uint32_t h = img.get_height(ch);
  size_t stride;
  T* p = img.get_channel_memory<T>(ch, &stride);
  stride /= sizeof(T);
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      p[y * stride + x] = static_cast<T>((x * 2654435761U) ^ (y * 40503U));
    }
  }
}


static std::shared_ptr<HeifPixelImage> make_image(uint32_t w, uint32_t h, int bits,
                                                  const heif_security_limits* limits)
{
  auto img = std::make_shared<HeifPixelImage>();
  img->create(w, h, heif_colorspace_YCbCr, heif_chroma_420);
  for (heif_channel ch : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}) {
    uint32_t cw = ch == heif_channel_Y ? w : (w + 1) / 2;
    uint32_t chh = ch == heif_channel_Y ? h : (h + 1) / 2;
    if (img->add_channel(ch, cw, chh, bits, limits, heif_component_datatype_unsigned_integer)) {
      return nullptr;
    }
    if (bits <= 8) {
      fill_plane<uint8_t>(*img, ch);
    }
    else {
      fill_plane<uint16_t>(*img, ch);
    }
  }
  return img;
}


int main(int argc, char** argv)
{
  int rounds = argc > 1 ? atoi(argv[1]) : 9;
  int evict_mb = argc > 2 ? atoi(argv[2]) : 256;
  if (rounds < 1 || evict_mb < 1) {
    fprintf(stderr, "usage: %s [rounds] [eviction buffer in MB]\n", argv[0]);
    return 1;
  }

  std::vector<uint8_t> evict(static_cast<size_t>(evict_mb) << 20);
  const heif_security_limits* limits = heif_get_global_security_limits();

  const uint32_t sizes[][2] = {{4032, 3024}, {4096, 3072}, {2048, 1536}};

  printf("| image | luma stride | angle | median | min |\n");
  printf("|---|---|---|---|---|\n");

  for (auto& size : sizes) {
    for (int bits : {8, 10}) {
      auto img = make_image(size[0], size[1], bits, limits);
      if (!img) {
        fprintf(stderr, "cannot create a %ux%u %d-bit image\n", size[0], size[1], bits);
        return 1;
      }

      size_t stride;
      img->get_channel_memory(heif_channel_Y, &stride);

      for (int angle : {90, 180, 270}) {
        std::vector<double> ms;
        for (int round = 0; round < rounds; round++) {
          memset(evict.data(), round, evict.size());

          auto t0 = std::chrono::steady_clock::now();
          auto result = img->rotate_ccw(angle, limits);
          auto t1 = std::chrono::steady_clock::now();

          if (!result) {
            fprintf(stderr, "rotation failed: %s\n", result.error().message.c_str());
            return 1;
          }
          ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }

        std::sort(ms.begin(), ms.end());
        printf("| %ux%u 4:2:0 %d-bit | %zu | %d | %.2f ms | %.2f ms |\n",
               size[0], size[1], bits, stride, angle, ms[ms.size() / 2], ms[0]);
      }
    }
  }

  return 0;
}
