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
#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <errno.h>
#include <string.h>

#include "heif.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <getopt.h>


/*
  image: 20005 (1920x1080), primary
    thumbnail: 20010 (320x240)
    alpha channel: 20012 (1920x1080)
    metadata: Exif

  image: 1920x1080 (20005), primary
    thumbnail: 320x240 (20010)
    alpha channel: 1920x1080 (20012)

info *file
info -w 20012 -o out.265 *file
info -d // dump
 */

static struct option long_options[] = {
  {"write-raw", required_argument, 0, 'w' },
  {"output",    required_argument, 0, 'o' },
  {"dump-boxes", no_argument,      0, 'd' },
  {"help",       no_argument,      0, 'h' },
  {0,         0,                 0,  0 }
};

void show_help(const char* argv0)
{
    fprintf(stderr," heif-info  libheif version: %s\n",heif_get_version());
    fprintf(stderr,"------------------------------------\n");
    fprintf(stderr,"usage: heif-info [options] image.heic\n");
    fprintf(stderr,"\n");
    fprintf(stderr,"options:\n");
    fprintf(stderr,"  -w, --write-raw ID   write raw compressed data of image 'ID'\n");
    fprintf(stderr,"  -o, --output NAME    output file name for image selected by -w\n");
    fprintf(stderr,"  -d, --dump-boxes     show a low-level dump of all MP4 file boxes\n");
    fprintf(stderr,"  -h, --help           show help\n");
}

int main(int argc, char** argv)
{
  bool dump_boxes = false;

  bool write_raw_image = false;
  heif_image_id raw_image_id;
  std::string output_filename = "output.265";

  while (true) {
    int option_index = 0;
    int c = getopt_long(argc, argv, "w:o:dh", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'd':
      dump_boxes = true;
      break;
    case 'h':
      show_help(argv[0]);
      return 0;
    case 'w':
      write_raw_image = true;
      raw_image_id = atoi(optarg);
      break;
    case 'o':
      output_filename = optarg;
      break;
    }
  }

  if (optind != argc-1) {
    show_help(argv[0]);
    return 0;
  }


  (void)raw_image_id;
  (void)write_raw_image;

  const char* input_filename = argv[1];

  // ==============================================================================

  std::shared_ptr<heif_context> ctx(heif_context_alloc(),
                                    [] (heif_context* c) { heif_context_free(c); });
  if (!ctx) {
    fprintf(stderr, "Could not create HEIF context\n");
    return 1;
  }

  struct heif_error err;
  err = heif_context_read_from_file(ctx.get(), input_filename);
  if (err.code != 0) {
    std::cerr << "Could not read HEIF file: " << err.message << "\n";
    return 1;
  }

  if (dump_boxes) {
    heif_context_debug_dump_boxes(ctx.get());
    std::cout << "----------------------------------------------------------\n";
  }


  // ==============================================================================





  std::cout << "num images: " << heif_context_get_number_of_top_level_images(ctx.get()) << "\n";

  struct heif_image_handle* handle;
  err = heif_context_get_primary_image_handle(ctx.get(), &handle);
  if (err.code != 0) {
    std::cerr << "Could not get primage image handle: " << err.message << "\n";
    return 1;
  }

  struct heif_image* image;
  err = heif_decode_image(handle, heif_colorspace_undefined, heif_chroma_undefined, &image);
  if (err.code != 0) {
    heif_image_handle_release(handle);
    std::cerr << "Could not decode primage image: " << err.message << "\n";
    return 1;
  }

  heif_image_release(image);
  heif_image_handle_release(handle);
  return 0;
}
