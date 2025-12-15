/*
 * HEIF codec.
 * Copyright (c) 2025 Dirk Farin <dirk.farin@gmail.com>
 *
 * This file is part of libheif.
 *
 * libheif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libheif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libheif.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "SAI_datafile.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>


SAI_datafile::~SAI_datafile()
{
  heif_tai_clock_info_release(tai_clock_info);

  for (auto tai : tai_timestamps) {
    heif_tai_timestamp_packet_release(tai);
  }
}


void SAI_datafile::handleHeaderEntry(const std::string& code,
                                const std::vector<std::string>& values)
{
  if (code == "suid") {
    active_sais.push_back(code);
    if (!values.empty()) {
      std::cerr << "Invalid 'suid' header line. May not have additional parameters.\n";
      exit(5);
    }
  }
  else if (code == "stai") {
    active_sais.push_back(code);
    if (values.size() > 4) {
      std::cerr << "Invalid 'stai' header line. May not have more than 4 parameters.\n";
      exit(5);
    }

    tai_clock_info = heif_tai_clock_info_alloc();

    for (size_t i=0;i<values.size();i++) {
      int64_t val = std::stoll(values[i]);
      if (i==1 && (val < 0 || val > 0xffffffff)) {
        std::cerr << "Invalid SAI tai clock info entry in header\n";
        exit(5);
      }

      if (i==2 && (val < 0 || val > 0x7fffffff)) {
        std::cerr << "Invalid SAI tai clock info entry in header\n";
        exit(5);
      }

      if (i==3 && (val < 0 || val > 0xff)) {
        std::cerr << "Invalid SAI tai clock info entry in header\n";
        exit(5);
      }

      switch (i) {
        case 0:
          tai_clock_info->time_uncertainty = val;
          break;
        case 1:
          tai_clock_info->clock_resolution = static_cast<uint32_t>(val);
          break;
        case 2:
          tai_clock_info->clock_drift_rate = static_cast<int32_t>(val);
          break;
        case 3:
          tai_clock_info->clock_type = static_cast<uint8_t>(val);
          break;
      }
    }
  }
  else {
    std::cerr << "Unknown code in SAI data file header: " << code << "\n";
    exit(5);
  }
}

void SAI_datafile::handleMainEntry(const std::vector<std::string>& values, int line, int main_item_line)
{
  if (active_sais.empty()) {
    std::cerr << "Invalid SAI data file: data received, but no SAIs defined.";
    exit(5);
  }

  size_t idx = main_item_line % active_sais.size();
  if (active_sais[idx] == "suid") {
    if (values.size() > 1) {
      std::cerr << "Invalid SAI content-id entry in line " << line << "\n";
      exit(5);
    }

    if (values.empty()) {
      gimi_content_ids.push_back({});
    }
    else {
      gimi_content_ids.push_back(values[0]);
    }
  }
  else if (active_sais[idx] == "stai") {
    if (values.size() > 4) {
      std::cerr << "Invalid SAI timestamp entry in line " << line << "\n";
      exit(5);
    }

    if (values.empty()) {
      tai_timestamps.push_back(nullptr);
    }
    else {
      heif_tai_timestamp_packet* tai = heif_tai_timestamp_packet_alloc();
      for (size_t i=0;i<values.size();i++) {
        int64_t val = std::stoll(values[i]);
        if (i>=1 && i<=3 && (val < 0 || val > 1)) {
          std::cerr << "Invalid SAI timestamp entry in line " << line << "\n";
          exit(5);
        }

        switch (i) {
          case 0:
            tai->tai_timestamp = val;
            break;
          case 1:
            tai->synchronization_state = static_cast<uint8_t>(val);
            break;
          case 2:
            tai->timestamp_generation_failure = static_cast<uint8_t>(val);
            break;
          case 3:
            tai->timestamp_is_modified = static_cast<uint8_t>(val);
            break;
        }
      }

      tai_timestamps.push_back(tai);
    }
  }
}

bool SAI_datafile::isSeparatorLine(const std::string& line)
{
  return line.starts_with("---");
}

std::vector<std::string> SAI_datafile::splitCSV(const std::string& line)
{
  std::vector<std::string> parts;
  std::stringstream ss(line);
  std::string item;

  while (std::getline(ss, item, ',')) {
    // Trim whitespace
    item.erase(item.begin(),
        std::find_if(item.begin(), item.end(), [](unsigned char ch){ return !std::isspace(ch); }));
    item.erase(
        std::find_if(item.rbegin(), item.rend(), [](unsigned char ch){ return !std::isspace(ch); }).base(),
        item.end()
    );
    parts.push_back(item);
  }

  return parts;
}


void SAI_datafile::load_sai_data_from_file(const char* sai_file)
{
  std::ifstream istr(sai_file);
  if (!istr) {
    std::cerr << "Could not open SAI data file\n";
    exit(5);
  }

  // --- read header

  std::string line;
  bool inHeader = true;
  int line_counter = 0;
  int main_item_line = 0;

  while (std::getline(istr, line)) {
    line_counter++;

    if (inHeader && line.empty())
      continue;

    if (inHeader && isSeparatorLine(line)) {
      // Switch to main part
      inHeader = false;
      continue;
    }

    if (inHeader) {
      // Header line: starts with 4-character code, optional CSV list
      if (line.size() < 4) {
        std::cerr << "Invalid header line: " << line << "\n";
        continue;
      }

      std::string code = line.substr(0, 4);

      std::vector<std::string> values;
      if (line.size() > 4) {
        // Skip the space after the code, if present
        std::string rest = line.substr(4);
        if (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t'))
          rest.erase(0, 1);

        if (!rest.empty())
          values = splitCSV(rest);
      }

      handleHeaderEntry(code, values);
    }
    else {
      // Main section: entire line is CSV
      auto values = splitCSV(line);
      handleMainEntry(values, line_counter, main_item_line++);
    }
  }
}
