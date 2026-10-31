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

#include "image/pixelimage.h"
#include "catch_amalgamated.hpp"

TEST_CASE("planar channels allocate one component regardless of image chroma") {
  const auto chroma = GENERATE(heif_chroma_interleaved_RGB, heif_chroma_interleaved_RGBA,
                               heif_chroma_interleaved_RRGGBB_LE, heif_chroma_interleaved_RRGGBBAA_BE);
  const auto channel = GENERATE(heif_channel_Y, heif_channel_R, heif_channel_G, heif_channel_B);
  HeifPixelImage image;
  image.create(4, 4, heif_colorspace_RGB, chroma);
  REQUIRE(!image.add_channel(channel, 4, 4, 8, nullptr));
  REQUIRE(image.get_number_of_interleaved_components(channel) == 1);
  REQUIRE(image.get_storage_bits_per_pixel(channel) == 8);
}

TEST_CASE("copy_new_channel_from rejects incompatible interleaved layouts") {
  const auto source_chroma = GENERATE(heif_chroma_interleaved_RGB, heif_chroma_interleaved_RGBA,
                                      heif_chroma_interleaved_RRGGBB_BE, heif_chroma_interleaved_RRGGBB_LE);
  const auto destination_chroma = GENERATE(heif_chroma_interleaved_RGB, heif_chroma_interleaved_RGBA,
                                           heif_chroma_interleaved_RRGGBB_BE, heif_chroma_interleaved_RRGGBB_LE);
  auto source = std::make_shared<HeifPixelImage>();
  source->create(4, 4, heif_colorspace_RGB, source_chroma);
  const int depth = source_chroma == heif_chroma_interleaved_RGB ||
                    source_chroma == heif_chroma_interleaved_RGBA ? 8 : 10;
  REQUIRE(!source->add_channel(heif_channel_interleaved, 4, 4, depth, nullptr));
  size_t stride;
  auto* data = source->get_channel_memory(heif_channel_interleaved, &stride);
  data[0] = 37;
  HeifPixelImage destination;
  destination.create(4, 4, heif_colorspace_RGB, destination_chroma);
  auto err = destination.copy_new_channel_from(source, heif_channel_interleaved, heif_channel_interleaved, nullptr);
  if (source_chroma != destination_chroma) {
    REQUIRE(err.error_code == heif_error_Usage_error);
    REQUIRE_FALSE(destination.has_channel(heif_channel_interleaved));
  }
  else {
    REQUIRE(!err);
    REQUIRE(destination.get_number_of_interleaved_components(heif_channel_interleaved) ==
            source->get_number_of_interleaved_components(heif_channel_interleaved));
    REQUIRE(destination.get_channel_memory(heif_channel_interleaved, &stride)[0] == 37);
  }
  HeifPixelImage planar;
  planar.create(4, 4, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(planar.copy_new_channel_from(source, heif_channel_interleaved, heif_channel_Y, nullptr).error_code ==
          heif_error_Usage_error);
  REQUIRE_FALSE(planar.has_channel(heif_channel_Y));
}

TEST_CASE("alpha extraction and planar copying allocate only the copied component") {
  const auto chroma = GENERATE(heif_chroma_interleaved_RGB, heif_chroma_interleaved_RGBA,
                               heif_chroma_interleaved_RRGGBB_LE, heif_chroma_interleaved_RRGGBBAA_BE);
  const bool extract = GENERATE(false, true);
  auto source = std::make_shared<HeifPixelImage>();
  source->create(4, 4, heif_colorspace_RGB, heif_chroma_interleaved_RGBA);
  REQUIRE(!source->add_channel(heif_channel_interleaved, 4, 4, 8, nullptr));
  size_t stride;
  auto* data = source->get_channel_memory(heif_channel_interleaved, &stride);
  for (uint32_t y = 0; y < 4; y++) {
    for (uint32_t x = 0; x < 4; x++) {
      data[y * stride + x * 4 + 3] = static_cast<uint8_t>(x + y * 4);
    }
  }
  auto alpha = std::make_shared<HeifPixelImage>();
  alpha->create(4, 4, heif_colorspace_monochrome, heif_chroma_monochrome);
  REQUIRE(!alpha->extract_alpha_from_RGBA(source, nullptr));
  HeifPixelImage destination;
  destination.create(4, 4, heif_colorspace_RGB, chroma);
  REQUIRE(!(extract ? destination.extract_alpha_from_RGBA(source, nullptr) :
                      destination.copy_new_channel_from(alpha, heif_channel_Y, heif_channel_Y, nullptr)));
  REQUIRE(destination.get_number_of_interleaved_components(heif_channel_Y) == 1);
  REQUIRE(destination.get_storage_bits_per_pixel(heif_channel_Y) == 8);
  data = destination.get_channel_memory(heif_channel_Y, &stride);
  for (uint32_t y = 0; y < 4; y++) {
    for (uint32_t x = 0; x < 4; x++) {
      REQUIRE(data[y * stride + x] == x + y * 4);
    }
  }
}

// Regression tests for the Cb/Cr size check in HeifPixelImage::add_channel().
//
// A Cb/Cr plane must have exactly the chroma-subsampled size of the logical image
// (round_up). Building one at a different size makes the plane's allocation disagree
// with the size other code derives from the logical dimensions. An oversized plane
// let extend_to_size_with_zero() underflow its per-row fill length and write ~4 GB
// past the plane (GHSA-j2rv-58fh-w8pw); an undersized plane caused out-of-bounds
// reads during RGB conversion (issue #1796). add_channel() now rejects the mismatch
// at construction. Only Cb/Cr are constrained; alpha/depth/... may differ.

TEST_CASE("add_channel rejects an oversized Cb plane (GHSA-j2rv-58fh-w8pw)") {
  auto* limits = heif_get_global_security_limits();

  auto image = std::make_shared<HeifPixelImage>();
  image->create(12, 8, heif_colorspace_YCbCr, heif_chroma_420);
  REQUIRE(image->add_channel(heif_channel_Y, 12, 8, 8, limits).error_code == heif_error_Ok);

  // The subsampled Cb size of a 12x8 4:2:0 image is 6x4; 32x8 is far too large.
  Error err = image->add_channel(heif_channel_Cb, 32, 8, 8, limits);
  REQUIRE(err.error_code == heif_error_Usage_error);
  REQUIRE(err.sub_error_code == heif_suberror_Invalid_parameter_value);
}

TEST_CASE("add_channel rejects an undersized (round-down) chroma plane") {
  auto* limits = heif_get_global_security_limits();

  // Odd dimensions: the round_up subsampled size is 7x5, so a floor-division 6x4
  // plane (as an out-of-spec producer might compute) must be rejected.
  auto image = std::make_shared<HeifPixelImage>();
  image->create(13, 9, heif_colorspace_YCbCr, heif_chroma_420);
  REQUIRE(image->add_channel(heif_channel_Y, 13, 9, 8, limits).error_code == heif_error_Ok);

  REQUIRE(image->add_channel(heif_channel_Cb, 6, 4, 8, limits).error_code == heif_error_Usage_error);
  REQUIRE(image->add_channel(heif_channel_Cr, 6, 4, 8, limits).error_code == heif_error_Usage_error);
}

TEST_CASE("add_channel accepts correctly-subsampled chroma planes") {
  auto* limits = heif_get_global_security_limits();

  SECTION("4:2:0, even dimensions") {
    auto image = std::make_shared<HeifPixelImage>();
    image->create(12, 8, heif_colorspace_YCbCr, heif_chroma_420);
    REQUIRE(image->add_channel(heif_channel_Y, 12, 8, 8, limits).error_code == heif_error_Ok);
    REQUIRE(image->add_channel(heif_channel_Cb, 6, 4, 8, limits).error_code == heif_error_Ok);
    REQUIRE(image->add_channel(heif_channel_Cr, 6, 4, 8, limits).error_code == heif_error_Ok);
  }

  SECTION("4:2:0, odd dimensions round up") {
    auto image = std::make_shared<HeifPixelImage>();
    image->create(13, 9, heif_colorspace_YCbCr, heif_chroma_420);
    REQUIRE(image->add_channel(heif_channel_Y, 13, 9, 8, limits).error_code == heif_error_Ok);
    REQUIRE(image->add_channel(heif_channel_Cb, 7, 5, 8, limits).error_code == heif_error_Ok);
    REQUIRE(image->add_channel(heif_channel_Cr, 7, 5, 8, limits).error_code == heif_error_Ok);
  }

  SECTION("4:2:2 subsamples width only") {
    auto image = std::make_shared<HeifPixelImage>();
    image->create(12, 8, heif_colorspace_YCbCr, heif_chroma_422);
    REQUIRE(image->add_channel(heif_channel_Y, 12, 8, 8, limits).error_code == heif_error_Ok);
    REQUIRE(image->add_channel(heif_channel_Cb, 6, 8, 8, limits).error_code == heif_error_Ok);
    REQUIRE(image->add_channel(heif_channel_Cr, 6, 8, 8, limits).error_code == heif_error_Ok);
  }

  SECTION("4:4:4 chroma is full size") {
    auto image = std::make_shared<HeifPixelImage>();
    image->create(12, 8, heif_colorspace_YCbCr, heif_chroma_444);
    REQUIRE(image->add_channel(heif_channel_Y, 12, 8, 8, limits).error_code == heif_error_Ok);
    REQUIRE(image->add_channel(heif_channel_Cb, 12, 8, 8, limits).error_code == heif_error_Ok);
    REQUIRE(image->add_channel(heif_channel_Cr, 12, 8, 8, limits).error_code == heif_error_Ok);
  }
}

TEST_CASE("add_channel does not constrain non-chroma auxiliary planes") {
  auto* limits = heif_get_global_security_limits();

  // The size rule is Cb/Cr only. An auxiliary plane such as depth may legitimately
  // have a size unrelated to the colour planes, so add_channel must still accept it.
  auto image = std::make_shared<HeifPixelImage>();
  image->create(12, 8, heif_colorspace_YCbCr, heif_chroma_420);
  REQUIRE(image->add_channel(heif_channel_Y, 12, 8, 8, limits).error_code == heif_error_Ok);
  REQUIRE(image->add_channel(heif_channel_Cb, 6, 4, 8, limits).error_code == heif_error_Ok);
  REQUIRE(image->add_channel(heif_channel_Cr, 6, 4, 8, limits).error_code == heif_error_Ok);

  REQUIRE(image->add_channel(heif_channel_depth, 5, 5, 8, limits).error_code == heif_error_Ok);
}

// The interleaved chroma formats define the bit depth of their components: 8 bits for RGB
// and RGBA, 9 to 16 bits (stored as 16-bit words) for the RRGGBB formats. add_channel()
// accepted any bit depth for them. An interleaved plane with 64-bit or 128-bit components
// has pixels of more than 255 bits, for which get_storage_bits_per_pixel() ran into its
// assertion, or, without assertions, reported a truncated size (384 bits as 128, and 256
// bits as 0, which the API turned into "no such channel").
TEST_CASE("add_channel checks the bit depth of interleaved formats") {
  auto* limits = heif_get_global_security_limits();

  auto add = [limits](heif_chroma chroma, int bit_depth, std::shared_ptr<HeifPixelImage>* out_image = nullptr) {
    auto image = std::make_shared<HeifPixelImage>();
    image->create(16, 16, heif_colorspace_RGB, chroma);
    Error err = image->add_channel(heif_channel_interleaved, 16, 16, bit_depth, limits);
    if (out_image) {
      *out_image = image;
    }
    return err;
  };

  SECTION("RGB and RGBA have 8 bits per component") {
    for (heif_chroma chroma : {heif_chroma_interleaved_RGB, heif_chroma_interleaved_RGBA}) {
      const int num_components = (chroma == heif_chroma_interleaved_RGB) ? 3 : 4;
      INFO("chroma " << chroma);

      std::shared_ptr<HeifPixelImage> image;
      REQUIRE(!add(chroma, 8, &image));
      CHECK(image->get_bits_per_pixel(heif_channel_interleaved) == 8);
      CHECK(image->get_storage_bits_per_pixel(heif_channel_interleaved) == 8 * num_components);

      // for backwards compatibility, the size of the whole pixel is accepted as well
      REQUIRE(!add(chroma, 8 * num_components, &image));
      CHECK(image->get_bits_per_pixel(heif_channel_interleaved) == 8);
      CHECK(image->get_storage_bits_per_pixel(heif_channel_interleaved) == 8 * num_components);

      for (int bit_depth : {1, 7, 9, 10, 16, 64, 128}) {
        INFO("bit depth " << bit_depth);
        Error err = add(chroma, bit_depth);
        CHECK(err.error_code == heif_error_Usage_error);
        CHECK(err.sub_error_code == heif_suberror_Invalid_parameter_value);
      }
    }
  }

  SECTION("the RRGGBB formats have 9 to 16 bits per component") {
    for (heif_chroma chroma : {heif_chroma_interleaved_RRGGBB_LE, heif_chroma_interleaved_RRGGBB_BE,
                               heif_chroma_interleaved_RRGGBBAA_LE, heif_chroma_interleaved_RRGGBBAA_BE}) {
      const int num_components = (chroma == heif_chroma_interleaved_RRGGBB_LE ||
                                  chroma == heif_chroma_interleaved_RRGGBB_BE) ? 3 : 4;
      INFO("chroma " << chroma);

      for (int bit_depth : {9, 10, 12, 16}) {
        INFO("bit depth " << bit_depth);
        std::shared_ptr<HeifPixelImage> image;
        REQUIRE(!add(chroma, bit_depth, &image));
        CHECK(image->get_bits_per_pixel(heif_channel_interleaved) == bit_depth);
        CHECK(image->get_storage_bits_per_pixel(heif_channel_interleaved) == 16 * num_components);
      }

      for (int bit_depth : {1, 8, 17, 32, 64, 128}) {
        INFO("bit depth " << bit_depth);
        Error err = add(chroma, bit_depth);
        CHECK(err.error_code == heif_error_Usage_error);
        CHECK(err.sub_error_code == heif_suberror_Invalid_parameter_value);
      }
    }
  }

  SECTION("planes of other formats keep their wide components") {
    auto image = std::make_shared<HeifPixelImage>();
    image->create(16, 16, heif_colorspace_custom, heif_chroma_planar);
    REQUIRE(!image->add_channel(heif_channel_Y, 16, 16, 128, limits, heif_component_datatype_complex_number));
    CHECK(image->get_storage_bits_per_pixel(heif_channel_Y) == 128);
  }

  SECTION("a channel that does not exist") {
    auto image = std::make_shared<HeifPixelImage>();
    image->create(16, 16, heif_colorspace_RGB, heif_chroma_interleaved_RGB);
    CHECK(image->get_storage_bits_per_pixel(heif_channel_interleaved) == 0);
  }
}
