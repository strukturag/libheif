/*
  libheif example application "heif".

  MIT License

  Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>

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

#include <errno.h>
#include <string.h>
#include <getopt.h>

#include <fstream>
#include <iostream>
#include <iomanip>
#include <memory>
#include <algorithm>
#include <vector>
#include <string>

#include <libheif/heif.h>

#if HAVE_LIBJPEG
#include "decoder_jpeg.h"
#endif

#if HAVE_LIBPNG
#include "decoder_png.h"
#endif

#include "decoder_y4m.h"

#include <assert.h>
#include "benchmark.h"
#include "libheif/exif.h"

int master_alpha = 1;
int thumb_alpha = 1;
int list_encoders = 0;
int two_colr_boxes = 0;
int premultiplied_alpha = 0;
int run_benchmark = 0;
int metadata_compression = 0;
const char* encoderId = nullptr;
std::string chroma_downsampling;

uint16_t nclx_matrix_coefficients = 6;
uint16_t nclx_colour_primaries = 2;
uint16_t nclx_transfer_characteristic = 2;
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

static struct option long_options[] = {
    {(char* const) "help",                    no_argument,       0,              'h'},
    {(char* const) "quality",                 required_argument, 0,              'q'},
    {(char* const) "output",                  required_argument, 0,              'o'},
    {(char* const) "lossless",                no_argument,       0,              'L'},
    {(char* const) "thumb",                   required_argument, 0,              't'},
    {(char* const) "verbose",                 no_argument,       0,              'v'},
    {(char* const) "params",                  no_argument,       0,              'P'},
    {(char* const) "no-alpha",                no_argument,       &master_alpha,  0},
    {(char* const) "no-thumb-alpha",          no_argument,       &thumb_alpha,   0},
    {(char* const) "list-encoders",           no_argument,       &list_encoders, 1},
    {(char* const) "encoder",                 required_argument, 0,              'e'},
    {(char* const) "bit-depth",               required_argument, 0,              'b'},
    {(char* const) "even-size",               no_argument,       0,              'E'},
    {(char* const) "avif",                    no_argument,       0,              'A'},
#if false && WITH_UNCOMPRESSED_CODEC
    {(char* const) "uncompressed",                no_argument,       0,                     'U'},
#endif
    {(char* const) "matrix_coefficients",     required_argument, 0,              OPTION_NCLX_MATRIX_COEFFICIENTS},
    {(char* const) "colour_primaries",        required_argument, 0,              OPTION_NCLX_COLOUR_PRIMARIES},
    {(char* const) "transfer_characteristic", required_argument, 0,              OPTION_NCLX_TRANSFER_CHARACTERISTIC},
    {(char* const) "full_range_flag",         required_argument, 0,              OPTION_NCLX_FULL_RANGE_FLAG},
    {(char* const) "enable-two-colr-boxes",   no_argument,       &two_colr_boxes, 1},
    {(char* const) "premultiplied-alpha",     no_argument,       &premultiplied_alpha, 1},
    {(char* const) "plugin-directory",        required_argument, 0,              OPTION_PLUGIN_DIRECTORY},
    {(char* const) "benchmark",               no_argument,       &run_benchmark,  1},
    {(char* const) "enable-metadata-compression", no_argument,       &metadata_compression,  1},
    {(char* const) "pitm-description",            required_argument, 0,                     OPTION_PITM_DESCRIPTION},
    {(char* const) "chroma-downsampling", required_argument, 0, 'C'},
    {0, 0,                                                       0,               0},
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
            << "  -q, --quality     set output quality (0-100) for lossy compression\n"
            << "  -L, --lossless    generate lossless output (-q has no effect)\n"
            << "  -t, --thumb #     generate thumbnail with maximum size # (default: off)\n"
            << "      --no-alpha    do not save alpha channel\n"
            << "      --no-thumb-alpha  do not save alpha channel in thumbnail image\n"
            << "  -o, --output          output filename (optional)\n"
            << "  -v, --verbose         enable logging output (more -v will increase logging level)\n"
            << "  -P, --params          show all encoder parameters\n"
            << "  -b, --bit-depth #     bit-depth of generated HEIF/AVIF file when using 16-bit PNG input (default: 10 bit)\n"
            << "  -p                    set encoder parameter (NAME=VALUE)\n"
            << "  -A, --avif            encode as AVIF (not needed if output filename with .avif suffix is provided)\n"
#if false && WITH_UNCOMPRESSED_CODEC
            << "  -U, --uncompressed    encode as uncompressed image (according to ISO 23001-17) (EXPERIMENTAL)\n"
#endif
            << "      --list-encoders         list all available encoders for all compression formats\n"
            << "  -e, --encoder ID            select encoder to use (the IDs can be listed with --list-encoders)\n"
            << "      --plugin-directory DIR  load all codec plugins in the directory\n"
            << "  -E, --even-size   [deprecated] crop images to even width and height (odd sizes are not decoded correctly by some software)\n"
            << "  --matrix_coefficients     nclx profile: color conversion matrix coefficients, default=6 (see h.273)\n"
            << "  --colour_primaries        nclx profile: color primaries (see h.273)\n"
            << "  --transfer_characteristic nclx profile: transfer characteristics (see h.273)\n"
            << "  --full_range_flag         nclx profile: full range flag, default: 1\n"
            << "  --enable-two-colr-boxes   will write both an ICC and an nclx color profile if both are present\n"
            << "  --premultiplied-alpha     input image has premultiplied alpha\n"
            << "  --enable-metadata-compression   enable XMP metadata compression (experimental)\n"
            << "  -C,--chroma-downsampling ALGO   force chroma downsampling algorithm (nn = nearest-neighbor / average / sharp-yuv)\n"
            << "                                  (sharp-yuv makes edges look sharper when using YUV420 with bilinear chroma upsampling)\n"
            << "  --benchmark               measure encoding time, PSNR, and output file size\n"
            << "  --pitm-description TEXT   (EXPERIMENTAL) set user description for primary image\n"

            << "\n"
            << "Note: to get lossless encoding, you need this set of options:\n"
            << "  -L                       switch encoder to lossless mode\n"
            << "  -p chroma=444            switch off chroma subsampling\n"
            << "  --matrix_coefficients=0  encode in RGB color-space\n";
}


#if !HAVE_LIBJPEG
InputImage loadJPEG(const char* filename)
{
  std::cerr << "Cannot load JPEG because libjpeg support was not compiled.\n";
  exit(1);

  return {};
}
#endif


#if !HAVE_LIBPNG
InputImage loadPNG(const char* filename, int output_bit_depth)
{
  std::cerr << "Cannot load PNG because libpng support was not compiled.\n";
  exit(1);

  return {};
}
#endif


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


static void show_list_of_all_encoders()
{
  for (auto compression_format : {heif_compression_HEVC, heif_compression_AV1
#if false && WITH_UNCOMPRESSED_CODEC
, heif_compression_uncompressed
#endif
  }) {

    switch (compression_format) {
      case heif_compression_AV1:
        std::cout << "AVIF";
        break;
      case heif_compression_HEVC:
        std::cout << "HEIC";
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
  else if (ends_with(filename_lowercase, ".heic")) {
    return heif_compression_HEVC;
  }
  else {
    return heif_compression_undefined;
  }
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
  bool force_enc_uncompressed = false;
  bool crop_to_even_size = false;

  std::vector<std::string> raw_params;


  while (true) {
    int option_index = 0;
    int c = getopt_long(argc, argv, "hq:Lo:vPp:t:b:AEe:C:"
#if false && WITH_UNCOMPRESSED_CODEC
        "U"
#endif
        , long_options, &option_index);
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
      case 'b':
        output_bit_depth = atoi(optarg);
        break;
      case 'A':
        force_enc_av1f = true;
        break;
#if false && WITH_UNCOMPRESSED_CODEC
        case 'U':
        force_enc_uncompressed = true;
        break;
#endif
      case 'E':
        crop_to_even_size = true;
        break;
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
    }
  }

  if (quality < 0 || quality > 100) {
    std::cerr << "Invalid quality factor. Must be between 0 and 100.\n";
    return 5;
  }

  if (force_enc_av1f && force_enc_uncompressed) {
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

  if (optind > argc - 1) {
    show_help(argv[0]);
    return 0;
  }


  // --- determine output compression format (from output filename or command line parameter)

  heif_compression_format compressionFormat;

  if (force_enc_av1f) {
    compressionFormat = heif_compression_AV1;
  }
  else if (force_enc_uncompressed) {
    compressionFormat = heif_compression_uncompressed;
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
    std::cerr << "No " << (compressionFormat == heif_compression_AV1 ? "AV1" : "HEVC") << " encoder available.\n";
    return 5;
  }

  if (option_show_parameters) {
    list_encoder_parameters(encoder);
    return 0;
  }


  struct heif_error error;

  std::shared_ptr<heif_image> primary_image;

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

      output_filename = filename_without_suffix + (compressionFormat == heif_compression_AV1 ? ".avif" : ".heic");
    }


    // ==============================================================================

    // get file type from file name

    std::string suffix;
    auto suffix_pos = input_filename.find_last_of('.');
    if (suffix_pos != std::string::npos) {
      suffix = input_filename.substr(suffix_pos + 1);
      std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
    }

    enum
    {
      PNG, JPEG, Y4M
    } filetype = JPEG;
    if (suffix == "png") {
      filetype = PNG;
    }
    else if (suffix == "y4m") {
      filetype = Y4M;
    }

    InputImage input_image;
    if (filetype == PNG) {
      input_image = loadPNG(input_filename.c_str(), output_bit_depth);
    }
    else if (filetype == Y4M) {
      input_image = loadY4M(input_filename.c_str());
    }
    else {
      input_image = loadJPEG(input_filename.c_str());
    }

    std::shared_ptr<heif_image> image = input_image.image;

    if (!primary_image) {
      primary_image = image;
    }

#if HAVE_GETTIMEOFDAY
    if (run_benchmark) {
      gettimeofday(&time_encoding_start, nullptr);
    }
#endif

    heif_color_profile_nclx nclx;
    error = heif_nclx_color_profile_set_matrix_coefficients(&nclx, nclx_matrix_coefficients);
    if (error.code) {
      std::cerr << "Invalid matrix coefficients specified.\n";
      exit(5);
    }
    error = heif_nclx_color_profile_set_transfer_characteristics(&nclx, nclx_transfer_characteristic);
    if (error.code) {
      std::cerr << "Invalid transfer characteristics specified.\n";
      exit(5);
    }
    error = heif_nclx_color_profile_set_color_primaries(&nclx, nclx_colour_primaries);
    if (error.code) {
      std::cerr << "Invalid color primaries specified.\n";
      exit(5);
    }
    nclx.full_range_flag = (uint8_t) nclx_full_range;

    //heif_image_set_nclx_color_profile(image.get(), &nclx);

    if (lossless) {
      if (heif_encoder_descriptor_supports_lossless_compression(active_encoder_descriptor)) {
        heif_encoder_set_lossless(encoder, lossless);
      }
      else {
        std::cerr << "Warning: the selected encoder does not support lossless encoding. Encoding in lossy mode.\n";
      }
    }

    heif_encoder_set_lossy_quality(encoder, quality);
    heif_encoder_set_logging_level(encoder, logging_level);

    set_params(encoder, raw_params);
    struct heif_encoding_options* options = heif_encoding_options_alloc();
    options->save_alpha_channel = (uint8_t) master_alpha;
    options->save_two_colr_boxes_when_ICC_and_nclx_available = (uint8_t) two_colr_boxes;
    options->output_nclx_profile = &nclx;
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

    if (crop_to_even_size) {
      if (heif_image_get_primary_width(image.get()) == 1 ||
          heif_image_get_primary_height(image.get()) == 1) {
        std::cerr << "Image only has a size of 1 pixel width or height. Cannot crop to even size.\n";
        return 1;
      }

      std::cerr << "Warning: option --even-size/-E is deprecated as it is not needed anymore.\n";

      int right = heif_image_get_primary_width(image.get()) % 2;
      int bottom = heif_image_get_primary_height(image.get()) % 2;

      error = heif_image_crop(image.get(), 0, right, 0, bottom);
      if (error.code != 0) {
        heif_encoding_options_free(options);
        std::cerr << "Could not crop image: " << error.message << "\n";
        return 1;
      }
    }

    if (premultiplied_alpha) {
      heif_image_set_premultiplied_alpha(image.get(), premultiplied_alpha);
    }


    struct heif_image_handle* handle;
    error = heif_context_encode_image(context.get(),
                                      image.get(),
                                      encoder,
                                      options,
                                      &handle);
    if (error.code != 0) {
      heif_encoding_options_free(options);
      std::cerr << "Could not encode HEIF/AVIF file: " << error.message << "\n";
      return 1;
    }

    // write EXIF to HEIC
    if (!input_image.exif.empty()) {
      // Note: we do not modify the EXIF Orientation here because we want it to match the HEIF transforms.
      // TODO: is this a good choice? Or should we set it to 1 (normal) so that other, faulty software will not transform it once more?

      error = heif_context_add_exif_metadata(context.get(), handle,
                                             input_image.exif.data(), (int) input_image.exif.size());
      if (error.code != 0) {
        heif_encoding_options_free(options);
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
    heif_encoding_options_free(options);
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

  if (run_benchmark) {
    double psnr = compute_psnr(primary_image.get(), output_filename);
    std::cout << "PSNR: " << std::setprecision(2) << std::fixed << psnr << " ";

#if HAVE_GETTIMEOFDAY
    double t = (double) (time_encoding_end.tv_sec - time_encoding_start.tv_sec) + (double) (time_encoding_end.tv_usec - time_encoding_start.tv_usec) / 1000000.0;
    std::cout << "time: " << std::setprecision(1) << std::fixed << t << " ";
#endif

    std::ifstream istr(output_filename.c_str());
    istr.seekg(0, std::ios_base::end);
    std::streamoff size = istr.tellg();
    std::cout << "size: " << size << "\n";
  }

  return 0;
}
