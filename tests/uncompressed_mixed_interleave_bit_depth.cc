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

// In mixed interleave mode of 'unci' (ISO/IEC 23001-17, 5.2.1.6.4), the Cb and Cr samples
// are stored like the components of a pixel in pixel interleave mode: each value is coded
// with exactly component_bit_depth bits, or, if component_align_size is set, as a
// byte-aligned word whose upper bits are padding.
//
// The decoder read each chroma sample with the width of the plane's storage word instead
// (16 bits for a 12-bit component). The bits of the neighbouring sample, or the padding bits,
// ended up in the upper bits of the value, so the decoded planes contained samples far above
// the bit depth they declare (GHSA-v5rj-g4wv-j5w3). Code that relies on the bit depth
// (libsharpyuv uses the samples as table indices) then read out of bounds. For well-formed
// files with a chroma bit depth other than 8 or 16, the chroma planes were simply decoded
// wrongly. The existing test files only use 8 and 16 bits, where the two widths coincide.
//
// These tests write the sample data with their own bit writer, following the specification,
// and require that every sample is decoded exactly as it was written. Padding bits are
// written as ones in one of the variants: the specification wants them to be zero, but a
// decoder must not let them leak into the sample values.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test_utils.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

struct Component
{
  uint16_t type;       // 1 = Y, 2 = Cb, 3 = Cr
  uint8_t bit_depth;
  uint8_t align_size;  // component_align_size
};

struct Layout
{
  std::string name;
  uint32_t width;
  uint32_t height;
  uint8_t sampling_type;  // 1 = 4:2:2, 2 = 4:2:0
  std::vector<Component> components;
  uint32_t row_align_size;
};


class TestBitWriter
{
public:
  explicit TestBitWriter(bool padding_bit) : m_padding_bit(padding_bit) {}

  void write_bits(uint32_t value, int n)
  {
    for (int i = n - 1; i >= 0; i--) {
      write_bit((value >> i) & 1);
    }
  }

  void write_padding_bits(int n)
  {
    for (int i = 0; i < n; i++) {
      write_bit(m_padding_bit);
    }
  }

  void pad_to_byte_boundary()
  {
    while (m_bits_in_last_byte != 0) {
      write_bit(m_padding_bit);
    }
  }

  void pad_row(size_t row_start, uint32_t row_align_size)
  {
    pad_to_byte_boundary();
    if (row_align_size != 0) {
      while ((m_data.size() - row_start) % row_align_size != 0) {
        m_data.push_back(m_padding_bit ? 0xFF : 0x00);
      }
    }
  }

  // Only valid on a byte boundary.
  size_t size() const { return m_data.size(); }

  const std::vector<uint8_t>& data() const { return m_data; }

private:
  void write_bit(uint32_t bit)
  {
    if (m_bits_in_last_byte == 0) {
      m_data.push_back(0);
    }
    if (bit) {
      m_data.back() = static_cast<uint8_t>(m_data.back() | (0x80 >> m_bits_in_last_byte));
    }
    m_bits_in_last_byte = (m_bits_in_last_byte + 1) % 8;
  }

  std::vector<uint8_t> m_data;
  int m_bits_in_last_byte = 0;
  bool m_padding_bit;
};


// The value that is stored for a sample. It uses the whole range of the bit depth, including
// the largest value, so that a bit that is taken from the wrong place shows up.
uint32_t sample_value(const Component& c, uint32_t x, uint32_t y)
{
  uint32_t max_value = (1u << c.bit_depth) - 1;
  uint32_t v = (x * 37 + y * 101 + c.type * 211) * 2654435761u;
  if ((x + y) % 3 == 0) {
    return max_value;
  }
  return (v >> 7) & max_value;
}


bool is_chroma(const Component& c) { return c.type == 2 || c.type == 3; }


void write_component_value(TestBitWriter& writer, const Component& c, uint32_t value)
{
  if (c.align_size != 0) {
    writer.pad_to_byte_boundary();
    writer.write_padding_bits(c.align_size * 8 - c.bit_depth);
  }
  writer.write_bits(value, c.bit_depth);
}


std::vector<uint8_t> build_sample_data(const Layout& layout, bool padding_bit)
{
  uint32_t chroma_width = layout.width / 2;
  uint32_t chroma_height = (layout.sampling_type == 2) ? layout.height / 2 : layout.height;

  TestBitWriter writer(padding_bit);
  bool chroma_written = false;

  for (size_t i = 0; i < layout.components.size(); i++) {
    const Component& c = layout.components[i];

    if (!is_chroma(c)) {
      // as in component interleave mode
      for (uint32_t y = 0; y < layout.height; y++) {
        size_t row_start = writer.size();
        for (uint32_t x = 0; x < layout.width; x++) {
          write_component_value(writer, c, sample_value(c, x, y));
        }
        writer.pad_row(row_start, layout.row_align_size);
      }
    }
    else if (!chroma_written) {
      // The two chroma components are consecutive in the component list. They are stored
      // as in pixel interleave mode, in the order in which they are declared.
      const Component& c2 = layout.components[i + 1];
      REQUIRE(is_chroma(c2));

      for (uint32_t y = 0; y < chroma_height; y++) {
        size_t row_start = writer.size();
        for (uint32_t x = 0; x < chroma_width; x++) {
          write_component_value(writer, c, sample_value(c, x, y));
          write_component_value(writer, c2, sample_value(c2, x, y));
        }
        writer.pad_row(row_start, layout.row_align_size);
      }
      chroma_written = true;
    }
  }

  return writer.data();
}


std::vector<uint8_t> build_file(const Layout& layout, bool padding_bit)
{
  std::vector<uint8_t> ftyp_payload;
  append_fourcc(ftyp_payload, "mif1");
  put_u32_be(ftyp_payload, 0);
  append_fourcc(ftyp_payload, "mif1");
  append_fourcc(ftyp_payload, "heic");
  auto ftyp = make_box("ftyp", ftyp_payload);

  std::vector<uint8_t> hdlr_payload;
  put_u32_be(hdlr_payload, 0);
  append_fourcc(hdlr_payload, "pict");
  put_u32_be(hdlr_payload, 0);
  put_u32_be(hdlr_payload, 0);
  put_u32_be(hdlr_payload, 0);
  hdlr_payload.push_back(0);
  auto hdlr = make_box("hdlr", hdlr_payload, /*full=*/true);

  std::vector<uint8_t> pitm_payload;
  put_u16_be(pitm_payload, 1);
  auto pitm = make_box("pitm", pitm_payload, /*full=*/true);

  std::vector<uint8_t> infe_payload;
  put_u16_be(infe_payload, 1);
  put_u16_be(infe_payload, 0);
  append_fourcc(infe_payload, "unci");
  append_cstr(infe_payload, "");
  auto infe = make_box("infe", infe_payload, /*full=*/true, /*version=*/2);

  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, 1);
  append(iinf_payload, infe);
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  std::vector<uint8_t> ispe_payload;
  put_u32_be(ispe_payload, layout.width);
  put_u32_be(ispe_payload, layout.height);
  auto ispe = make_box("ispe", ispe_payload, /*full=*/true);

  std::vector<uint8_t> cmpd_payload;
  put_u32_be(cmpd_payload, static_cast<uint32_t>(layout.components.size()));
  for (const Component& c : layout.components) {
    put_u16_be(cmpd_payload, c.type);
  }
  auto cmpd = make_box("cmpd", cmpd_payload);

  std::vector<uint8_t> uncC_payload;
  put_u32_be(uncC_payload, 0); // profile
  put_u32_be(uncC_payload, static_cast<uint32_t>(layout.components.size()));
  for (uint16_t idx = 0; idx < layout.components.size(); idx++) {
    put_u16_be(uncC_payload, idx);                                               // component_index
    uncC_payload.push_back(static_cast<uint8_t>(layout.components[idx].bit_depth - 1)); // component_bit_depth_minus_one
    uncC_payload.push_back(0);                                                   // component_format (unsigned)
    uncC_payload.push_back(layout.components[idx].align_size);                   // component_align_size
  }
  uncC_payload.push_back(layout.sampling_type);
  uncC_payload.push_back(2);                     // interleave_type = mixed
  uncC_payload.push_back(0);                     // block_size
  uncC_payload.push_back(0);                     // flags (big-endian components)
  put_u32_be(uncC_payload, 0);                   // pixel_size
  put_u32_be(uncC_payload, layout.row_align_size);
  put_u32_be(uncC_payload, 0);                   // tile_align_size
  put_u32_be(uncC_payload, 0);                   // num_tile_cols_minus_one
  put_u32_be(uncC_payload, 0);                   // num_tile_rows_minus_one
  auto uncC = make_box("uncC", uncC_payload, /*full=*/true);

  std::vector<uint8_t> ipco_payload;
  append(ipco_payload, ispe);
  append(ipco_payload, cmpd);
  append(ipco_payload, uncC);
  auto ipco = make_box("ipco", ipco_payload);

  std::vector<uint8_t> ipma_payload;
  put_u32_be(ipma_payload, 1);      // entry_count
  put_u16_be(ipma_payload, 1);      // item_ID 1
  ipma_payload.push_back(3);        // association_count
  ipma_payload.push_back(0x80 | 1); // essential, ispe
  ipma_payload.push_back(0x80 | 2); // essential, cmpd
  ipma_payload.push_back(0x80 | 3); // essential, uncC
  auto ipma = make_box("ipma", ipma_payload, /*full=*/true);

  std::vector<uint8_t> iprp_payload;
  append(iprp_payload, ipco);
  append(iprp_payload, ipma);
  auto iprp = make_box("iprp", iprp_payload);

  // The item contains exactly the bytes of the image. If the decoder computed a larger
  // size for the sample data than the layout has, it would refuse the file.
  std::vector<uint8_t> sample_data = build_sample_data(layout, padding_bit);
  auto idat = make_box("idat", sample_data);

  std::vector<uint8_t> iloc_payload;
  put_u16_be(iloc_payload, (4 << 12) | (4 << 8)); // offset_size=4, length_size=4
  put_u16_be(iloc_payload, 1);      // item_count
  put_u16_be(iloc_payload, 1);      // item_ID
  put_u16_be(iloc_payload, 0x0001); // construction_method=1 (idat)
  put_u16_be(iloc_payload, 0);      // data_reference_index
  put_u16_be(iloc_payload, 1);      // extent_count
  put_u32_be(iloc_payload, 0);      // extent_offset (within idat)
  put_u32_be(iloc_payload, static_cast<uint32_t>(sample_data.size())); // extent_length
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true, /*version=*/1);

  std::vector<uint8_t> meta_payload;
  append(meta_payload, hdlr);
  append(meta_payload, pitm);
  append(meta_payload, iinf);
  append(meta_payload, iprp);
  append(meta_payload, iloc);
  append(meta_payload, idat);
  auto meta = make_box("meta", meta_payload, /*full=*/true);

  std::vector<uint8_t> file;
  append(file, ftyp);
  append(file, meta);
  return file;
}


heif_channel channel_of(const Component& c)
{
  switch (c.type) {
    case 2: return heif_channel_Cb;
    case 3: return heif_channel_Cr;
    default: return heif_channel_Y;
  }
}


void check_decoding(const Layout& layout, bool padding_bit)
{
  INFO(layout.name << ", padding bits " << (padding_bit ? 1 : 0));

  std::vector<uint8_t> file = build_file(layout, padding_bit);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  INFO("read: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);

  // Decode without any conversion, so that the planes come back as the decoder filled them.
  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
  INFO("decode: " << err.message);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(img != nullptr);

  uint32_t chroma_width = layout.width / 2;
  uint32_t chroma_height = (layout.sampling_type == 2) ? layout.height / 2 : layout.height;

  for (const Component& c : layout.components) {
    heif_channel channel = channel_of(c);
    uint32_t w = is_chroma(c) ? chroma_width : layout.width;
    uint32_t h = is_chroma(c) ? chroma_height : layout.height;

    REQUIRE(heif_image_get_bits_per_pixel_range(img, channel) == c.bit_depth);
    REQUIRE(heif_image_get_width(img, channel) == static_cast<int>(w));
    REQUIRE(heif_image_get_height(img, channel) == static_cast<int>(h));

    size_t stride = 0;
    const uint8_t* plane = heif_image_get_plane_readonly2(img, channel, &stride);
    REQUIRE(plane != nullptr);

    const uint32_t max_value = (1u << c.bit_depth) - 1;

    for (uint32_t y = 0; y < h; y++) {
      for (uint32_t x = 0; x < w; x++) {
        uint32_t value;
        if (c.bit_depth <= 8) {
          value = plane[y * stride + x];
        }
        else {
          value = reinterpret_cast<const uint16_t*>(plane + y * stride)[x];
        }

        INFO("component type " << c.type << " at (" << x << "," << y << ")");
        REQUIRE(value <= max_value);
        REQUIRE(value == sample_value(c, x, y));
      }
    }
  }

  heif_image_release(img);
  heif_image_handle_release(handle);
  heif_context_free(ctx);
}

} // namespace


TEST_CASE("unci mixed interleave reads chroma samples with their component bit depth")
{
  const uint16_t Y = 1, Cb = 2, Cr = 3;

  const std::vector<Layout> layouts = {
      // The configuration of the report: all components 12 bit, 4:2:2, no alignment.
      {"12 bit 4:2:2", 16, 8, 1, {{Y, 12, 0}, {Cb, 12, 0}, {Cr, 12, 0}}, 0},
      {"12 bit 4:2:0", 16, 8, 2, {{Y, 12, 0}, {Cb, 12, 0}, {Cr, 12, 0}}, 0},
      {"10 bit 4:2:0", 16, 8, 2, {{Y, 10, 0}, {Cb, 10, 0}, {Cr, 10, 0}}, 0},

      // Rows that do not end on a byte boundary: 6 luma samples of 10 bits are 60 bits,
      // 3 chroma pairs of 2 x 10 bits are 60 bits.
      {"10 bit 4:2:0, rows with padding to the byte boundary", 6, 4, 2, {{Y, 10, 0}, {Cb, 10, 0}, {Cr, 10, 0}}, 0},
      {"9 bit 4:2:2, rows with padding to the byte boundary", 6, 4, 1, {{Y, 9, 0}, {Cb, 9, 0}, {Cr, 9, 0}}, 0},

      // Bit depths below 8 are stored in bytes and were read with 8 bits.
      {"4 bit 4:2:2", 6, 4, 1, {{Y, 4, 0}, {Cb, 4, 0}, {Cr, 4, 0}}, 0},
      {"8 bit luma, 5 bit chroma", 8, 4, 2, {{Y, 8, 0}, {Cb, 5, 0}, {Cr, 5, 0}}, 0},

      // Different depths of the two chroma components, and Cr declared before Cb.
      {"Cb 10 bit, Cr 12 bit", 8, 4, 2, {{Y, 8, 0}, {Cb, 10, 0}, {Cr, 12, 0}}, 0},
      {"Cr 12 bit before Cb 10 bit", 8, 4, 2, {{Y, 8, 0}, {Cr, 12, 0}, {Cb, 10, 0}}, 0},
      {"chroma before luma", 8, 4, 1, {{Cb, 12, 0}, {Cr, 12, 0}, {Y, 12, 0}}, 0},

      // Byte-aligned components: each value is a 16-bit word with padding in the upper bits.
      {"12 bit in 16-bit words", 8, 4, 2, {{Y, 12, 2}, {Cb, 12, 2}, {Cr, 12, 2}}, 0},
      {"only Cb byte-aligned", 6, 4, 2, {{Y, 12, 0}, {Cb, 12, 2}, {Cr, 12, 0}}, 0},
      {"only Cr byte-aligned", 6, 4, 2, {{Y, 12, 0}, {Cb, 12, 0}, {Cr, 12, 2}}, 0},
      {"5 bit in bytes", 6, 4, 1, {{Y, 5, 1}, {Cb, 5, 1}, {Cr, 5, 1}}, 0},

      // Row alignment.
      {"10 bit, rows aligned to 4 bytes", 6, 4, 2, {{Y, 10, 0}, {Cb, 10, 0}, {Cr, 10, 0}}, 4},
      {"12 bit, rows aligned to 8 bytes", 6, 4, 1, {{Y, 12, 0}, {Cb, 12, 0}, {Cr, 12, 0}}, 8},

      // The depths at which the storage width and the bit depth coincide.
      {"8 bit", 8, 4, 2, {{Y, 8, 0}, {Cb, 8, 0}, {Cr, 8, 0}}, 0},
      {"16 bit", 8, 4, 1, {{Y, 16, 0}, {Cb, 16, 0}, {Cr, 16, 0}}, 0},
  };

  for (const Layout& layout : layouts) {
    for (bool padding_bit : {false, true}) {
      check_decoding(layout, padding_bit);
    }
  }
}
