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

// An interleaved chroma format fixes the storage width of its samples: the RGB(A) formats keep
// one byte per component, the RRGGBB(AA) formats two. The interleaved encoders of the uncompressed
// codec take the bytes per pixel from the plane's bit depth but address the plane through the
// layout the chroma format implies, so an image whose bit depth disagrees with its chroma format
// makes an encoder size its output buffer for one layout and copy with the other.
//
// The block-pixel encoder was the clearest case: with an 8-bit plane on an RRGGBB format it sized
// the tile buffer at three bytes per pixel while its writer emitted four, overrunning the buffer
// by width*height bytes with sample-derived contents.
//
// add_channel() refuses such a plane, but transfer_channel_from_image_as() moved planes without
// going through it, and for an interleaved channel it could not even derive the component list:
// it labelled every component with the same placeholder type. Interleaved planes are no longer
// transferable, and the uncompressed encoder factory checks bit depth against chroma format as
// defense in depth.
//
// These tests need library-internal classes and therefore only build with full symbol visibility
// (WITH_REDUCED_VISIBILITY=OFF).

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "image/pixelimage.h"
#include "codecs/uncompressed/unc_encoder.h"

#include <memory>

namespace {

constexpr uint32_t WIDTH = 128;
constexpr uint32_t HEIGHT = 64;

std::shared_ptr<HeifPixelImage> make_interleaved_image(heif_chroma chroma, int bit_depth)
{
  auto image = std::make_shared<HeifPixelImage>();
  image->create(WIDTH, HEIGHT, heif_colorspace_RGB, chroma);
  REQUIRE(image->fill_new_channel(heif_channel_interleaved, 0x80, WIDTH, HEIGHT, bit_depth,
                                  heif_get_global_security_limits()).error_code == heif_error_Ok);
  return image;
}

// Build an image whose interleaved plane does not match its chroma format. add_channel() rejects
// this combination, so the plane is added under a chroma format that accepts it and the format is
// then changed with a second create() call, which only overwrites the format fields. No supported
// route produces such an image; this only exists to reach the encoder-side check.
std::shared_ptr<HeifPixelImage> make_mismatched_image(heif_chroma plane_chroma, int bit_depth,
                                                      heif_chroma claimed_chroma)
{
  auto image = make_interleaved_image(plane_chroma, bit_depth);
  image->create(WIDTH, HEIGHT, heif_colorspace_RGB, claimed_chroma);
  return image;
}

} // namespace


TEST_CASE("Interleaved planes cannot be transferred between images")
{
  auto source = make_interleaved_image(heif_chroma_interleaved_RGB, 8);

  auto destination = std::make_shared<HeifPixelImage>();
  destination->create(WIDTH, HEIGHT, heif_colorspace_RGB, heif_chroma_interleaved_RRGGBB_LE);

  Error err = destination->transfer_channel_from_image_as(source, heif_channel_interleaved,
                                                          heif_channel_interleaved);
  REQUIRE(err.error_code == heif_error_Usage_error);

  // The source keeps its plane and the destination gained none.
  REQUIRE(source->has_channel(heif_channel_interleaved));
  REQUIRE(!destination->has_channel(heif_channel_interleaved));

  // Also refused when only the destination channel is interleaved.
  auto mono = std::make_shared<HeifPixelImage>();
  mono->create(WIDTH, HEIGHT, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(mono->fill_new_channel(heif_channel_Y, 0xFF, WIDTH, HEIGHT, 8,
                                 heif_get_global_security_limits()).error_code == heif_error_Ok);

  auto rgb = std::make_shared<HeifPixelImage>();
  rgb->create(WIDTH, HEIGHT, heif_colorspace_RGB, heif_chroma_interleaved_RGB);
  REQUIRE(rgb->transfer_channel_from_image_as(mono, heif_channel_Y,
                                             heif_channel_interleaved).error_code == heif_error_Usage_error);
}


TEST_CASE("Uncompressed encoder refuses an interleaved plane that does not match its chroma format")
{
  struct Mismatch
  {
    heif_chroma plane_chroma;
    int bit_depth;
    heif_chroma claimed_chroma;
    const char* name;
  };

  // An 8-bit plane under a 16-bit format sizes the buffer too small; a 16-bit plane under an
  // 8-bit format addresses the plane with the wrong stride.
  const Mismatch mismatches[] = {
      {heif_chroma_interleaved_RGB, 8, heif_chroma_interleaved_RRGGBB_LE, "8-bit plane, RRGGBB_LE"},
      {heif_chroma_interleaved_RGB, 8, heif_chroma_interleaved_RRGGBB_BE, "8-bit plane, RRGGBB_BE"},
      {heif_chroma_interleaved_RGBA, 8, heif_chroma_interleaved_RRGGBBAA_LE, "8-bit plane, RRGGBBAA_LE"},
      {heif_chroma_interleaved_RRGGBB_LE, 10, heif_chroma_interleaved_RGB, "10-bit plane, RGB"},
      {heif_chroma_interleaved_RRGGBBAA_LE, 10, heif_chroma_interleaved_RGBA, "10-bit plane, RGBA"},
  };

  heif_encoding_options* options = heif_encoding_options_alloc();

  for (const auto& mismatch : mismatches) {
    INFO(mismatch.name);

    auto image = make_mismatched_image(mismatch.plane_chroma, mismatch.bit_depth,
                                       mismatch.claimed_chroma);

    auto encoder = unc_encoder_factory::get_unc_encoder(image, *options);
    REQUIRE(!encoder);
    REQUIRE(encoder.error().error_code == heif_error_Invalid_input);
  }

  heif_encoding_options_free(options);
}


TEST_CASE("Uncompressed encoder writes one full tile buffer for every interleaved format")
{
  struct Format
  {
    heif_chroma chroma;
    int bit_depth;
    const char* name;
  };

  // Covers all three interleaved encoders: 8-bit RGB/RGBA use the pixel-interleave encoder,
  // RRGGBB below 14 bits the block-pixel encoder, and the rest the byte-aligned one. The
  // block-pixel encoder's bytes per pixel is (3*bit_depth+7)/8, which is 4 at 9 and 10 bits and
  // 5 from 11 bits up, so both of its widths are exercised.
  const Format formats[] = {
      {heif_chroma_interleaved_RGB, 8, "RGB 8"},
      {heif_chroma_interleaved_RGBA, 8, "RGBA 8"},
      {heif_chroma_interleaved_RRGGBB_LE, 9, "RRGGBB_LE 9"},
      {heif_chroma_interleaved_RRGGBB_LE, 10, "RRGGBB_LE 10"},
      {heif_chroma_interleaved_RRGGBB_BE, 11, "RRGGBB_BE 11"},
      {heif_chroma_interleaved_RRGGBB_BE, 13, "RRGGBB_BE 13"},
      {heif_chroma_interleaved_RRGGBB_BE, 16, "RRGGBB_BE 16"},
      {heif_chroma_interleaved_RRGGBBAA_LE, 10, "RRGGBBAA_LE 10"},
      {heif_chroma_interleaved_RRGGBBAA_BE, 16, "RRGGBBAA_BE 16"},
  };

  heif_encoding_options* options = heif_encoding_options_alloc();

  for (const auto& format : formats) {
    INFO(format.name);

    auto image = make_interleaved_image(format.chroma, format.bit_depth);

    auto encoder = unc_encoder_factory::get_unc_encoder(image, *options);
    REQUIRE(encoder);

    auto tile = (*encoder)->encode_tile(image);
    REQUIRE(tile);

    // The encoder must fill exactly the buffer it declares for a tile: a writer emitting more than
    // that overruns the buffer, one emitting less leaves the tail of the tile uninitialised.
    REQUIRE((*tile).size() == (*encoder)->compute_tile_data_size_bytes(WIDTH, HEIGHT));
  }

  heif_encoding_options_free(options);
}
