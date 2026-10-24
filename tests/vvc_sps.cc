/*
  libheif VVC SPS parsing unit tests

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

#include "catch_amalgamated.hpp"
#include "bitstream.h"
#include "codecs/vvc_boxes.h"
#include "codecs/vvc_dec.h"
#include "libheif/heif.h"

#include <cstdint>
#include <memory>
#include <vector>


static void write_uvlc(BitWriter& writer, uint32_t value)
{
  uint32_t code = value + 1;
  int bits = 0;
  for (uint32_t n = code; n != 0; n >>= 1) {
    bits++;
  }
  writer.write_bits(0, bits - 1);
  writer.write_bits(code, bits);
}


static std::vector<uint8_t> make_vvc_sps(bool valid)
{
  BitWriter writer;
  writer.write_bits8(0, 8);     // forbidden bit, reserved bit, layer ID
  writer.write_bits8(0x79, 8);  // SPS NAL type 15, temporal_id_plus1 = 1
  writer.write_bits(0, 4);      // sps_seq_parameter_set_id
  writer.write_bits(0, 4);      // sps_video_parameter_set_id
  writer.write_bits(0, 3);      // sps_max_sublayers_minus1
  writer.write_bits(1, 2);      // sps_chroma_format_idc = 4:2:0
  writer.write_bits(0, 2);      // sps_log2_ctu_size_minus5
  writer.write_flag(false);     // sps_ptl_dpb_hrd_params_present_flag
  writer.write_flag(false);     // sps_gdr_enabled_flag
  writer.write_flag(false);     // sps_ref_pic_resampling_enabled_flag

  if (!valid) {
    // BitReader::get_uvlc() rejects the width after 21 leading zeros.
    writer.write_bits(0, 21);
    writer.write_flag(true);
    return writer.get_data();
  }

  write_uvlc(writer, 64);       // sps_pic_width_max_in_luma_samples
  write_uvlc(writer, 48);       // sps_pic_height_max_in_luma_samples
  writer.write_flag(false);     // sps_conformance_window_flag
  writer.write_flag(false);     // sps_subpic_info_present_flag
  write_uvlc(writer, 0);        // sps_bitdepth_minus8
  return writer.get_data();
}


static void append_nal(std::vector<uint8_t>& data, const std::vector<uint8_t>& nal)
{
  uint32_t size = static_cast<uint32_t>(nal.size());
  data.push_back(static_cast<uint8_t>(size >> 24));
  data.push_back(static_cast<uint8_t>(size >> 16));
  data.push_back(static_cast<uint8_t>(size >> 8));
  data.push_back(static_cast<uint8_t>(size));
  data.insert(data.end(), nal.begin(), nal.end());
}


TEST_CASE("VVC coded-size scan rejects an unparseable SPS")
{
  std::vector<uint8_t> data;
  Decoder_VVC decoder(std::make_shared<Box_vvcC>());

  SECTION("valid SPS") {
    append_nal(data, make_vvc_sps(true));

    auto result = decoder.get_max_coded_image_size(data);
    REQUIRE(!result.is_error());
    REQUIRE(result->has_value());
    CHECK((*result)->width == 64);
    CHECK((*result)->height == 48);
  }

  SECTION("unparseable SPS after a valid one") {
    append_nal(data, make_vvc_sps(true));
    append_nal(data, make_vvc_sps(false));

    auto result = decoder.get_max_coded_image_size(data);
    REQUIRE(result.is_error());
    CHECK(result.error().error_code == heif_error_Invalid_input);
  }
}
