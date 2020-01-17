/*
  libheif example application "convert".

  MIT License

  Copyright (c) 2017 struktur AG, Joachim Bauch <bauch@struktur.de>

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
#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include "string.h"

#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif
#include <fstream>
#include <iostream>
#include <sstream>
#include <assert.h>
#include <algorithm>
#include <cctype>

#include <libheif/heif.h>

#include "encoder.h"
#if HAVE_LIBJPEG
#include "encoder_jpeg.h"
#endif
#if HAVE_LIBPNG
#include "encoder_png.h"
#endif
#include "encoder_y4m.h"

#if defined(_MSC_VER)
#include "getopt.h"
#endif

#define UNUSED(x) (void)x

static int usage(const char* command) {
  fprintf(stderr, "USAGE: %s [-q quality 0..100] <filename> <output>\n", command);
  return 1;
}

class ContextReleaser {
 public:
  ContextReleaser(struct heif_context* ctx) : ctx_(ctx) {}
  ~ContextReleaser() {
    heif_context_free(ctx_);
  }

 private:
  struct heif_context* ctx_;
};

int main(int argc, char** argv)
{
  int opt;
  int quality = -1;  // Use default quality.
  UNUSED(quality);  // The quality will only be used by encoders that support it.
  while ((opt = getopt(argc, argv, "q:")) != -1) {
    switch (opt) {
    case 'q':
      quality = atoi(optarg);
      break;
    default: /* '?' */
      return usage(argv[0]);
    }
  }

  if (optind + 2 > argc) {
    // Need input and output filenames as additional arguments.
    return usage(argv[0]);
  }

  std::string input_filename(argv[optind++]);
  std::string output_filename(argv[optind++]);

  std::unique_ptr<Encoder> encoder;

  size_t dot_pos = output_filename.rfind('.');
  if (dot_pos != std::string::npos) {
    std::string suffix_lowercase = output_filename.substr(dot_pos+1);

    std::transform(suffix_lowercase.begin(), suffix_lowercase.end(),
                   suffix_lowercase.begin(), ::tolower);

    if (suffix_lowercase == "jpg" || suffix_lowercase == "jpeg") {
#if HAVE_LIBJPEG
      static const int kDefaultJpegQuality = 90;
      if (quality == -1) {
        quality = kDefaultJpegQuality;
      }
      encoder.reset(new JpegEncoder(quality));
#else
      fprintf(stderr, "JPEG support has not been compiled in.\n");
      return 1;
#endif  // HAVE_LIBJPEG
    }

    if (suffix_lowercase == "png") {
#if HAVE_LIBPNG
      encoder.reset(new PngEncoder());
#else
      fprintf(stderr, "PNG support has not been compiled in.\n");
      return 1;
#endif  // HAVE_LIBPNG
    }

    if (suffix_lowercase == "y4m") {
      encoder.reset(new Y4MEncoder());
    }
  }

  if (!encoder) {
    fprintf(stderr, "Unknown file type in %s\n", output_filename.c_str());
    return 1;
  }


  // --- check whether input is a supported HEIF file

  // TODO: when we are reading from named pipes, we probably should not consume any bytes
  // just for file-type checking.
  // TODO: check, whether reading from named pipes works at all.

  std::ifstream istr(input_filename.c_str(), std::ios_base::binary);
  uint8_t magic[12];
  istr.read((char*)magic,12);
  enum heif_filetype_result filetype_check = heif_check_filetype(magic,12);
  if (filetype_check == heif_filetype_no) {
    fprintf(stderr, "Input file is not an HEIF file\n");
    return 1;
  }

  if (filetype_check == heif_filetype_yes_unsupported) {
    fprintf(stderr, "Input file is an unsupported HEIF file type\n");
    return 1;
  }



  // --- read the HEIF file

  struct heif_context* ctx = heif_context_alloc();
  if (!ctx) {
    fprintf(stderr, "Could not create HEIF context\n");
    return 1;
  }

  ContextReleaser cr(ctx);
  struct heif_error err;
  err = heif_context_read_from_file(ctx, input_filename.c_str(), nullptr);
  if (err.code != 0) {
    std::cerr << "Could not read HEIF file: " << err.message << "\n";
    return 1;
  }

  int num_images = heif_context_get_number_of_top_level_images(ctx);
  if (num_images == 0) {
    fprintf(stderr, "File doesn't contain any images\n");
    return 1;
  }

  printf("File contains %d images\n", num_images);

  heif_item_id* image_IDs = (heif_item_id*)alloca(num_images * sizeof(heif_item_id));
  num_images = heif_context_get_list_of_top_level_image_IDs(ctx, image_IDs, num_images);


  std::string filename;
  size_t image_index = 1;  // Image filenames are "1" based.

  for (int idx = 0; idx < num_images; ++idx) {

    if (num_images>1) {
      std::ostringstream s;
      s << output_filename.substr(0, output_filename.find_last_of('.'));
      s << "-" << image_index;
      s << output_filename.substr(output_filename.find_last_of('.'));
      filename.assign(s.str());
    } else {
      filename.assign(output_filename);
    }

    struct heif_image_handle* handle;
    err = heif_context_get_image_handle(ctx, image_IDs[idx], &handle);
    if (err.code) {
      std::cerr << "Could not read HEIF image " << idx << ": "
                << err.message << "\n";
      return 1;
    }

    int has_alpha = heif_image_handle_has_alpha_channel(handle);
    struct heif_decoding_options* decode_options = heif_decoding_options_alloc();
    encoder->UpdateDecodingOptions(handle, decode_options);

    int bit_depth = heif_image_handle_get_luma_bits_per_pixel(handle);
    if (bit_depth < 0) {
      heif_decoding_options_free(decode_options);
      heif_image_handle_release(handle);
      std::cerr << "Input image has undefined bit-depth\n";
      return 1;
    }

    struct heif_image* image;
    err = heif_decode_image(handle,
                            &image,
                            encoder->colorspace(has_alpha),
                            encoder->chroma(has_alpha, bit_depth),
                            decode_options);
    heif_decoding_options_free(decode_options);
    if (err.code) {
      heif_image_handle_release(handle);
      std::cerr << "Could not decode HEIF image: " << idx << ": "
                << err.message << "\n";
      return 1;
    }

    if (image) {
      bool written = encoder->Encode(handle, image, filename.c_str());
      if (!written) {
        fprintf(stderr,"could not write image\n");
      } else {
        printf("Written to %s\n", filename.c_str());
      }
      heif_image_release(image);


      int has_depth = heif_image_handle_has_depth_image(handle);
      if (has_depth) {
        heif_item_id depth_id;
        int nDepthImages = heif_image_handle_get_list_of_depth_image_IDs(handle, &depth_id, 1);
        assert(nDepthImages==1);
        (void)nDepthImages;

        struct heif_image_handle* depth_handle;
        err = heif_image_handle_get_depth_image_handle(handle, depth_id, &depth_handle);
        if (err.code) {
          heif_image_handle_release(handle);
          std::cerr << "Could not read depth channel\n";
          return 1;
        }

        int bit_depth = heif_image_handle_get_luma_bits_per_pixel(depth_handle);

        struct heif_image* depth_image;
        err = heif_decode_image(depth_handle,
                                &depth_image,
                                encoder->colorspace(false),
                                encoder->chroma(false, bit_depth),
                                nullptr);
        if (err.code) {
          heif_image_handle_release(depth_handle);
          heif_image_handle_release(handle);
          std::cerr << "Could not decode depth image: " << err.message << "\n";
          return 1;
        }

        std::ostringstream s;
        s << output_filename.substr(0, output_filename.find('.'));
        s << "-depth";
        s << output_filename.substr(output_filename.find('.'));

        written = encoder->Encode(depth_handle, depth_image, s.str());
        if (!written) {
          fprintf(stderr,"could not write depth image\n");
        } else {
          printf("Depth image written to %s\n", s.str().c_str());
        }
      }
      heif_image_handle_release(handle);
    }

    image_index++;
  }

  return 0;
}
