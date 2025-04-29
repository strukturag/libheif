/*
  libheif example application "heif".

  MIT License

  Copyright (c) 2024 Dirk Farin <dirk.farin@gmail.com>

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
#include <cerrno>
#include <cstring>
#include <getopt.h>

#include <fstream>
#include <iostream>
#include <iomanip>
#include <memory>
#include <algorithm>
#include <vector>
#include <string>
#include <sstream>
#include <filesystem>
#include <regex>
#include <optional>

#include <libheif/heif.h>
#include <libheif/heif_properties.h>
#include "libheif/heif_items.h"
#include <libheif/heif_tai_timestamps.h>

#include "heifio/decoder_jpeg.h"
#include "heifio/decoder_png.h"
#include "heifio/decoder_tiff.h"
#include "heifio/decoder_y4m.h"

#include "benchmark.h"
#include "common.h"
#include "libheif/heif_experimental.h"
#include "libheif/heif_sequences.h"

int master_alpha = 1;
int thumb_alpha = 1;
int list_encoders = 0;
int two_colr_boxes = 0;
int premultiplied_alpha = 0;
int run_benchmark = 0;
int metadata_compression = 0;
int tiled_input_x_y = 0;
const char* encoderId = nullptr;
std::string chroma_downsampling;
int tiled_image_width = 0;
int tiled_image_height = 0;
std::string tiling_method = "grid";
heif_unci_compression unci_compression = heif_unci_compression_brotli;
int add_pyramid_group = 0;

uint16_t nclx_colour_primaries = 1;
uint16_t nclx_transfer_characteristic = 13;
uint16_t nclx_matrix_coefficients = 6;
int nclx_full_range = true;

std::string property_pitm_description;

// for benchmarking

#if !defined(_MSC_VER)
#define HAVE_GETTIMEOFDAY 1  // TODO: should be set by CMake
#endif

#if HAVE_GETTIMEOFDAY
#include <sys/time.h>
struct timeval time_encoding_start;
struct timeval time_encoding_end;
#endif

const int OPTION_NCLX_MATRIX_COEFFICIENTS = 1000;
const int OPTION_NCLX_COLOUR_PRIMARIES = 1001;
const int OPTION_NCLX_TRANSFER_CHARACTERISTIC = 1002;
const int OPTION_NCLX_FULL_RANGE_FLAG = 1003;
const int OPTION_PLUGIN_DIRECTORY = 1004;
const int OPTION_PITM_DESCRIPTION = 1005;
const int OPTION_USE_JPEG_COMPRESSION = 1006;
const int OPTION_USE_JPEG2000_COMPRESSION = 1007;
const int OPTION_VERBOSE = 1008;
const int OPTION_USE_HTJ2K_COMPRESSION = 1009;
const int OPTION_USE_VVC_COMPRESSION = 1010;
const int OPTION_TILED_IMAGE_WIDTH = 1011;
const int OPTION_TILED_IMAGE_HEIGHT = 1012;
const int OPTION_TILING_METHOD = 1013;
const int OPTION_UNCI_COMPRESSION = 1014;


static struct option long_options[] = {
    {(char* const) "help",                    no_argument,       0,              'h'},
    {(char* const) "version",                 no_argument,       0,              'v'},
    {(char* const) "quality",                 required_argument, 0,              'q'},
    {(char* const) "output",                  required_argument, 0,              'o'},
    {(char* const) "lossless",                no_argument,       0,              'L'},
    {(char* const) "thumb",                   required_argument, 0,              't'},
    {(char* const) "verbose",                 no_argument,       0,              OPTION_VERBOSE},
    {(char* const) "params",                  no_argument,       0,              'P'},
    {(char* const) "no-alpha",                no_argument,       &master_alpha,  0},
    {(char* const) "no-thumb-alpha",          no_argument,       &thumb_alpha,   0},
    {(char* const) "list-encoders",           no_argument,       &list_encoders, 1},
    {(char* const) "encoder",                 required_argument, 0,              'e'},
    {(char* const) "bit-depth",               required_argument, 0,              'b'},
    {(char* const) "even-size",               no_argument,       0,              'E'},
    {(char* const) "avif",                    no_argument,       0,              'A'},
    {(char* const) "vvc",                     no_argument,       0,              OPTION_USE_VVC_COMPRESSION},
    {(char* const) "jpeg",                    no_argument,       0,              OPTION_USE_JPEG_COMPRESSION},
    {(char* const) "jpeg2000",                no_argument,       0,              OPTION_USE_JPEG2000_COMPRESSION},
    {(char* const) "htj2k",                   no_argument,       0,              OPTION_USE_HTJ2K_COMPRESSION},
#if WITH_UNCOMPRESSED_CODEC
    {(char* const) "uncompressed",                no_argument,       0,                     'U'},
    {(char* const) "unci-compression-method",     required_argument, nullptr, OPTION_UNCI_COMPRESSION},
#endif
    {(char* const) "matrix_coefficients",         required_argument, 0,                     OPTION_NCLX_MATRIX_COEFFICIENTS},
    {(char* const) "colour_primaries",            required_argument, 0,                     OPTION_NCLX_COLOUR_PRIMARIES},
    {(char* const) "transfer_characteristic",     required_argument, 0,                     OPTION_NCLX_TRANSFER_CHARACTERISTIC},
    {(char* const) "full_range_flag",             required_argument, 0,                     OPTION_NCLX_FULL_RANGE_FLAG},
    {(char* const) "enable-two-colr-boxes",       no_argument,       &two_colr_boxes,       1},
    {(char* const) "premultiplied-alpha",         no_argument,       &premultiplied_alpha,  1},
    {(char* const) "plugin-directory",            required_argument, 0,                     OPTION_PLUGIN_DIRECTORY},
    {(char* const) "benchmark",                   no_argument,       &run_benchmark,        1},
    {(char* const) "enable-metadata-compression", no_argument,       &metadata_compression, 1},
    {(char* const) "pitm-description",            required_argument, 0,                     OPTION_PITM_DESCRIPTION},
    {(char* const) "chroma-downsampling",         required_argument, 0, 'C'},
    {(char* const) "tiled-input",                 no_argument, 0, 'T'},
    {(char* const) "tiled-image-width",           required_argument, nullptr, OPTION_TILED_IMAGE_WIDTH},
    {(char* const) "tiled-image-height",          required_argument, nullptr, OPTION_TILED_IMAGE_HEIGHT},
    {(char* const) "tiled-input-x-y",             no_argument,       &tiled_input_x_y, 1},
    {(char* const) "tiling-method",               required_argument, nullptr, OPTION_TILING_METHOD},
    {(char* const) "add-pyramid-group",           no_argument,       &add_pyramid_group, 1},
    {0, 0,                                                           0,  0},
};


void show_help(const char* argv0)
{
  std::cerr << " heif-enc  libheif version: " << heif_get_version() << "\n"
            << "----------------------------------------\n"
            << "Usage: heif-enc [options] image.jpeg ...\n"
            << "\n"
            << "When specifying multiple source images, they will all be saved into the same HEIF/AVIF file.\n"
            << "\n"
            << "When using the x265 encoder, you may pass it any of its parameters by\n"
            << "prefixing the parameter name with 'x265:'. Hence, to set the 'ctu' parameter,\n"
            << "you will have to set 'x265:ctu' in libheif (e.g.: -p x265:ctu=64).\n"
            << "Note that there is no checking for valid parameters when using the prefix.\n"
            << "\n"
            << "Options:\n"
            << "  -h, --help        show help\n"
            << "  -v, --version     show version\n"
            << "  -q, --quality     set output quality (0-100) for lossy compression\n"
            << "  -L, --lossless    generate lossless output (-q has no effect). Image will be encoded as RGB (matrix_coefficients=0).\n"
            << "  -t, --thumb #     generate thumbnail with maximum size # (default: off)\n"
            << "      --no-alpha    do not save alpha channel\n"
            << "      --no-thumb-alpha  do not save alpha channel in thumbnail image\n"
            << "  -o, --output          output filename (optional)\n"
            << "      --verbose         enable logging output (more will increase logging level)\n"
            << "  -P, --params          show all encoder parameters and exit, input file not required or used.\n"
            << "  -b, --bit-depth #     bit-depth of generated HEIF/AVIF file when using 16-bit PNG input (default: 10 bit)\n"
            << "  -p                    set encoder parameter (NAME=VALUE)\n"
            << "  -A, --avif            encode as AVIF (not needed if output filename with .avif suffix is provided)\n"
            << "      --vvc             encode as VVC (experimental)\n"
            << "      --jpeg            encode as JPEG\n"
            << "      --jpeg2000        encode as JPEG 2000 (experimental)\n"
            << "      --htj2k           encode as High Throughput JPEG 2000 (experimental)\n"
#if WITH_UNCOMPRESSED_CODEC
            << "  -U, --uncompressed             encode as uncompressed image (according to ISO 23001-17) (EXPERIMENTAL)\n"
            << "      --unci-compression METHOD  choose one of these methods: none, deflate, zlib, brotli.\n"
#endif
            << "      --list-encoders         list all available encoders for all compression formats\n"
            << "  -e, --encoder ID            select encoder to use (the IDs can be listed with --list-encoders)\n"
            << "      --plugin-directory DIR  load all codec plugins in the directory\n"
            << "  --matrix_coefficients     nclx profile: color conversion matrix coefficients, default=6 (see h.273)\n"
            << "  --colour_primaries        nclx profile: color primaries (see h.273)\n"
            << "  --transfer_characteristic nclx profile: transfer characteristics (see h.273)\n"
            << "  --full_range_flag         nclx profile: full range flag, default: 1\n"
            << "  --enable-two-colr-boxes   will write both an ICC and an nclx color profile if both are present\n"
            << "  --premultiplied-alpha     input image has premultiplied alpha\n"
#if WITH_HEADER_COMPRESSION
            << "  --enable-metadata-compression   enable XMP metadata compression (experimental)\n"
#endif
            << "  -C,--chroma-downsampling ALGO   force chroma downsampling algorithm (nn = nearest-neighbor / average / sharp-yuv)\n"
            << "                                  (sharp-yuv makes edges look sharper when using YUV420 with bilinear chroma upsampling)\n"
            << "  --benchmark               measure encoding time, PSNR, and output file size\n"
            << "  --pitm-description TEXT   (experimental) set user description for primary image\n"
            << "  -T,--tiled-input          input is a set of tile images (only provide one filename with two tile position numbers).\n"
            << "                            For example, 'tile-01-05.jpg' would be a valid input filename.\n"
            << "                            You only have to provide the filename of one tile as input, heif-enc will scan the directory\n"
            << "                            for the other tiles and determine the range of tiles automatically.\n"
            << "  --tiled-image-width #     override image width of tiled image\n"
            << "  --tiled-image-height #    override image height of tiled image\n"
            << "  --tiled-input-x-y         usually, the first number in the input tile filename should be the y position.\n"
            << "                            With this option, this can be swapped so that the first number is x, the second number y.\n"
#if HEIF_ENABLE_EXPERIMENTAL_FEATURES
            << "  --tiling-method METHOD    choose one of these methods: grid, tili, unci. The default is 'grid'.\n"
            << "  --add-pyramid-group       when several images are given, put them into a multi-resolution pyramid group.\n"
#endif
            ;
}


void list_encoder_parameters(heif_encoder* encoder)
{
  std::cerr << "Parameters for encoder `" << heif_encoder_get_name(encoder) << "`:\n";

  const struct heif_encoder_parameter* const* params = heif_encoder_list_parameters(encoder);
  for (int i = 0; params[i]; i++) {
    const char* name = heif_encoder_parameter_get_name(params[i]);

    switch (heif_encoder_parameter_get_type(params[i])) {
      case heif_encoder_parameter_type_integer: {
        heif_error error;

        std::cerr << "  " << name;

        if (heif_encoder_has_default(encoder, name)) {
          int value;
          error = heif_encoder_get_parameter_integer(encoder, name, &value);
          (void) error;

          std::cerr << ", default=" << value;
        }

        int have_minimum, have_maximum, minimum, maximum, num_valid_values;
        const int* valid_values = nullptr;
        error = heif_encoder_parameter_integer_valid_values(encoder, name,
                                                            &have_minimum, &have_maximum,
                                                            &minimum, &maximum,
                                                            &num_valid_values,
                                                            &valid_values);

        if (have_minimum || have_maximum) {  // TODO: only one is set
          std::cerr << ", [" << minimum << ";" << maximum << "]";
        }

        if (num_valid_values > 0) {
          std::cerr << ", {";

          for (int p = 0; p < num_valid_values; p++) {
            if (p > 0) {
              std::cerr << ", ";
            }

            std::cerr << valid_values[p];
          }

          std::cerr << "}";
        }

        std::cerr << "\n";
      }
        break;

      case heif_encoder_parameter_type_boolean: {
        heif_error error;
        std::cerr << "  " << name;

        if (heif_encoder_has_default(encoder, name)) {
          int value;
          error = heif_encoder_get_parameter_boolean(encoder, name, &value);
          (void) error;

          std::cerr << ", default=" << (value ? "true" : "false");
        }

        std::cerr << "\n";
      }
        break;

      case heif_encoder_parameter_type_string: {
        heif_error error;
        std::cerr << "  " << name;

        if (heif_encoder_has_default(encoder, name)) {
          const int value_size = 50;
          char value[value_size];
          error = heif_encoder_get_parameter_string(encoder, name, value, value_size);
          (void) error;

          std::cerr << ", default=" << value;
        }

        const char* const* valid_options;
        error = heif_encoder_parameter_string_valid_values(encoder, name, &valid_options);

        if (valid_options) {
          std::cerr << ", { ";
          for (int k = 0; valid_options[k]; k++) {
            if (k > 0) { std::cerr << ","; }
            std::cerr << valid_options[k];
          }
          std::cerr << " }";
        }

        std::cerr << "\n";
      }
        break;
    }
  }
}


void set_params(struct heif_encoder* encoder, const std::vector<std::string>& params)
{
  for (const std::string& p : params) {
    auto pos = p.find_first_of('=');
    if (pos == std::string::npos || pos == 0 || pos == p.size() - 1) {
      std::cerr << "Encoder parameter must be in the format 'name=value'\n";
      exit(5);
    }

    std::string name = p.substr(0, pos);
    std::string value = p.substr(pos + 1);

    struct heif_error error = heif_encoder_set_parameter(encoder, name.c_str(), value.c_str());
    if (error.code) {
      std::cerr << "Error: " << error.message << "\n";
      exit(5);
    }
  }
}


static void show_list_of_encoders(const heif_encoder_descriptor* const* encoder_descriptors,
                                  int count)
{
  for (int i = 0; i < count; i++) {
    std::cout << "- " << heif_encoder_descriptor_get_id_name(encoder_descriptors[i])
              << " = "
              << heif_encoder_descriptor_get_name(encoder_descriptors[i]);

    if (i == 0) {
      std::cout << " [default]";
    }

    std::cout << "\n";
  }
}


static const char* get_compression_format_name(heif_compression_format format)
{
  switch (format) {
    case heif_compression_AV1:
      return "AV1";
      break;
    case heif_compression_AVC:
      return "AVC";
      break;
    case heif_compression_VVC:
      return "VVC";
      break;
    case heif_compression_HEVC:
      return "HEVC";
      break;
    case heif_compression_JPEG:
      return "JPEG";
      break;
    case heif_compression_JPEG2000:
      return "JPEG 2000";
      break;
    case heif_compression_HTJ2K:
      return "HT-J2K";
      break;
    case heif_compression_uncompressed:
      return "Uncompressed";
      break;
    default:
      assert(false);
      return "unknown";
  }
}

static void show_list_of_all_encoders()
{
  for (auto compression_format: {heif_compression_AVC, heif_compression_AV1, heif_compression_HEVC,
                                 heif_compression_JPEG, heif_compression_JPEG2000, heif_compression_HTJ2K,
                                 heif_compression_uncompressed, heif_compression_VVC
  }) {

    switch (compression_format) {
      case heif_compression_AVC:
        std::cout << "AVC";
        break;
      case heif_compression_AV1:
        std::cout << "AVIF";
        break;
      case heif_compression_HEVC:
        std::cout << "HEIC";
        break;
      case heif_compression_JPEG:
        std::cout << "JPEG";
        break;
      case heif_compression_JPEG2000:
        std::cout << "JPEG 2000";
        break;
      case heif_compression_HTJ2K:
        std::cout << "JPEG 2000 (HT)";
        break;
      case heif_compression_uncompressed:
        std::cout << "Uncompressed";
        break;
      case heif_compression_VVC:
        std::cout << "VVIC";
        break;
      default:
        assert(false);
    }

    std::cout << " encoders:\n";

#define MAX_ENCODERS 10
    const heif_encoder_descriptor* encoder_descriptors[MAX_ENCODERS];
    int count = heif_get_encoder_descriptors(compression_format,
                                             nullptr,
                                             encoder_descriptors, MAX_ENCODERS);
#undef MAX_ENCODERS

    show_list_of_encoders(encoder_descriptors, count);
  }
}


bool ends_with(const std::string& str, const std::string& end)
{
  if (str.length() < end.length()) {
    return false;
  }
  else {
    return str.compare(str.length() - end.length(), end.length(), end) == 0;
  }
}


heif_compression_format guess_compression_format_from_filename(const std::string& filename)
{
  std::string filename_lowercase = filename;
  std::transform(filename_lowercase.begin(), filename_lowercase.end(), filename_lowercase.begin(), ::tolower);

  if (ends_with(filename_lowercase, ".avif")) {
    return heif_compression_AV1;
  }
  else if (ends_with(filename_lowercase, ".vvic")) {
    return heif_compression_VVC;
  }
  else if (ends_with(filename_lowercase, ".heic")) {
    return heif_compression_HEVC;
  }
  else if (ends_with(filename_lowercase, ".hej2")) {
    return heif_compression_JPEG2000;
  }
  else {
    return heif_compression_undefined;
  }
}


std::string suffix_for_compression_format(heif_compression_format format)
{
  switch (format) {
    case heif_compression_AV1: return "avif";
    case heif_compression_VVC: return "vvic";
    case heif_compression_HEVC: return "heic";
    case heif_compression_JPEG2000: return "hej2";
    default: return "data";
  }
}


InputImage load_image(const std::string& input_filename, int output_bit_depth)
{
  InputImage input_image;

  // get file type from file name

  std::string suffix;
  auto suffix_pos = input_filename.find_last_of('.');
  if (suffix_pos != std::string::npos) {
    suffix = input_filename.substr(suffix_pos + 1);
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
  }

  enum
  {
    PNG, JPEG, Y4M, TIFF
  } filetype = JPEG;
  if (suffix == "png") {
    filetype = PNG;
  }
  else if (suffix == "y4m") {
    filetype = Y4M;
  }
  else if (suffix == "tif" || suffix == "tiff") {
    filetype = TIFF;
  }

  if (filetype == PNG) {
    heif_error err = loadPNG(input_filename.c_str(), output_bit_depth, &input_image);
    if (err.code != heif_error_Ok) {
      std::cerr << "Can not load TIFF input_image: " << err.message << '\n';
      exit(1);
    }
  }
  else if (filetype == Y4M) {
    heif_error err = loadY4M(input_filename.c_str(), &input_image);
    if (err.code != heif_error_Ok) {
      std::cerr << "Can not load TIFF input_image: " << err.message << '\n';
      exit(1);
    }
  }
  else if (filetype == TIFF) {
    heif_error err = loadTIFF(input_filename.c_str(), &input_image);
    if (err.code != heif_error_Ok) {
      std::cerr << "Can not load TIFF input_image: " << err.message << '\n';
      exit(1);
    }
  }
  else {
    heif_error err = loadJPEG(input_filename.c_str(), &input_image);
    if (err.code != heif_error_Ok) {
      std::cerr << "Can not load JPEG input_image: " << err.message << '\n';
      exit(1);
    }
  }

  return input_image;
}


heif_error create_output_nclx_profile_and_configure_encoder(heif_encoder* encoder,
                                                            heif_color_profile_nclx** out_nclx,
                                                            std::shared_ptr<heif_image> input_image,
                                                            bool lossless)
{
  *out_nclx = heif_nclx_color_profile_alloc();
  if (!*out_nclx) {
    return {heif_error_Encoding_error, heif_suberror_Unspecified, "Cannot allocate NCLX color profile."};
  }

  heif_color_profile_nclx* nclx = *out_nclx; // abbreviation;

  if (lossless) {
      heif_encoder_set_lossless(encoder, true);

      if (heif_image_get_colorspace(input_image.get()) == heif_colorspace_RGB) {
        nclx->matrix_coefficients = heif_matrix_coefficients_RGB_GBR;
        nclx->full_range_flag = true;

        heif_error error = heif_encoder_set_parameter(encoder, "chroma", "444");
        if (error.code) {
          return error;
        }
      }
      else {
        heif_color_profile_nclx* input_nclx = nullptr;

        heif_error error = heif_image_get_nclx_color_profile(input_image.get(), &input_nclx);
        if (error.code == heif_error_Color_profile_does_not_exist) {
          // NOP, use default NCLX profile
        }
        else if (error.code) {
          std::cerr << "Cannot get input NCLX color profile.\n";
          return error;
        }
        else {
          nclx->matrix_coefficients = input_nclx->matrix_coefficients;
          nclx->transfer_characteristics = input_nclx->transfer_characteristics;
          nclx->color_primaries = input_nclx->color_primaries;
          nclx->full_range_flag = input_nclx->full_range_flag;

          heif_nclx_color_profile_free(input_nclx);
          input_nclx = nullptr;
        }

        assert(!input_nclx);

        // TODO: this assumes that the encoder plugin has a 'chroma' parameter. Currently, they do, but there should be a better way to set this.
        switch (heif_image_get_chroma_format(input_image.get())) {
          case heif_chroma_420:
          case heif_chroma_monochrome:
            error = heif_encoder_set_parameter(encoder, "chroma", "420");
            break;
          case heif_chroma_422:
            error = heif_encoder_set_parameter(encoder, "chroma", "422");
            break;
          case heif_chroma_444:
            error = heif_encoder_set_parameter(encoder, "chroma", "444");
            break;
          default:
            assert(false);
            exit(5);
        }

        if (error.code) {
          return error;
        }
      }
  }

  if (!lossless) {
    heif_error error = heif_nclx_color_profile_set_matrix_coefficients(nclx, nclx_matrix_coefficients);
    if (error.code) {
      std::cerr << "Invalid matrix coefficients specified.\n";
      exit(5);
    }
    error = heif_nclx_color_profile_set_transfer_characteristics(nclx, nclx_transfer_characteristic);
    if (error.code) {
      std::cerr << "Invalid transfer characteristics specified.\n";
      exit(5);
    }
    error = heif_nclx_color_profile_set_color_primaries(nclx, nclx_colour_primaries);
    if (error.code) {
      std::cerr << "Invalid color primaries specified.\n";
      exit(5);
    }
    nclx->full_range_flag = (uint8_t) nclx_full_range;
  }

  return {heif_error_Ok};
}


struct input_tiles_generator
{
  uint32_t first_start;
  uint32_t first_end;
  uint32_t first_digits;
  uint32_t second_start;
  uint32_t second_end;
  uint32_t second_digits;

  std::filesystem::path directory;
  std::string prefix;
  std::string separator;
  std::string suffix;

  bool first_is_x = false;

  uint32_t nColumns() const { return first_is_x ? (first_end - first_start + 1) : (second_end - second_start + 1); }
  uint32_t nRows() const { return first_is_x ? (second_end - second_start + 1) : (first_end - first_start + 1); }

  uint32_t nTiles() const { return (first_end - first_start + 1) * (second_end - second_start + 1); }

  std::filesystem::path filename(uint32_t tx, uint32_t ty) const
  {
    std::stringstream sstr;

    sstr << prefix << std::setw(first_digits) << std::setfill('0') << (first_is_x ? tx : ty) + first_start;
    sstr << separator << std::setw(second_digits) << std::setfill('0') << (first_is_x ? ty : tx) + second_start;
    sstr << suffix;

    std::filesystem::path p = directory / sstr.str();
    return p;
  }
};

std::optional<input_tiles_generator> determine_input_images_tiling(const std::string& filename)
{
  std::regex pattern(R"((.*\D)?(\d+)(\D+?)(\d+)(\..+)$)");
  std::smatch match;

  input_tiles_generator generator;

  if (std::regex_match(filename, match, pattern)) {
    std::string prefix = match[1];

    auto p = std::filesystem::absolute(std::filesystem::path(prefix));
    generator.directory = p.parent_path();
    generator.prefix = p.filename().string(); // TODO: we could also use u8string(), but it is not well supported in C++20

    generator.separator = match[3];
    generator.suffix = match[5];

    generator.first_start = 9999;
    generator.first_end = 0;
    generator.first_digits = 9;

    generator.second_start = 9999;
    generator.second_end = 0;
    generator.second_digits = 9;
  }
  else {
    return std::nullopt;
  }

  std::string patternString = generator.prefix + "(\\d+)" + generator.separator + "(\\d+)" + generator.suffix + "$";
  pattern = patternString;

  for (const auto& dirEntry : std::filesystem::directory_iterator(generator.directory))
  {
    if (dirEntry.is_regular_file()) {
      std::string s{dirEntry.path().filename().string()};

      if (std::regex_match(s, match, pattern)) {
        uint32_t first = std::stoi(match[1]);
        uint32_t second = std::stoi(match[2]);

        generator.first_digits = std::min(generator.first_digits, (uint32_t)match[1].length());
        generator.second_digits = std::min(generator.second_digits, (uint32_t)match[2].length());

        generator.first_start = std::min(generator.first_start, first);
        generator.first_end = std::max(generator.first_end, first);
        generator.second_start = std::min(generator.second_start, second);
        generator.second_end = std::max(generator.second_end, second);
      }
    }
  }

  return generator;
}


heif_image_handle* encode_tiled(heif_context* ctx, heif_encoder* encoder, heif_encoding_options* options,
                                int output_bit_depth,
                                const input_tiles_generator& tile_generator,
                                const heif_image_tiling& tiling)
{
  heif_image_handle* tiled_image = nullptr;


  // --- create the main grid image

  if (tiling_method == "grid") {
    heif_error error = heif_context_add_grid_image(ctx, tiling.image_width, tiling.image_height,
                                                   tiling.num_columns, tiling.num_rows,
                                                   options,
                                                   &tiled_image);
    if (error.code != 0) {
      std::cerr << "Could not generate grid image: " << error.message << "\n";
      return nullptr;
    }
  }
#if HEIF_ENABLE_EXPERIMENTAL_FEATURES
  else if (tiling_method == "tili") {
    heif_tiled_image_parameters tiled_params{};
    tiled_params.version = 1;
    tiled_params.image_width = tiling.image_width;
    tiled_params.image_height = tiling.image_height;
    tiled_params.tile_width = tiling.tile_width;
    tiled_params.tile_height = tiling.tile_height;
    tiled_params.offset_field_length = 32;
    tiled_params.size_field_length = 24;
    tiled_params.tiles_are_sequential = 1;

    heif_error error = heif_context_add_tiled_image(ctx, &tiled_params, options, encoder, &tiled_image);
    if (error.code != 0) {
      std::cerr << "Could not generate tili image: " << error.message << "\n";
      return nullptr;
    }
  }
  else if (tiling_method == "unci") {
    heif_unci_image_parameters params{};
    params.version = 1;
    params.image_width = tiling.image_width;
    params.image_height = tiling.image_height;
    params.tile_width = tiling.tile_width;
    params.tile_height = tiling.tile_height;
    params.compression = unci_compression;

    std::string input_filename = tile_generator.filename(0, 0).string();
    InputImage prototype_image = load_image(input_filename, output_bit_depth);

    heif_error error = heif_context_add_unci_image(ctx, &params, options, prototype_image.image.get(), &tiled_image);
    if (error.code != 0) {
      std::cerr << "Could not generate unci image: " << error.message << "\n";
      return nullptr;
    }
  }
#endif
  else {
    assert(false);
    exit(10);
  }


  // --- add all the image tiles

  std::cout << "encoding tiled image, tile size: " << tiling.tile_width << "x" << tiling.tile_height
            << " image size: " << tiling.image_width << "x" << tiling.image_height << "\n";

  int tile_width = 0, tile_height = 0;

  for (uint32_t ty = 0; ty < tile_generator.nRows(); ty++)
    for (uint32_t tx = 0; tx < tile_generator.nColumns(); tx++) {
      std::string input_filename = tile_generator.filename(tx,ty).string();

      InputImage input_image = load_image(input_filename, output_bit_depth);

      if (tile_width == 0) {
        tile_width = heif_image_get_primary_width(input_image.image.get());
        tile_height = heif_image_get_primary_height(input_image.image.get());

        if (tile_width <= 0 || tile_height <= 0) {
          std::cerr << "Could not read input image size correctly\n";
          return nullptr;
        }
      }

      heif_error error;
      error = heif_image_extend_to_size_fill_with_zero(input_image.image.get(), tile_width, tile_height);
      if (error.code) {
        std::cerr << error.message << "\n";
      }

      std::cout << "encoding tile " << ty+1 << " " << tx+1
                << " (of " << tile_generator.nRows() << "x" << tile_generator.nColumns() << ")  \r";
      std::cout.flush();

      error = heif_context_add_image_tile(ctx, tiled_image, tx, ty,
                                          input_image.image.get(),
                                          encoder);
      if (error.code != 0) {
        std::cerr << "Could not encode HEIF/AVIF file: " << error.message << "\n";
        return nullptr;
      }
    }

  std::cout << "\n";

  return tiled_image;
}


class LibHeifInitializer
{
public:
  LibHeifInitializer() { heif_init(nullptr); }

  ~LibHeifInitializer() { heif_deinit(); }
};


int main(int argc, char** argv)
{
  // This takes care of initializing libheif and also deinitializing it at the end to free all resources.
  LibHeifInitializer initializer;

  int quality = 50;
  bool lossless = false;
  std::string output_filename;
  int logging_level = 0;
  bool option_show_parameters = false;
  int thumbnail_bbox_size = 0;
  int output_bit_depth = 10;
  bool force_enc_av1f = false;
  bool force_enc_vvc = false;
  bool force_enc_uncompressed = false;
  bool force_enc_jpeg = false;
  bool force_enc_jpeg2000 = false;
  bool force_enc_htj2k = false;
  bool use_tiling = false;

  std::vector<std::string> raw_params;


  while (true) {
    int option_index = 0;
    int c = getopt_long(argc, argv, "hq:Lo:vPp:t:b:Ae:C:T"
#if WITH_UNCOMPRESSED_CODEC
        "U"
#endif
        , long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
      case 'h':
        show_help(argv[0]);
        return 0;
      case 'v':
        show_version();
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
      case OPTION_VERBOSE:
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
      case 'b':
        output_bit_depth = atoi(optarg);
        break;
      case 'A':
        force_enc_av1f = true;
        break;
#if WITH_UNCOMPRESSED_CODEC
        case 'U':
        force_enc_uncompressed = true;
        break;
#endif
      case 'e':
        encoderId = optarg;
        break;
      case OPTION_NCLX_MATRIX_COEFFICIENTS:
        nclx_matrix_coefficients = (uint16_t) strtoul(optarg, nullptr, 0);
        break;
      case OPTION_NCLX_COLOUR_PRIMARIES:
        nclx_colour_primaries = (uint16_t) strtoul(optarg, nullptr, 0);
        break;
      case OPTION_NCLX_TRANSFER_CHARACTERISTIC:
        nclx_transfer_characteristic = (uint16_t) strtoul(optarg, nullptr, 0);
        break;
      case OPTION_NCLX_FULL_RANGE_FLAG:
        nclx_full_range = atoi(optarg);
        break;
      case OPTION_PITM_DESCRIPTION:
        property_pitm_description = optarg;
        break;
      case OPTION_USE_VVC_COMPRESSION:
        force_enc_vvc = true;
        break;
      case OPTION_USE_JPEG_COMPRESSION:
        force_enc_jpeg = true;
        break;
      case OPTION_USE_JPEG2000_COMPRESSION:
        force_enc_jpeg2000 = true;
        break;
      case OPTION_USE_HTJ2K_COMPRESSION:
        force_enc_htj2k = true;
        break;
      case OPTION_PLUGIN_DIRECTORY: {
        int nPlugins;
        heif_error error = heif_load_plugins(optarg, nullptr, &nPlugins, 0);
        if (error.code) {
          std::cerr << "Error loading libheif plugins.\n";
          return 1;
        }

        // Note: since we process the option within the loop, we can only consider the '-v' flags coming before the plugin loading option.
        if (logging_level > 0) {
          std::cout << nPlugins << " plugins loaded from directory " << optarg << "\n";
        }
        break;
      }
      case OPTION_TILED_IMAGE_WIDTH:
        tiled_image_width = (int) strtol(optarg, nullptr, 0);
        break;
      case OPTION_TILED_IMAGE_HEIGHT:
        tiled_image_height = (int) strtol(optarg, nullptr, 0);
        break;
      case OPTION_TILING_METHOD:
        tiling_method = optarg;
        if (tiling_method != "grid"
#if HEIF_ENABLE_EXPERIMENTAL_FEATURES
            && tiling_method != "tili" && tiling_method != "unci"
#endif
          ) {
          std::cerr << "Invalid tiling method '" << tiling_method << "'\n";
          exit(5);
        }
        break;
      case OPTION_UNCI_COMPRESSION: {
        std::string option(optarg);
        if (option == "none") {
          unci_compression = heif_unci_compression_off;
        }
        else if (option == "brotli") {
          unci_compression = heif_unci_compression_brotli;
        }
        else if (option == "deflate") {
          unci_compression = heif_unci_compression_deflate;
        }
        else if (option == "zlib") {
          unci_compression = heif_unci_compression_zlib;
        }
        else {
          std::cerr << "Invalid unci compression method '" << option << "'\n";
          exit(5);
        }
        break;
      }
      case 'C':
        chroma_downsampling = optarg;
        if (chroma_downsampling != "nn" &&
            chroma_downsampling != "nearest-neighbor" &&
            chroma_downsampling != "average" &&
            chroma_downsampling != "sharp-yuv") {
          fprintf(stderr, "Undefined chroma downsampling algorithm.\n");
          exit(5);
        }
        if (chroma_downsampling == "nn") { // abbreviation
          chroma_downsampling = "nearest-neighbor";
        }
#if !HAVE_LIBSHARPYUV
        if (chroma_downsampling == "sharp-yuv") {
          std::cerr << "Error: sharp-yuv chroma downsampling method has not been compiled into libheif.\n";
          return 5;
        }
#endif
        break;
      case 'T':
        use_tiling = true;
        break;
    }
  }

  if (quality < 0 || quality > 100) {
    std::cerr << "Invalid quality factor. Must be between 0 and 100.\n";
    return 5;
  }

  if ((force_enc_av1f ? 1 : 0) + (force_enc_vvc ? 1 : 0) + (force_enc_uncompressed ? 1 : 0) + (force_enc_jpeg ? 1 : 0) +
      (force_enc_jpeg2000 ? 1 : 0) > 1) {
    std::cerr << "Choose at most one output compression format.\n";
  }

  if (logging_level > 0) {
    logging_level += 2;

    if (logging_level > 4) {
      logging_level = 4;
    }
  }


  // ==============================================================================

  struct heif_encoder* encoder = nullptr;

  if (list_encoders) {
    show_list_of_all_encoders();
    return 0;
  }

  // --- determine output compression format (from output filename or command line parameter)

  heif_compression_format compressionFormat;

  if (force_enc_av1f) {
    compressionFormat = heif_compression_AV1;
  }
  else if (force_enc_vvc) {
    compressionFormat = heif_compression_VVC;
  }
  else if (force_enc_uncompressed) {
    compressionFormat = heif_compression_uncompressed;
  }
  else if (force_enc_jpeg) {
    compressionFormat = heif_compression_JPEG;
  }
  else if (force_enc_jpeg2000) {
    compressionFormat = heif_compression_JPEG2000;
  }
  else if (force_enc_htj2k) {
    compressionFormat = heif_compression_HTJ2K;
  }
  else {
    compressionFormat = guess_compression_format_from_filename(output_filename);
  }

  if (compressionFormat == heif_compression_undefined) {
    compressionFormat = heif_compression_HEVC;
  }


  // --- select encoder

  std::shared_ptr<heif_context> context(heif_context_alloc(),
                                        [](heif_context* c) { heif_context_free(c); });
  if (!context) {
    std::cerr << "Could not create context object\n";
    return 1;
  }


#define MAX_ENCODERS 10
  const heif_encoder_descriptor* encoder_descriptors[MAX_ENCODERS];
  int count = heif_get_encoder_descriptors(compressionFormat,
                                           nullptr,
                                           encoder_descriptors, MAX_ENCODERS);
#undef MAX_ENCODERS

  const heif_encoder_descriptor* active_encoder_descriptor = nullptr;
  if (count > 0) {
    int idx = 0;
    if (encoderId != nullptr) {
      for (int i = 0; i <= count; i++) {
        if (i == count) {
          std::cerr << "Unknown encoder ID. Choose one from the list below.\n";
          show_list_of_encoders(encoder_descriptors, count);
          return 5;
        }

        if (strcmp(encoderId, heif_encoder_descriptor_get_id_name(encoder_descriptors[i])) == 0) {
          idx = i;
          break;
        }
      }
    }

    heif_error error = heif_context_get_encoder(context.get(), encoder_descriptors[idx], &encoder);
    if (error.code) {
      std::cerr << error.message << "\n";
      return 5;
    }

    active_encoder_descriptor = encoder_descriptors[idx];
  }
  else {
    std::cerr << "No " << get_compression_format_name(compressionFormat) << " encoder available.\n";
    return 5;
  }

  if (option_show_parameters) {
    list_encoder_parameters(encoder);
    heif_encoder_release(encoder);
    return 0;
  }

  if (optind > argc - 1) {
    show_help(argv[0]);
    return 0;
  }


  if (lossless && !heif_encoder_descriptor_supports_lossless_compression(active_encoder_descriptor)) {
    std::cerr << "Warning: the selected encoder does not support lossless encoding. Encoding in lossy mode.\n";
    lossless = false;
  }

  // If we were given a list of filenames and no '-o' option, check whether the last filename is the desired output filename.

  if (output_filename.empty() && argc>1) {
    if (guess_compression_format_from_filename(argv[argc-1]) != heif_compression_undefined) {
      output_filename = argv[argc-1];
      argc--;
    }
  }

  struct heif_error error;

  bool first_image = true;

  heif_track* track = nullptr;
  heif_track* metadata_track = nullptr;

  for (; optind < argc; optind++) {
    std::string input_filename = argv[optind];

    if (output_filename.empty()) {
      std::string filename_without_suffix;
      std::string::size_type dot_position = input_filename.find_last_of('.');
      if (dot_position != std::string::npos) {
        filename_without_suffix = input_filename.substr(0, dot_position);
      }
      else {
        filename_without_suffix = input_filename;
      }

      std::string suffix = suffix_for_compression_format(compressionFormat);
      output_filename = filename_without_suffix + '.' + suffix;
    }


    // ==============================================================================

    InputImage input_image = load_image(input_filename, output_bit_depth);

    std::shared_ptr<heif_image> image = input_image.image;

#if HAVE_GETTIMEOFDAY
    if (run_benchmark) {
      gettimeofday(&time_encoding_start, nullptr);
    }
#endif

    if (first_image) {
      heif_tai_clock_info taic;
      taic.version = 1;
      taic.time_uncertainty = 1;
      taic.clock_resolution = 2;
      taic.clock_drift_rate = 3;
      taic.clock_type = 4;

      heif_track_info track_info;
      track_info.version = 1;
      track_info.track_timescale = 90000;
      track_info.write_aux_info_interleaved = true;
      track_info.with_tai_timestamps = heif_sample_aux_info_presence_optional;
      track_info.tai_clock_info = &taic;
      track_info.with_sample_content_ids = heif_sample_aux_info_presence_optional;

      track_info.with_gimi_track_content_id = true;
      std::string track_id{"track-ContentID-test"};
      track_info.gimi_track_content_id = track_id.c_str();

      heif_context_set_sequence_timescale(context.get(), 30);

      heif_context_add_visual_sequence_track(context.get(),
                                             heif_image_get_primary_width(image.get()),
                                             heif_image_get_primary_height(image.get()),
                                             &track_info,
                                             heif_track_type_video,
                                             &track);

      heif_context_add_uri_metadata_sequence_track(context.get(),
                                                   &track_info,
                                                   "urn:smpte:ul:0123456789abcdef",
                                                   &metadata_track);

      heif_track_add_reference_to_track(metadata_track, heif_track_reference_type_description, track);

      for (int i=0;i<5;i++) {
        std::stringstream testdata_str;
        testdata_str << "{ testdata: " << i+1 << " }";
        std::string testdata = testdata_str.str();

        auto* sample = heif_raw_sequence_sample_alloc();
        heif_raw_sequence_sample_set_data(sample, (const uint8_t*)testdata.c_str(), testdata.length()+1);
        heif_raw_sequence_sample_set_duration(sample, 10);
        heif_error err = heif_track_add_raw_sequence_sample(metadata_track, sample);
        if (err.code) {
          std::cerr << "error: " << err.message << "\n";
          exit(1);
        }
      }

      first_image = false;
    }

#if HEIF_ENABLE_EXPERIMENTAL_FEATURES
    // add a dummy timestamp
    heif_tai_timestamp_packet* tai = heif_tai_timestamp_packet_alloc();
    tai->tai_timestamp = optind; // just some increasing dummy number
    heif_image_set_tai_timestamp(image.get(), tai);
    heif_tai_timestamp_packet_release(tai);
#endif

    // add a dummy sample ContentID
    std::string contentId = input_filename;
    heif_image_set_gimi_sample_content_id(image.get(), contentId.c_str());

    heif_color_profile_nclx* nclx;
    heif_error error = create_output_nclx_profile_and_configure_encoder(encoder, &nclx, image, lossless);
    if (error.code) {
      std::cerr << error.message << "\n";
      return 5;
    }

    if (!lossless) {
      heif_encoder_set_lossy_quality(encoder, quality);
    }

    heif_encoder_set_logging_level(encoder, logging_level);

    set_params(encoder, raw_params);
    struct heif_encoding_options* options = heif_encoding_options_alloc();
    options->save_alpha_channel = (uint8_t) master_alpha;
    options->save_two_colr_boxes_when_ICC_and_nclx_available = (uint8_t) two_colr_boxes;
    options->output_nclx_profile = nclx;
    options->image_orientation = input_image.orientation;

    if (chroma_downsampling == "average") {
      options->color_conversion_options.preferred_chroma_downsampling_algorithm = heif_chroma_downsampling_average;
      options->color_conversion_options.only_use_preferred_chroma_algorithm = true;
    }
    else if (chroma_downsampling == "sharp-yuv") {
      options->color_conversion_options.preferred_chroma_downsampling_algorithm = heif_chroma_downsampling_sharp_yuv;
      options->color_conversion_options.only_use_preferred_chroma_algorithm = true;
    }
    else if (chroma_downsampling == "nearest-neighbor") {
      options->color_conversion_options.preferred_chroma_downsampling_algorithm = heif_chroma_downsampling_nearest_neighbor;
      options->color_conversion_options.only_use_preferred_chroma_algorithm = true;
    }

    if (premultiplied_alpha) {
      heif_image_set_premultiplied_alpha(image.get(), premultiplied_alpha);
    }


    heif_image_set_duration(image.get(), 90000);

    error = heif_track_encode_sequence_image(track, image.get(), encoder, nullptr);
    if (error.code) {
      std::cerr << "Cannot encode sequence image: " << error.message << "\n";
      return 5;
    }

#if 0
    struct heif_image_handle* handle;

    error = heif_context_encode_image(context.get(),
                                      image.get(),
                                      encoder,
                                      options,
                                      &handle);
    if (error.code != 0) {
      heif_encoding_options_free(options);
      heif_nclx_color_profile_free(nclx);
      heif_encoder_release(encoder);
      std::cerr << "Could not encode HEIF/AVIF file: " << error.message << "\n";
      return 1;
    }

    if (handle==nullptr) {
      std::cerr << "Could not encode image\n";
      return 1;
    }

    if (is_primary_image) {
      heif_context_set_primary_image(context.get(), handle);
    }

    encoded_image_ids.push_back(heif_image_handle_get_item_id(handle));

    // write EXIF to HEIC
    if (!input_image.exif.empty()) {
      // Note: we do not modify the EXIF Orientation here because we want it to match the HEIF transforms.
      // TODO: is this a good choice? Or should we set it to 1 (normal) so that other, faulty software will not transform it once more?

      error = heif_context_add_exif_metadata(context.get(), handle,
                                             input_image.exif.data(), (int) input_image.exif.size());
      if (error.code != 0) {
        heif_encoding_options_free(options);
        heif_nclx_color_profile_free(nclx);
        heif_encoder_release(encoder);
        std::cerr << "Could not write EXIF metadata: " << error.message << "\n";
        return 1;
      }
    }

    // write XMP to HEIC
    if (!input_image.xmp.empty()) {
      error = heif_context_add_XMP_metadata2(context.get(), handle,
                                             input_image.xmp.data(), (int) input_image.xmp.size(),
                                             metadata_compression ? heif_metadata_compression_deflate : heif_metadata_compression_off);
      if (error.code != 0) {
        heif_encoding_options_free(options);
        heif_nclx_color_profile_free(nclx);
        heif_encoder_release(encoder);
        std::cerr << "Could not write XMP metadata: " << error.message << "\n";
        return 1;
      }
    }

    if (thumbnail_bbox_size > 0) {
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
        heif_encoding_options_free(options);
        heif_nclx_color_profile_free(nclx);
        heif_encoder_release(encoder);
        std::cerr << "Could not generate thumbnail: " << error.message << "\n";
        return 5;
      }

      if (thumbnail_handle) {
        heif_image_handle_release(thumbnail_handle);
      }
    }

#if HAVE_GETTIMEOFDAY
    if (run_benchmark) {
      gettimeofday(&time_encoding_end, nullptr);
    }
#endif

    heif_image_handle_release(handle);
#endif
    heif_encoding_options_free(options);
    heif_nclx_color_profile_free(nclx);
  }

  heif_encoder_release(encoder);

  if (!property_pitm_description.empty()) {
    heif_image_handle* primary_image_handle;
    struct heif_error err = heif_context_get_primary_image_handle(context.get(), &primary_image_handle);
    if (err.code) {
      std::cerr << "No primary image set, cannot set user description\n";
      return 5;
    }

    heif_item_id pitm_id = heif_image_handle_get_item_id(primary_image_handle);

    heif_property_user_description udes;
    udes.lang = nullptr;
    udes.name = nullptr;
    udes.tags = nullptr;
    udes.description = property_pitm_description.c_str();
    err = heif_item_add_property_user_description(context.get(), pitm_id, &udes, nullptr);
    if (err.code) {
      std::cerr << "Cannot set user description\n";
      return 5;
    }

    heif_image_handle_release(primary_image_handle);
  }

  error = heif_context_write_to_file(context.get(), output_filename.c_str());
  if (error.code) {
    std::cerr << error.message << "\n";
    return 5;
  }

  heif_track_release(track);
  heif_track_release(metadata_track);

  return 0;
}
