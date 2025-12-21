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

#ifndef SAI_DATAFILE_H
#define SAI_DATAFILE_H
#include <string>
#include <vector>

#include "libheif/heif_tai_timestamps.h"

// Helper class for heif-enc to read a SAI data file and provide the data for the track's SAI items.
struct SAI_datafile
{
  ~SAI_datafile();

  void load_sai_data_from_file(const char* sai_file);

  heif_tai_clock_info* tai_clock_info = nullptr;

  std::vector<heif_tai_timestamp_packet*> tai_timestamps;
  std::vector<std::string> gimi_content_ids;

  std::vector<std::string> active_sais;

private:
  void handleHeaderEntry(const std::string& code,
                         const std::vector<std::string>& values);

  void handleMainEntry(const std::vector<std::string>& values, int line, int main_item_line);

  bool isSeparatorLine(const std::string& line);

  std::vector<std::string> splitCSV(const std::string& line);
};


#endif //SAI_DATAFILE_H
