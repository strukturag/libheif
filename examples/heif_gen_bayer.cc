/*
  libheif example application "heif-gen-bayer".

  MIT License

  Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>

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

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <libheif/heif.h>
#include <libheif/heif_uncompressed.h>

#include "heifio/decoder_png.h"


struct PatternDefinition
{
  const char* name;
  uint16_t width;
  uint16_t height;
  std::vector<heif_bayer_pattern_pixel> cpat;
};


static const PatternDefinition patterns[] = {
  // RGGB (standard Bayer)
  //   R G
  //   G B
  {
    "rggb", 2, 2,
    {
      {heif_uncompressed_component_type_red,   1.0f},
      {heif_uncompressed_component_type_green, 1.0f},
      {heif_uncompressed_component_type_green, 1.0f},
      {heif_uncompressed_component_type_blue,  1.0f},
    }
  },

  // GBRG
  //   G B
  //   R G
  {
    "gbrg", 2, 2,
    {
      {heif_uncompressed_component_type_green, 1.0f},
      {heif_uncompressed_component_type_blue,  1.0f},
      {heif_uncompressed_component_type_red,   1.0f},
      {heif_uncompressed_component_type_green, 1.0f},
    }
  },

  // RGBW (Red-Green-Blue-White) — 4×4
  //   W G W R
  //   G W B W
  //   W B W G
  //   R W G W
  // White is an unfiltered (panchromatic) pixel → Y component type.
  {
    "rgbw", 4, 4,
    {
      {heif_uncompressed_component_type_Y,     1.0f},
      {heif_uncompressed_component_type_green, 1.0f},
      {heif_uncompressed_component_type_Y,     1.0f},
      {heif_uncompressed_component_type_red,   1.0f},

      {heif_uncompressed_component_type_green, 1.0f},
      {heif_uncompressed_component_type_Y,     1.0f},
      {heif_uncompressed_component_type_blue,  1.0f},
      {heif_uncompressed_component_type_Y,     1.0f},

      {heif_uncompressed_component_type_Y,     1.0f},
      {heif_uncompressed_component_type_blue,  1.0f},
      {heif_uncompressed_component_type_Y,     1.0f},
      {heif_uncompressed_component_type_green, 1.0f},

      {heif_uncompressed_component_type_red,   1.0f},
      {heif_uncompressed_component_type_Y,     1.0f},
      {heif_uncompressed_component_type_green, 1.0f},
      {heif_uncompressed_component_type_Y,     1.0f},
    }
  },

  // QBC (Quad Bayer Coding) — 4×4
  //   G G R R
  //   G G R R
  //   B B G G
  //   B B G G
  {
    "qbc", 4, 4,
    {
      {heif_uncompressed_component_type_green, 1.0f},
      {heif_uncompressed_component_type_green, 1.0f},
      {heif_uncompressed_component_type_red,   1.0f},
      {heif_uncompressed_component_type_red,   1.0f},

      {heif_uncompressed_component_type_green, 1.0f},
      {heif_uncompressed_component_type_green, 1.0f},
      {heif_uncompressed_component_type_red,   1.0f},
      {heif_uncompressed_component_type_red,   1.0f},

      {heif_uncompressed_component_type_blue,  1.0f},
      {heif_uncompressed_component_type_blue,  1.0f},
      {heif_uncompressed_component_type_green, 1.0f},
      {heif_uncompressed_component_type_green, 1.0f},

      {heif_uncompressed_component_type_blue,  1.0f},
      {heif_uncompressed_component_type_blue,  1.0f},
      {heif_uncompressed_component_type_green, 1.0f},
      {heif_uncompressed_component_type_green, 1.0f},
    }
  },
};

static constexpr int num_patterns = sizeof(patterns) / sizeof(patterns[0]);


static const PatternDefinition* find_pattern(const char* name)
{
  for (int i = 0; i < num_patterns; i++) {
    if (strcasecmp(patterns[i].name, name) == 0) {
      return &patterns[i];
    }
  }
  return nullptr;
}


static void print_usage()
{
  std::cerr << "Usage: heif-gen-bayer [options] <input.png> <output.heif>\n\n"
            << "Options:\n"
            << "  -h, --help              show this help\n"
            << "  -p, --pattern <name>    filter array pattern (default: rggb)\n\n"
            << "Patterns:\n";
  for (int i = 0; i < num_patterns; i++) {
    std::cerr << "  " << patterns[i].name
              << " (" << patterns[i].width << "x" << patterns[i].height << ")"
              << (i == 0 ? "  [default]" : "")
              << "\n";
  }
}


static struct option long_options[] = {
    {(char* const) "help",    no_argument,       nullptr, 'h'},
    {(char* const) "pattern", required_argument, nullptr, 'p'},
    {nullptr, 0, nullptr, 0}
};


int main(int argc, char* argv[])
{
  const PatternDefinition* pat = &patterns[0]; // default: RGGB

  while (true) {
    int option_index = 0;
    int c = getopt_long(argc, argv, "hp:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
      case 'h':
        print_usage();
        return 0;

      case 'p':
        pat = find_pattern(optarg);
        if (!pat) {
          std::cerr << "Unknown pattern: " << optarg << "\n";
          print_usage();
          return 1;
        }
        break;

      default:
        print_usage();
        return 1;
    }
  }

  if (argc - optind != 2) {
    print_usage();
    return 1;
  }

  const char* input_filename = argv[optind];
  const char* output_filename = argv[optind + 1];

  // --- Load PNG

  InputImage input_image;
  heif_error err = loadPNG(input_filename, 8, &input_image);
  if (err.code != heif_error_Ok) {
    std::cerr << "Cannot load PNG: " << err.message << "\n";
    return 1;
  }

  heif_image* src_img = input_image.image.get();

  int width = heif_image_get_primary_width(src_img);
  int height = heif_image_get_primary_height(src_img);

  int bpp = heif_image_get_bits_per_pixel_range(src_img, heif_channel_interleaved);
  if (bpp != 8) {
    std::cerr << "Only 8-bit PNG input is supported. Got " << bpp << " bits per pixel.\n";
    return 1;
  }

  if (width % pat->width != 0 || height % pat->height != 0) {
    std::cerr << "Image dimensions must be multiples of the pattern size ("
              << pat->width << "x" << pat->height << "). Got "
              << width << "x" << height << "\n";
    return 1;
  }

  // --- Get source RGB data

  int src_stride;
  const uint8_t* src_data = heif_image_get_plane_readonly(src_img, heif_channel_interleaved, &src_stride);
  if (!src_data) {
    std::cerr << "Failed to get interleaved RGB plane from PNG.\n";
    return 1;
  }

  // --- Create Bayer image

  heif_image* bayer_img = nullptr;
  err = heif_image_create(width, height,
                          heif_colorspace_filter_array,
                          heif_chroma_monochrome,
                          &bayer_img);
  if (err.code != heif_error_Ok) {
    std::cerr << "Cannot create image: " << err.message << "\n";
    return 1;
  }

  err = heif_image_add_plane(bayer_img, heif_channel_filter_array, width, height, 8);
  if (err.code != heif_error_Ok) {
    std::cerr << "Cannot add plane: " << err.message << "\n";
    heif_image_release(bayer_img);
    return 1;
  }

  int dst_stride;
  uint8_t* dst_data = heif_image_get_plane(bayer_img, heif_channel_filter_array, &dst_stride);

  // --- Convert RGB to filter array using the selected pattern

  for (int y = 0; y < height; y++) {
    const uint8_t* src_row = src_data + y * src_stride;
    uint8_t* dst_row = dst_data + y * dst_stride;

    for (int x = 0; x < width; x++) {
      uint8_t r = src_row[x * 3 + 0];
      uint8_t g = src_row[x * 3 + 1];
      uint8_t b = src_row[x * 3 + 2];

      int px = x % pat->width;
      int py = y % pat->height;
      uint16_t comp_type = pat->cpat[py * pat->width + px].component_type;

      switch (comp_type) {
        case heif_uncompressed_component_type_red:   dst_row[x] = r; break;
        case heif_uncompressed_component_type_green: dst_row[x] = g; break;
        case heif_uncompressed_component_type_blue:  dst_row[x] = b; break;
        case heif_uncompressed_component_type_Y: dst_row[x] = static_cast<uint8_t>((r + g + b) / 3); break; // Y / white
        default:
          assert(false);
      }
    }
  }

  // --- Set Bayer pattern metadata

  err = heif_image_set_bayer_pattern(bayer_img,
                                     pat->width, pat->height,
                                     pat->cpat.data());
  if (err.code != heif_error_Ok) {
    std::cerr << "Cannot set Bayer pattern: " << err.message << "\n";
    heif_image_release(bayer_img);
    return 1;
  }

  // --- Encode

  heif_context* ctx = heif_context_alloc();

  heif_encoder* encoder = nullptr;
  err = heif_context_get_encoder_for_format(ctx, heif_compression_uncompressed, &encoder);
  if (err.code != heif_error_Ok) {
    std::cerr << "Cannot get uncompressed encoder: " << err.message << "\n";
    heif_image_release(bayer_img);
    heif_context_free(ctx);
    return 1;
  }

  heif_encoding_options* options = heif_encoding_options_alloc();

  heif_image_handle* handle = nullptr;
  err = heif_context_encode_image(ctx, bayer_img, encoder, options, &handle);
  if (err.code != heif_error_Ok) {
    std::cerr << "Cannot encode image: " << err.message << "\n";
    heif_encoding_options_free(options);
    heif_encoder_release(encoder);
    heif_image_release(bayer_img);
    heif_context_free(ctx);
    return 1;
  }

  heif_encoding_options_free(options);
  heif_encoder_release(encoder);
  heif_image_handle_release(handle);
  heif_image_release(bayer_img);

  // --- Write file

  err = heif_context_write_to_file(ctx, output_filename);
  if (err.code != heif_error_Ok) {
    std::cerr << "Cannot write file: " << err.message << "\n";
    heif_context_free(ctx);
    return 1;
  }

  heif_context_free(ctx);

  std::cout << "Wrote " << pat->name << " ("
            << pat->width << "x" << pat->height
            << ") Bayer image to " << output_filename << "\n";

  return 0;
}
