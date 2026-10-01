/*
  libheif HEVC SPS parsing unit tests

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
#include "codecs/hevc_boxes.h"
#include "codecs/hevc_dec.h"
#include "codecs/decoder.h"
#include "bitstream.h"
#include "error.h"
#include "libheif/heif.h"
#include <cstdint>
#include <memory>
#include <vector>

// SPS NAL unit (with emulation prevention bytes) taken from the hvcC box of
// tests/data/rainbow-451x461.heic. x265 padded the 451x461 input to a coded
// size of 456x464 and signals a conformance window that crops it to 452x462.
static const std::vector<uint8_t> rainbow_sps{
    0x42, 0x01, 0x01, 0x03, 0x70, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x03, 0x00, 0x3f, 0xa0, 0x0e, 0x48, 0x07, 0x47, 0x75,
    0x96, 0xea, 0x49, 0x29, 0xae, 0x6e, 0x02, 0x1a, 0x0c, 0x08, 0x00, 0x00,
    0x03, 0x00, 0xc8, 0x00, 0x00, 0x03, 0x00, 0x08, 0x40};

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


static std::vector<uint8_t> sps_with_sub_layer_profile()
{
  BitWriter writer;
  writer.write_bits8(0x42, 8); // SPS NAL header
  writer.write_bits8(0x01, 8);
  writer.write_bits(0, 4);     // sps_video_parameter_set_id
  writer.write_bits(1, 3);     // sps_max_sub_layers_minus1
  writer.write_flag(true);     // sps_temporal_id_nesting_flag

  writer.write_bits(1, 8);     // general profile space, tier, and idc
  writer.write_bits32(0, 32);  // general compatibility flags
  writer.write_bits16(0, 16);
  writer.write_bits16(0, 16);
  writer.write_bits16(0, 16);  // 48 general source and constraint flags
  writer.write_bits8(120, 8);  // general_level_idc

  writer.write_flag(true);     // sub_layer_profile_present_flag[0]
  writer.write_flag(true);     // sub_layer_level_present_flag[0]
  for (int i = 1; i < 8; i++) {
    writer.write_bits(0, 2);   // reserved_zero_2bits
  }
  writer.write_bits(1, 8);     // sub-layer profile space, tier, and idc
  writer.write_bits32(0, 32);  // sub-layer compatibility flags
  writer.write_bits16(0, 16);
  writer.write_bits16(0, 16);
  writer.write_bits16(0, 16);  // 48 sub-layer source and constraint flags
  writer.write_bits8(120, 8);  // sub_layer_level_idc[0]

  write_uvlc(writer, 0);       // sps_seq_parameter_set_id
  write_uvlc(writer, 1);       // chroma_format_idc (4:2:0)
  write_uvlc(writer, 64);      // pic_width_in_luma_samples
  write_uvlc(writer, 48);      // pic_height_in_luma_samples
  writer.write_flag(false);    // conformance_window_flag
  write_uvlc(writer, 0);       // bit_depth_luma_minus8
  write_uvlc(writer, 0);       // bit_depth_chroma_minus8
  return writer.get_data();
}


TEST_CASE("SPS sub-layer profile consumes all 88 bits")
{
  std::vector<uint8_t> sps = sps_with_sub_layer_profile();
  HEVCDecoderConfigurationRecord config{};
  uint32_t width = 0, height = 0;
  ImageSize coded{};

  Error err = parse_sps_for_hvcC_configuration(sps.data(), sps.size(),
                                               &config, &width, &height, &coded);
  REQUIRE(!err);
  CHECK(width == 64);
  CHECK(height == 48);
  CHECK(coded.width == 64);
  CHECK(coded.height == 48);
}


TEST_CASE("HEVC coded-size scan rejects a malformed SPS after a valid one")
{
  std::vector<uint8_t> invalid_sps = rainbow_sps;
  REQUIRE((invalid_sps[18] & 0xF0) == 0xA0);
  invalid_sps[18] = 0x94 | (invalid_sps[18] & 0x03); // chroma_format_idc = 4

  std::vector<uint8_t> data;
  auto append_nal = [&data](const std::vector<uint8_t>& nal) {
    uint32_t size = static_cast<uint32_t>(nal.size());
    data.push_back(static_cast<uint8_t>(size >> 24));
    data.push_back(static_cast<uint8_t>(size >> 16));
    data.push_back(static_cast<uint8_t>(size >> 8));
    data.push_back(static_cast<uint8_t>(size));
    data.insert(data.end(), nal.begin(), nal.end());
  };
  append_nal(rainbow_sps);
  append_nal(invalid_sps);

  auto box = std::make_shared<Box_hvcC>();
  Decoder_HEVC decoder(box);
  auto result = decoder.get_max_coded_image_size(data);
  REQUIRE(result.is_error());
  CHECK(result.error().error_code == heif_error_Invalid_input);
}


TEST_CASE("SPS conformance window yields visible and coded size")
{
  HEVCDecoderConfigurationRecord config;
  uint32_t width = 0, height = 0;
  ImageSize coded{};

  Error err = parse_sps_for_hvcC_configuration(rainbow_sps.data(), rainbow_sps.size(),
                                               &config, &width, &height, &coded);
  REQUIRE(!err);

  CHECK(config.chroma_format == 1);
  CHECK(config.bit_depth_luma == 8);
  CHECK(config.bit_depth_chroma == 8);

  CHECK(width == 452);
  CHECK(height == 462);
  CHECK(coded.width == 456);
  CHECK(coded.height == 464);
}


TEST_CASE("SPS constraint flags are copied to hvcC")
{
  HEVCDecoderConfigurationRecord config{};
  uint32_t width = 0, height = 0;
  ImageSize coded{};

  Error err = parse_sps_for_hvcC_configuration(rainbow_sps.data(), rainbow_sps.size(),
                                               &config, &width, &height, &coded);
  REQUIRE(!err);

  // The SPS has general_progressive_source_flag and general_frame_only_constraint_flag
  // set: the constraint flags are 0x90 and five zero bytes.

  CHECK(config.general_profile_idc == HEVCDecoderConfigurationRecord::Profile_MainStillPicture);

  for (int i = 0; i < HEVCDecoderConfigurationRecord::NUM_CONSTRAINT_INDICATOR_FLAGS; i++) {
    INFO("constraint flag " << i);
    CHECK(config.general_constraint_indicator_flags[i] == (i == 0 || i == 3));
  }

  // They are written in the same order, the first flag in the most significant bit.

  config.configuration_version = 1;

  // a flag in each of the bytes, among them the first and the last bit of a byte
  config.general_constraint_indicator_flags[8] = true;
  config.general_constraint_indicator_flags[23] = true;
  config.general_constraint_indicator_flags[47] = true;

  StreamWriter writer;
  err = config.write(writer);
  REQUIRE(!err);

  const std::vector<uint8_t> data = writer.get_data();
  REQUIRE(data.size() >= 12);

  const std::vector<uint8_t> constraint_flags(data.begin() + 6, data.begin() + 12);
  CHECK(constraint_flags == std::vector<uint8_t>{0x90, 0x80, 0x01, 0x00, 0x00, 0x01});

  // ... and read back into the same flags.

  BitstreamRange range(std::make_shared<StreamReader_memory>(data.data(), data.size(), false), data.size());

  HEVCDecoderConfigurationRecord parsed{};
  err = parsed.parse(range, heif_get_global_security_limits());
  REQUIRE(!err);
  CHECK(parsed.general_constraint_indicator_flags == config.general_constraint_indicator_flags);
}


TEST_CASE("SPS chroma_format_idc out of range is rejected")
{
  // Same SPS with chroma_format_idc changed from 1 (uvlc '010') to 4
  // (uvlc '00101'). The byte at index 18 holds sps_seq_parameter_set_id and
  // the start of chroma_format_idc: '1 010 ....' becomes '1 00101 ..'.
  std::vector<uint8_t> sps = rainbow_sps;
  REQUIRE((sps[18] & 0xF0) == 0xA0);
  sps[18] = 0x94 | (sps[18] & 0x03);

  HEVCDecoderConfigurationRecord config;
  uint32_t width = 0, height = 0;
  ImageSize coded{};

  Error err = parse_sps_for_hvcC_configuration(sps.data(), sps.size(),
                                               &config, &width, &height, &coded);
  REQUIRE(err);
  CHECK(err.error_code == heif_error_Invalid_input);
  CHECK(err.sub_error_code == heif_suberror_Invalid_parameter_value);
  CHECK(err.message.find("chroma_format_idc") != std::string::npos);
}
