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

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>
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

void add_plane(heif_image* img, heif_channel channel, int channel_index, uint32_t w, uint32_t h, int bit_depth)
{
  heif_error err = heif_image_add_plane(img, channel, w, h, bit_depth);
  REQUIRE(err.code == heif_error_Ok);

  size_t stride = 0;
  uint8_t* p = heif_image_get_plane2(img, channel, &stride);
  REQUIRE(p != nullptr);

  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      const uint16_t value = pattern(x, y, channel_index, bit_depth);
      if (bit_depth > 8) {
        memcpy(p + y * stride + 2 * x, &value, 2);
      }
      else {
        p[y * stride + x] = static_cast<uint8_t>(value);
      }
    }
  }
}

heif_image* create_image(uint32_t w, uint32_t h, heif_chroma chroma, int bit_depth)
{
  heif_image* img = nullptr;
  heif_error err = heif_image_create(w, h,
                                     chroma == heif_chroma_monochrome ? heif_colorspace_monochrome : heif_colorspace_YCbCr,
                                     chroma, &img);
  REQUIRE(err.code == heif_error_Ok);

  add_plane(img, heif_channel_Y, 0, w, h, bit_depth);

  if (chroma != heif_chroma_monochrome) {
    const uint32_t cw = (chroma == heif_chroma_444) ? w : (w + 1) / 2;
    const uint32_t ch = (chroma == heif_chroma_420) ? (h + 1) / 2 : h;
    add_plane(img, heif_channel_Cb, 1, cw, ch, bit_depth);
    add_plane(img, heif_channel_Cr, 2, cw, ch, bit_depth);
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

EncodeResult encode_lossless(heif_image* img, const char* tool = nullptr)
{
  heif_context* ctx = heif_context_alloc();
  heif_encoder* encoder = get_hm_encoder(ctx);
  REQUIRE(encoder != nullptr);

  heif_error err = heif_encoder_set_lossless(encoder, 1);
  REQUIRE(err.code == heif_error_Ok);

  if (heif_image_get_chroma_format(img) != heif_chroma_monochrome) {
    err = heif_encoder_set_parameter_string(encoder, "chroma", chroma_name(heif_image_get_chroma_format(img)));
    REQUIRE(err.code == heif_error_Ok);
  }

  if (tool) {
    err = heif_encoder_set_parameter_boolean(encoder, tool, 1);
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
