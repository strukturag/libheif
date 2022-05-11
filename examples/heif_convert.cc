

#include <cstring>

#if defined(HAVE_UNISTD_H)

#include <unistd.h>

#endif

#include <fstream>
#include <iostream>
#include <sstream>
#include <cassert>
#include <algorithm>
#include <vector>
#include <cctype>

#include <libheif/heif.h>

#include "encoder.h"

#if HAVE_LIBJPEG
#include "encoder_jpeg.h"
#endif


void checkError(struct heif_error error) {
      if (error.code) {
        std::cerr << error.message << "\n";
        exit(5);
      }
}
void assertTrue(bool b) {
  if (!b) {

  }
}
void decodeWarnings(struct heif_image* image, struct heif_error err) {
  for (int i = 0;; i++) {
    int n = heif_image_get_decoding_warnings(image, i, &err, 1);
    if (n == 0) {
      break;
    }
    std::cerr << "Warning: " << err.message << "\n";
  }
}

int heif_to_jpg() {
  //VARIABLES
  std::string input_filename("input/heif.heif");
  std::string output_filename("out/heif_convert.jpg");
  std::unique_ptr<Encoder> encoder;
  heif_item_id image_id = 1;
  struct heif_error err;
  struct heif_image_handle* handle;
  struct heif_image* image;

  //SET ENCODER
  static const int kDefaultJpegQuality = 90;
  encoder.reset(new JpegEncoder(kDefaultJpegQuality));
  if (!encoder) {
    fprintf(stderr, "Unknown file type in %s\n", output_filename.c_str());
    return 1;
  }

  //CREATE CONTEXT
  struct heif_context* ctx = heif_context_alloc();
  if (!ctx) {
    fprintf(stderr, "Could not create context object\n");
    return 1;
  }
  
  //READ FILE
  err = heif_context_read_from_file(ctx, input_filename.c_str(), nullptr);
  if (err.code != 0) {
    std::cerr << "Could not read HEIF/AVIF file: " << err.message << "\n";
    return 1;
  }

  //GET HANDLE
  err = heif_context_get_image_handle(ctx, image_id, &handle);
  checkError(err);

  //OPTIONS
  int has_alpha = heif_image_handle_has_alpha_channel(handle);
  struct heif_decoding_options* decode_options = heif_decoding_options_alloc();
  encoder->UpdateDecodingOptions(handle, decode_options);
  decode_options->strict_decoding = false;
  int bit_depth = heif_image_handle_get_luma_bits_per_pixel(handle);
  if (bit_depth < 0) {
    heif_decoding_options_free(decode_options);
    heif_image_handle_release(handle);
    std::cerr << "Input image has undefined bit-depth\n";
    return 1;
  }

  //DECODE
  err = heif_decode_image(handle, &image, encoder->colorspace(has_alpha), encoder->chroma(has_alpha, bit_depth), decode_options);
  checkError(err);
  decodeWarnings(image, err);

  //ENCODE
  bool written = encoder->Encode(handle, image, output_filename);
  if (!written) {
    fprintf(stderr, "could not write image\n");
    exit(1);
  }
  

  //FREE
  heif_image_release(image);
  heif_image_handle_release(handle);
  heif_decoding_options_free(decode_options);

  std::cout << "Written to " << output_filename << "\n";
  return 0;
}

int main(int argc, char** argv) {
    
  heif_to_jpg();
  return 0;
}
