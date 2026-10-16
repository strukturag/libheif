/*
  libheif example application "heif-enc" - VMT metadata track support.

  MIT License

  Copyright (c) 2025 Dirk Farin <dirk.farin@gmail.com>
  Copyright (c) 2026 Rob Smith <rob.smith@awayteam.co.uk>

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
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <libheif/heif.h>
#include <libheif/heif_sequences.h>


// The parsers in this file are written by hand instead of with std::regex. The libstdc++ regex matcher
// recurses once per input character for patterns like ".*", which overflows the stack for inputs of
// a few ten thousand characters, e.g. a base64 blob in a sync command.

static bool is_space(char c)
{
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static bool is_digit(char c)
{
  return c >= '0' && c <= '9';
}

static bool is_blank(const std::string& line)
{
  for (char c : line) {
    if (!is_space(c)) {
      return false;
    }
  }

  return true;
}

// Shorten a string for an error message.
static std::string excerpt(const std::string& s)
{
  const size_t max_length = 40;

  if (s.size() <= max_length) {
    return s;
  }

  return s.substr(0, max_length) + "...";
}

static std::string trim(const std::string& s)
{
  size_t start = 0;
  while (start < s.size() && is_space(s[start])) {
    start++;
  }

  size_t end = s.size();
  while (end > start && is_space(s[end - 1])) {
    end--;
  }

  return s.substr(start, end - start);
}


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

// Convert hex data to raw binary and append it to 'data'. Whitespace between the digits is allowed.
static bool hex_to_binary(const std::string& text, std::vector<uint8_t>& data, std::string& error)
{
  std::optional<uint8_t> high_nibble;

  for (char c : text) {
    if (is_space(c)) {
      continue;
    }

    auto v = nibble_to_val(c);
    if (!v) {
      error = std::string("invalid hex character '") + c + "'";
      return false;
    }

    if (!high_nibble) {
      high_nibble = *v;
    }
    else {
      data.push_back(static_cast<uint8_t>((*high_nibble << 4) | *v));
      high_nibble.reset();
    }
  }

  if (high_nibble) {
    error = "odd number of hex digits";
    return false;
  }

  return true;
}


// Convert base64 data (RFC 4648, padded with '=') to raw binary and append it to 'data'.
static bool decode_base64(const std::string& text, std::vector<uint8_t>& data, std::string& error)
{
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };

  if (text.size() % 4 != 0) {
    error = "base64 length is not a multiple of four";
    return false;
  }

  for (size_t i = 0; i < text.size(); i += 4) {
    int v[4];
    int padding = 0;
    bool last_group = (i + 4 == text.size());

    for (int j = 0; j < 4; j++) {
      char c = text[i + j];

      if (c == '=' && j >= 2 && last_group) {
        v[j] = 0;
        padding++;
      }
      else if (padding > 0) {
        error = "base64 padding is not at the end";
        return false;
      }
      else {
        v[j] = value(c);
        if (v[j] < 0) {
          error = std::string("invalid base64 character '") + c + "'";
          return false;
        }
      }
    }

    data.push_back(static_cast<uint8_t>((v[0] << 2) | (v[1] >> 4)));
    if (padding < 2) {
      data.push_back(static_cast<uint8_t>(((v[1] & 0x0f) << 4) | (v[2] >> 2)));
    }
    if (padding < 1) {
      data.push_back(static_cast<uint8_t>(((v[2] & 0x03) << 6) | v[3]));
    }
  }

  return true;
}


static void append_utf8(std::string& out, uint32_t cp)
{
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  }
  else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}


// A minimal JSON reader, sufficient to find the "sync" commands in a cue payload. It decodes string
// escapes and skips over values of any other type. The nesting depth is limited so that malformed
// input cannot exhaust the stack.
class JsonReader
{
public:
  explicit JsonReader(const std::string& text) : m_text(text) {}

  void skip_whitespace()
  {
    while (m_pos < m_text.size() && is_space(m_text[m_pos])) {
      m_pos++;
    }
  }

  bool at_end()
  {
    skip_whitespace();
    return m_pos >= m_text.size();
  }

  // Consume the character 'c' if it is the next non-whitespace character.
  bool consume(char c)
  {
    if (!at_end() && m_text[m_pos] == c) {
      m_pos++;
      return true;
    }

    return false;
  }

  // Parse a JSON string literal into UTF-8.
  bool parse_string(std::string& out)
  {
    if (!consume('"')) {
      return false;
    }

    out.clear();

    while (true) {
      if (m_pos >= m_text.size()) {
        return false; // unterminated string
      }

      char c = m_text[m_pos++];

      if (c == '"') {
        return true;
      }

      if (static_cast<unsigned char>(c) < 0x20) {
        return false; // control characters must be escaped
      }

      if (c != '\\') {
        out.push_back(c);
        continue;
      }

      if (m_pos >= m_text.size()) {
        return false;
      }

      char e = m_text[m_pos++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          uint32_t cp;
          if (!parse_hex4(cp)) {
            return false;
          }

          if (cp >= 0xD800 && cp <= 0xDBFF) {
            // high surrogate, must be followed by an escaped low surrogate
            if (m_pos + 2 > m_text.size() || m_text[m_pos] != '\\' || m_text[m_pos + 1] != 'u') {
              return false;
            }
            m_pos += 2;

            uint32_t low;
            if (!parse_hex4(low) || low < 0xDC00 || low > 0xDFFF) {
              return false;
            }

            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          }
          else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return false; // lone low surrogate
          }

          append_utf8(out, cp);
          break;
        }
        default:
          return false;
      }
    }
  }

  // Skip over a value of any type.
  bool skip_value(int depth = 0)
  {
    if (depth > MAX_DEPTH || at_end()) {
      return false;
    }

    char c = m_text[m_pos];

    if (c == '"') {
      std::string s;
      return parse_string(s);
    }

    if (c == '{') {
      m_pos++;
      if (consume('}')) {
        return true;
      }

      while (true) {
        std::string key;
        if (!parse_string(key) || !consume(':') || !skip_value(depth + 1)) {
          return false;
        }

        if (consume(',')) {
          continue;
        }

        return consume('}');
      }
    }

    if (c == '[') {
      m_pos++;
      if (consume(']')) {
        return true;
      }

      while (true) {
        if (!skip_value(depth + 1)) {
          return false;
        }

        if (consume(',')) {
          continue;
        }

        return consume(']');
      }
    }

    // number or literal
    size_t start = m_pos;
    while (m_pos < m_text.size() && (is_digit(m_text[m_pos]) ||
                                     (m_text[m_pos] >= 'a' && m_text[m_pos] <= 'z') ||
                                     (m_text[m_pos] >= 'A' && m_text[m_pos] <= 'Z') ||
                                     m_text[m_pos] == '-' || m_text[m_pos] == '+' || m_text[m_pos] == '.')) {
      m_pos++;
    }

    std::string token = m_text.substr(start, m_pos - start);
    if (token == "true" || token == "false" || token == "null") {
      return true;
    }

    return !token.empty() && (token[0] == '-' || is_digit(token[0]));
  }

private:
  bool parse_hex4(uint32_t& cp)
  {
    if (m_pos + 4 > m_text.size()) {
      return false;
    }

    cp = 0;
    for (int i = 0; i < 4; i++) {
      auto v = nibble_to_val(m_text[m_pos++]);
      if (!v) {
        return false;
      }
      cp = (cp << 4) | *v;
    }

    return true;
  }

  const std::string& m_text;
  size_t m_pos = 0;

  static const int MAX_DEPTH = 32;
};


// Parse the parameter object of a "sync" command and append its data to 'data'.
// The data is decoded according to the ".hex" or ".base64" suffix of the type.
static bool parse_vmt_sync_command(JsonReader& reader, std::vector<uint8_t>& data, std::string& error)
{
  if (!reader.consume('{')) {
    error = "sync parameters are not a JSON object";
    return false;
  }

  std::optional<std::string> type;
  std::optional<std::string> value;

  if (!reader.consume('}')) {
    while (true) {
      std::string key;
      if (!reader.parse_string(key) || !reader.consume(':')) {
        error = "invalid JSON in sync command";
        return false;
      }

      if (key == "type") {
        std::string s;
        if (!reader.parse_string(s)) {
          error = "sync type is not a string";
          return false;
        }
        type = s;
      }
      else if (key == "data") {
        std::string s;
        if (!reader.parse_string(s)) {
          error = "sync data is not a string";
          return false;
        }
        value = s;
      }
      else if (!reader.skip_value()) {
        error = "invalid JSON in sync command";
        return false;
      }

      if (reader.consume(',')) {
        continue;
      }

      if (reader.consume('}')) {
        break;
      }

      error = "invalid JSON in sync command";
      return false;
    }
  }

  if (!type) {
    error = "sync command without type";
    return false;
  }

  if (!value) {
    error = "sync command without data";
    return false;
  }

  if (type->ends_with(".hex")) {
    return hex_to_binary(*value, data, error);
  }
  else if (type->ends_with(".base64")) {
    return decode_base64(*value, data, error);
  }
  else {
    data.insert(data.end(), value->begin(), value->end());
    return true;
  }
}


// Parse the metadata from the WebVMT sync commands in a cue payload. The payload is a list of JSON objects,
// one per command. The data of all sync commands is concatenated.
static bool parse_vmt_sync_data(const std::string& content, std::vector<uint8_t>& data, bool& have_sync, std::string& error)
{
  JsonReader reader(content);
  have_sync = false;

  while (!reader.at_end()) {
    if (!reader.consume('{')) {
      error = "cue payload is not a list of JSON objects";
      return false;
    }

    if (reader.consume('}')) {
      continue;
    }

    while (true) {
      std::string command;
      if (!reader.parse_string(command) || !reader.consume(':')) {
        error = "invalid JSON in cue payload";
        return false;
      }

      if (command == "sync") {
        if (!parse_vmt_sync_command(reader, data, error)) {
          return false;
        }
        have_sync = true;
      }
      else if (!reader.skip_value()) {
        error = "invalid JSON in cue payload";
        return false;
      }

      if (reader.consume(',')) {
        continue;
      }

      if (reader.consume('}')) {
        break;
      }

      error = "invalid JSON in cue payload";
      return false;
    }
  }

  return true;
}


// Parse a WebVMT timestamp "[hh:]mm:ss.fff" into milliseconds.
// Hours are optional and may have any number of digits, minutes and seconds are exactly two digits
// and at most 59, the fraction is mandatory and exactly three digits.
static std::optional<uint32_t> parse_vmt_timestamp(const std::string& vmt_time)
{
  size_t pos = 0;
  const size_t n = vmt_time.size();

  bool negative = false;
  if (pos < n && vmt_time[pos] == '-') {
    negative = true;
    pos++;
  }

  auto read_digits = [&](std::string& out) {
    size_t start = pos;
    while (pos < n && is_digit(vmt_time[pos])) {
      pos++;
    }
    out = vmt_time.substr(start, pos - start);
    return !out.empty();
  };

  std::string g1, g2, g3, fs;

  if (!read_digits(g1) || pos >= n || vmt_time[pos] != ':') {
    return std::nullopt;
  }
  pos++;

  if (!read_digits(g2)) {
    return std::nullopt;
  }

  bool has_hours = false;
  if (pos < n && vmt_time[pos] == ':') {
    pos++;
    if (!read_digits(g3)) {
      return std::nullopt;
    }
    has_hours = true;
  }

  if (pos >= n || vmt_time[pos] != '.') {
    return std::nullopt;
  }
  pos++;

  if (!read_digits(fs) || fs.size() != 3 || pos != n) {
    return std::nullopt;
  }

  const std::string hh = has_hours ? g1 : "";
  const std::string mm = has_hours ? g2 : g1;
  const std::string ss = has_hours ? g3 : g2;

  if (mm.size() != 2 || ss.size() != 2) {
    return std::nullopt;
  }

  int minutes = std::stoi(mm);
  int seconds = std::stoi(ss);
  uint32_t ms = std::stoi(fs);

  if (minutes > 59 || seconds > 59) {
    return std::nullopt;
  }

  // The timestamp is stored in a 32 bit millisecond counter. Reject hour values that cannot fit
  // (and that would overflow std::stoull) before doing any arithmetic.
  if (hh.size() > 9) {
    return std::nullopt;
  }

  if (negative) {
    return 0; // negative timestamps are interpreted as zero
  }

  uint64_t ts = ((hh != "" ? std::stoull(hh) : 0) * 3600 * 1000 +
                 minutes * 60 * 1000 +
                 seconds * 1000 +
                 ms);

  if (ts > UINT32_MAX) {
    return std::nullopt;
  }

  return static_cast<uint32_t>(ts);
}


// Read the file and split it into lines. CR, LF and CRLF are accepted as line terminators.
// A byte order mark at the start of the file is removed.
static bool read_vmt_lines(const std::string& filename, std::vector<std::string>& lines)
{
  std::ifstream istr(filename.c_str(), std::ios::binary);
  if (!istr) {
    return false;
  }

  std::string content((std::istreambuf_iterator<char>(istr)), std::istreambuf_iterator<char>());

  if (content.starts_with("\xEF\xBB\xBF")) {
    content.erase(0, 3);
  }

  std::string line;
  for (size_t i = 0; i < content.size(); i++) {
    char c = content[i];

    if (c == '\r' || c == '\n') {
      if (c == '\r' && i + 1 < content.size() && content[i + 1] == '\n') {
        i++;
      }

      lines.push_back(line);
      line.clear();
    }
    else {
      line.push_back(c);
    }
  }

  if (!line.empty()) {
    lines.push_back(line);
  }

  return true;
}


static bool is_vmt_signature(const std::string& line)
{
  return line.starts_with("WEBVMT") && (line.size() == 6 || is_space(line[6]));
}


// A comment block starts with "NOTE" followed by a space or the end of the line.
static bool is_vmt_note(const std::string& line)
{
  size_t p = 0;
  while (p < line.size() && is_space(line[p])) {
    p++;
  }

  return line.compare(p, 4, "NOTE") == 0 && (p + 4 == line.size() || is_space(line[p + 4]));
}


int encode_vmt_metadata_track(heif_context* context, heif_track* visual_track,
                              const std::string& vmt_metadata_file,
                              const std::string& track_uri, bool binary)
{
  // --- read the WebVMT file

  std::vector<std::string> lines;
  if (!read_vmt_lines(vmt_metadata_file, lines)) {
    std::cerr << "Cannot read WebVMT file: " << vmt_metadata_file << "\n";
    return 5;
  }

  if (lines.empty() || !is_vmt_signature(lines[0])) {
    std::cerr << "Not a WebVMT file (missing WEBVMT signature): " << vmt_metadata_file << "\n";
    return 5;
  }

  // --- add metadata track

  heif_track* track = nullptr;

  heif_track_options* track_options = heif_track_options_alloc();
  heif_track_options_set_timescale(track_options, 1000);

  heif_error err = heif_context_add_uri_metadata_sequence_track(context, track_uri.c_str(), track_options, &track);
  if (err.code) {
    std::cerr << "Cannot add metadata track: " << err.message << "\n";
    heif_track_options_release(track_options);
    return 5;
  }

  heif_raw_sequence_sample* sample = heif_raw_sequence_sample_alloc();

  auto release_all = [&]() {
    heif_raw_sequence_sample_release(sample);
    heif_track_options_release(track_options);
    heif_track_release(track);
  };

  auto add_packet = [&](const std::vector<uint8_t>& data, uint32_t duration) {
    heif_raw_sequence_sample_set_data(sample, data.data(), data.size());
    heif_raw_sequence_sample_set_duration(sample, duration);

    heif_error e = heif_track_add_raw_sequence_sample(track, sample);
    if (e.code) {
      std::cerr << "Cannot add metadata packet: " << e.message << "\n";
      return false;
    }

    return true;
  };

  // Each cue closes the packet that was opened by the previous cue. A packet is written as soon as its duration is known.
  // The track starts at time 0. If the first cue starts later, an empty packet covers the time before it.
  std::vector<uint8_t> prev_metadata;
  uint32_t prev_ts = 0;

  for (size_t i = 1; i < lines.size(); i++) {
    const std::string& line = lines[i];

    if (is_vmt_note(line)) {
      while (i + 1 < lines.size() && !is_blank(lines[i + 1])) {
        i++;
      }

      continue;
    }

    // A cue starts with its timing line. Header lines, cue identifiers and other blocks are skipped.

    size_t arrow = line.find("-->");
    if (arrow == std::string::npos) {
      continue;
    }

    const size_t cue_line = i + 1;
    std::string cue_start = trim(line.substr(0, arrow));
    // The cue end time and the cue settings after the arrow are ignored. Unbounded cues have no end time.

    std::optional<uint32_t> parsed_ts = parse_vmt_timestamp(cue_start);

    // --- collect the cue payload up to the next blank line

    std::vector<std::string> payload;
    while (i + 1 < lines.size() && !is_blank(lines[i + 1])) {
      i++;
      payload.push_back(lines[i]);
    }

    if (!parsed_ts) {
      std::cerr << "WebVMT line " << cue_line << ": bad timestamp '" << excerpt(cue_start) << "'\n";
      continue;
    }

    uint32_t ts = *parsed_ts;

    if (ts < prev_ts) {
      std::cerr << "WebVMT line " << cue_line << ": timestamp '" << excerpt(cue_start) << "' is before the previous cue\n";
      continue;
    }

    // --- decode the payload

    std::vector<uint8_t> concat;
    std::string error;
    bool ok = true;

    if (binary) {
      for (const std::string& payload_line : payload) {
        if (!hex_to_binary(payload_line, concat, error)) {
          ok = false;
          break;
        }
      }
    }
    else {
      std::string content;
      for (const std::string& payload_line : payload) {
        content += payload_line;
        content += '\n';
      }

      bool have_sync = false;
      ok = parse_vmt_sync_data(content, concat, have_sync, error);

      if (ok && !have_sync && !payload.empty()) {
        std::cerr << "WebVMT line " << cue_line << ": cue has no sync command, writing an empty packet\n";
      }
    }

    if (!ok) {
      std::cerr << "WebVMT line " << cue_line << ": cannot parse cue payload: " << error << "\n";
      continue;
    }

    // --- write the packet of the previous cue

    if (ts == prev_ts) {
      // Cues with the same start time are combined into one packet.
      prev_metadata.insert(prev_metadata.end(), concat.begin(), concat.end());
    }
    else {
      if (!add_packet(prev_metadata, ts - prev_ts)) {
        release_all();
        return 5;
      }

      prev_ts = ts;
      prev_metadata = concat;
    }
  }

  // --- flush last metadata packet

  if (!add_packet(prev_metadata, 1)) {
    release_all();
    return 5;
  }

  // --- add track reference

  heif_track_add_reference_to_track(track, heif_track_reference_type_description, visual_track);

  // --- release all objects

  release_all();

  return 0;
}
