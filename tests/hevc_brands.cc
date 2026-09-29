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

// ISO/IEC 23008-12 has brands for HEVC images in the Main and Main Still Picture profiles
// ('heic', for sequences 'hevc') and in the Main 10 and format range extensions profiles
// ('heix', 'hevx'). It has none for the other profiles of HEVC, like the high throughput
// profiles and the screen content coding profiles. libheif wrote 'heix' and 'hevx' for them.
//
// No encoder of libheif may be able to write these profiles, and the brands only depend
// on the profile in the 'hvcC' box. So the tests change the profile in the 'hvcC' box of a
// file and compute the brands that libheif would write for it.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "brands.h"
#include "context.h"
#include "test_utils.h"
#include "test-config.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

const int PROFILE_MAIN = 1;
const int PROFILE_MAIN_10 = 2;
const int PROFILE_MAIN_STILL_PICTURE = 3;
const int PROFILE_FORMAT_RANGE_EXTENSIONS = 4;
const int PROFILE_HIGH_THROUGHPUT = 5;
const int PROFILE_SCREEN_CONTENT_CODING = 9;
const int PROFILE_HIGH_THROUGHPUT_SCREEN_CONTENT_CODING = 11;

struct profile_brands
{
  int profile_idc;
  heif_brand2 image_brand;    // 0: there is no brand for this profile
  heif_brand2 sequence_brand;
};

const profile_brands all_profiles[] = {
  {PROFILE_MAIN,                                  heif_brand2_heic, heif_brand2_hevc},
  {PROFILE_MAIN_STILL_PICTURE,                    heif_brand2_heic, heif_brand2_hevc},
  {PROFILE_MAIN_10,                               heif_brand2_heix, heif_brand2_hevx},
  {PROFILE_FORMAT_RANGE_EXTENSIONS,               heif_brand2_heix, heif_brand2_hevx},
  {PROFILE_HIGH_THROUGHPUT,                       0,                0},
  {PROFILE_SCREEN_CONTENT_CODING,                 0,                0},
  {PROFILE_HIGH_THROUGHPUT_SCREEN_CONTENT_CODING, 0,                0}
};


// Sets general_profile_idc in all 'hvcC' boxes of the file, and the compatibility flag
// of this profile as the only one.
void set_hevc_profile(std::vector<uint8_t>& file, int profile_idc)
{
  static const uint8_t fourcc[4] = {'h', 'v', 'c', 'C'};

  int num_boxes = 0;

  auto box = std::search(file.begin(), file.end(), fourcc, fourcc + 4);
  while (box != file.end()) {
    REQUIRE(file.end() - box > 10);

    // HEVCDecoderConfigurationRecord: configurationVersion, then the profile
    auto record = box + 4;
    record[1] = static_cast<uint8_t>((record[1] & 0xE0) | profile_idc);

    const uint32_t compatibility_flags = uint32_t{1} << (31 - profile_idc);
    for (int i = 0; i < 4; i++) {
      record[2 + i] = static_cast<uint8_t>(compatibility_flags >> (24 - 8 * i));
    }

    num_boxes++;
    box = std::search(box + 4, file.end(), fourcc, fourcc + 4);
  }

  REQUIRE(num_boxes > 0);
}


struct Brands
{
  heif_brand2 main_brand = 0;
  std::vector<heif_brand2> compatible_brands;

  bool has(heif_brand2 brand) const
  {
    return std::find(compatible_brands.begin(), compatible_brands.end(), brand) != compatible_brands.end();
  }
};

Brands brands_for(const std::vector<uint8_t>& file)
{
  HeifContext ctx;
  Error err = ctx.read_from_memory(file.data(), file.size(), false);
  INFO("read error: " << err.message);
  REQUIRE(!err);

  Brands brands;
  brands.compatible_brands = compute_compatible_brands(&ctx, &brands.main_brand);
  return brands;
}


heif_error write_to_vector(heif_context*, const void* data, size_t size, void* userdata)
{
  auto* file = static_cast<std::vector<uint8_t>*>(userdata);
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  file->insert(file->end(), bytes, bytes + size);
  return heif_error{heif_error_Ok, heif_suberror_Unspecified, nullptr};
}

std::vector<uint8_t> encode_hevc_sequence()
{
  const uint16_t size = 64;

  heif_context* ctx = heif_context_alloc();

  heif_encoder* encoder = nullptr;
  heif_error err = heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);
  REQUIRE(err.code == heif_error_Ok);

  heif_track* track = nullptr;
  err = heif_context_add_visual_sequence_track(ctx, size, size, heif_track_type_image_sequence,
                                               nullptr, nullptr, &track);
  REQUIRE(err.code == heif_error_Ok);

  for (int frame = 0; frame < 2; frame++) {
    heif_image* img = nullptr;
    err = heif_image_create(size, size, heif_colorspace_YCbCr, heif_chroma_420, &img);
    REQUIRE(err.code == heif_error_Ok);

    for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}) {
      const uint32_t plane_size = (channel == heif_channel_Y ? size : size / 2);
      err = heif_image_add_plane(img, channel, plane_size, plane_size, 8);
      REQUIRE(err.code == heif_error_Ok);

      size_t stride = 0;
      uint8_t* p = heif_image_get_plane2(img, channel, &stride);
      for (uint32_t y = 0; y < plane_size; y++) {
        std::fill(p + y * stride, p + y * stride + plane_size, static_cast<uint8_t>(100 + 50 * frame));
      }
    }

    heif_image_set_duration(img, 1);

    err = heif_track_encode_sequence_image(track, img, encoder, nullptr);
    INFO("encoding error: " << (err.message ? err.message : ""));
    REQUIRE(err.code == heif_error_Ok);

    heif_image_release(img);
  }

  err = heif_track_encode_end_of_sequence(track, encoder);
  REQUIRE(err.code == heif_error_Ok);

  std::vector<uint8_t> file;
  heif_writer writer{};
  writer.writer_api_version = 1;
  writer.write = write_to_vector;
  err = heif_context_write(ctx, &writer, &file);
  REQUIRE(err.code == heif_error_Ok);

  heif_track_release(track);
  heif_encoder_release(encoder);
  heif_context_free(ctx);

  return file;
}

} // namespace


TEST_CASE("brand of an HEVC image follows its profile")
{
  std::ifstream istr(tests_data_directory + "/rainbow-451x461.heic", std::ios::binary);
  REQUIRE(istr.good());
  const std::vector<uint8_t> original{std::istreambuf_iterator<char>(istr), std::istreambuf_iterator<char>()};

  for (const profile_brands& profile : all_profiles) {
    INFO("general_profile_idc " << profile.profile_idc);

    std::vector<uint8_t> file = original;
    set_hevc_profile(file, profile.profile_idc);

    const Brands brands = brands_for(file);

    CHECK(brands.has(heif_brand2_mif1));
    CHECK(brands.has(heif_brand2_heic) == (profile.image_brand == heif_brand2_heic));
    CHECK(brands.has(heif_brand2_heix) == (profile.image_brand == heif_brand2_heix));

    // Without a brand for the image, what remains is the structural brand.
    CHECK(brands.main_brand == (profile.image_brand ? profile.image_brand : heif_brand2_mif1));
  }
}


TEST_CASE("brand of an HEVC image sequence follows its profile")
{
  if (!heif_have_encoder_for_format(heif_compression_HEVC)) {
    SKIP("no HEVC encoder available");
  }

  const std::vector<uint8_t> original = encode_hevc_sequence();

  for (const profile_brands& profile : all_profiles) {
    INFO("general_profile_idc " << profile.profile_idc);

    std::vector<uint8_t> file = original;
    set_hevc_profile(file, profile.profile_idc);

    const Brands brands = brands_for(file);

    CHECK(brands.has(heif_brand2_msf1));
    CHECK(brands.has(heif_brand2_hevc) == (profile.sequence_brand == heif_brand2_hevc));
    CHECK(brands.has(heif_brand2_hevx) == (profile.sequence_brand == heif_brand2_hevx));

    CHECK(brands.main_brand == (profile.sequence_brand ? profile.sequence_brand : heif_brand2_msf1));
  }
}
