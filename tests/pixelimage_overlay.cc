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
