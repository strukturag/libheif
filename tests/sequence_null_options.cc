/*
  libheif regression test for encoding a sequence image with NULL options.

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

// heif_context_add_visual_sequence_track() and heif_track_encode_sequence_image()
// both document their options parameters as optional ("If NULL, default options
// will be used"). Track_Visual::encode_image() nevertheless read
// in_options->save_alpha_channel without a NULL check, so passing NULL crashed
// with a null pointer dereference. A second, later dereference lurked on the same
// path: the color conversion options were forwarded as a NULL pointer that
// Encoder::convert_colorspace_for_encoding() dereferences whenever a conversion
// is actually needed.
//
// The fix resolves the caller's pointer into a fully populated options struct at
// the top of the function, so a NULL argument means "the documented defaults"
// rather than "read through a null pointer". This test pins both halves: that
// NULL options encode at all, and that they behave like the documented default
// save_alpha_channel = 1 rather than silently dropping the alpha track.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t WIDTH = 64;
constexpr uint32_t HEIGHT = 64;

heif_error mem_writer(heif_context*, const void* data, size_t size, void* userdata)
{
  auto* out = static_cast<std::vector<uint8_t>*>(userdata);
  const auto* p = static_cast<const uint8_t*>(data);
  out->insert(out->end(), p, p + size);
  return heif_error{heif_error_Ok, heif_suberror_Unspecified, nullptr};
}

heif_image* make_image(bool with_alpha)
{
  heif_image* img = nullptr;
  REQUIRE(heif_image_create(WIDTH, HEIGHT, heif_colorspace_YCbCr, heif_chroma_420, &img).code == heif_error_Ok);
  REQUIRE(img != nullptr);

  REQUIRE(heif_image_add_plane(img, heif_channel_Y, WIDTH, HEIGHT, 8).code == heif_error_Ok);
  REQUIRE(heif_image_add_plane(img, heif_channel_Cb, WIDTH / 2, HEIGHT / 2, 8).code == heif_error_Ok);
  REQUIRE(heif_image_add_plane(img, heif_channel_Cr, WIDTH / 2, HEIGHT / 2, 8).code == heif_error_Ok);
  if (with_alpha) {
    REQUIRE(heif_image_add_plane(img, heif_channel_Alpha, WIDTH, HEIGHT, 8).code == heif_error_Ok);
  }

  for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr, heif_channel_Alpha}) {
    if (!heif_image_has_channel(img, channel)) {
      continue;
    }
    size_t stride = 0;
    uint8_t* p = heif_image_get_plane2(img, channel, &stride);
    REQUIRE(p != nullptr);
    uint32_t rows = (channel == heif_channel_Cb || channel == heif_channel_Cr) ? HEIGHT / 2 : HEIGHT;
    memset(p, 0x40, stride * rows);
  }

  // Without a duration the encoder is handed a zero timebase and rejects the frame.
  heif_image_set_duration(img, 1000);

  return img;
}

// Encode a one frame sequence. 'save_alpha' < 0 means "pass NULL options".
std::vector<uint8_t> encode_sequence(bool with_alpha, int save_alpha)
{
  heif_image* img = make_image(with_alpha);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_encoder* encoder = nullptr;
  REQUIRE(heif_context_get_encoder_for_format(ctx, heif_compression_AV1, &encoder).code == heif_error_Ok);
  REQUIRE(encoder != nullptr);

  heif_sequence_encoding_options* options = nullptr;
  if (save_alpha >= 0) {
    options = heif_sequence_encoding_options_alloc();
    REQUIRE(options != nullptr);
    options->save_alpha_channel = save_alpha;
  }

  heif_track* track = nullptr;
  heif_error err = heif_context_add_visual_sequence_track(ctx, (uint16_t) WIDTH, (uint16_t) HEIGHT,
                                                          heif_track_type_image_sequence,
                                                          nullptr, options, &track);
  INFO("add_visual_sequence_track: " << (err.message ? err.message : ""));
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(track != nullptr);

  // The formerly crashing call.
  err = heif_track_encode_sequence_image(track, img, encoder, options);
  INFO("encode_sequence_image: " << (err.message ? err.message : ""));
  REQUIRE(err.code == heif_error_Ok);

  err = heif_track_encode_end_of_sequence(track, encoder);
  REQUIRE(err.code == heif_error_Ok);

  std::vector<uint8_t> file;
  heif_writer writer{};
  writer.writer_api_version = 1;
  writer.write = mem_writer;
  REQUIRE(heif_context_write(ctx, &writer, &file).code == heif_error_Ok);

  if (options) {
    heif_sequence_encoding_options_release(options);
  }
  heif_encoder_release(encoder);
  heif_context_free(ctx);
  heif_image_release(img);

  return file;
}

} // namespace

TEST_CASE("sequence encoding accepts NULL options")
{
  if (!heif_have_encoder_for_format(heif_compression_AV1)) {
    SKIP("Skipping test because no AV1 encoder is compiled.");
  }

  // Passing NULL for both options parameters must work, as documented.
  std::vector<uint8_t> file = encode_sequence(/*with_alpha=*/false, /*save_alpha=*/-1);
  REQUIRE(!file.empty());

  // The resulting file must be readable and hold one sequence track.
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  REQUIRE(heif_context_read_from_memory(ctx, file.data(), file.size(), nullptr).code == heif_error_Ok);
  REQUIRE(heif_context_has_sequence(ctx) == 1);
  heif_context_free(ctx);
}

TEST_CASE("NULL sequence options mean the documented defaults, not zeroed fields")
{
  if (!heif_have_encoder_for_format(heif_compression_AV1)) {
    SKIP("Skipping test because no AV1 encoder is compiled.");
  }

  // heif_sequence_encoding_options_alloc() defaults save_alpha_channel to 1, so
  // an alpha input encoded with NULL options must produce the same file as one
  // encoded with an explicitly enabled alpha channel, and must differ from one
  // with the alpha channel explicitly disabled. Guarding the NULL dereference
  // alone (treating a missing options struct as all-zero) would silently drop
  // the alpha track and make these two comparisons swap places.
  std::vector<uint8_t> with_null = encode_sequence(/*with_alpha=*/true, /*save_alpha=*/-1);
  std::vector<uint8_t> alpha_on = encode_sequence(/*with_alpha=*/true, /*save_alpha=*/1);
  std::vector<uint8_t> alpha_off = encode_sequence(/*with_alpha=*/true, /*save_alpha=*/0);

  REQUIRE(!with_null.empty());
  REQUIRE(with_null == alpha_on);
  REQUIRE(with_null != alpha_off);
}
