/*
 * libheif example application "convert".
 * Copyright (c) 2017 struktur AG, Joachim Bauch <bauch@struktur.de>
 *
 * This file is part of convert, an example application using libheif.
 *
 * convert is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * convert is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with convert.  If not, see <http://www.gnu.org/licenses/>.
 */
#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <fstream>
#include <iostream>
#include <sstream>

#include "box.h"
#include "heif_file.h"
#include "libde265/de265.h"

#include "encoder.h"
#if HAVE_LIBJPEG
#include "encoder_jpeg.h"
#endif
#if HAVE_LIBPNG
#include "encoder_png.h"
#endif

using namespace heif;

int main(int argc, char** argv)
{
  using heif::BoxHeader;
  using heif::Box;
  using heif::fourcc;

  if (argc < 3) {
    fprintf(stderr, "USAGE: %s <filename> <output>\n", argv[0]);
    return 1;
  }

  std::string input_filename(argv[1]);
  std::string output_filename(argv[2]);
  std::ifstream istr(input_filename.c_str());

  std::unique_ptr<Encoder> encoder;
#if HAVE_LIBJPEG
  if (output_filename.find(".jpg") == output_filename.size() - 4) {
    // TODO(jojo): Should this be configurable?
    static const int kJpegQuality = 90;
    encoder.reset(new JpegEncoder(kJpegQuality));
  }
#endif  // HAVE_LIBJPEG
#if HAVE_LIBPNG
  if (output_filename.find(".png") == output_filename.size() - 4) {
    encoder.reset(new PngEncoder());
  }
#endif  // HAVE_LIBPNG
  if (!encoder) {
    fprintf(stderr, "Unknown file type in %s\n", output_filename.c_str());
    return 1;
  }


  HeifFile heifFile;
  Error err = heifFile.read_from_file(input_filename.c_str());
  if (err != Error::OK) {
    std::cerr << "Could not read HEIF file: " << err << "\n";
    return 1;
  }

  int num_images = heifFile.get_num_images();

  if (num_images==0) {
    fprintf(stderr, "File doesn't contain any images\n");
    return 1;
  }

  printf("File contains %d images\n", num_images);


  std::vector<uint32_t> imageIDs = heifFile.get_image_IDs();

  std::string filename;
  size_t image_index = 1;  // Image filenames are "1" based.

  for (uint32_t imageID : imageIDs) {

    if (num_images>1) {
      std::ostringstream s;
      s << output_filename.substr(0, output_filename.find('.'));
      s << "-" << image_index;
      s << output_filename.substr(output_filename.find('.'));
      filename.assign(s.str());
    } else {
      filename.assign(output_filename);
    }

    const de265_image* img;
    err = heifFile.get_image(imageID, &img, istr);
    if (err != Error::OK) {
      std::cerr << "Could not read HEIF image: " << err << "\n";
      return 1;
    }

    bool written = encoder->Encode(img, filename.c_str());
    if (!written) {
      fprintf(stderr,"could not write image\n");
    }

    //de265_release_next_picture(ctx);

    printf("Written to %s\n", filename.c_str());
    image_index++;
  }

  return 0;
}
