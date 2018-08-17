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
#include <algorithm>
#include <vector>

#include <libheif/heif.h>

#if HAVE_LIBJPEG
extern "C" {
#include <jpeglib.h>
}
#endif

#if HAVE_LIBPNG
extern "C" {
#include <png.h>
}
#endif

#include <assert.h>

int master_alpha = 1;
int thumb_alpha = 1;

static struct option long_options[] = {
  {"help",       no_argument,       0, 'h' },
  {"quality",    required_argument, 0, 'q' },
  {"output",     required_argument, 0, 'o' },
  {"lossless",   no_argument,       0, 'L' },
  {"thumb",      required_argument, 0, 't' },
  {"verbose",    no_argument,       0, 'v' },
  {"params",     no_argument,       0, 'P' },
  {"no-alpha",   no_argument, &master_alpha, 0 },
  {"no-thumb-alpha",   no_argument, &thumb_alpha, 0 },
  {0,         0,                 0,  0 }
};

void show_help(const char* argv0)
{
  std::cerr << " heif-enc  libheif version: " << heif_get_version() << "\n"
            << "----------------------------------------\n"
            << "usage: heif-enc [options] image.jpeg ...\n"
            << "\n"
            << "When specifying multiple source images, they will all be saved into the same HEIF file.\n"
            << "\n"
            << "options:\n"
            << "  -h, --help      show help\n"
            << "  -q, --quality   set output quality (0-100) for lossy compression\n"
            << "  -L, --lossless  generate lossless output (-q has no effect)\n"
            << "  -t, --thumb #   generate thumbnail with maximum size # (default: off)\n"
            << "      --no-alpha  do not save alpha channel\n"
            << "      --no-thumb-alpha  do not save alpha channel in thumbnail image\n"
            << "  -o, --output    output filename (optional)\n"
            << "  -v, --verbose   enable logging output (more -v will increase logging level)\n"
            << "  -P, --params    show all encoder parameters\n"
            << "  -p              set encoder parameter (NAME=VALUE)\n";
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




#if HAVE_LIBPNG
static void
user_read_fn(png_structp png_ptr, png_bytep data, png_size_t length)
{
  FILE* fh = (FILE*)png_get_io_ptr(png_ptr);
  size_t n = fread((char*)data,length,1,fh);
  (void)n;
} // user_read_data


std::shared_ptr<heif_image> loadPNG(const char* filename)
{
  FILE* fh = fopen(filename,"rb");
  if (!fh) {
    std::cerr << "Can't open " << filename << "\n";
    exit(1);
  }


  // ### Code copied from LibVideoGfx and slightly modified to use HeifPixelImage

  struct heif_image* image = nullptr;

  png_structp png_ptr;
  png_infop info_ptr;
  png_uint_32 width, height;
  int bit_depth, color_type, interlace_type;
  int compression_type;
  png_charp name;
#if (PNG_LIBPNG_VER < 10500)
  png_charp png_profile_data;
#else
  png_bytep png_profile_data;
#endif
  uint8_t * profile_data;
  png_uint_32 profile_length = 5;
  bool color_profile_valid = false;

  /* Create and initialize the png_struct with the desired error handler
   * functions.  If you want to use the default stderr and longjump method,
   * you can supply NULL for the last three parameters.  We also supply the
   * the compiler header file version, so that we know if the application
   * was compiled with a compatible version of the library.  REQUIRED
   */
  png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  assert(png_ptr != NULL);

  /* Allocate/initialize the memory for image information.  REQUIRED. */
  info_ptr = png_create_info_struct(png_ptr);
  if (info_ptr == NULL) {
    png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
    assert(false); // , "could not create info_ptr");
  } // if

    /* Set error handling if you are using the setjmp/longjmp method (this is
     * the normal method of doing things with libpng).  REQUIRED unless you
     * set up your own error handlers in the png_create_read_struct() earlier.
     */
  if (setjmp(png_jmpbuf(png_ptr))) {
    /* Free all of the memory associated with the png_ptr and info_ptr */
    png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
    /* If we get here, we had a problem reading the file */
    assert(false); // , "fatal error in png library");
  } // if

  /* If you are using replacement read functions, instead of calling
   * png_init_io() here you would call: */
  png_set_read_fn(png_ptr, (void *)fh, user_read_fn);
  /* where user_io_ptr is a structure you want available to the callbacks */

  /* The call to png_read_info() gives us all of the information from the
   * PNG file before the first IDAT (image data chunk).  REQUIRED
   */
  png_read_info(png_ptr, info_ptr);

  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
               &interlace_type, NULL, NULL);

  assert(bit_depth < 16); // , "cannot handle 16 bit images");

  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_iCCP)) {
    if (PNG_INFO_iCCP == png_get_iCCP(png_ptr, info_ptr, &name, &compression_type, &png_profile_data, &profile_length)) {
      color_profile_valid = 1;
      profile_data = (uint8_t*) malloc(profile_length);
      memcpy(profile_data, png_profile_data, profile_length);
    }
  }
  /**** Set up the data transformations you want.  Note that these are all
   **** optional.  Only call them if you want/need them.  Many of the
   **** transformations only work on specific types of images, and many
   **** are mutually exclusive.
   ****/

  // \TODO
  //      /* Strip alpha bytes from the input data without combining with the
  //       * background (not recommended).
  //       */
  //      png_set_strip_alpha(png_ptr);

  /* Extract multiple pixels with bit depths of 1, 2, and 4 from a single
   * byte into separate bytes (useful for paletted and grayscale images).
   */
  png_set_packing(png_ptr);


  /* Expand paletted colors into true RGB triplets */
  if (color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_expand(png_ptr);

  /* Expand grayscale images to the full 8 bits from 1, 2, or 4 bits/pixel */
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    png_set_expand(png_ptr);

  /* Set the background color to draw transparent and alpha images over.
   * It is possible to set the red, green, and blue components directly
   * for paletted images instead of supplying a palette index.  Note that
   * even if the PNG file supplies a background, you are not required to
   * use it - you should use the (solid) application background if it has one.
   */

#if 0
  // \TODO 0 is index in color lookup table - correct? used already?
  png_color_16 my_background = {0, 255, 255, 255, 255};
  png_color_16 *image_background;

  if (png_get_bKGD(png_ptr, info_ptr, &image_background))
    png_set_background(png_ptr, image_background, PNG_BACKGROUND_GAMMA_FILE, 1, 1.0);
  else
    png_set_background(png_ptr, &my_background, PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
#endif


  /* Optional call to gamma correct and add the background to the palette
   * and update info structure.  REQUIRED if you are expecting libpng to
   * update the palette for you (ie you selected such a transform above).
   */
  png_read_update_info(png_ptr, info_ptr);

  /* Allocate the memory to hold the image using the fields of info_ptr. */

  /* The easiest way to read the image: */
  uint8_t** row_pointers = new png_bytep[height];
  assert(row_pointers != NULL);

  for (uint32_t y = 0; y < height; y++) {
    row_pointers[y] = (png_bytep)malloc(png_get_rowbytes(png_ptr, info_ptr));
    assert(row_pointers[y] != NULL);
  } // for

  /* Now it's time to read the image.  One of these methods is REQUIRED */
  png_read_image(png_ptr, row_pointers);

  /* read rest of file, and get additional chunks in info_ptr - REQUIRED */
  png_read_end(png_ptr, info_ptr);

  /* clean up after the read, and free any memory allocated - REQUIRED */
  png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);


  // OK, now we should have the png image in some way in
  // row_pointers, have fun with it

  int band;
  switch (color_type) {
  case PNG_COLOR_TYPE_GRAY:
  case PNG_COLOR_TYPE_GRAY_ALPHA:
    band = 1;
    break;
  case PNG_COLOR_TYPE_PALETTE:
  case PNG_COLOR_TYPE_RGB:
  case PNG_COLOR_TYPE_RGB_ALPHA:
    band = 3;
    break;
  default:
    assert(false); // , "unknown color type in png image.");
  } // switch




  struct heif_error err;

  bool has_alpha = (color_type & PNG_COLOR_MASK_ALPHA);

  if (band==1) {
    err = heif_image_create((int)width, (int)height,
                            heif_colorspace_YCbCr,
                            heif_chroma_monochrome,
                            &image);
    (void)err;

    heif_image_add_plane(image, heif_channel_Y, (int)width, (int)height, 8);

    int y_stride;
    int a_stride;
    uint8_t* py  = heif_image_get_plane(image, heif_channel_Y, &y_stride);
    uint8_t* pa  = nullptr;

    if (has_alpha) {
      heif_image_add_plane(image, heif_channel_Alpha, (int)width, (int)height, 8);

      pa = heif_image_get_plane(image, heif_channel_Alpha, &a_stride);
    }


    for (uint32_t y = 0; y < height; y++) {
      uint8_t* p = row_pointers[y];

      if (has_alpha)
        {
          for (uint32_t x = 0; x < width; x++) {
            py[y*y_stride + x] = *p++;
            pa[y*a_stride + x] = *p++;
          }
        }
      else
        {
          memcpy(&py[y*y_stride],p, width);
        }
    }
  }
  else {
    err = heif_image_create((int)width, (int)height,
                            heif_colorspace_RGB,
                            has_alpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB,
                            &image);
    (void)err;

    heif_image_add_plane(image, heif_channel_interleaved, (int)width, (int)height,
                         has_alpha ? 32 : 24);

    int stride;
    uint8_t* p = heif_image_get_plane(image, heif_channel_interleaved, &stride);

    for (uint32_t y = 0; y < height; y++) {
      if (has_alpha) {
        memcpy(p + y*stride, row_pointers[y], width*4);
      }
      else {
        memcpy(p + y*stride, row_pointers[y], width*3);
      }
    }
  }

  if (color_profile_valid && profile_length > 0){
    heif_image_set_raw_color_profile(image, "prof", profile_data, (size_t) profile_length);
    free(profile_data);
  }

  for (uint32_t y = 0; y < height; y++) {
    free(row_pointers[y]);
  } // for

  delete[] row_pointers;

  return std::shared_ptr<heif_image>(image,
                                    [] (heif_image* img) { heif_image_release(img); });
}
#else
std::shared_ptr<heif_image> loadPNG(const char* filename)
{
  std::cerr << "Cannot load PNG because libpng support was not compiled.\n";
  exit(1);

  return nullptr;
}
#endif


void list_encoder_parameters(heif_encoder* encoder)
{
  std::cerr << "Parameters for encoder `" << heif_encoder_get_name(encoder) << "`:\n";

  const struct heif_encoder_parameter*const* params = heif_encoder_list_parameters(encoder);
  for (int i=0;params[i];i++) {
    const char* name = heif_encoder_parameter_get_name(params[i]);

    switch (heif_encoder_parameter_get_type(params[i])) {
    case heif_encoder_parameter_type_integer:
      {
        int value;
        heif_error error = heif_encoder_get_parameter_integer(encoder, name, &value);
        (void)error;

        std::cerr << "  " << name << ", default=" << value;

        int have_minmax, minimum, maximum;
        error = heif_encoder_parameter_integer_valid_range(encoder, name,
                                                           &have_minmax, &minimum, &maximum);

        if (have_minmax) {
          std::cerr << ", [" << minimum << ";" << maximum << "]";
        }

        std::cerr << "\n";
      }
      break;

    case heif_encoder_parameter_type_boolean:
      {
        int value;
        heif_error error = heif_encoder_get_parameter_boolean(encoder, name, &value);
        (void)error;

        std::cerr << "  " << name << ", default=" << (value ? "true":"false") << "\n";
      }
      break;

    case heif_encoder_parameter_type_string:
      {
        const int value_size = 50;
        char value[value_size];
        heif_error error = heif_encoder_get_parameter_string(encoder, name, value, value_size);
        (void)error;

        std::cerr << "  " << name << ", default=" << value;

        const char*const* valid_options;
        error = heif_encoder_parameter_string_valid_values(encoder, name, &valid_options);

        if (valid_options) {
          std::cerr << ", { ";
          for (int i=0;valid_options[i];i++) {
            if (i>0) { std::cerr << ","; }
            std::cerr << valid_options[i];
          }
          std::cerr << " }";
        }

        std::cerr << "\n";
      }
      break;
    }
  }
}


void set_params(struct heif_encoder* encoder, std::vector<std::string> params)
{
  for (std::string p : params) {
    auto pos = p.find_first_of('=');
    if (pos == std::string::npos || pos==0 || pos==p.size()-1) {
      std::cerr << "Encoder parameter must be in the format 'name=value'\n";
      exit(5);
    }

    std::string name = p.substr(0,pos);
    std::string value = p.substr(pos+1);

    struct heif_error error = heif_encoder_set_parameter(encoder, name.c_str(), value.c_str());
    if (error.code) {
      std::cerr << "Error: " << error.message << "\n";
      exit(5);
    }
  }
}


int main(int argc, char** argv)
{
  int quality = 50;
  bool lossless = false;
  std::string output_filename;
  int logging_level = 0;
  bool option_show_parameters = false;
  int thumbnail_bbox_size = 0;

  std::vector<std::string> raw_params;


  while (true) {
    int option_index = 0;
    int c = getopt_long(argc, argv, "hq:Lo:vPp:t:", long_options, &option_index);
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
    case 'P':
      option_show_parameters = true;
      break;
    case 'p':
      raw_params.push_back(optarg);
      break;
    case 't':
      thumbnail_bbox_size = atoi(optarg);
      break;
    }
  }

  if (optind > argc-1) {
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



  // ==============================================================================

  std::shared_ptr<heif_context> context(heif_context_alloc(),
                                        [] (heif_context* c) { heif_context_free(c); });
  if (!context) {
    std::cerr << "Could not create HEIF context\n";
    return 1;
  }


  struct heif_encoder* encoder = nullptr;

#define MAX_ENCODERS 5
  const heif_encoder_descriptor* encoder_descriptors[MAX_ENCODERS];
  int count = heif_context_get_encoder_descriptors(context.get(), heif_compression_HEVC, nullptr,
                                                   encoder_descriptors, MAX_ENCODERS);

  if (count>0) {
    if (logging_level>0) {
      std::cerr << "Encoder: "
                << heif_encoder_descriptor_get_id_name(encoder_descriptors[0])
                << " = "
                << heif_encoder_descriptor_get_name(encoder_descriptors[0])
                << "\n";
    }

    heif_error error = heif_context_get_encoder(context.get(), encoder_descriptors[0], &encoder);
    if (error.code) {
      std::cerr << error.message << "\n";
      return 5;
    }
  }
  else {
    std::cerr << "No HEVC encoder available.\n";
    return 5;
  }


  if (option_show_parameters) {
    list_encoder_parameters(encoder);
    return 0;
  }



  struct heif_error error;

  for ( ; optind<argc ; optind++) {
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

    // get file type from file name

    std::string suffix;
    auto suffix_pos = input_filename.find_last_of('.');
    if (suffix_pos != std::string::npos) {
      suffix = input_filename.substr(suffix_pos+1);
      std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
    }

    enum { PNG, JPEG } filetype = JPEG;
    if (suffix == "png") {
      filetype = PNG;
    }

    std::shared_ptr<heif_image> image;
    if (filetype==PNG) {
      image = loadPNG(input_filename.c_str());
    }
    else {
      image = loadJPEG(input_filename.c_str());
    }


    if (heif_image_get_colorspace(image.get()) == heif_colorspace_RGB &&
        lossless) {
      std::cerr << "Warning: input image is in RGB colorspace, but encoding is currently\n"
                << "  always done in YCbCr colorspace. Hence, even though you specified lossless\n"
                << "  compression, there will be differences because of the color conversion.\n";
    }


    heif_encoder_set_lossy_quality(encoder, quality);
    heif_encoder_set_lossless(encoder, lossless);
    heif_encoder_set_logging_level(encoder, logging_level);

    set_params(encoder, raw_params);

    struct heif_encoding_options* options = heif_encoding_options_alloc();
    options->save_alpha_channel = (uint8_t)master_alpha;

    struct heif_image_handle* handle;
    error = heif_context_encode_image(context.get(),
                                      image.get(),
                                      encoder,
                                      options,
                                      &handle);
    if (error.code != 0) {
      std::cerr << "Could not read HEIF file: " << error.message << "\n";
      return 1;
    }


    if (thumbnail_bbox_size > 0)
      {
        // encode thumbnail

        struct heif_image_handle* thumbnail_handle;

        options->save_alpha_channel = master_alpha && thumb_alpha;

        error = heif_context_encode_thumbnail(context.get(),
                                              image.get(),
                                              handle,
                                              encoder,
                                              options,
                                              thumbnail_bbox_size,
                                              &thumbnail_handle);
        if (error.code) {
          std::cerr << "Could not generate thumbnail: " << error.message << "\n";
          return 5;
        }

        if (thumbnail_handle) {
          heif_image_handle_release(thumbnail_handle);
        }
      }

    heif_image_handle_release(handle);
  }

  heif_encoder_release(encoder);

  error = heif_context_write_to_file(context.get(), output_filename.c_str());
  if (error.code) {
    std::cerr << error.message << "\n";
    return 5;
  }

  return 0;
}
