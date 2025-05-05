/*
  libheif example application "heif".

  MIT License

  Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>

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

#include "common.h"
#include "libheif/heif.h"
#include <iostream>
#include <fstream>
#include <array>
#include <vector>
#include <algorithm>

namespace heif_examples {
  void show_version()
  {
    std::cout << LIBHEIF_VERSION << '\n'
              << "libheif: " << heif_get_version() << '\n';
    {
      auto paths = heif_get_plugin_directories();
      for (int i = 0; paths[i]; i++) {
        std::cout << "plugin path: " << paths[i] << '\n';
      }

      if (paths[0] == nullptr) {
        std::cout << "plugin path: plugins are disabled\n";
      }

      heif_free_plugin_directories(paths);
    }
  }


#define MAX_DECODERS 20

  void list_decoders(heif_compression_format format)
  {
    const heif_decoder_descriptor* decoders[MAX_DECODERS];
    int n = heif_get_decoder_descriptors(format, decoders, MAX_DECODERS);

    for (int i = 0; i < n; i++) {
      const char* id = heif_decoder_descriptor_get_id_name(decoders[i]);
      if (id == nullptr) {
        id = "---";
      }

      std::cout << "- " << id << " = " << heif_decoder_descriptor_get_name(decoders[i]) << "\n";
    }
  }


  void list_all_decoders()
  {
    std::cout << "AVC decoders:\n";
    list_decoders(heif_compression_AVC);

    std::cout << "AVIF decoders:\n";
    list_decoders(heif_compression_AV1);

    std::cout << "HEIC decoders:\n";
    list_decoders(heif_compression_HEVC);

    std::cout << "JPEG decoders:\n";
    list_decoders(heif_compression_JPEG);

    std::cout << "JPEG 2000 decoders:\n";
    list_decoders(heif_compression_JPEG2000);

    std::cout << "JPEG 2000 (HT) decoders:\n";
    list_decoders(heif_compression_HTJ2K);

    std::cout << "uncompressed:\n";
    list_decoders(heif_compression_uncompressed);

    std::cout << "VVIC decoders:\n";
    list_decoders(heif_compression_VVC);
  }


  std::string fourcc_to_string(uint32_t fourcc)
  {
    char s[5];
    s[0] = static_cast<char>((fourcc >> 24) & 0xFF);
    s[1] = static_cast<char>((fourcc >> 16) & 0xFF);
    s[2] = static_cast<char>((fourcc >> 8) & 0xFF);
    s[3] = static_cast<char>((fourcc) & 0xFF);
    s[4] = 0;

    return s;
  }


  int check_for_valid_input_HEIF_file(const std::string& input_filename)
  {
    std::ifstream istr(input_filename.c_str(), std::ios_base::binary);
    if (istr.fail()) {
      fprintf(stderr, "Input file does not exist.\n");
      return 10;
    }

    std::array<uint8_t, 4> length{};
    istr.read((char*) length.data(), length.size());
    uint32_t box_size = (length[0] << 24) + (length[1] << 16) + (length[2] << 8) + (length[3]);
    if ((box_size < 16) || (box_size > 512)) {
      fprintf(stderr, "Input file does not appear to start with a valid box length.");
      if ((box_size & 0xFFFFFFF0) == 0xFFD8FFE0) {
        fprintf(stderr, " Possibly could be a JPEG file instead.\n");
      }
      else {
        fprintf(stderr, "\n");
      }
      return 1;
    }

    std::vector<uint8_t> ftyp_bytes(box_size);
    std::copy(length.begin(), length.end(), ftyp_bytes.begin());
    istr.read((char*) ftyp_bytes.data() + 4, ftyp_bytes.size() - 4);

    heif_error filetype_check = heif_has_compatible_filetype(ftyp_bytes.data(), (int) ftyp_bytes.size());
    if (filetype_check.code != heif_error_Ok) {
      fprintf(stderr, "Input file is not a supported format. %s\n", filetype_check.message);
      return 1;
    }

    return 0;
  }

}
