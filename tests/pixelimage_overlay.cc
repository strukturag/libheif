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

#include <climits>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

// Regression tests for HeifPixelImage::overlay() with the overlay image placed
// partially or completely outside of the canvas.
//
// The former overlap computation clipped the overlay against the right and
// bottom canvas borders by shrinking in_w/in_h to an end coordinate, then
// against the left and top borders by converting them into a count, but the
// copy loops kept using them as end coordinates. An overlay with a negative
// offset of at least half its size was therefore not drawn at all, and smaller
// negative offsets drew a truncated part. The alpha path additionally added the
// source x offset twice and wrote to the wrong destination column.
//
// Every test composites a small overlay onto a uniformly filled canvas at some
// offset and compares each canvas pixel with a straightforward reference
// computation.

namespace {

const uint32_t CANVAS = 8;

// Non-square and not a divisor of the canvas size, to catch swapped axes.
const uint32_t OVL_W = 5;
const uint32_t OVL_H = 4;

const uint8_t BACKGROUND = 100;


// Overlay pixel values. Every (channel, x, y) combination is unique and none of
// the color values equals BACKGROUND.
uint8_t alpha_value(uint32_t x, uint32_t y)
{
  switch ((x + y) % 3) {
    case 0: return 0;
    case 1: return 255;
    default: return 128;
  }
}

uint8_t overlay_value(heif_channel ch, uint32_t x, uint32_t y)
{
  uint8_t idx = static_cast<uint8_t>(16 * y + x);
  switch (ch) {
    case heif_channel_R: return static_cast<uint8_t>(1 + idx);
    case heif_channel_G: return static_cast<uint8_t>(200 - idx);
    case heif_channel_B: return static_cast<uint8_t>(128 + idx);
    case heif_channel_Alpha: return alpha_value(x, y);
    default: return 0;
  }
}


std::shared_ptr<HeifPixelImage> make_canvas()
{
  auto img = std::make_shared<HeifPixelImage>();
  img->create(CANVAS, CANVAS, heif_colorspace_RGB, heif_chroma_444);

  for (heif_channel ch : {heif_channel_R, heif_channel_G, heif_channel_B}) {
    REQUIRE(img->add_channel(ch, CANVAS, CANVAS, 8, heif_get_global_security_limits()).error_code == heif_error_Ok);

    size_t stride = 0;
    uint8_t* p = img->get_channel_memory(ch, &stride);
    for (uint32_t y = 0; y < CANVAS; y++) {
      memset(p + y * stride, BACKGROUND, CANVAS);
    }
  }

  return img;
}


std::shared_ptr<HeifPixelImage> make_overlay(bool with_alpha)
{
  auto img = std::make_shared<HeifPixelImage>();
  img->create(OVL_W, OVL_H, heif_colorspace_RGB, heif_chroma_444);

  std::vector<heif_channel> channels = {heif_channel_R, heif_channel_G, heif_channel_B};
  if (with_alpha) {
    channels.push_back(heif_channel_Alpha);
  }

  for (heif_channel ch : channels) {
    REQUIRE(img->add_channel(ch, OVL_W, OVL_H, 8, heif_get_global_security_limits()).error_code == heif_error_Ok);

    size_t stride = 0;
    uint8_t* p = img->get_channel_memory(ch, &stride);
    for (uint32_t y = 0; y < OVL_H; y++) {
      for (uint32_t x = 0; x < OVL_W; x++) {
        p[y * stride + x] = overlay_value(ch, x, y);
      }
    }
  }

  return img;
}


// The value that canvas pixel (cx,cy) must have after compositing the overlay at (dx,dy).
uint8_t expected_value(heif_channel ch, uint32_t cx, uint32_t cy, int32_t dx, int32_t dy, bool with_alpha)
{
  int64_t ox = static_cast<int64_t>(cx) - dx;
  int64_t oy = static_cast<int64_t>(cy) - dy;

  if (ox < 0 || oy < 0 || ox >= OVL_W || oy >= OVL_H) {
    return BACKGROUND;
  }

  uint8_t in = overlay_value(ch, static_cast<uint32_t>(ox), static_cast<uint32_t>(oy));
  if (!with_alpha) {
    return in;
  }

  int a = alpha_value(static_cast<uint32_t>(ox), static_cast<uint32_t>(oy));
  return static_cast<uint8_t>((in * a + BACKGROUND * (255 - a)) / 255);
}


void check_overlay_at(int32_t dx, int32_t dy, bool with_alpha)
{
  INFO("offset (" << dx << "," << dy << "), alpha=" << with_alpha);

  auto canvas = make_canvas();
  auto overlay = make_overlay(with_alpha);

  REQUIRE(canvas->overlay(overlay, dx, dy).error_code == heif_error_Ok);

  for (heif_channel ch : {heif_channel_R, heif_channel_G, heif_channel_B}) {
    size_t stride = 0;
    const uint8_t* p = canvas->get_channel_memory(ch, &stride);

    for (uint32_t cy = 0; cy < CANVAS; cy++) {
      for (uint32_t cx = 0; cx < CANVAS; cx++) {
        INFO("channel " << static_cast<int>(ch) << ", canvas pixel (" << cx << "," << cy << ")");
        REQUIRE(p[cy * stride + cx] == expected_value(ch, cx, cy, dx, dy, with_alpha));
      }
    }
  }
}


struct Offset {
  int32_t dx;
  int32_t dy;
};

// Offsets that leave part of the overlay on the canvas.
const std::vector<Offset> partially_visible_offsets = {
    {2, 3},    // completely inside
    {0, 0},    // aligned with the top-left corner
    {-1, -1},  // slightly outside on the top-left; used to draw a truncated part
    {-3, -2},  // outside by at least half its size on both axes; used to be dropped
    {-4, 0},   // one column left
    {0, -3},   // one row left
    {-4, 3},   // negative x only
    {3, -3},   // negative y only
    {6, 5},    // partially outside on the right and bottom
    {7, 7},    // one pixel visible at the bottom-right corner
    {-2, 6},   // outside on the left and bottom
    {5, -1},   // outside on the right and top
};

// Offsets that place the overlay completely outside of the canvas, including
// the extreme values, which must neither draw anything nor overflow.
const std::vector<Offset> invisible_offsets = {
    {-5, 0}, {0, -4}, {8, 0}, {0, 8}, {-5, -4}, {8, 8},
    {INT32_MIN, 0}, {0, INT32_MIN}, {INT32_MIN, INT32_MIN},
    {INT32_MAX, 0}, {0, INT32_MAX}, {INT32_MAX, INT32_MAX},
    {INT32_MIN, INT32_MAX},
};

} // namespace


TEST_CASE("overlay without alpha at offsets partially outside the canvas") {
  for (const auto& o : partially_visible_offsets) {
    check_overlay_at(o.dx, o.dy, false);
  }
}

TEST_CASE("overlay with alpha at offsets partially outside the canvas") {
  for (const auto& o : partially_visible_offsets) {
    check_overlay_at(o.dx, o.dy, true);
  }
}

TEST_CASE("overlay completely outside the canvas leaves it unchanged") {
  for (const auto& o : invisible_offsets) {
    check_overlay_at(o.dx, o.dy, false);
    check_overlay_at(o.dx, o.dy, true);
  }
}


// overlay() reads all planes of the overlay with the coordinates of the color planes and
// blends them byte by byte. HeifPixelImage itself puts no constraint on the size or the
// bit depth of R, G, B and alpha planes, so overlay() has to refuse an image that does not
// have the form it needs. It only compared the alpha plane with the size of the image:
// with color planes that are larger than the image, the alpha plane passed that check and
// the blend loop read past its end.

namespace {

// An overlay whose planes can have sizes and bit depths that do not fit the image.
std::shared_ptr<HeifPixelImage> make_irregular_overlay(uint32_t image_size,
                                                       uint32_t color_plane_size, int color_bits,
                                                       uint32_t alpha_plane_size, int alpha_bits)
{
  auto* limits = heif_get_global_security_limits();

  auto img = std::make_shared<HeifPixelImage>();
  img->create(image_size, image_size, heif_colorspace_RGB, heif_chroma_444);

  for (heif_channel ch : {heif_channel_R, heif_channel_G, heif_channel_B}) {
    REQUIRE(img->fill_new_channel(ch, 100, color_plane_size, color_plane_size, color_bits, limits).error_code == heif_error_Ok);
  }

  if (alpha_plane_size > 0) {
    REQUIRE(img->fill_new_channel(heif_channel_Alpha, 100, alpha_plane_size, alpha_plane_size, alpha_bits, limits).error_code == heif_error_Ok);
  }

  return img;
}

void require_canvas_unchanged(const std::shared_ptr<HeifPixelImage>& canvas)
{
  for (heif_channel ch : {heif_channel_R, heif_channel_G, heif_channel_B}) {
    size_t stride = 0;
    const uint8_t* p = canvas->get_channel_memory(ch, &stride);
    for (uint32_t y = 0; y < CANVAS; y++) {
      for (uint32_t x = 0; x < CANVAS; x++) {
        REQUIRE(p[y * stride + x] == BACKGROUND);
      }
    }
  }
}

} // namespace


TEST_CASE("overlay refuses an image whose planes do not have the size of the image") {
  auto canvas = make_canvas();

  SECTION("control: regular overlay with alpha") {
    auto overlay = make_irregular_overlay(4, 4, 8, 4, 8);
    CHECK(canvas->overlay(overlay, 0, 0).error_code == heif_error_Ok);
  }

  SECTION("color planes larger than the image, alpha plane of the image size") {
    // 64x64 color planes on a 4x4 image: the blend loop used to read 64 rows and columns of
    // an alpha plane that has 4.
    auto overlay = make_irregular_overlay(4, 64, 8, 4, 8);
    CHECK(canvas->overlay(overlay, 0, 0).error_code == heif_error_Unsupported_feature);
    require_canvas_unchanged(canvas);
  }

  SECTION("color planes larger than the image, no alpha plane") {
    auto overlay = make_irregular_overlay(4, 64, 8, 0, 8);
    CHECK(canvas->overlay(overlay, 0, 0).error_code == heif_error_Unsupported_feature);
    require_canvas_unchanged(canvas);
  }

  SECTION("alpha plane smaller than the image") {
    auto overlay = make_irregular_overlay(4, 4, 8, 2, 8);
    CHECK(canvas->overlay(overlay, 0, 0).error_code == heif_error_Unsupported_feature);
    require_canvas_unchanged(canvas);
  }

  SECTION("alpha plane larger than the image") {
    auto overlay = make_irregular_overlay(4, 4, 8, 64, 8);
    CHECK(canvas->overlay(overlay, 0, 0).error_code == heif_error_Unsupported_feature);
    require_canvas_unchanged(canvas);
  }
}


// --- samples with more than 8 bits
//
// overlay() composed the planes byte by byte whatever their bit depth. The two bytes of a
// 16-bit sample were blended like two pixels, and only the left half of each row was
// drawn. The samples are now composed with their own width (8 or 16 bits), the alpha plane
// may have another bit depth than the color planes, and what cannot be composed (planes
// with more than 16 bits, samples that are not unsigned integers, an overlay with another
// bit depth than the canvas) is refused.

namespace {

uint32_t max_sample(int bits)
{
  return (uint32_t{1} << bits) - 1;
}

// The values use the whole range of the bit depth, with different upper and lower bytes.
uint16_t wide_background(int bits)
{
  return static_cast<uint16_t>((BACKGROUND * 257 + 77) >> (16 - bits));
}

uint16_t wide_alpha_value(uint32_t x, uint32_t y, int bits)
{
  switch ((x + y) % 3) {
    case 0: return 0;
    case 1: return static_cast<uint16_t>(max_sample(bits));
    default: return static_cast<uint16_t>(max_sample(bits) / 3);
  }
}

uint16_t wide_overlay_value(heif_channel ch, uint32_t x, uint32_t y, int bits)
{
  if (ch == heif_channel_Alpha) {
    return wide_alpha_value(x, y, bits);
  }

  return static_cast<uint16_t>(((overlay_value(ch, x, y) * 257) ^ 0x5A) >> (16 - bits));
}

void write_sample(const std::shared_ptr<HeifPixelImage>& img, heif_channel ch, uint32_t x, uint32_t y, uint16_t value)
{
  size_t stride = 0;
  uint8_t* p = img->get_channel_memory(ch, &stride);

  if (img->get_bits_per_pixel(ch) <= 8) {
    p[y * stride + x] = static_cast<uint8_t>(value);
  }
  else {
    memcpy(p + y * stride + 2 * x, &value, 2);
  }
}

uint16_t read_sample(const std::shared_ptr<HeifPixelImage>& img, heif_channel ch, uint32_t x, uint32_t y)
{
  size_t stride = 0;
  const uint8_t* p = img->get_channel_memory(ch, &stride);

  if (img->get_bits_per_pixel(ch) <= 8) {
    return p[y * stride + x];
  }

  uint16_t value;
  memcpy(&value, p + y * stride + 2 * x, 2);
  return value;
}

std::shared_ptr<HeifPixelImage> make_wide_canvas(int bits,
                                                 heif_component_datatype datatype = heif_component_datatype_unsigned_integer)
{
  auto img = std::make_shared<HeifPixelImage>();
  img->create(CANVAS, CANVAS, heif_colorspace_RGB, heif_chroma_444);

  for (heif_channel ch : {heif_channel_R, heif_channel_G, heif_channel_B}) {
    REQUIRE(img->add_channel(ch, CANVAS, CANVAS, bits, heif_get_global_security_limits(), datatype).error_code == heif_error_Ok);

    if (bits <= 16) {
      for (uint32_t y = 0; y < CANVAS; y++) {
        for (uint32_t x = 0; x < CANVAS; x++) {
          write_sample(img, ch, x, y, wide_background(bits));
        }
      }
    }
  }

  return img;
}

// 'alpha_bits' = 0: no alpha plane.
std::shared_ptr<HeifPixelImage> make_wide_overlay(int color_bits, int alpha_bits,
                                                  heif_component_datatype color_datatype = heif_component_datatype_unsigned_integer,
                                                  heif_component_datatype alpha_datatype = heif_component_datatype_unsigned_integer)
{
  auto img = std::make_shared<HeifPixelImage>();
  img->create(OVL_W, OVL_H, heif_colorspace_RGB, heif_chroma_444);

  std::vector<heif_channel> channels = {heif_channel_R, heif_channel_G, heif_channel_B};
  if (alpha_bits > 0) {
    channels.push_back(heif_channel_Alpha);
  }

  for (heif_channel ch : channels) {
    const bool is_alpha = (ch == heif_channel_Alpha);
    const int bits = is_alpha ? alpha_bits : color_bits;

    REQUIRE(img->add_channel(ch, OVL_W, OVL_H, bits, heif_get_global_security_limits(),
                             is_alpha ? alpha_datatype : color_datatype).error_code == heif_error_Ok);

    if (bits <= 16) {
      for (uint32_t y = 0; y < OVL_H; y++) {
        for (uint32_t x = 0; x < OVL_W; x++) {
          write_sample(img, ch, x, y, wide_overlay_value(ch, x, y, bits));
        }
      }
    }
  }

  return img;
}

void require_wide_canvas_unchanged(const std::shared_ptr<HeifPixelImage>& canvas, int bits)
{
  for (heif_channel ch : {heif_channel_R, heif_channel_G, heif_channel_B}) {
    for (uint32_t y = 0; y < CANVAS; y++) {
      for (uint32_t x = 0; x < CANVAS; x++) {
        REQUIRE(read_sample(canvas, ch, x, y) == wide_background(bits));
      }
    }
  }
}

void check_wide_overlay_at(int32_t dx, int32_t dy, int color_bits, int alpha_bits)
{
  INFO("offset (" << dx << "," << dy << "), " << color_bits << "-bit color, " << alpha_bits << "-bit alpha");

  auto canvas = make_wide_canvas(color_bits);
  auto overlay = make_wide_overlay(color_bits, alpha_bits);

  Error err = canvas->overlay(overlay, dx, dy);
  INFO("error: " << err.message);
  REQUIRE(err.error_code == heif_error_Ok);

  for (heif_channel ch : {heif_channel_R, heif_channel_G, heif_channel_B}) {
    REQUIRE(canvas->get_bits_per_pixel(ch) == color_bits);

    for (uint32_t cy = 0; cy < CANVAS; cy++) {
      for (uint32_t cx = 0; cx < CANVAS; cx++) {
        INFO("channel " << static_cast<int>(ch) << ", canvas pixel (" << cx << "," << cy << ")");

        int64_t ox = static_cast<int64_t>(cx) - dx;
        int64_t oy = static_cast<int64_t>(cy) - dy;

        uint64_t expected = wide_background(color_bits);
        if (ox >= 0 && oy >= 0 && ox < OVL_W && oy < OVL_H) {
          uint64_t in = wide_overlay_value(ch, static_cast<uint32_t>(ox), static_cast<uint32_t>(oy), color_bits);

          if (alpha_bits == 0) {
            expected = in;
          }
          else {
            uint64_t a = wide_alpha_value(static_cast<uint32_t>(ox), static_cast<uint32_t>(oy), alpha_bits);
            uint64_t a_max = max_sample(alpha_bits);
            expected = (in * a + expected * (a_max - a)) / a_max;
          }
        }

        REQUIRE(read_sample(canvas, ch, cx, cy) == expected);
      }
    }
  }
}

} // namespace


TEST_CASE("overlay of images with more than 8 bits per sample") {
  // {color bits, alpha bits}; 0 = no alpha plane
  const std::vector<std::pair<int, int>> formats = {
      {10, 0}, {12, 0}, {16, 0},     // no alpha
      {10, 10}, {12, 12}, {16, 16},  // alpha with the bit depth of the color planes
      {10, 8}, {16, 8},              // 8-bit alpha on wide color planes
      {8, 16}, {8, 10},              // wide alpha on 8-bit color planes
      {12, 16}, {16, 9},             // different wide bit depths
      {8, 1}, {16, 1},               // binary alpha
  };

  for (const auto& format : formats) {
    for (const auto& o : partially_visible_offsets) {
      check_wide_overlay_at(o.dx, o.dy, format.first, format.second);
    }

    for (const auto& o : invisible_offsets) {
      check_wide_overlay_at(o.dx, o.dy, format.first, format.second);
    }
  }
}


TEST_CASE("overlay limits alpha samples to the range of their bit depth") {
  // Nothing guarantees that the samples of a 10-bit plane are below 1024. With an alpha
  // sample above the maximum, the weight of the canvas (maximum - alpha) would wrap around.
  for (int color_bits : {8, 16}) {
    auto canvas = make_wide_canvas(color_bits);
    auto overlay = make_wide_overlay(color_bits, 10);

    for (uint32_t y = 0; y < OVL_H; y++) {
      for (uint32_t x = 0; x < OVL_W; x++) {
        write_sample(overlay, heif_channel_Alpha, x, y, 0xFFFF);
      }
    }

    REQUIRE(canvas->overlay(overlay, 0, 0).error_code == heif_error_Ok);

    // treated as an opaque pixel
    for (heif_channel ch : {heif_channel_R, heif_channel_G, heif_channel_B}) {
      for (uint32_t y = 0; y < OVL_H; y++) {
        for (uint32_t x = 0; x < OVL_W; x++) {
          REQUIRE(read_sample(canvas, ch, x, y) == wide_overlay_value(ch, x, y, color_bits));
        }
      }
    }
  }
}


TEST_CASE("overlay refuses sample formats that it cannot compose") {
  SECTION("overlay with more bits than the canvas") {
    for (int bits : {10, 16}) {
      auto canvas = make_wide_canvas(8);
      auto overlay = make_wide_overlay(bits, 0);
      CHECK(canvas->overlay(overlay, 0, 0).error_code == heif_error_Unsupported_feature);
      require_wide_canvas_unchanged(canvas, 8);
    }
  }

  SECTION("overlay with fewer bits than the canvas") {
    for (int alpha_bits : {0, 8}) {
      auto canvas = make_wide_canvas(16);
      auto overlay = make_wide_overlay(8, alpha_bits);
      CHECK(canvas->overlay(overlay, 0, 0).error_code == heif_error_Unsupported_feature);
      require_wide_canvas_unchanged(canvas, 16);
    }
  }

  SECTION("different bit depths with the same sample size") {
    auto canvas = make_wide_canvas(12);
    auto overlay = make_wide_overlay(10, 0);
    CHECK(canvas->overlay(overlay, 0, 0).error_code == heif_error_Unsupported_feature);
    require_wide_canvas_unchanged(canvas, 12);
  }

  SECTION("color planes with more than 16 bits") {
    auto canvas = make_wide_canvas(32);
    auto overlay = make_wide_overlay(32, 0);
    CHECK(canvas->overlay(overlay, 0, 0).error_code == heif_error_Unsupported_feature);
  }

  SECTION("alpha plane with more than 16 bits") {
    auto canvas = make_wide_canvas(8);
    auto overlay = make_wide_overlay(8, 32);
    CHECK(canvas->overlay(overlay, 0, 0).error_code == heif_error_Unsupported_feature);
    require_wide_canvas_unchanged(canvas, 8);
  }

  SECTION("samples that are not unsigned integers") {
    {
      auto canvas = make_wide_canvas(16, heif_component_datatype_signed_integer);
      auto overlay = make_wide_overlay(16, 0, heif_component_datatype_signed_integer);
      CHECK(canvas->overlay(overlay, 0, 0).error_code == heif_error_Unsupported_feature);
    }

    {
      auto canvas = make_wide_canvas(16);
      auto overlay = make_wide_overlay(16, 16, heif_component_datatype_unsigned_integer, heif_component_datatype_signed_integer);
      CHECK(canvas->overlay(overlay, 0, 0).error_code == heif_error_Unsupported_feature);
      require_wide_canvas_unchanged(canvas, 16);
    }

    {
      auto canvas = make_wide_canvas(16, heif_component_datatype_signed_integer);
      auto overlay = make_wide_overlay(16, 0);
      CHECK(canvas->overlay(overlay, 0, 0).error_code == heif_error_Unsupported_feature);
    }
  }
}
