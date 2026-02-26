/*
  libheif example application "heif-enc" - VMT metadata track support.

  MIT License

  Copyright (c) 2017 Dirk Farin <dirk.farin@gmail.com>

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

#include "vmt.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <libheif/heif.h>
#include <libheif/heif_sequences.h>


static const uint32_t BAD_VMT_TIMESTAMP = 0xFFFFFFFE;


static std::optional<uint8_t> nibble_to_val(char c)
{
  if (c>='0' && c<='9') {
    return c - '0';
  }
  if (c>='a' && c<='f') {
    return c - 'a' + 10;
  }
  if (c>='A' && c<='F') {
    return c - 'A' + 10;
  }

  return std::nullopt;
}

// Convert hex data to raw binary. Ignore any non-hex characters.
static std::vector<uint8_t> hex_to_binary(const std::string& line)
{
  std::vector<uint8_t> data;
  uint8_t current_value = 0;

  bool high_nibble = true;
  for (auto c : line) {
    auto v = nibble_to_val(c);
    if (v) {
      if (high_nibble) {
        current_value = static_cast<uint8_t>(*v << 4);
        high_nibble = false;
      }
      else {
        current_value |= *v;
        data.push_back(current_value);
        high_nibble = true;
      }
    }
  }

  return data;
}


// Convert base64 data to raw binary.
static std::vector<uint8_t> decode_base64(const std::string& line)
{
  const std::string base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::vector<uint8_t> data;

  size_t len = line.size();
  if (len % 4 != 0) {
    len = int(len / 4) * 4;
  }

  for (size_t i = 0; i < len; i += 4) {
    uint8_t buf[4];
    for (uint8_t j = 0; j < 4; j++) {
      size_t k = base64.find(line[i + j]);
      buf[j] = (uint8_t)(k == std::string::npos ? base64.size() : k);
    }

    data.push_back((uint8_t)(buf[0] << 2) + (buf[1] >> 4));

    if (line[i + 2] != '=') {
      data.push_back((uint8_t)((buf[1] & 0x0f) << 4) + (buf[2] >> 2));
    }

    if (line[i + 3] != '=') {
      data.push_back((uint8_t)((buf[2] & 0x03) << 6) + buf[3]);
    }
  }

  return data;
}


// Parse metadata from WebVMT sync commmands
static std::vector<uint8_t> parse_vmt_sync_data(const std::string& content)
{
  std::vector<uint8_t> data;

  std::regex pattern_sync(R"(\s*\{\s*\"sync\"\s*:\s*\{(.*?)\}\s*\}\s*)");
  const std::sregex_token_iterator ti_end;

  for (std::sregex_token_iterator ti(content.begin(), content.end(), pattern_sync, 1); ti != ti_end; ++ti)
  {
    std::string sync = *ti;
    std::regex pattern_type(R"(.*\"type\"\s*:\s*\"(.*?)\".*)");
    std::regex pattern_data(R"(.*\"data\"\s*:\s*\"(.*?)\".*)");
    std::smatch match;

    if (std::regex_match(sync, match, pattern_type)) {
      std::string type = match[1];

      std::regex pattern_hex(R"(.*\.hex$)");
      std::regex pattern_b64(R"(.*\.base64$)");

      std::string textData;
      if (std::regex_match(sync, match, pattern_data)) {
        textData = match[1];
      }

      if (std::regex_match(type, match, pattern_hex)) {
        std::vector<uint8_t> binaryData = hex_to_binary(textData);
        data.insert(data.end(), binaryData.begin(), binaryData.end());
      }
      else if (std::regex_match(type, match, pattern_b64)) {
        std::vector<uint8_t> binaryData = decode_base64(textData);
        data.insert(data.end(), binaryData.begin(), binaryData.end());
      }
      else {
        data.insert(data.end(), textData.data(), textData.data() + textData.length());
      }
    }
  }

  return data;
}


static uint32_t parse_vmt_timestamp(const std::string& vmt_time)
{
  std::regex pattern(R"(-?((\d*):)?(\d\d):(\d\d)(\.(\d*))?)");
  std::smatch match;

  if (!std::regex_match(vmt_time, match, pattern)) {
    return 0; // no match
  }

  std::string hh = match[2]; // optional
  std::string mm = match[3];
  std::string ss = match[4];
  std::string fs = match[6]; // optional

  if (vmt_time.find('-') != std::string::npos) {
    return 0; // negative time not supported
  }

  uint32_t ms = 0;

  if (fs != "") {
    if (fs.length() == 3) {
      ms = std::stoi(fs);
    }
    else {
      return BAD_VMT_TIMESTAMP; // invalid
    }
  }

  uint32_t ts = ((hh != "" ? std::stoi(hh) : 0) * 3600 * 1000 +
                 std::stoi(mm) * 60 * 1000 +
                 std::stoi(ss) * 1000 +
                 ms);

  return ts;
}

int encode_vmt_metadata_track(heif_context* context, heif_track* visual_track,
                              const std::string& vmt_metadata_file,
                              const std::string& track_uri, bool binary)
{
  // --- add metadata track

  heif_track* track = nullptr;

  heif_track_options* track_options = heif_track_options_alloc();
  heif_track_options_set_timescale(track_options, 1000);

  heif_context_add_uri_metadata_sequence_track(context, track_uri.c_str(), track_options, &track);
  heif_raw_sequence_sample* sample = heif_raw_sequence_sample_alloc();


  std::ifstream istr(vmt_metadata_file.c_str());

  std::regex pattern_cue(R"(^\s*(-?(\d|:|\.)*)\s*-->\s*(-?(\d|:|\.)*)?.*)");
  std::regex pattern_note(R"(^\s*(NOTE).*)");

  static std::vector<uint8_t> prev_metadata;
  static std::optional<uint32_t> prev_ts;

  std::string line;
  while (std::getline(istr, line))
  {
    std::smatch match;

    if (std::regex_match(line, match, pattern_note)) {
      while (std::getline(istr, line)) {
        if (line.empty()) {
          break;
        }
      }

      continue;
    }

    if (!std::regex_match(line, match, pattern_cue)) {
      continue;
    }

    std::string cue_start = match[1];
    std::string cue_end = match[3]; // == "" for unbounded cues

    uint32_t ts = parse_vmt_timestamp(cue_start);

    std::vector<uint8_t> concat;

    if (binary) {
      while (std::getline(istr, line)) {
        if (line.empty()) {
          break;
        }

        std::vector<uint8_t> binaryData = hex_to_binary(line);
        concat.insert(concat.end(), binaryData.begin(), binaryData.end());
      }

    }
    else {
      while (std::getline(istr, line)) {
        if (line.empty()) {
          break;
        }

        concat.insert(concat.end(), line.data(), line.data() + line.length());
        concat.push_back('\n');
      }

      concat.push_back(0);
      std::string content(concat.begin(), concat.end());
      concat = parse_vmt_sync_data(content);
    }

    if (ts != BAD_VMT_TIMESTAMP) {

      if (ts > *prev_ts) {
        heif_raw_sequence_sample_set_data(sample, (const uint8_t*)prev_metadata.data(), prev_metadata.size());
        heif_raw_sequence_sample_set_duration(sample, ts - *prev_ts);
        heif_track_add_raw_sequence_sample(track, sample);
      }
      else if (ts == *prev_ts) {
        concat.insert(concat.begin(), prev_metadata.begin(), prev_metadata.end());
      }
      else {
        std::cerr << "Bad WebVMT timestamp order: " << cue_start << "\n";
      }

      prev_ts = ts;
      prev_metadata = concat;
    }
    else {
      std::cerr << "Bad WebVMT timestamp: " << cue_start << "\n";
    }
  }

  // --- flush last metadata packet

  heif_raw_sequence_sample_set_data(sample, (const uint8_t*)prev_metadata.data(), prev_metadata.size());
  heif_raw_sequence_sample_set_duration(sample, 1);
  heif_track_add_raw_sequence_sample(track, sample);

  // --- add track reference

  heif_track_add_reference_to_track(track, heif_track_reference_type_description, visual_track);

  // --- release all objects

  heif_raw_sequence_sample_release(sample);
  heif_track_options_release(track_options);
  heif_track_release(track);

  return 0;
}
