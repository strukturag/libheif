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

// ImageOverlay::parse() reads the canvas size and the per-image offsets from
// 2-byte fields, or from 4-byte fields when bit 0 of the flags byte is set
// (ISO/IEC 23008-12 6.6.2.2). The offsets are signed two's complement values.
//
// A regression in v1.19.2 (ab0565a6) sign-extended the 2-byte offsets as if
// they were 4 bytes wide, so every negative 16-bit offset came out as about
// -2^31 and the image was silently dropped from the canvas. That went unnoticed
// for a long time because the sign extension was only checked indirectly and
// only for a few values. tests/overlay_offsets.cc covers a handful of offsets
// through the full decode path. This file checks the field decoding itself over
// the complete 16-bit range and a broad sample of the 32-bit range, plus the
// write()/parse() round trip including the automatic field-size selection.

#include "image-items/overlay.h"
#include "catch_amalgamated.hpp"

#include <climits>
#include <cstdint>
#include <vector>


namespace {

void put_be(std::vector<uint8_t>& out, uint32_t v, int len)
{
  for (int i = len - 1; i >= 0; i--) {
    out.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
}

// ImageOverlay payload: version 0, flags, black background, a 1x1 canvas and
// then the raw offset fields in file order (x0, y0, x1, y1, ...).
std::vector<uint8_t> make_spec(bool long_fields, const std::vector<uint32_t>& raw_offset_fields)
{
  const int len = long_fields ? 4 : 2;

  std::vector<uint8_t> s;
  s.push_back(0);                    // version
  s.push_back(long_fields ? 1 : 0);  // flags
  for (int i = 0; i < 4; i++) {
    put_be(s, 0, 2);                 // background RGBA
  }
  put_be(s, 1, len);                 // canvas width
  put_be(s, 1, len);                 // canvas height
  for (uint32_t f : raw_offset_fields) {
    put_be(s, f, len);
  }

  return s;
}

// Parse one overlay holding all 'values' as x offsets and their bitwise
// complements as y offsets, and compare each decoded pair with the expected
// two's complement interpretation. Using the complement for y makes a wrong
// field order or a wrong read-pointer advance visible as well.
void check_offset_fields(bool long_fields, const std::vector<uint32_t>& values)
{
  std::vector<uint32_t> fields;
  fields.reserve(2 * values.size());

  const uint32_t field_mask = long_fields ? 0xFFFFFFFFu : 0xFFFFu;

  for (uint32_t v : values) {
    fields.push_back(v);
    fields.push_back(~v & field_mask);
  }

  ImageOverlay ovl;
  Error err = ovl.parse(values.size(), make_spec(long_fields, fields));
  INFO("parse error: " << err.message);
  REQUIRE(!err);
  REQUIRE(ovl.get_num_offsets() == values.size());

  for (size_t i = 0; i < values.size(); i++) {
    const uint32_t v = values[i];
    const uint32_t v_compl = ~v & field_mask;

    const int32_t expected_x = long_fields ? static_cast<int32_t>(v) : static_cast<int16_t>(v);
    const int32_t expected_y = long_fields ? static_cast<int32_t>(v_compl) : static_cast<int16_t>(v_compl);

    int32_t x = 0, y = 0;
    ovl.get_offset(i, &x, &y);

    // A plain REQUIRE per value would work but bloats the assertion count.
    if (x != expected_x || y != expected_y) {
      FAIL((long_fields ? 32 : 16) << "-bit field 0x" << std::hex << v << std::dec
           << " decoded as (" << x << "," << y << "), expected ("
           << expected_x << "," << expected_y << ")");
    }
  }

  SUCCEED();
}

} // namespace


TEST_CASE("overlay spec: every 16-bit offset value")
{
  std::vector<uint32_t> values;
  values.reserve(0x10000);
  for (uint32_t v = 0; v <= 0xFFFF; v++) {
    values.push_back(v);
  }

  check_offset_fields(false, values);
}


TEST_CASE("overlay spec: 32-bit offsets over the edges and a spread sample of the range")
{
  std::vector<uint32_t> values = {
      0, 1, 0x7FFF, 0x8000, 0xFFFF, 0x10000,
      0x7FFFFFFF, 0x80000000, 0x80000001,
      0xFFFF0000, 0xFFFF7FFF, 0xFFFF8000,
      0xFFFFFD80, // -640, the C021 offset
      0xFFFFFFFE, 0xFFFFFFFF,
  };

  // Multiples of an odd constant wrap around 2^32 and spread evenly over the
  // range, so every high byte and both signs are visited many times.
  uint32_t v = 0;
  for (int i = 0; i < 0x10000; i++) {
    v += 0x9E3779B1u;
    values.push_back(v);
  }

  check_offset_fields(true, values);
}


TEST_CASE("overlay spec: write() and parse() round trip with automatic field size")
{
  struct Case
  {
    uint32_t canvas_w, canvas_h;
    int32_t x, y;
    bool expect_long_fields;
  };

  const Case cases[] = {
      {640, 360, 0, 0, false},
      {640, 360, -640, -360, false},          // C021
      {640, 360, 32767, -32768, false},       // 16-bit extremes still fit
      {640, 360, 32768, 0, true},             // one past the positive 16-bit limit
      {640, 360, 0, -32769, true},            // one past the negative 16-bit limit
      {640, 360, INT32_MAX, INT32_MIN, true},
      {0xFFFF, 0xFFFF, -1, -1, false},        // largest 16-bit canvas
      {0x10000, 1, -1, -1, true},             // canvas forces long fields
  };

  for (const Case& c : cases) {
    INFO("canvas " << c.canvas_w << "x" << c.canvas_h << ", offset (" << c.x << "," << c.y << ")");

    ImageOverlay ovl;
    ovl.set_canvas_size(c.canvas_w, c.canvas_h);
    ovl.add_image_on_top(1, c.x, c.y);

    std::vector<uint8_t> data = ovl.write();

    const size_t field_len = c.expect_long_fields ? 4 : 2;
    REQUIRE(data.size() == 2 + 4 * 2 + field_len * 4);
    REQUIRE(data[0] == 0);
    REQUIRE(data[1] == (c.expect_long_fields ? 1 : 0));

    ImageOverlay parsed;
    Error err = parsed.parse(1, data);
    INFO("parse error: " << err.message);
    REQUIRE(!err);

    REQUIRE(parsed.get_canvas_width() == c.canvas_w);
    REQUIRE(parsed.get_canvas_height() == c.canvas_h);
    REQUIRE(parsed.get_num_offsets() == 1);

    int32_t x = 0, y = 0;
    parsed.get_offset(0, &x, &y);
    REQUIRE(x == c.x);
    REQUIRE(y == c.y);
  }
}
