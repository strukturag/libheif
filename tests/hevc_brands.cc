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
// The 'miaf' brand also depends on the profile. A MIAF image item has to conform to a MIAF
// codec profile (ISO/IEC 23000-22, 6.3), and the MIAF profiles for HEVC (A.3 to A.5) list the
// HEVC profiles of their images, which end at 10 bits. libheif wrote 'miaf' for all HEVC
// images. The limits of MIAF for the tier and the level are not checked.
//
// No encoder of libheif may be able to write these profiles, and the brands only depend
// on the profile in the 'hvcC' box. So the tests change the profile in the 'hvcC' box of a
// file and compute the brands that libheif would write for it.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "libheif/heif_tiling.h"
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

struct hevc_profile
{
  const char* name;
  int profile_idc;

  // The constraint flags that tell the format range extensions profiles apart (Table A.2 of
  // ITU-T H.265), from general_max_12bit_constraint_flag to
  // general_lower_bit_rate_constraint_flag.
  const char* constraint_flags;

  heif_brand2 image_brand;    // 0: there is no brand for this profile
  heif_brand2 sequence_brand;
  bool is_miaf_profile;
};

const char* const NO_FLAGS = "000000000";

const heif_brand2 heic = heif_brand2_heic;
const heif_brand2 heix = heif_brand2_heix;
const heif_brand2 hevc = heif_brand2_hevc;
const heif_brand2 hevx = heif_brand2_hevx;

const hevc_profile all_profiles[] = {
  // --- MIAF HEVC Basic profile
  {"Main",                        PROFILE_MAIN,                    NO_FLAGS,    heic, hevc, true},
  {"Main Still Picture",          PROFILE_MAIN_STILL_PICTURE,      NO_FLAGS,    heic, hevc, true},

  // --- MIAF HEVC Advanced profile
  {"Main 10",                     PROFILE_MAIN_10,                 NO_FLAGS,    heix, hevx, true},
  {"Main Intra",                  PROFILE_FORMAT_RANGE_EXTENSIONS, "111110100", heix, hevx, true},
  {"Main 10 Intra",               PROFILE_FORMAT_RANGE_EXTENSIONS, "110110101", heix, hevx, true},
  {"Main 4:2:2 10 Intra",         PROFILE_FORMAT_RANGE_EXTENSIONS, "110100100", heix, hevx, true},

  // --- MIAF HEVC Extended profile
  {"Main 4:4:4",                  PROFILE_FORMAT_RANGE_EXTENSIONS, "111000001", heix, hevx, true},
  {"Main 4:4:4 10",               PROFILE_FORMAT_RANGE_EXTENSIONS, "110000001", heix, hevx, true},
  {"Main 4:4:4 10 Intra",         PROFILE_FORMAT_RANGE_EXTENSIONS, "110000100", heix, hevx, true},
  {"Main 4:4:4 Still Picture",    PROFILE_FORMAT_RANGE_EXTENSIONS, "111000110", heix, hevx, true},
  {"Monochrome",                  PROFILE_FORMAT_RANGE_EXTENSIONS, "111111001", heix, hevx, true},
  {"Monochrome 10",               PROFILE_FORMAT_RANGE_EXTENSIONS, "110111001", heix, hevx, true},

  // --- not listed by MIAF, but subsets of a profile that is
  {"Main 4:4:4 Intra",            PROFILE_FORMAT_RANGE_EXTENSIONS, "111000101", heix, hevx, true},
  {"Main 4:2:2 10",               PROFILE_FORMAT_RANGE_EXTENSIONS, "110100001", heix, hevx, true},

  // --- no MIAF profile: more than 10 bits
  {"Main 12",                     PROFILE_FORMAT_RANGE_EXTENSIONS, "100110001", heix, hevx, false},
  {"Main 12 Intra",               PROFILE_FORMAT_RANGE_EXTENSIONS, "100110100", heix, hevx, false},
  {"Main 4:2:2 12",               PROFILE_FORMAT_RANGE_EXTENSIONS, "100100001", heix, hevx, false},
  {"Main 4:4:4 12 Intra",         PROFILE_FORMAT_RANGE_EXTENSIONS, "100000100", heix, hevx, false},
  {"Main 4:4:4 16 Intra",         PROFILE_FORMAT_RANGE_EXTENSIONS, "000000100", heix, hevx, false},
  {"Main 4:4:4 16 Still Picture", PROFILE_FORMAT_RANGE_EXTENSIONS, "000000110", heix, hevx, false},
  {"Monochrome 12",               PROFILE_FORMAT_RANGE_EXTENSIONS, "100111001", heix, hevx, false},
  {"Monochrome 16",               PROFILE_FORMAT_RANGE_EXTENSIONS, "000111001", heix, hevx, false},

  // --- profiles without a brand of their own and without a MIAF profile
  {"High Throughput 4:4:4 16 Intra",        PROFILE_HIGH_THROUGHPUT,                       "000000100", 0, 0, false},
  {"Screen-Extended Main",                  PROFILE_SCREEN_CONTENT_CODING,                 "111110001", 0, 0, false},
  {"Screen-Extended Main 4:4:4 10",         PROFILE_SCREEN_CONTENT_CODING,                 "110000001", 0, 0, false},
  {"Screen-Extended High Throughput 4:4:4", PROFILE_HIGH_THROUGHPUT_SCREEN_CONTENT_CODING, "111000001", 0, 0, false}
};


// Overwrites the profile in all 'hvcC' boxes of the file. The compatibility flag of the
// profile is the only one that is set.
void set_hevc_profile(std::vector<uint8_t>& file, const hevc_profile& profile)
{
  static const uint8_t fourcc[4] = {'h', 'v', 'c', 'C'};

  int num_boxes = 0;

  auto box = std::search(file.begin(), file.end(), fourcc, fourcc + 4);
  while (box != file.end()) {
    REQUIRE(file.end() - box > 17);

    // HEVCDecoderConfigurationRecord: configurationVersion, then profile_tier_level
    auto record = box + 4;
    record[1] = static_cast<uint8_t>((record[1] & 0xE0) | profile.profile_idc);

    const uint32_t compatibility_flags = uint32_t{1} << (31 - profile.profile_idc);
    for (int i = 0; i < 4; i++) {
      record[2 + i] = static_cast<uint8_t>(compatibility_flags >> (24 - 8 * i));
    }

    // The 48 constraint flags. The first four describe the source and are kept, the
    // next nine are the flags of the format range extensions.
    record[6] &= 0xF0;
    for (int i = 7; i < 12; i++) {
      record[i] = 0;
    }

    for (int i = 0; i < 9; i++) {
      if (profile.constraint_flags[i] == '1') {
        const int bit = 4 + i;
        record[6 + bit / 8] |= static_cast<uint8_t>(0x80 >> (bit % 8));
      }
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

std::vector<uint8_t> write_to_memory(heif_context* ctx)
{
  std::vector<uint8_t> file;
  heif_writer writer{};
  writer.writer_api_version = 1;
  writer.write = write_to_vector;
  heif_error err = heif_context_write(ctx, &writer, &file);
  REQUIRE(err.code == heif_error_Ok);

  return file;
}

heif_image* create_gray_image(uint32_t size, uint8_t value)
{
  heif_image* img = nullptr;
  heif_error err = heif_image_create(size, size, heif_colorspace_YCbCr, heif_chroma_420, &img);
  REQUIRE(err.code == heif_error_Ok);

  for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}) {
    const uint32_t plane_size = (channel == heif_channel_Y ? size : size / 2);
    err = heif_image_add_plane(img, channel, plane_size, plane_size, 8);
    REQUIRE(err.code == heif_error_Ok);

    size_t stride = 0;
    uint8_t* p = heif_image_get_plane2(img, channel, &stride);
    for (uint32_t y = 0; y < plane_size; y++) {
      std::fill(p + y * stride, p + y * stride + plane_size, value);
    }
  }

  return img;
}

// Two tiles next to each other.
std::vector<uint8_t> encode_hevc_grid()
{
  heif_context* ctx = heif_context_alloc();

  heif_encoder* encoder = nullptr;
  heif_error err = heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);
  REQUIRE(err.code == heif_error_Ok);

  heif_image* tiles[2] = {create_gray_image(64, 100), create_gray_image(64, 150)};

  heif_image_handle* handle = nullptr;
  err = heif_context_encode_grid(ctx, tiles, 1, 2, encoder, nullptr, &handle);
  INFO("encoding error: " << (err.message ? err.message : ""));
  REQUIRE(err.code == heif_error_Ok);

  std::vector<uint8_t> file = write_to_memory(ctx);

  heif_image_handle_release(handle);
  heif_image_release(tiles[0]);
  heif_image_release(tiles[1]);
  heif_encoder_release(encoder);
  heif_context_free(ctx);

  return file;
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
    heif_image* img = create_gray_image(size, static_cast<uint8_t>(100 + 50 * frame));
    heif_image_set_duration(img, 1);

    err = heif_track_encode_sequence_image(track, img, encoder, nullptr);
    INFO("encoding error: " << (err.message ? err.message : ""));
    REQUIRE(err.code == heif_error_Ok);

    heif_image_release(img);
  }

  err = heif_track_encode_end_of_sequence(track, encoder);
  REQUIRE(err.code == heif_error_Ok);

  std::vector<uint8_t> file = write_to_memory(ctx);

  heif_track_release(track);
  heif_encoder_release(encoder);
  heif_context_free(ctx);

  return file;
}


void check_image_brands(const std::vector<uint8_t>& original)
{
  for (const hevc_profile& profile : all_profiles) {
    INFO("profile: " << profile.name);

    std::vector<uint8_t> file = original;
    set_hevc_profile(file, profile);

    const Brands brands = brands_for(file);

    CHECK(brands.has(heif_brand2_mif1));
    CHECK(brands.has(heif_brand2_heic) == (profile.image_brand == heif_brand2_heic));
    CHECK(brands.has(heif_brand2_heix) == (profile.image_brand == heif_brand2_heix));
    CHECK(brands.has(heif_brand2_miaf) == profile.is_miaf_profile);

    // Without a brand for the image, what remains is the structural brand.
    CHECK(brands.main_brand == (profile.image_brand ? profile.image_brand : heif_brand2_mif1));
  }
}

} // namespace


TEST_CASE("brands of an HEVC image follow its profile")
{
  std::ifstream istr(tests_data_directory + "/rainbow-451x461.heic", std::ios::binary);
  REQUIRE(istr.good());
  const std::vector<uint8_t> file{std::istreambuf_iterator<char>(istr), std::istreambuf_iterator<char>()};

  check_image_brands(file);
}


TEST_CASE("brands of an HEVC grid image follow the profile of its tiles")
{
  if (!heif_have_encoder_for_format(heif_compression_HEVC)) {
    SKIP("no HEVC encoder available");
  }

  check_image_brands(encode_hevc_grid());
}


TEST_CASE("brand of an HEVC image sequence follows its profile")
{
  if (!heif_have_encoder_for_format(heif_compression_HEVC)) {
    SKIP("no HEVC encoder available");
  }

  const std::vector<uint8_t> original = encode_hevc_sequence();

  for (const hevc_profile& profile : all_profiles) {
    INFO("profile: " << profile.name);

    std::vector<uint8_t> file = original;
    set_hevc_profile(file, profile);

    const Brands brands = brands_for(file);

    CHECK(brands.has(heif_brand2_msf1));
    CHECK(brands.has(heif_brand2_hevc) == (profile.sequence_brand == heif_brand2_hevc));
    CHECK(brands.has(heif_brand2_hevx) == (profile.sequence_brand == heif_brand2_hevx));

    CHECK(brands.main_brand == (profile.sequence_brand ? profile.sequence_brand : heif_brand2_msf1));
  }
}
