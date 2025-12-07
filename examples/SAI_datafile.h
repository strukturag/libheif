/*
 * ImageMeter confidential
 *
 * Copyright (C) 2025 by Dirk Farin, Kronenstr. 49b, 70174 Stuttgart, Germany
 * All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains the property
 * of Dirk Farin.  The intellectual and technical concepts contained
 * herein are proprietary to Dirk Farin and are protected by trade secret
 * and copyright law.
 * Dissemination of this information or reproduction of this material
 * is strictly forbidden unless prior written permission is obtained
 * from Dirk Farin.
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
