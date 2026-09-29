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

// The HM encoder plugin (experimental) encodes the bit depths that x265 and kvazaar do not
// support. These tests encode without loss and compare the decoded samples with the input.
// They are skipped when libheif was built without the HM plugin. The comparison needs
// libde265, the only decoder that handles all bit depths.
//
// The plugin can contain two versions of HM. The tests of the screen content coding tools
// are skipped when the plugin was built without the version that has them. The other tests
// run with whatever version the plugin uses for them.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "test_utils.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

heif_encoder* get_hm_encoder(heif_context* ctx)
{
  const heif_encoder_descriptor* descriptor = nullptr;
  int n = heif_get_encoder_descriptors(heif_compression_HEVC, "hm", &descriptor, 1);
  if (n != 1) {
    return nullptr;
  }

  heif_encoder* encoder = nullptr;
  heif_error err = heif_context_get_encoder(ctx, descriptor, &encoder);
  REQUIRE(err.code == heif_error_Ok);
  return encoder;
}

bool have_hm_encoder()
{
  const heif_encoder_descriptor* descriptor = nullptr;
  return heif_get_encoder_descriptors(heif_compression_HEVC, "hm", &descriptor, 1) == 1;
}

// The screen content coding tools are parameters of the plugin only when it contains a
// version of HM that has them.
bool have_screen_content_coding_tools()
{
  heif_context* ctx = heif_context_alloc();
  heif_encoder* encoder = get_hm_encoder(ctx);
  REQUIRE(encoder != nullptr);

  bool found = false;
  for (const heif_encoder_parameter* const* p = heif_encoder_list_parameters(encoder); *p; p++) {
    if (strcmp(heif_encoder_parameter_get_name(*p), "palette-mode") == 0) {
      found = true;
    }
  }

  heif_encoder_release(encoder);
  heif_context_free(ctx);
  return found;
}

bool have_libde265()
{
  int n = heif_get_decoder_descriptors(heif_compression_HEVC, nullptr, 0);
  std::vector<const heif_decoder_descriptor*> descriptors(n);
  n = heif_get_decoder_descriptors(heif_compression_HEVC, descriptors.data(), n);
  for (int i = 0; i < n; i++) {
    const char* id = heif_decoder_descriptor_get_id_name(descriptors[i]);
    if (id && strcmp(id, "libde265") == 0) {
      return true;
    }
  }
  return false;
}


// Covers the whole range of values, with the extremes next to each other.
uint16_t pattern(uint32_t x, uint32_t y, int channel, int bit_depth)
{
  const uint32_t maxval = (1u << bit_depth) - 1;
  if (x == 0 && y == 0) return static_cast<uint16_t>(maxval);
  if (x == 1 && y == 0) return 0;
  return static_cast<uint16_t>((x * 37 + y * 101 + x * y + static_cast<uint32_t>(channel) * 1237) & maxval);
}

// What the screen content coding tools are made for: few colours, sharp edges, and the same
// shapes again and again.
uint16_t screen_pattern(uint32_t x, uint32_t y, int channel, int bit_depth)
{
  static const char* const glyph[8] = {
    "..##..#.",
    ".#..#.#.",
    ".#..#.##",
    ".####.#.",
    ".#..#.#.",
    ".#..#.#.",
    ".#..#.##",
    "........"
  };

  static const uint32_t colours[4][3] = {
    {235, 128, 128},
    {16,  128, 128},
    {81,  90,  240},
    {145, 54,  34}
  };

  const bool window = ((x / 40 + y / 32) % 2) != 0;
  const bool ink = (glyph[y % 8][x % 8] == '#' && (y / 8) % 3 != 2);
  const int colour = ink ? (window ? 2 : 1) : (window ? 3 : 0);

  const uint32_t maxval = (1u << bit_depth) - 1;
  return static_cast<uint16_t>(colours[colour][channel] * maxval / 255);
}

typedef uint16_t (* pattern_function)(uint32_t x, uint32_t y, int channel, int bit_depth);

void add_plane(heif_image* img, heif_channel channel, int channel_index, uint32_t w, uint32_t h, int bit_depth,
               pattern_function pattern_of, uint32_t subsampling_x, uint32_t subsampling_y)
{
  heif_error err = heif_image_add_plane(img, channel, w, h, bit_depth);
  REQUIRE(err.code == heif_error_Ok);

  size_t stride = 0;
  uint8_t* p = heif_image_get_plane2(img, channel, &stride);
  REQUIRE(p != nullptr);

  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      const uint16_t value = pattern_of(x * subsampling_x, y * subsampling_y, channel_index, bit_depth);
      if (bit_depth > 8) {
        memcpy(p + y * stride + 2 * x, &value, 2);
      }
      else {
        p[y * stride + x] = static_cast<uint8_t>(value);
      }
    }
  }
}

heif_image* create_image(uint32_t w, uint32_t h, heif_chroma chroma, int bit_depth, bool screen_content = false)
{
  heif_image* img = nullptr;
  heif_error err = heif_image_create(w, h,
                                     chroma == heif_chroma_monochrome ? heif_colorspace_monochrome : heif_colorspace_YCbCr,
                                     chroma, &img);
  REQUIRE(err.code == heif_error_Ok);

  // The test pattern is a function of the position in the plane, the screen content is
  // a picture that the chroma planes show in their resolution.
  const pattern_function pattern_of = screen_content ? screen_pattern : pattern;

  add_plane(img, heif_channel_Y, 0, w, h, bit_depth, pattern_of, 1, 1);

  if (chroma != heif_chroma_monochrome) {
    const uint32_t cw = (chroma == heif_chroma_444) ? w : (w + 1) / 2;
    const uint32_t ch = (chroma == heif_chroma_420) ? (h + 1) / 2 : h;
    const uint32_t sx = (screen_content && chroma != heif_chroma_444) ? 2 : 1;
    const uint32_t sy = (screen_content && chroma == heif_chroma_420) ? 2 : 1;
    add_plane(img, heif_channel_Cb, 1, cw, ch, bit_depth, pattern_of, sx, sy);
    add_plane(img, heif_channel_Cr, 2, cw, ch, bit_depth, pattern_of, sx, sy);
  }

  return img;
}

const char* chroma_name(heif_chroma chroma)
{
  switch (chroma) {
    case heif_chroma_422: return "422";
    case heif_chroma_444: return "444";
    default: return "420";
  }
}


// heif_error::message may point into the context that reported the error, so it has to be
// copied before the context is freed.
struct EncodeResult
{
  heif_error_code code;
  heif_suberror_code subcode;
  std::string message;
  std::vector<uint8_t> file;
};

heif_error write_to_vector(heif_context*, const void* data, size_t size, void* userdata)
{
  auto* file = static_cast<std::vector<uint8_t>*>(userdata);
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  file->insert(file->end(), bytes, bytes + size);
  return heif_error_success;
}

typedef std::vector<std::pair<const char*, bool>> CodingTools;

EncodeResult encode(heif_image* img, bool lossless, const CodingTools& tools)
{
  heif_context* ctx = heif_context_alloc();
  heif_encoder* encoder = get_hm_encoder(ctx);
  REQUIRE(encoder != nullptr);

  heif_error err = heif_encoder_set_lossless(encoder, lossless ? 1 : 0);
  REQUIRE(err.code == heif_error_Ok);

  if (heif_image_get_chroma_format(img) != heif_chroma_monochrome) {
    err = heif_encoder_set_parameter_string(encoder, "chroma", chroma_name(heif_image_get_chroma_format(img)));
    REQUIRE(err.code == heif_error_Ok);
  }

  for (const auto& tool : tools) {
    err = heif_encoder_set_parameter_boolean(encoder, tool.first, tool.second ? 1 : 0);
    INFO("coding tool " << tool.first);
    REQUIRE(err.code == heif_error_Ok);
  }

  err = heif_context_encode_image(ctx, img, encoder, nullptr, nullptr);
  EncodeResult result{err.code, err.subcode, err.message, {}};

  if (err.code == heif_error_Ok) {
    heif_writer writer{};
    writer.writer_api_version = 1;
    writer.write = write_to_vector;
    err = heif_context_write(ctx, &writer, &result.file);
    REQUIRE(err.code == heif_error_Ok);
  }

  heif_encoder_release(encoder);
  heif_context_free(ctx);
  return result;
}


EncodeResult encode_lossless(heif_image* img, const char* tool = nullptr)
{
  CodingTools tools;
  if (tool) {
    tools.emplace_back(tool, true);
  }

  return encode(img, true, tools);
}


// general_profile_idc of the HEVC image in the file. It is the second byte of the
// HEVCDecoderConfigurationRecord in the 'hvcC' box.
int hevc_profile_idc(const std::vector<uint8_t>& file)
{
  static const uint8_t fourcc[4] = {'h', 'v', 'c', 'C'};

  auto box = std::search(file.begin(), file.end(), fourcc, fourcc + 4);
  REQUIRE(file.end() - box > 6);

  return box[5] & 0x1F;
}

const int HEVC_PROFILE_FORMAT_RANGE_EXTENSIONS = 4;
const int HEVC_PROFILE_HIGH_THROUGHPUT = 5;
const int HEVC_PROFILE_SCREEN_CONTENT_CODING = 9;


// The compatible brands in the 'ftyp' box, which is the first box of the file.
std::vector<std::string> compatible_brands(const std::vector<uint8_t>& file)
{
  REQUIRE(file.size() >= 16);
  REQUIRE(memcmp(file.data() + 4, "ftyp", 4) == 0);

  const size_t box_size = (size_t{file[0]} << 24) | (size_t{file[1]} << 16) | (size_t{file[2]} << 8) | file[3];
  REQUIRE(box_size <= file.size());

  // box header, major brand, minor version
  std::vector<std::string> brands;
  for (size_t p = 16; p + 4 <= box_size; p += 4) {
    brands.emplace_back(file.begin() + p, file.begin() + p + 4);
  }

  return brands;
}

bool has_brand(const std::vector<uint8_t>& file, const char* brand)
{
  const std::vector<std::string> brands = compatible_brands(file);
  return std::find(brands.begin(), brands.end(), brand) != brands.end();
}


void check_decodes_to(const std::vector<uint8_t>& file, const heif_image* expected, int bit_depth)
{
  heif_context* ctx = heif_context_alloc();
  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);

  CHECK(heif_image_handle_get_luma_bits_per_pixel(handle) == bit_depth);
  CHECK(heif_image_handle_get_width(handle) == heif_image_get_width(expected, heif_channel_Y));
  CHECK(heif_image_handle_get_height(handle) == heif_image_get_height(expected, heif_channel_Y));

  heif_decoding_options* options = heif_decoding_options_alloc();
  options->decoder_id = "libde265";
  options->output_image_nclx_profile_passthrough = 1;

  heif_image* decoded = nullptr;
  err = heif_decode_image(handle, &decoded, heif_colorspace_undefined, heif_chroma_undefined, options);
  INFO("decoding error: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  CHECK(heif_image_get_chroma_format(decoded) == heif_image_get_chroma_format(expected));

  for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}) {
    if (!heif_image_has_channel(expected, channel)) {
      continue;
    }

    REQUIRE(heif_image_has_channel(decoded, channel));
    REQUIRE(heif_image_get_bits_per_pixel_range(decoded, channel) == bit_depth);

    const int w = heif_image_get_width(expected, channel);
    const int h = heif_image_get_height(expected, channel);
    REQUIRE(heif_image_get_width(decoded, channel) == w);
    REQUIRE(heif_image_get_height(decoded, channel) == h);

    size_t stride_expected = 0, stride_decoded = 0;
    const uint8_t* p_expected = heif_image_get_plane_readonly2(expected, channel, &stride_expected);
    const uint8_t* p_decoded = heif_image_get_plane_readonly2(decoded, channel, &stride_decoded);

    int differing_rows = 0;
    for (int y = 0; y < h; y++) {
      if (memcmp(p_expected + y * stride_expected, p_decoded + y * stride_decoded,
                 static_cast<size_t>(w) * (bit_depth > 8 ? 2 : 1)) != 0) {
        differing_rows++;
      }
    }

    INFO("channel " << channel);
    CHECK(differing_rows == 0);
  }

  heif_image_release(decoded);
  heif_decoding_options_free(options);
  heif_image_handle_release(handle);
  heif_context_free(ctx);
}

} // namespace


TEST_CASE("HM encodes the bit depths from 8 to 15 without loss")
{
  if (!have_hm_encoder()) {
    SKIP("libheif was built without the HM encoder plugin");
  }

  const int bit_depth = GENERATE(8, 9, 10, 11, 12, 13, 14, 15);
  const heif_chroma chroma = GENERATE(heif_chroma_monochrome, heif_chroma_420, heif_chroma_422, heif_chroma_444);

  // 72x56 is a multiple of the minimum coding block size, 61x47 needs padding and, with
  // subsampled chroma, a 'clap' property.
  const bool odd_size = GENERATE(false, true);
  const uint32_t w = odd_size ? 61 : 72;
  const uint32_t h = odd_size ? 47 : 56;

  INFO("bit depth " << bit_depth << ", chroma " << chroma << ", size " << w << "x" << h);

  heif_image* img = create_image(w, h, chroma, bit_depth);

  EncodeResult result = encode_lossless(img);
  INFO("encode error (" << result.code << "/" << result.subcode << "): " << result.message);
  REQUIRE(result.code == heif_error_Ok);

  if (have_libde265()) {
    check_decodes_to(result.file, img, bit_depth);
  }

  heif_image_release(img);
}


TEST_CASE("HM refuses images with 16 bits per sample")
{
  if (!have_hm_encoder()) {
    SKIP("libheif was built without the HM encoder plugin");
  }

  heif_image* img = create_image(64, 64, heif_chroma_420, 16);

  EncodeResult result = encode_lossless(img);
  INFO("encode error (" << result.code << "/" << result.subcode << "): " << result.message);
  CHECK(result.code != heif_error_Ok);
  CHECK(result.subcode == heif_suberror_Unsupported_bit_depth);

  heif_image_release(img);
}


TEST_CASE("HM reports a configuration that it does not accept as an error")
{
  if (!have_hm_encoder()) {
    SKIP("libheif was built without the HM encoder plugin");
  }

  // HM, which is written as a command line program, exits when it does not accept its
  // parameters. The plugin has to turn this into an error.
  //
  // The extended precision processing is only defined for the profiles with 16 bits, and
  // the High Throughput profile, which the alignment of the bypass bins selects, does not
  // allow it below 16 bits together with the alignment.

  heif_image* img = create_image(64, 64, heif_chroma_444, 10);

  heif_context* ctx = heif_context_alloc();
  heif_encoder* encoder = get_hm_encoder(ctx);
  REQUIRE(encoder != nullptr);

  heif_error err = heif_encoder_set_parameter_integer(encoder, "transform-skip-log2-max-size", 6);
  CHECK(err.code != heif_error_Ok);

  err = heif_encoder_set_parameter_string(encoder, "chroma", "411");
  CHECK(err.code != heif_error_Ok);

  // After an error, the encoder has to be usable again.

  err = heif_encoder_set_parameter_string(encoder, "chroma", "444");
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_encode_image(ctx, img, encoder, nullptr, nullptr);
  INFO("encode error: " << err.message);
  CHECK(err.code == heif_error_Ok);

  heif_encoder_release(encoder);
  heif_context_free(ctx);
  heif_image_release(img);
}


TEST_CASE("HM encodes with the coding tools of the range extensions")
{
  if (!have_hm_encoder()) {
    SKIP("libheif was built without the HM encoder plugin");
  }

  // Whether a decoder handles these tools is a different question. The reference decoder of
  // HM decodes all of these images without loss. libde265 before version 1.1.3 does not
  // decode cross-component prediction and aligned bypass bins correctly, and no version
  // implements the extended precision processing. The decoded image is compared for the
  // other tools.

  struct tool
  {
    const char* name;
    bool compare_decoded_image;
  };

  const tool t = GENERATE(tool{"cross-component-prediction", false},
                          tool{"implicit-rdpcm", true},
                          tool{"explicit-rdpcm", true},
                          tool{"transform-skip-rotation", true},
                          tool{"transform-skip-context", true},
                          tool{"persistent-rice-adaptation", true},
                          tool{"extended-precision", false},
                          tool{"cabac-bypass-alignment", false});

  INFO("coding tool " << t.name);

  heif_image* img = create_image(72, 56, heif_chroma_444, 12);

  EncodeResult result = encode_lossless(img, t.name);
  INFO("encode error (" << result.code << "/" << result.subcode << "): " << result.message);
  REQUIRE(result.code == heif_error_Ok);

  if (t.compare_decoded_image && have_libde265()) {
    check_decodes_to(result.file, img, 12);
  }

  heif_image_release(img);
}


TEST_CASE("HM does not encode image sequences")
{
  if (!have_hm_encoder()) {
    SKIP("libheif was built without the HM encoder plugin");
  }

  heif_image* img = create_image(64, 64, heif_chroma_420, 8);

  heif_context* ctx = heif_context_alloc();
  heif_encoder* encoder = get_hm_encoder(ctx);
  REQUIRE(encoder != nullptr);

  heif_track* track = nullptr;
  heif_error err = heif_context_add_visual_sequence_track(ctx, 64, 64, heif_track_type_video,
                                                          nullptr, nullptr, &track);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_set_duration(img, 1);
  err = heif_track_encode_sequence_image(track, img, encoder, nullptr);
  const heif_error_code code = err.code;
  const std::string message = err.message;

  heif_track_release(track);
  heif_encoder_release(encoder);
  heif_context_free(ctx);
  heif_image_release(img);

  INFO("error: " << message);
  CHECK(code == heif_error_Unsupported_feature);
}


TEST_CASE("HM encodes with the screen content coding tools")
{
  if (!have_hm_encoder()) {
    SKIP("libheif was built without the HM encoder plugin");
  }

  if (!have_screen_content_coding_tools()) {
    SKIP("the HM encoder plugin was built without the HM version that has the screen content coding tools");
  }

  // There is no decoder in libheif for these images. The reference decoder of HM decodes
  // them, which was verified when the tests were written, but it is no library. So we check
  // what can be seen from the outside: the image is encoded in a screen content coding
  // profile, and the coding tool changes the coded data.

  struct tool
  {
    const char* name;
    bool enable;
    bool makes_image_smaller; // for the picture that we encode
    bool needs_444;
  };

  const tool t = GENERATE(tool{"palette-mode", true, true, false},
                          tool{"intra-block-copy", true, true, false},
                          tool{"adaptive-colour-transform", true, false, true},
                          tool{"intra-boundary-filter", false, false, false});

  const heif_chroma chroma = GENERATE(heif_chroma_monochrome, heif_chroma_420, heif_chroma_444);
  const int bit_depth = GENERATE(8, 10);
  const bool lossless = GENERATE(false, true);

  if (t.needs_444 && chroma != heif_chroma_444) {
    return;
  }

  INFO("coding tool " << t.name << ", chroma " << chroma << ", bit depth " << bit_depth << ", lossless " << lossless);

  heif_image* img = create_image(160, 96, chroma, bit_depth, true);

  EncodeResult without_tool = encode(img, lossless, {});
  INFO("encode error (" << without_tool.code << "/" << without_tool.subcode << "): " << without_tool.message);
  REQUIRE(without_tool.code == heif_error_Ok);
  CHECK(hevc_profile_idc(without_tool.file) != HEVC_PROFILE_SCREEN_CONTENT_CODING);

  EncodeResult with_tool = encode(img, lossless, {{t.name, t.enable}});
  INFO("encode error (" << with_tool.code << "/" << with_tool.subcode << "): " << with_tool.message);
  REQUIRE(with_tool.code == heif_error_Ok);
  CHECK(hevc_profile_idc(with_tool.file) == HEVC_PROFILE_SCREEN_CONTENT_CODING);

  // There is no brand for the screen content coding profiles, and no MIAF profile.
  CHECK(has_brand(with_tool.file, "mif1"));
  CHECK_FALSE(has_brand(with_tool.file, "heic"));
  CHECK_FALSE(has_brand(with_tool.file, "heix"));
  CHECK_FALSE(has_brand(with_tool.file, "miaf"));

  CHECK(with_tool.file != without_tool.file);
  if (t.makes_image_smaller) {
    CHECK(with_tool.file.size() < without_tool.file.size());
  }

  heif_image_release(img);
}


TEST_CASE("HM combines the screen content coding tools with those of the range extensions")
{
  if (!have_hm_encoder()) {
    SKIP("libheif was built without the HM encoder plugin");
  }

  if (!have_screen_content_coding_tools()) {
    SKIP("the HM encoder plugin was built without the HM version that has the screen content coding tools");
  }

  heif_image* img = create_image(160, 96, heif_chroma_444, 10, true);

  EncodeResult result = encode(img, false, {{"palette-mode",               true},
                                            {"intra-block-copy",           true},
                                            {"adaptive-colour-transform",  true},
                                            {"intra-boundary-filter",      false},
                                            {"cross-component-prediction", true},
                                            {"implicit-rdpcm",             true},
                                            {"explicit-rdpcm",             true},
                                            {"transform-skip-rotation",    true},
                                            {"transform-skip-context",     true},
                                            {"persistent-rice-adaptation", true},
                                            {"intra-smoothing",            false}});
  INFO("encode error (" << result.code << "/" << result.subcode << "): " << result.message);
  REQUIRE(result.code == heif_error_Ok);
  CHECK(hevc_profile_idc(result.file) == HEVC_PROFILE_SCREEN_CONTENT_CODING);

  heif_image_release(img);
}


TEST_CASE("HM refuses what the screen content coding profiles do not allow")
{
  if (!have_hm_encoder()) {
    SKIP("libheif was built without the HM encoder plugin");
  }

  if (!have_screen_content_coding_tools()) {
    SKIP("the HM encoder plugin was built without the HM version that has the screen content coding tools");
  }

  struct combination
  {
    const char* description;
    heif_chroma chroma;
    int bit_depth;
    CodingTools tools;
  };

  const combination c = GENERATE(combination{"more than 10 bits", heif_chroma_444, 12, {{"palette-mode", true}}},
                                 combination{"chroma 4:2:2", heif_chroma_422, 8, {{"palette-mode", true}}},
                                 combination{"colour transform without 4:4:4", heif_chroma_420, 8, {{"adaptive-colour-transform", true}}},
                                 combination{"extended precision", heif_chroma_444, 8, {{"intra-block-copy", true}, {"extended-precision", true}}},
                                 combination{"aligned bypass bins", heif_chroma_444, 8, {{"intra-boundary-filter", false}, {"cabac-bypass-alignment", true}}});

  INFO(c.description);

  heif_image* img = create_image(160, 96, c.chroma, c.bit_depth, true);

  EncodeResult refused = encode(img, false, c.tools);
  INFO("encode error (" << refused.code << "/" << refused.subcode << "): " << refused.message);
  CHECK(refused.code == heif_error_Encoder_plugin_error);
  CHECK(refused.subcode == heif_suberror_Invalid_parameter_value);

  // The same image is encoded when the screen content coding tools are not used.

  EncodeResult accepted = encode(img, false, {});
  INFO("encode error (" << accepted.code << "/" << accepted.subcode << "): " << accepted.message);
  CHECK(accepted.code == heif_error_Ok);

  heif_image_release(img);
}


TEST_CASE("HM images in a high throughput profile have no HEVC brand")
{
  if (!have_hm_encoder()) {
    SKIP("libheif was built without the HM encoder plugin");
  }

  // The alignment of the bypass bins selects the High Throughput 4:4:4 16 Intra profile.
  // 'heix' is for the Main 10 profile and the format range extensions profiles only.

  heif_image* img = create_image(72, 56, heif_chroma_444, 12);

  EncodeResult high_throughput = encode_lossless(img, "cabac-bypass-alignment");
  INFO("encode error (" << high_throughput.code << "/" << high_throughput.subcode << "): " << high_throughput.message);
  REQUIRE(high_throughput.code == heif_error_Ok);
  CHECK(hevc_profile_idc(high_throughput.file) == HEVC_PROFILE_HIGH_THROUGHPUT);
  CHECK(has_brand(high_throughput.file, "mif1"));
  CHECK_FALSE(has_brand(high_throughput.file, "heix"));
  CHECK_FALSE(has_brand(high_throughput.file, "miaf"));

  EncodeResult range_extensions = encode_lossless(img);
  INFO("encode error (" << range_extensions.code << "/" << range_extensions.subcode << "): " << range_extensions.message);
  REQUIRE(range_extensions.code == heif_error_Ok);
  CHECK(hevc_profile_idc(range_extensions.file) == HEVC_PROFILE_FORMAT_RANGE_EXTENSIONS);
  CHECK(has_brand(range_extensions.file, "heix"));

  heif_image_release(img);
}


TEST_CASE("HM images are MIAF images up to 10 bits")
{
  if (!have_hm_encoder()) {
    SKIP("libheif was built without the HM encoder plugin");
  }

  // The HEVC profiles of the MIAF profiles end at 10 bits (ISO/IEC 23000-22, A.3 to A.5).

  const int bit_depth = GENERATE(8, 9, 10, 11, 12, 15);
  const heif_chroma chroma = GENERATE(heif_chroma_monochrome, heif_chroma_420, heif_chroma_422, heif_chroma_444);

  INFO("bit depth " << bit_depth << ", chroma " << chroma);

  heif_image* img = create_image(72, 56, chroma, bit_depth);

  EncodeResult result = encode_lossless(img);
  INFO("encode error (" << result.code << "/" << result.subcode << "): " << result.message);
  REQUIRE(result.code == heif_error_Ok);

  CHECK(has_brand(result.file, "miaf") == (bit_depth <= 10));

  // The brand of the HEVC profile does not depend on the bit depth.
  const bool is_main_profile = (bit_depth == 8 && chroma == heif_chroma_420);
  CHECK(has_brand(result.file, "heic") == is_main_profile);
  CHECK(has_brand(result.file, "heix") == !is_main_profile);

  heif_image_release(img);
}


TEST_CASE("HM uses the profiles of the range extensions without screen content coding tools")
{
  if (!have_hm_encoder()) {
    SKIP("libheif was built without the HM encoder plugin");
  }

  // This also holds when the version of HM with the screen content coding tools is the only
  // one in the plugin and encodes all images.

  heif_image* img = create_image(72, 56, heif_chroma_444, 12);

  EncodeResult result = encode_lossless(img, "implicit-rdpcm");
  INFO("encode error (" << result.code << "/" << result.subcode << "): " << result.message);
  REQUIRE(result.code == heif_error_Ok);
  CHECK(hevc_profile_idc(result.file) == HEVC_PROFILE_FORMAT_RANGE_EXTENSIONS);

  heif_image_release(img);
}
