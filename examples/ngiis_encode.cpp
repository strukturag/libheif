#include <iostream>
#include <libheif/heif_cxx.h>
#include <string>
using namespace std;

#include "encoder.h"
#include "libheif/heif_plugin.h"
#include "libheif/plugins/heif_encoder_uncompressed.h"
#include "libheif/plugins/heif_encoder_openjpeg.h"

//handle error
void he (struct heif_error error) {
  if (error.code) {
    printf("ERROR! - %s\n", error.message);
    exit(error.code);
  }
}



//PRIMARY FUNCTIONS

static void encode_j2k(string input_filename, string output_filename) {
  //GET CONTEXT
  heif_context* ctx = heif_context_alloc();
  he (heif_context_read_from_file(ctx, input_filename.c_str(), nullptr) );

  //GET HANDLE
  heif_image_handle* handle;
  he (heif_context_get_primary_image_handle(ctx, &handle) );

  //GET IMAGE
  heif_image* img;
  he (heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr) ); // decode the image and convert colorspace to RGB, saved as 24bit interleaved




  //REGISTER ENCODER
  const heif_encoder_plugin* plugin = get_encoder_plugin_openjpeg();
  heif_register_encoder_plugin(plugin);


  //ENCODE
  heif_encoder* encoder;
  heif_context* ctx2 = heif_context_alloc(); //You need a separate context
  he (heif_context_get_encoder_for_format(ctx2, heif_compression_JPEG2000, &encoder) );
  he (heif_context_encode_image(ctx2, img, encoder, nullptr, &handle) );

  //WRITE
  he (heif_context_write_to_file(ctx2, output_filename.c_str()) );
  printf("Created: %s\n", output_filename.c_str());
}


//MAIN
int main(int argc, char* argv[]) {
  cout << "***** ngiis_encode.cpp *****\n";

  // char* exe_path = argv[0];
  char* input_filename = argv[1];
  char* output_filename = argv[2];

  encode_j2k(input_filename, output_filename);



  cout << "***** End of ngiis_encode.cpp *****\n";
  return 0;
}

