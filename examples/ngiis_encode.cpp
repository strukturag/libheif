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


void debug_display_image(heif_image* img) {
  int width = heif_image_get_primary_width(img);
  int height = heif_image_get_primary_height(img);
  int stride;
  const uint8_t* r = heif_image_get_plane_readonly(img, heif_channel_R, &stride);

  uint32_t index;
  uint32_t skipper = 0;
  uint32_t ff_counter = 0;
  uint32_t f_counter = 0;
  uint32_t e_counter = 0;
  uint32_t gray_counter = 0;
  uint32_t wrong_counter = 0;
  uint32_t zero_counter = 0;
  bool found_0F = false;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < stride; x++) {
      
      index = (y * stride) + x;
      switch(r[index]) {
      case 0xFF:
        ff_counter++;
        break;
      case 0x0F:
        f_counter++;
        found_0F = true;
        break;
      case 0x0E:
        e_counter++;
        break;
      case 0x82:
        gray_counter++;
        break;
      case 0x00:
        zero_counter++;

        // break;
      default:
        wrong_counter++;
        printf("ERROR! - debug_display_image() - (%dx%d) is %x\n", x, y, r[index]);
        exit(1);
      }

      if (skipper++ == 1000) {
        skipper = 0;
        // printf("(%dx%d)=%2x  ", x, y, r[index]);

      }
    }
    // printf("\n");
  }
  printf("ffcounter: %d\n", ff_counter);
  printf("0fcounter: %d\n", f_counter);
  printf("0ecounter: %d\n", f_counter);
  printf("82counter: %d\n", gray_counter);
  printf("00counter: %d\n", zero_counter);
  printf("WRONG    : %d\n", wrong_counter);
  printf("found_0f : %d\n", found_0F);

}

void generate_image_planar(heif_image** out_image) {
  
  //Variables
  struct heif_image* image;
  int width = 256;
  int height = 256;
  int bit_depth = 8;
  int stride;

  //Create Image  
// Planar RGB images are specified as heif_colorspace_RGB / heif_chroma_444.
  heif_image_create(width, height, heif_colorspace_RGB, heif_chroma_444, &image);
  heif_image_add_plane(image, heif_channel_R, width, height, bit_depth);
  heif_image_add_plane(image, heif_channel_G, width, height, bit_depth);
  heif_image_add_plane(image, heif_channel_B, width, height, bit_depth);

  
  //Set Pixels
  int index;
  uint8_t* r = heif_image_get_plane(image, heif_channel_R, &stride);
  uint8_t* g = heif_image_get_plane(image, heif_channel_G, &stride);
  uint8_t* b = heif_image_get_plane(image, heif_channel_B, &stride);

  for (int row = 0; row < height; row++) {
    for (int col = 0; col < stride; col++) {
      index = (row * stride) + col;

      if (row < (height/2) && col < (stride/2)) {
        //Top Left
        r[index] = 0xFF;
        g[index] = 0X0C;
        b[index] = 0X07;
      } else if (row < (height/2) && col >= (stride/2)) {
        //Top Right
        r[index] = 0x0F;
        g[index] = 0XF0;
        b[index] = 0X06;
      } else if (col < (stride/2)) {
        //Bottom Left
        r[index] = 0x0E;
        g[index] = 0X0B;
        b[index] = 0XE0;        
      } else {
        //Bottom Right
        r[index] = 0x82;
        g[index] = 0X81;
        b[index] = 0X80;
      }
    }
  }
  *out_image = image;

}
void generate_image_interleaved(heif_image** out_image) {
  
  //Variables
  struct heif_image* image;
  int width = 256;
  int height = 256;
  const int CHANNELS = 3;
  int bit_depth = 8;
  int stride;

  //Create Image  
  heif_image_create(width, height, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &image);
  heif_image_add_plane(image, heif_channel_interleaved, width, height, bit_depth);
  
  //Set Pixels
  int index;
  uint8_t* data = heif_image_get_plane(image, heif_channel_interleaved, &stride);
  for (int row = 0; row < height; row++) {
    for (int col = 0; col < stride; col += CHANNELS) {
      index = (row * stride) + col;

      if (row < (height/2) && col < (stride/2)) {
        //Top Left
        data[index + 0] = 0xFF;
        data[index + 1] = 0X0C;
        data[index + 2] = 0X07;
      } else if (row < (height/2) && col >= (stride/2)) {
        //Top Right
        data[index + 0] = 0x0F;
        data[index + 1] = 0XF0;
        data[index + 2] = 0X06;
      } else if (col < (stride/2)) {
        data[index + 0] = 0x0E;
        data[index + 1] = 0X0B;
        data[index + 2] = 0XE0;        
      } else {
        data[index + 0] = 0x82;
        data[index + 1] = 0X81;
        data[index + 2] = 0X80;
      }
    }
  }
  *out_image = image;

}
void generate_grid_interleaved(heif_image*** out_images, uint16_t columns, uint16_t rows) {
  
  //Variables
  int width  = 128;
  int height = 128;
  const int CHANNELS = 3;
  int bit_depth = 8;
  int stride;


  uint32_t tile_count = columns * rows;
  struct heif_image** tiles = new heif_image*[tile_count];

  for (uint32_t i = 0; i < tile_count; i++) {
    heif_image_create(width, height, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &tiles[i]);
    heif_image_add_plane(tiles[i], heif_channel_interleaved, width, height, bit_depth);

  }

  
  // for (uint32_t i = 0; i < tile_count; i++) {
  //   int index;
  //   uint8_t* data = heif_image_get_plane(tiles[i], heif_channel_interleaved, &stride);
  //   for (int row = 0; row < height; row++) {
  //     for (int col = 0; col < stride; col += CHANNELS) {
  //       index = (row * stride) + col;
  //       data[index + 0] = i * 50;
  //       data[index + 1] = i * 50;
  //       data[index + 2] = i * 50;

  //     }
  //   }
  // }

  int index;
  //Create Tile #1
  uint8_t* data_tile_1 = heif_image_get_plane(tiles[0], heif_channel_interleaved, &stride);
  for (int row = 0; row < height; row++) {
    for (int col = 0; col < stride; col += CHANNELS) {
      index = (row * stride) + col;
      data_tile_1[index + 0] = 0xFF;
      data_tile_1[index + 1] = 0x0C;
      data_tile_1[index + 2] = 0x07;
    }
  }
  //Create Tile #2
  uint8_t* data_tile_2 = heif_image_get_plane(tiles[1], heif_channel_interleaved, &stride);
  for (int row = 0; row < height; row++) {
    for (int col = 0; col < stride; col += CHANNELS) {
      index = (row * stride) + col;
      data_tile_2[index + 0] = 0x0F;
      data_tile_2[index + 1] = 0xF0;
      data_tile_2[index + 2] = 0x06;
    }
  }
  //Create Tile #3
    uint8_t* data_tile_3 = heif_image_get_plane(tiles[2], heif_channel_interleaved, &stride);
    for (int row = 0; row < height; row++) {
      for (int col = 0; col < stride; col += CHANNELS) {
        index = (row * stride) + col;
        data_tile_3[index + 0] = 0x0E;
        data_tile_3[index + 1] = 0x0B;
        data_tile_3[index + 2] = 0xE0;
      }
    }
  //Create Tile #4
    uint8_t* data_tile_4 = heif_image_get_plane(tiles[3], heif_channel_interleaved, &stride);
    for (int row = 0; row < height; row++) {
      for (int col = 0; col < stride; col += CHANNELS) {
        index = (row * stride) + col;
        data_tile_4[index + 0] = 0x82;
        data_tile_4[index + 1] = 0x81;
        data_tile_4[index + 2] = 0x80;
      }
    }

    *out_images = tiles;



}
void generate_grid_planar(heif_image*** out_images, uint16_t columns, uint16_t rows) {
  //Variables
  int width  = 128;
  int height = 128;
  int bit_depth = 8;
  int stride;
  uint32_t tile_count = columns * rows;
  struct heif_image** tiles = new heif_image*[tile_count];


  //Allocate Space For Each Image
  for (uint32_t i = 0; i < tile_count; i++) {
    heif_image_create(width, height, heif_colorspace_RGB, heif_chroma_444, &tiles[i]);
    heif_image_add_plane(tiles[i], heif_channel_R, width, height, bit_depth);
    heif_image_add_plane(tiles[i], heif_channel_G, width, height, bit_depth);
    heif_image_add_plane(tiles[i], heif_channel_B, width, height, bit_depth);
  }


  int index; uint8_t *r, *g, *b;
  //Create Tile #1
  r = heif_image_get_plane(tiles[0], heif_channel_R, &stride);
  g = heif_image_get_plane(tiles[0], heif_channel_G, &stride);
  b = heif_image_get_plane(tiles[0], heif_channel_B, &stride);
  for (int row = 0; row < height; row++) {
    for (int col = 0; col < stride; col++) {
      index = (row * stride) + col;
      r[index] = 0xFF;
      g[index] = 0x0C;
      b[index] = 0x07;
    }
  }

  //Create Tile #2
  r = heif_image_get_plane(tiles[1], heif_channel_R, &stride);
  g = heif_image_get_plane(tiles[1], heif_channel_G, &stride);
  b = heif_image_get_plane(tiles[1], heif_channel_B, &stride);
  for (int row = 0; row < height; row++) {
    for (int col = 0; col < stride; col++) {
      index = (row * stride) + col;
      r[index] = 0x0F;
      g[index] = 0xF0;
      b[index] = 0x06;
    }
  }
  
  //Create Tile #3
  r = heif_image_get_plane(tiles[2], heif_channel_R, &stride);
  g = heif_image_get_plane(tiles[2], heif_channel_G, &stride);
  b = heif_image_get_plane(tiles[2], heif_channel_B, &stride);
    for (int row = 0; row < height; row++) {
      for (int col = 0; col < stride; col++) {
        index = (row * stride) + col;
        r[index] = 0x0E;
        g[index] = 0x0B;
        b[index] = 0xE0;
      }
    }
  
  //Create Tile #4
  r = heif_image_get_plane(tiles[3], heif_channel_R, &stride);
  g = heif_image_get_plane(tiles[3], heif_channel_G, &stride);
  b = heif_image_get_plane(tiles[3], heif_channel_B, &stride);
    for (int row = 0; row < height; row++) {
      for (int col = 0; col < stride; col++) {
        index = (row * stride) + col;
        r[index] = 0x82;
        g[index] = 0x81;
        b[index] = 0x80;
      }
    }

    *out_images = tiles;


}


void write_test_file_1(string output_filename, bool include_hevc) {
  //RGB, Planar, not Tiled

    //Variables
  heif_context* ctx = heif_context_alloc(); //You need a separate context
  heif_image_handle* handle;  
  heif_image* image;

  //Test Image
  generate_image_planar(&image); //chroma = 444

  //Register Encoder
  const heif_encoder_plugin* plugin = get_encoder_plugin_uncompressed();
  heif_register_encoder_plugin(plugin);
  
  //Encode HEVC
  heif_encoder* encoder;
  heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);
  if (include_hevc)
    heif_context_encode_image(ctx, image, encoder, nullptr, &handle);


  //Encoding Options
  struct heif_encoding_options options;
  heif_uncompressed_codec_options uncC;
  options.uncC = &uncC;
  uncC.num_tile_cols_minus_one = 0x00;
  uncC.num_tile_rows_minus_one = 0x00;

  //Encode Uncompressed
  heif_context_get_encoder_for_format(ctx, heif_compression_UNCOMPRESSED, &encoder);
  heif_context_encode_image(ctx, image, encoder, &options, &handle);

  //Write To File
  heif_context_write_to_file(ctx, output_filename.c_str());
  printf("Created: %s\n", output_filename.c_str());

}
void write_test_file_2(string output_filename, bool include_hevc) {
  //RGB, Planar, Tiled

    //Variables
  heif_context* ctx = heif_context_alloc(); //You need a separate context
  heif_image_handle* handle;  
  heif_image** tiles;
  heif_encoder* encoder;
  heif_image* hevc_image;

  //Test Image
  uint16_t columns = 2; uint16_t rows = 2;  
  generate_grid_planar(&tiles, columns, rows);

  //Register Encoder
  const heif_encoder_plugin* plugin = get_encoder_plugin_uncompressed();
  heif_register_encoder_plugin(plugin);
  

  //Encode HEVC
  generate_image_planar(&hevc_image);
  heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);
  if (include_hevc)
    heif_context_encode_image(ctx, hevc_image, encoder, nullptr, &handle);


  // UncompressedOptions
  struct heif_encoding_options options;
  heif_uncompressed_codec_options uncC;
  options.uncC = &uncC;
  uncC.encode_grid_into_single_image = true;
  uncC.num_tile_cols_minus_one = 1;
  uncC.num_tile_rows_minus_one = 1; 

  //Encode Uncompressed
  heif_context_get_encoder_for_format(ctx, heif_compression_UNCOMPRESSED, &encoder);
  heif_context_encode_grid_image(ctx, tiles, columns, rows, encoder, &options, &handle);
  

  //Write To File
  heif_context_write_to_file(ctx, output_filename.c_str());
  printf("Created: %s\n", output_filename.c_str());
}
void write_test_file_3(string output_filename, bool include_hevc) {
  //RGB, Interleaved, Tiled

  //Variables
  heif_context* ctx = heif_context_alloc(); //You need a separate context
  heif_image_handle* handle;  
  heif_image** images;

  //Test Image
  uint16_t columns = 2;
  uint16_t rows = 2;  
  generate_grid_interleaved(&images, columns, rows);

  //Register Encoder
  const heif_encoder_plugin* plugin = get_encoder_plugin_uncompressed();
  heif_register_encoder_plugin(plugin);
  
 

  heif_encoder* encoder;
  heif_image* hevc_image;
  generate_image_planar(&hevc_image);

  //Encode HEVC
  heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);
  if (include_hevc)
    heif_context_encode_image(ctx, hevc_image, encoder, nullptr, &handle);

  //Encode Uncompressed
  struct heif_encoding_options options;
  heif_uncompressed_codec_options uncC;
  options.uncC = &uncC;
  uncC.encode_grid_into_single_image = true;
  uncC.num_tile_cols_minus_one = 1;
  uncC.num_tile_rows_minus_one = 1; 
  heif_context_get_encoder_for_format(ctx, heif_compression_UNCOMPRESSED, &encoder);
  heif_context_encode_grid_image(ctx, images, columns, rows, encoder, &options, &handle);
  
  //Write To File
  heif_context_write_to_file(ctx, output_filename.c_str());
  printf("Created: %s\n", output_filename.c_str());

}
void write_test_file_4(string output_filename, bool include_hevc) {
  //RGB, Interleaved, Tiled

  //Variables
  heif_context* ctx = heif_context_alloc(); //You need a separate context
  heif_image_handle* handle;  
  heif_image* image;

  //Test Image
  generate_image_interleaved(&image);

  //Register Encoder
  const heif_encoder_plugin* plugin = get_encoder_plugin_uncompressed();
  heif_register_encoder_plugin(plugin);
  
  //Encode HEVC
  heif_encoder* encoder;
  heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);
  if (include_hevc) 
    heif_context_encode_image(ctx, image, encoder, nullptr, &handle);

  //Encode Uncompressed
  heif_context_get_encoder_for_format(ctx, heif_compression_UNCOMPRESSED, &encoder);
  heif_context_encode_image(ctx, image, encoder, nullptr, &handle);

  //Write To File
  heif_context_write_to_file(ctx, output_filename.c_str());
  printf("Created: %s\n", output_filename.c_str());

}


//PRIMARY FUNCTIONS
static void encode_uncompressed(string input_filename, string output_filename) {
  
  //GET CONTEXT
  heif_context* ctx = heif_context_alloc();
  heif_context_read_from_file(ctx, input_filename.c_str(), nullptr);

  //GET HANDLE
  heif_image_handle* handle;
  heif_context_get_primary_image_handle(ctx, &handle);

  //GET IMAGE
  heif_image* img;
  heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr); // decode the image and convert colorspace to RGB, saved as 24bit interleaved


  //REGISTER ENCODER
  const heif_encoder_plugin* plugin = get_encoder_plugin_uncompressed();
  heif_register_encoder_plugin(plugin);


  // ... 


  heif_context* ctx2 = heif_context_alloc(); //You need a separate context

  //ENCODE
  heif_encoder* encoder;
  heif_context_get_encoder_for_format(ctx2, heif_compression_UNCOMPRESSED, &encoder);
  heif_context_encode_image(ctx2, img, encoder, nullptr, &handle);

  //WRITE
  heif_context_write_to_file(ctx2, output_filename.c_str());
  printf("Created: %s\n", output_filename.c_str());

}

static void encode_av1f(string input_filename, string output_filename) {
  //GET CONTEXT
  heif_context* ctx = heif_context_alloc();
  he (heif_context_read_from_file(ctx, input_filename.c_str(), nullptr) );

  //GET HANDLE
  heif_image_handle* handle;
  he (heif_context_get_primary_image_handle(ctx, &handle) );

  //GET IMAGE
  heif_image* img;
  he (heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr) ); // decode the image and convert colorspace to RGB, saved as 24bit interleaved



  //ENCODE
  heif_encoder* encoder;
  heif_context* ctx2 = heif_context_alloc(); //You need a separate context
  heif_compression_HEVC;
  heif_compression_AVC;
  heif_compression_JPEG;
  heif_compression_AV1;
  heif_compression_format codec = heif_compression_AV1;
  he (heif_context_get_encoder_for_format(ctx2, codec, &encoder) );

  he (heif_context_encode_image(ctx2, img, encoder, nullptr, &handle) );

  //WRITE
  he (heif_context_write_to_file(ctx2, output_filename.c_str()) );
  printf("Created: %s\n", output_filename.c_str());
}

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

static void generate_uncompressed_test_files() {
  bool include_hevc = false;

  write_test_file_1("out/uncC_1_rgb_planar_monolithic.heif", include_hevc);
  write_test_file_2("out/uncC_2_rgb_planar_tiled.heif", include_hevc);
  write_test_file_3("out/uncC_3_rgb_interleaved_tiled.heif", include_hevc);
  write_test_file_4("out/uncC_4_rgb_interleaved_monolithic.heif", include_hevc);
}

static void generate_pangea(string input_filename, string output_filename) {

  //GET CONTEXT
  heif_context* ctx = heif_context_alloc();
  he (heif_context_read_from_file(ctx, input_filename.c_str(), nullptr) );

  //GET HANDLE
  heif_image_handle* handle;
  he (heif_context_get_primary_image_handle(ctx, &handle) );

  //GET IMAGE
  heif_image* img;
  he (heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr) ); // decode the image and convert colorspace to RGB, saved as 24bit interleaved


  //REGISTER ENCODERS
  const heif_encoder_plugin* plugin;
  plugin = get_encoder_plugin_uncompressed();
  heif_register_encoder_plugin(plugin);
  plugin = get_encoder_plugin_openjpeg();
  heif_register_encoder_plugin(plugin);

  //GET ENCODERS
  heif_context* ctx2 = heif_context_alloc(); //You need a separate context
  heif_encoder* hevc_encoder, *av1_encoder, *j2k_encoder, *uncompressed_encoder;
  he (heif_context_get_encoder_for_format(ctx2, heif_compression_HEVC, &hevc_encoder) );
  he (heif_context_get_encoder_for_format(ctx2, heif_compression_AV1, &av1_encoder) );
  he (heif_context_get_encoder_for_format(ctx2, heif_compression_UNCOMPRESSED, &uncompressed_encoder) );
  he (heif_context_get_encoder_for_format(ctx2, heif_compression_J2K, &j2k_encoder) );

  //ENCODE
  he (heif_context_encode_image(ctx2, img, hevc_encoder, nullptr, &handle) );
  he (heif_context_encode_image(ctx2, img, av1_encoder,  nullptr, &handle) );
  he (heif_context_encode_image(ctx2, img, j2k_encoder,  nullptr, &handle) );
  he (heif_context_encode_image(ctx2, img, uncompressed_encoder,  nullptr, &handle) );


  //WRITE
  he (heif_context_write_to_file(ctx2, output_filename.c_str()) );
  printf("Created: %s\n", output_filename.c_str());

}

//MAIN
int main(int argc, char* argv[]) {
  cout << "***** ngiis_encode.cpp *****\n";

  char* exe_path = argv[0];
  char* input_filename = argv[1];
  char* output_filename = argv[2];
  int option = atoi(argv[3]);

  switch(option) {
    case 1:
      encode_uncompressed(input_filename, output_filename);
    break;
    case 2:
      encode_av1f(input_filename, output_filename);
    break;
    case 3:
      encode_j2k(input_filename, output_filename);
    break;
    case 4:
      generate_uncompressed_test_files();
    break;
    case 5:
      generate_pangea(input_filename, output_filename);
    break;
  }


  cout << "***** End of ngiis_encode.cpp *****\n";
  return 0;
}

