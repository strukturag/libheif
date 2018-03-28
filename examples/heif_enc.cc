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
#include <getopt.h>

#include <fstream>
#include <iostream>
#include <memory>

#include "heif.h"

#if HAVE_LIBJPEG
extern "C" {
#include <jpeglib.h>
}
#endif


static struct option long_options[] = {
  {"help",       no_argument,       0, 'h' },
  {"quality",    required_argument, 0, 'q' },
  {"output",     required_argument, 0, 'o' },
  {"lossless",   no_argument,       0, 'L' },
  {"verbose",    no_argument,       0, 'v' },
  {0,         0,                 0,  0 }
};

void show_help(const char* argv0)
{
  std::cerr << " heif-enc  libheif version: " << heif_get_version() << "\n"
            << "------------------------------------\n"
            << "usage: heif-enc [options] image.jpeg\n"
            << "\n"
            << "options:\n"
            << "  -h, --help      show help\n"
            << "  -q, --quality   set output quality (0-100) for lossy compression\n"
            << "  -L, --lossless  generate lossless output (-q has no effect)\n"
            << "  -o, --output    output filename (optional)\n"
            << "  -v, --verbose   enable logging output (more -v will increase logging level)\n";
}



#if HAVE_LIBJPEG
std::shared_ptr<heif_image> loadJPEG(const char* filename)
{
  struct heif_image* image = nullptr;


  // ### Code copied from LibVideoGfx and slightly modified to use HeifPixelImage

  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;

  // open input file

  FILE * infile;
  if ((infile = fopen(filename, "rb")) == NULL) {
    std::cerr << "Can't open " << filename << "\n";
    exit(1);
  }


  // initialize decompressor

  jpeg_create_decompress(&cinfo);

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_stdio_src(&cinfo, infile);

  jpeg_read_header(&cinfo, TRUE);

  if (cinfo.jpeg_color_space == JCS_GRAYSCALE)
    {
      cinfo.out_color_space = JCS_GRAYSCALE;

      jpeg_start_decompress(&cinfo);

      JSAMPARRAY buffer;
      buffer = (*cinfo.mem->alloc_sarray)
        ((j_common_ptr) &cinfo, JPOOL_IMAGE, cinfo.output_width * cinfo.output_components, 1);


      // create destination image

      struct heif_error err = heif_image_create(cinfo.output_width, cinfo.output_height,
                                                heif_colorspace_YCbCr,
                                                heif_chroma_monochrome,
                                                &image);
      (void)err;
      // TODO: handle error

      heif_image_add_plane(image, heif_channel_Y, cinfo.output_width, cinfo.output_height, 8);

      int y_stride;
      uint8_t* py = heif_image_get_plane(image, heif_channel_Y, &y_stride);


      // read the image

      while (cinfo.output_scanline < cinfo.output_height) {
        (void) jpeg_read_scanlines(&cinfo, buffer, 1);

        memcpy(py + (cinfo.output_scanline-1)*y_stride, buffer, cinfo.output_width);
      }

    }
  else
    {
      cinfo.out_color_space = JCS_YCbCr;

      jpeg_start_decompress(&cinfo);

      JSAMPARRAY buffer;
      buffer = (*cinfo.mem->alloc_sarray)
        ((j_common_ptr) &cinfo, JPOOL_IMAGE, cinfo.output_width * cinfo.output_components, 1);


      // create destination image

      struct heif_error err = heif_image_create(cinfo.output_width, cinfo.output_height,
                                                heif_colorspace_YCbCr,
                                                heif_chroma_420,
                                                &image);
      (void)err;

      heif_image_add_plane(image, heif_channel_Y, cinfo.output_width, cinfo.output_height, 8);
      heif_image_add_plane(image, heif_channel_Cb, (cinfo.output_width+1)/2, (cinfo.output_height+1)/2, 8);
      heif_image_add_plane(image, heif_channel_Cr, (cinfo.output_width+1)/2, (cinfo.output_height+1)/2, 8);

      int y_stride;
      int cb_stride;
      int cr_stride;
      uint8_t* py  = heif_image_get_plane(image, heif_channel_Y, &y_stride);
      uint8_t* pcb = heif_image_get_plane(image, heif_channel_Cb, &cb_stride);
      uint8_t* pcr = heif_image_get_plane(image, heif_channel_Cr, &cr_stride);

      // read the image

      //printf("jpeg size: %d %d\n",cinfo.output_width, cinfo.output_height);

      while (cinfo.output_scanline < cinfo.output_height) {
        JOCTET* bufp;

        (void) jpeg_read_scanlines(&cinfo, buffer, 1);

        bufp = buffer[0];

        int y = cinfo.output_scanline-1;

        for (unsigned int x=0;x<cinfo.output_width;x+=2) {
          py[y*y_stride + x] = *bufp++;
          pcb[y/2*cb_stride + x/2] = *bufp++;
          pcr[y/2*cr_stride + x/2] = *bufp++;

          if (x+1 < cinfo.output_width) {
            py[y*y_stride + x+1] = *bufp++;
          }

          bufp+=2;
        }


        if (cinfo.output_scanline < cinfo.output_height) {
          (void) jpeg_read_scanlines(&cinfo, buffer, 1);

          bufp = buffer[0];

          y = cinfo.output_scanline-1;

          for (unsigned int x=0;x<cinfo.output_width;x++) {
            py[y*y_stride + x] = *bufp++;
            bufp+=2;
          }
        }
      }
    }


  // cleanup

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);

  fclose(infile);


  return std::shared_ptr<heif_image>(image,
                                    [] (heif_image* img) { heif_image_release(img); });

}
#else
std::shared_ptr<heif_image> loadJPEG(const char* filename)
{
  std::cerr << "Cannot load JPEG because libjpeg support was not compiled.\n";
  exit(1);

  return nullptr;
}
#endif


int main(int argc, char** argv)
{
  int quality = 50;
  bool lossless = false;
  std::string output_filename;
  int logging_level = 0;

  while (true) {
    int option_index = 0;
    int c = getopt_long(argc, argv, "hq:Lo:v", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'h':
      show_help(argv[0]);
      return 0;
    case 'q':
      quality = atoi(optarg);
      break;
    case 'L':
      lossless = true;
      break;
    case 'o':
      output_filename = optarg;
      break;
    case 'v':
      logging_level++;
      break;
    }
  }

  if (optind != argc-1) {
    show_help(argv[0]);
    return 0;
  }


  if (quality<0 || quality>100) {
    std::cerr << "Invalid quality factor. Must be between 0 and 100.\n";
    return 5;
  }

  if (logging_level>0) {
    logging_level += 2;

    if (logging_level > 4) {
      logging_level = 4;
    }
  }

  std::string input_filename = argv[optind];

  if (output_filename.empty()) {
    std::string filename_without_suffix;
    std::string::size_type dot_position = input_filename.find_last_of('.');
    if (dot_position != std::string::npos) {
      filename_without_suffix = input_filename.substr(0 , dot_position);
    }
    else {
      filename_without_suffix = input_filename;
    }

    output_filename = filename_without_suffix + ".heic";
  }


  // ==============================================================================

  std::shared_ptr<heif_context> context(heif_context_alloc(),
                                        [] (heif_context* c) { heif_context_free(c); });
  if (!context) {
    std::cerr << "Could not create HEIF context\n";
    return 1;
  }


  std::shared_ptr<heif_image> image = loadJPEG(input_filename.c_str());



#define MAX_ENCODERS 5
  heif_encoder* encoders[MAX_ENCODERS];
  int count = heif_context_get_encoders(context.get(), heif_compression_HEVC, nullptr,
                                        encoders, MAX_ENCODERS);

  if (count>0) {
    if (logging_level>0) {
      std::cerr << "Encoder: " << heif_encoder_get_name(encoders[0]) << "\n";
    }

    heif_encoder_start(encoders[0]);

    heif_encoder_set_lossy_quality(encoders[0], quality);
    heif_encoder_set_lossless(encoders[0], lossless);
    heif_encoder_set_logging_level(encoders[0], logging_level);

    struct heif_image_handle* handle;
    heif_error error = heif_context_encode_image(context.get(),
                                                 image.get(),
                                                 encoders[0],
                                                 &handle);
    if (error.code != 0) {
      std::cerr << "Could not read HEIF file: " << error.message << "\n";
      return 1;
    }

    heif_image_handle_release(handle);

    error = heif_context_write_to_file(context.get(), output_filename.c_str());
  }
  else {
    std::cerr << "No HEVC encoder available.\n";
  }


  return 0;
}
