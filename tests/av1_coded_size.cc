/*
  libheif AV1 coded-size scan tests

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
#include "codecs/avif_boxes.h"
#include "bitstream.h"

#include <cstdint>
#include <string>
#include <vector>


static void append_sequence_header(std::vector<uint8_t>& data, uint32_t width, uint32_t height)
{
  BitWriter writer;
  writer.write_bits(0, 3); // seq_profile
  writer.write_bits(1, 1); // still_picture
  writer.write_bits(1, 1); // reduced_still_picture_header
  writer.write_bits(0, 5); // seq_level_idx[0]
  writer.write_bits(15, 4); // frame_width_bits_minus_1
  writer.write_bits(15, 4); // frame_height_bits_minus_1
  writer.write_bits(width - 1, 16);
  writer.write_bits(height - 1, 16);
  std::vector<uint8_t> payload = writer.get_data();

  data.push_back(0x0A); // obu_type = 1, obu_has_size_field = 1
  data.push_back(static_cast<uint8_t>(payload.size()));
  data.insert(data.end(), payload.begin(), payload.end());
}


static void append_sequence_header_with_uvlc(std::vector<uint8_t>& data,
                                             uint32_t width, uint32_t height,
                                             int leading_zeros)
{
  BitWriter writer;
  writer.write_bits(0, 3); // seq_profile
  writer.write_bits(0, 1); // still_picture
  writer.write_bits(0, 1); // reduced_still_picture_header
  writer.write_bits(1, 1); // timing_info_present_flag
  writer.write_bits(1, 32); // num_units_in_display_tick
  writer.write_bits(1, 32); // time_scale
  writer.write_bits(1, 1); // equal_picture_interval

  for (int i = 0; i < leading_zeros; i++) {
    writer.write_bits(0, 1);
  }
  writer.write_bits(1, 1);
  if (leading_zeros != 0) {
    writer.write_bits(0, leading_zeros); // uvlc value bits
  }

  writer.write_bits(0, 1); // decoder_model_info_present_flag
  writer.write_bits(0, 1); // initial_display_delay_present_flag
  writer.write_bits(0, 5); // operating_points_cnt_minus_1
  writer.write_bits(0, 12); // operating_point_idc[0]
  writer.write_bits(16, 5); // seq_level_idx[0]
  writer.write_bits(0, 1); // seq_tier[0]
  writer.write_bits(15, 4); // frame_width_bits_minus_1
  writer.write_bits(15, 4); // frame_height_bits_minus_1
  writer.write_bits(width - 1, 16);
  writer.write_bits(height - 1, 16);

  // Remaining sequence-header fields for an 8-bit 4:2:0 stream, followed by
  // the trailing_one_bit. These are not needed by the coded-size scanner, but
  // make the same OBU usable by fill_av1C_configuration_from_stream().
  writer.write_bits(0b000100101011111001000000000, 27);
  writer.write_bits(1, 1);

  std::vector<uint8_t> payload = writer.get_data();
  REQUIRE(payload.size() < 128);
  data.push_back(0x0a); // obu_type = 1, obu_has_size_field = 1
  data.push_back(static_cast<uint8_t>(payload.size()));
  data.insert(data.end(), payload.begin(), payload.end());
}


TEST_CASE("largest of several AV1 sequence headers is found")
{
  std::vector<uint8_t> data;
  append_sequence_header(data, 64, 64);
  append_sequence_header(data, 8192, 4096);

  uint32_t width = 0, height = 0;
  auto result = find_max_av1_frame_size_in_stream(data.data(), data.size(), &width, &height);
  REQUIRE(result);
  REQUIRE(*result);
  CHECK(width == 8192);
  CHECK(height == 4096);
}


TEST_CASE("AV1 sequence-header UVLC permits up to 31 leading zeros")
{
  int leading_zeros = GENERATE(21, 31);
  std::vector<uint8_t> data;
  append_sequence_header_with_uvlc(data, 8192, 4096, leading_zeros);

  uint32_t width = 0, height = 0;
  auto result = find_max_av1_frame_size_in_stream(data.data(), data.size(), &width, &height);
  REQUIRE(result);
  REQUIRE(*result);
  CHECK(width == 8192);
  CHECK(height == 4096);

  Box_av1C::configuration config;
  CHECK(fill_av1C_configuration_from_stream(&config, data.data(), static_cast<int>(data.size())));
}


TEST_CASE("AV1 sequence-header UVLC rejects 32 leading zeros")
{
  std::vector<uint8_t> data;
  append_sequence_header_with_uvlc(data, 8192, 4096, 32);

  uint32_t width = 0, height = 0;
  auto result = find_max_av1_frame_size_in_stream(data.data(), data.size(), &width, &height);
  REQUIRE(result.is_error());
  CHECK(result.error().message.find("sequence header cannot be scanned completely") != std::string::npos);

  Box_av1C::configuration config;
  CHECK_FALSE(fill_av1C_configuration_from_stream(&config, data.data(), static_cast<int>(data.size())));
}


TEST_CASE("zero-sized AV1 OBU completes the scan")
{
  std::vector<uint8_t> data;
  append_sequence_header(data, 64, 48);
  data.push_back(0x12); // temporal-delimiter OBU with a size field
  data.push_back(0x00); // zero-sized payload

  uint32_t width = 0, height = 0;
  auto result = find_max_av1_frame_size_in_stream(data.data(), data.size(), &width, &height);
  REQUIRE(result);
  REQUIRE(*result);
  CHECK(width == 64);
  CHECK(height == 48);
}


TEST_CASE("AV1 configuration OBU without a size field is rejected")
{
  uint8_t unsized_obu_header = GENERATE(as<uint8_t>{}, 0x00, 0x10, 0x78);

  std::vector<uint8_t> data;
  append_sequence_header(data, 64, 64);
  data.push_back(unsized_obu_header);
  append_sequence_header(data, 8192, 4096);

  uint32_t width = 0, height = 0;
  auto result = find_max_av1_frame_size_in_stream(data.data(), data.size(), &width, &height);
  REQUIRE(result.is_error());
  CHECK(result.error().error_code == heif_error_Invalid_input);
  CHECK(result.error().message.find("unknown OBU header size") != std::string::npos);
}


TEST_CASE("terminal AV1 sample OBU gets an explicit size")
{
  uint8_t unsized_obu_header = GENERATE(as<uint8_t>{}, 0x10, 0x78);

  std::vector<uint8_t> data;
  append_sequence_header(data, 64, 48);
  size_t header_pos = data.size();
  data.push_back(unsized_obu_header);
  data.push_back(0xaa);
  data.push_back(0xbb);

  Error err = normalize_av1_sample_data(data, 0);
  REQUIRE_FALSE(err);
  CHECK(data[header_pos] == (unsized_obu_header | 0x02));
  CHECK(data[header_pos + 1] == 2);
  CHECK(data[header_pos + 2] == 0xaa);
  CHECK(data[header_pos + 3] == 0xbb);

  uint32_t width = 0, height = 0;
  auto result = find_max_av1_frame_size_in_stream(data.data(), data.size(), &width, &height);
  REQUIRE(result);
  REQUIRE(*result);
  CHECK(width == 64);
  CHECK(height == 48);
}


TEST_CASE("terminal unsized AV1 sequence header remains scannable")
{
  std::vector<uint8_t> data;
  append_sequence_header(data, 320, 240);
  REQUIRE(data[0] == 0x0a);
  REQUIRE(data[1] < 128);
  data[0] = 0x08;
  data.erase(data.begin() + 1);

  Error err = normalize_av1_sample_data(data, 0);
  REQUIRE_FALSE(err);
  CHECK(data[0] == 0x0a);

  uint32_t width = 0, height = 0;
  auto result = find_max_av1_frame_size_in_stream(data.data(), data.size(), &width, &height);
  REQUIRE(result);
  REQUIRE(*result);
  CHECK(width == 320);
  CHECK(height == 240);
}


TEST_CASE("AV1 frame budget counts frame OBUs in both configuration and sample data")
{
  std::vector<uint8_t> config;
  std::vector<uint8_t> sample;
  for (int i = 0; i < 3; ++i) {
    config.push_back(0x32); // OBU_FRAME
    config.push_back(0x01);
    config.push_back(0x00);
  }
  for (int i = 0; i < 6; ++i) {
    sample.push_back(0x1a); // OBU_FRAME_HEADER
    sample.push_back(0x01);
    sample.push_back(0x00);
    sample.push_back(0x22); // OBU_TILE_GROUP (not a frame)
    sample.push_back(0x01);
    sample.push_back(0x00);
  }

  uint32_t width = 0, height = 0, count = 0;
  auto result = find_max_av1_frame_size_in_stream(config.data(), config.size(),
                                                  &width, &height, &count);
  REQUIRE(result);
  CHECK(count == 3);
  result = find_max_av1_frame_size_in_stream(sample.data(), sample.size(),
                                             &width, &height, &count);
  REQUIRE(result);
  CHECK(count == 6); // nine total, exceeding the per-decode limit of eight
}


TEST_CASE("bytes after an unsized AV1 sample OBU remain in its payload")
{
  std::vector<uint8_t> data;
  append_sequence_header(data, 64, 64);
  size_t header_pos = data.size();
  data.push_back(0x00); // unsized reserved OBU
  append_sequence_header(data, 8192, 4096);

  size_t payload_size = data.size() - header_pos - 1;
  REQUIRE(payload_size < 128);
  Error err = normalize_av1_sample_data(data, 0);
  REQUIRE_FALSE(err);
  CHECK(data[header_pos] == 0x02);
  CHECK(data[header_pos + 1] == payload_size);

  // The apparent second sequence header is part of the terminal OBU payload,
  // both for this scan and for the decoder that receives the same bytes.
  uint32_t width = 0, height = 0;
  auto result = find_max_av1_frame_size_in_stream(data.data(), data.size(), &width, &height);
  REQUIRE(result);
  REQUIRE(*result);
  CHECK(width == 64);
  CHECK(height == 64);
}


TEST_CASE("extended terminal AV1 sample OBU gets its size after the extension")
{
  std::vector<uint8_t> data{0x14, 0xab, 0xca, 0xfe};

  Error err = normalize_av1_sample_data(data, 0);
  REQUIRE_FALSE(err);
  REQUIRE(data.size() == 5);
  CHECK(data[0] == 0x16);
  CHECK(data[1] == 0xab);
  CHECK(data[2] == 2);
  CHECK(data[3] == 0xca);
  CHECK(data[4] == 0xfe);
}


TEST_CASE("terminal AV1 sample OBU gets a multi-byte size")
{
  std::vector<uint8_t> data(301, 0xaa);
  data[0] = 0x10;

  Error err = normalize_av1_sample_data(data, 0);
  REQUIRE_FALSE(err);
  REQUIRE(data.size() == 303);
  CHECK(data[0] == 0x12);
  CHECK(data[1] == 0xac);
  CHECK(data[2] == 0x02);
}


TEST_CASE("AV1 sample normalization preserves the configuration prefix")
{
  std::vector<uint8_t> data{0x00, 0x10, 0xaa};

  Error err = normalize_av1_sample_data(data, 1);
  REQUIRE_FALSE(err);
  REQUIRE(data.size() == 4);
  CHECK(data[0] == 0x00);
  CHECK(data[1] == 0x12);
  CHECK(data[2] == 0x01);
  CHECK(data[3] == 0xaa);

  uint32_t width = 0, height = 0;
  auto result = find_max_av1_frame_size_in_stream(data.data(), 1, &width, &height);
  REQUIRE(result.is_error());
  CHECK(result.error().message.find("unknown OBU header size") != std::string::npos);
}


TEST_CASE("malformed sized OBU prevents AV1 sample normalization")
{
  std::vector<uint8_t> data{0x12, 0x08};

  Error err = normalize_av1_sample_data(data, 0);
  REQUIRE(err);
  CHECK(err.message.find("exceeds the remaining data") != std::string::npos);
}


TEST_CASE("truncated AV1 OBU size is rejected")
{
  // Sequence-header OBU followed by an unterminated LEB128 size.
  const std::vector<uint8_t> data{0x0A, 0x80};

  uint32_t width = 0, height = 0;
  auto result = find_max_av1_frame_size_in_stream(data.data(), data.size(), &width, &height);
  REQUIRE(result.is_error());
  CHECK(result.error().message.find("truncated") != std::string::npos);
}


TEST_CASE("unparseable AV1 sequence header is rejected")
{
  const std::vector<uint8_t> data{0x0A, 0x00};

  uint32_t width = 0, height = 0;
  auto result = find_max_av1_frame_size_in_stream(data.data(), data.size(), &width, &height);
  REQUIRE(result.is_error());
  CHECK(result.error().message.find("sequence header cannot be scanned completely") != std::string::npos);
}


TEST_CASE("AV1 OBU extending past the input is rejected")
{
  // Temporal-delimiter OBU declares an eight-byte payload but supplies none.
  const std::vector<uint8_t> data{0x12, 0x08};

  uint32_t width = 0, height = 0;
  auto result = find_max_av1_frame_size_in_stream(data.data(), data.size(), &width, &height);
  REQUIRE(result.is_error());
  CHECK(result.error().message.find("exceeds the remaining data") != std::string::npos);
}
