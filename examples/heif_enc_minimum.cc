

#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <algorithm>
#include <vector>
#include <string>
#include <libheif/heif.h>
#define USED(x) (void)x  //An unused variable will throw an error

//PRIMARY FUNCTIONS
void heif_to_heif(std::string input_filename, std::string output_filename);


int main(int argc, char** argv) {

  char input_filename[] = "/mnt/c/repos/libheif-rlunatrax/build/examples/input/Devon.heic";
  char output_filename[] = "out/output5.heif";

  //EXAMPLES  
  heif_to_heif(input_filename, output_filename);


  //LIBHEIF
  // heif_add_box_example(input_filename);
  
  return 0;
}


//HELPERS
static heif_image* decode_heif_image(std::string input_filename) {
  //READ VARIABLES
  heif_context* ctx = heif_context_alloc();
  heif_image_handle* handle;
  heif_image* img;

  heif_context_read_from_file(ctx, input_filename.c_str(), nullptr);
  heif_context_get_primary_image_handle(ctx, &handle);
  heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr); // decode the image and convert colorspace to RGB, saved as 24bit interleaved

  // int stride;
  // const uint8_t* data;
  // data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
  // USED(data); 

  return img;
}
static heif_context* simple_encode_heif_image(heif_image* img) {
  heif_context* ctx = heif_context_alloc();
  heif_encoder* encoder;
  heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);
  heif_encoder_set_lossy_quality(encoder, 50);
  heif_context_encode_image(ctx, img, encoder, nullptr, nullptr);
  heif_encoder_release(encoder);
  return ctx;
}


//FUNCTIONS
void heif_to_heif(std::string input_filename, std::string output_filename) {

  heif_image* img = decode_heif_image(input_filename);
  heif_context* ctx = simple_encode_heif_image(img);
  heif_context_write_to_file(ctx, output_filename.c_str());

}