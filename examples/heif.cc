/*
 * libheif example application "heif".
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of heif, an example application using libheif.
 *
 * heif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * heif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with heif.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <string.h>

#include "box.h"
#include "heif_file.h"
#include "libde265/de265.h"

#include <fstream>
#include <iostream>

using namespace heif;


int main(int argc, char** argv)
{
  using heif::BoxHeader;
  using heif::Box;
  using heif::fourcc;

  if (argc < 2) {
    fprintf(stderr, "USAGE: %s <filename> [output]\n", argv[0]);
    return 1;
  }

  const char* input_filename = argv[1];

#if 0
  const char* output_filename = nullptr;
  if (argc >= 3) {
    output_filename = argv[2];
  }
#endif

  // ==============================================================================

  HeifFile heifFile;
  Error err = heifFile.read_from_file(input_filename);

  if (err != Error::OK) {
    std::cerr << "error: " << err << "\n";
    return 0;
  }


  std::cout << "----------------------------------------------------------\n";

  std::cout << "num images: " << heifFile.get_num_images() << "\n";
  std::cout << "primary image: " << heifFile.get_primary_image_ID() << "\n";

  uint16_t primary_image_ID = heifFile.get_primary_image_ID();

  std::shared_ptr<HeifPixelImage> img;
  err = heifFile.decode_image(primary_image_ID, img);

  return 0;
}
