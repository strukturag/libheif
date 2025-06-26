/*
  libheif example application.

  MIT License

  Copyright (c) 2017-2024 Dirk Farin <dirk.farin@gmail.com>
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

#include <cstring>
#include <getopt.h>
#include <filesystem>
#include "libheif/heif_items.h"
#include "libheif/heif_experimental.h"
#include "libheif/heif_sequences.h"

#if defined(HAVE_UNISTD_H)

#include <unistd.h>

#endif

#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cassert>
#include <algorithm>
#include <vector>
#include <array>
#include <cctype>

#include <libheif/heif.h>
#include <libheif/heif_tai_timestamps.h>

#include "heifio/encoder.h"

#if HAVE_LIBJPEG
#include "heifio/encoder_jpeg.h"
#endif

#if HAVE_LIBPNG
#include "heifio/encoder_png.h"
#endif

#if HAVE_LIBTIFF
#include "heifio/encoder_tiff.h"
#endif

#include "../heifio/encoder_y4m.h"
#include "common.h"

#if defined(_MSC_VER)
#include "getopt.h"
#endif

#define UNUSED(x) (void)x


static void show_help(const char* argv0)
{
  std::filesystem::path p(argv0);
  std::string filename = p.filename().string();

  std::stringstream sstr;
  sstr << " " << filename << "  libheif version: " << heif_get_version();

  std::string title = sstr.str();

  std::cerr << title << "\n"
            << std::string(title.length() + 1, '-') << "\n"
            << "Usage: " << filename << " [options]  <input-image> [output-image]\n"
            << "\n"
               "The program determines the output file format from the output filename suffix.\n"
               "These suffixes are recognized: jpg, jpeg, png, tif, tiff, y4m. If no output filename is specified, 'jpg' is used.\n"
               "\n"
               "Options:\n"
               "  -h, --help                     show help\n"
               "  -v, --version                  show version\n"
               "  -q, --quality                  quality (for JPEG output)\n"
               "  -o, --output FILENAME          write output to FILENAME (optional)\n"
               "  -d, --decoder ID               use a specific decoder (see --list-decoders)\n"
               "      --with-aux                 also write auxiliary images (e.g. depth images)\n"
               "      --with-xmp                 write XMP metadata to file (output filename with .xmp suffix)\n"
               "      --with-exif                write EXIF metadata to file (output filename with .exif suffix)\n"
               "      --skip-exif-offset         skip EXIF metadata offset bytes\n"
               "      --no-colons                replace ':' characters in auxiliary image filenames with '_'\n"
               "      --list-decoders            list all available decoders (built-in and plugins)\n"
               "      --tiles                    output all image tiles as separate images\n"
               "      --quiet                    do not output status messages to console\n"
               "  -S, --sequence                 decode image sequence instead of still image\n"
               "  -C, --chroma-upsampling ALGO   Force chroma upsampling algorithm (nn = nearest-neighbor / bilinear)\n"
               "      --png-compression-level #  Set to integer between 0 (fastest) and 9 (best). Use -1 for default.\n"
               "      --transparency-composition-mode MODE  Controls how transparent images are rendered when the output format\n"
               "                                            support transparency. MODE must be one of: white, black, checkerboard.\n"
               "      --disable-limits           disable all security limits (do not use in production environment)\n";
}


class ContextReleaser
{
public:
  ContextReleaser(struct heif_context* ctx) : ctx_(ctx)
  {}

  ~ContextReleaser()
  {
    heif_context_free(ctx_);
  }

private:
  struct heif_context* ctx_;
};


int option_quiet = 0;
int option_aux = 0;
int option_no_colons = 0;
int option_with_xmp = 0;
int option_with_exif = 0;
int option_skip_exif_offset = 0;
int option_list_decoders = 0;
int option_png_compression_level = -1; // use zlib default
int option_output_tiles = 0;
int option_disable_limits = 0;
int option_sequence = 0;
std::string output_filename;

std::string chroma_upsampling;
std::string transparency_composition_mode = "checkerboard";

#define OPTION_PNG_COMPRESSION_LEVEL 1000
#define OPTION_TRANSPARENCY_COMPOSITION_MODE 1001

static struct option long_options[] = {
    {(char* const) "quality",          required_argument, 0,                        'q'},
    {(char* const) "strict",           no_argument,       0,                        's'},
    {(char* const) "decoder",          required_argument, 0,                        'd'},
    {(char* const) "output",           required_argument, 0,                        'o'},
    {(char* const) "quiet",            no_argument,       &option_quiet,            1},
    {(char* const) "with-aux",         no_argument,       &option_aux,              1},
    {(char* const) "with-xmp",         no_argument,       &option_with_xmp,         1},
    {(char* const) "with-exif",        no_argument,       &option_with_exif,        1},
    {(char* const) "skip-exif-offset", no_argument,       &option_skip_exif_offset, 1},
    {(char* const) "no-colons",        no_argument,       &option_no_colons,        1},
    {(char* const) "list-decoders",    no_argument,       &option_list_decoders,    1},
    {(char* const) "tiles",            no_argument,       &option_output_tiles,     1},
    {(char* const) "sequence",            no_argument,       &option_sequence,     1},
    {(char* const) "help",             no_argument,       0,                        'h'},
    {(char* const) "chroma-upsampling", required_argument, 0,                     'C'},
    {(char* const) "png-compression-level", required_argument, 0,  OPTION_PNG_COMPRESSION_LEVEL},
    {(char* const) "transparency-composition-mode", required_argument, 0,  OPTION_TRANSPARENCY_COMPOSITION_MODE},
    {(char* const) "version",          no_argument,       0,                        'v'},
    {(char* const) "disable-limits", no_argument, &option_disable_limits, 1},
    {nullptr, no_argument, nullptr, 0}
};


#define MAX_DECODERS 20

bool is_integer_string(const char* s)
{
  if (strlen(s)==0) {
    return false;
  }

  if (!(isdigit(s[0]) || s[0]=='-')) {
    return false;
  }

  for (size_t i=strlen(s)-1; i>=1 ; i--) {
    if (!isdigit(s[i])) {
      return false;
    }
  }

  return true;
}


void show_png_compression_level_usage_warning()
{
  fprintf(stderr, "Invalid PNG compression level. Has to be between 0 (fastest) and 9 (best).\n"
                  "You can also use -1 to use the default compression level.\n");
}


int decode_single_image(heif_image_handle* handle,
                        std::string filename_stem,
                        std::string filename_suffix,
                        heif_decoding_options* decode_options,
                        std::unique_ptr<Encoder>& encoder)
{
  int bit_depth = heif_image_handle_get_luma_bits_per_pixel(handle);
  if (bit_depth < 0) {
    std::cerr << "Input image has undefined bit-depth\n";
    return 1;
  }

  int has_alpha = heif_image_handle_has_alpha_channel(handle);

  struct heif_image* image;
  struct heif_error err;
  err = heif_decode_image(handle,
                          &image,
                          encoder->colorspace(has_alpha),
                          encoder->chroma(has_alpha, bit_depth),
                          decode_options);
  if (err.code) {
    std::cerr << "Could not decode image: "
              << err.message << "\n";
    return 1;
  }

  // show decoding warnings

  for (int i = 0;; i++) {
    int n = heif_image_get_decoding_warnings(image, i, &err, 1);
    if (n == 0) {
      break;
    }

    std::cerr << "Warning: " << err.message << "\n";
  }

  if (image) {
    std::string filename = filename_stem + '.' + filename_suffix;

    bool written = encoder->Encode(handle, image, filename);
    if (!written) {
      fprintf(stderr, "could not write image\n");
    }
    else {
      if (!option_quiet) {
        std::cout << "Written to " << filename << "\n";
      }
    }
    heif_image_release(image);


    if (option_aux) {
      int has_depth = heif_image_handle_has_depth_image(handle);
      if (has_depth) {
        heif_item_id depth_id;
        int nDepthImages = heif_image_handle_get_list_of_depth_image_IDs(handle, &depth_id, 1);
        assert(nDepthImages == 1);
        (void) nDepthImages;

        struct heif_image_handle* depth_handle = nullptr;
        err = heif_image_handle_get_depth_image_handle(handle, depth_id, &depth_handle);
        if (err.code) {
          std::cerr << "Could not read depth channel\n";
          return 1;
        }

        int depth_bit_depth = heif_image_handle_get_luma_bits_per_pixel(depth_handle);

        struct heif_image* depth_image;
        err = heif_decode_image(depth_handle,
                                &depth_image,
                                encoder->colorspace(false),
                                encoder->chroma(false, depth_bit_depth),
                                nullptr);
        if (err.code) {
          heif_image_handle_release(depth_handle);
          std::cerr << "Could not decode depth image: " << err.message << "\n";
          return 1;
        }

        std::ostringstream s;
        s << filename_stem;
        s << "-depth.";
        s << filename_suffix;

        written = encoder->Encode(depth_handle, depth_image, s.str());
        if (!written) {
          fprintf(stderr, "could not write depth image\n");
        }
        else {
          if (!option_quiet) {
            std::cout << "Depth image written to " << s.str() << "\n";
          }
        }

        heif_image_release(depth_image);
        heif_image_handle_release(depth_handle);
      }
    }


    // --- aux images

    if (option_aux) {
      int nAuxImages = heif_image_handle_get_number_of_auxiliary_images(handle, LIBHEIF_AUX_IMAGE_FILTER_OMIT_ALPHA | LIBHEIF_AUX_IMAGE_FILTER_OMIT_DEPTH);
      if (nAuxImages > 0) {

        std::vector<heif_item_id> auxIDs(nAuxImages);
        heif_image_handle_get_list_of_auxiliary_image_IDs(handle,
                                                          LIBHEIF_AUX_IMAGE_FILTER_OMIT_ALPHA | LIBHEIF_AUX_IMAGE_FILTER_OMIT_DEPTH,
                                                          auxIDs.data(), nAuxImages);

        for (heif_item_id auxId : auxIDs) {

          struct heif_image_handle* aux_handle = nullptr;
          err = heif_image_handle_get_auxiliary_image_handle(handle, auxId, &aux_handle);
          if (err.code) {
            std::cerr << "Could not read auxiliary image\n";
            return 1;
          }

          int aux_bit_depth = heif_image_handle_get_luma_bits_per_pixel(aux_handle);

          struct heif_image* aux_image = nullptr;
          err = heif_decode_image(aux_handle,
                                  &aux_image,
                                  encoder->colorspace(false),
                                  encoder->chroma(false, aux_bit_depth),
                                  nullptr);
          if (err.code) {
            heif_image_handle_release(aux_handle);
            std::cerr << "Could not decode auxiliary image: " << err.message << "\n";
            return 1;
          }

          const char* auxTypeC = nullptr;
          err = heif_image_handle_get_auxiliary_type(aux_handle, &auxTypeC);
          if (err.code) {
            heif_image_release(aux_image);
            heif_image_handle_release(aux_handle);
            std::cerr << "Could not get type of auxiliary image: " << err.message << "\n";
            return 1;
          }

          std::string auxType = std::string(auxTypeC);

          heif_image_handle_release_auxiliary_type(aux_handle, &auxTypeC);

          if (option_no_colons) {
            std::replace(auxType.begin(), auxType.end(), ':', '_');
          }

          std::ostringstream s;
          s << filename_stem;
          s << "-" + auxType + ".";
          s << filename_suffix;

          std::string auxFilename = s.str();

          written = encoder->Encode(aux_handle, aux_image, auxFilename);
          if (!written) {
            fprintf(stderr, "could not write auxiliary image\n");
          }
          else {
            if (!option_quiet) {
              std::cout << "Auxiliary image written to " << auxFilename << "\n";
            }
          }

          heif_image_release(aux_image);
          heif_image_handle_release(aux_handle);
        }
      }
    }


    // --- write metadata

    if (option_with_xmp || option_with_exif) {
      int numMetadata = heif_image_handle_get_number_of_metadata_blocks(handle, nullptr);
      if (numMetadata > 0) {
        std::vector<heif_item_id> ids(numMetadata);
        heif_image_handle_get_list_of_metadata_block_IDs(handle, nullptr, ids.data(), numMetadata);

        for (int n = 0; n < numMetadata; n++) {

          // check whether metadata block is XMP

          std::string itemtype = heif_image_handle_get_metadata_type(handle, ids[n]);
          std::string contenttype = heif_image_handle_get_metadata_content_type(handle, ids[n]);

          if (option_with_xmp && contenttype == "application/rdf+xml") {
            // read XMP data to memory array

            size_t xmpSize = heif_image_handle_get_metadata_size(handle, ids[n]);
            std::vector<uint8_t> xmp(xmpSize);
            err = heif_image_handle_get_metadata(handle, ids[n], xmp.data());
            if (err.code) {
              std::cerr << "Could not read XMP metadata: " << err.message << "\n";
              return 1;
            }

            // write XMP data to file

            std::string xmp_filename = filename_stem + ".xmp";
            std::ofstream ostr(xmp_filename.c_str());
            ostr.write((char*) xmp.data(), xmpSize);
          }
          else if (option_with_exif && itemtype == "Exif") {
            // read EXIF data to memory array

            size_t exifSize = heif_image_handle_get_metadata_size(handle, ids[n]);
            std::vector<uint8_t> exif(exifSize);
            err = heif_image_handle_get_metadata(handle, ids[n], exif.data());
            if (err.code) {
              std::cerr << "Could not read EXIF metadata: " << err.message << "\n";
              return 1;
            }

            uint32_t offset = 0;
            if (option_skip_exif_offset) {
              if (exifSize < 4) {
                std::cerr << "Invalid EXIF metadata, it is too small.\n";
                return 1;
              }

              offset = (exif[0] << 24) | (exif[1] << 16) | (exif[2] << 8) | exif[3];
              offset += 4;

              if (offset >= exifSize) {
                std::cerr << "Invalid EXIF metadata, offset out of range.\n";
                return 1;
              }
            }

            // write EXIF data to file

            std::string exif_filename = filename_stem + ".exif";
            std::ofstream ostr(exif_filename.c_str());
            ostr.write((char*) exif.data() + offset, exifSize - offset);
          }
        }
      }
    }
  }

  return 0;
}


int digits_for_integer(uint32_t v)
{
  int digits=1;

  while (v>=10) {
    digits++;
    v /= 10;
  }

  return digits;
}


int decode_image_tiles(heif_image_handle* handle,
                       std::string filename_stem,
                       std::string filename_suffix,
                       heif_decoding_options* decode_options,
                       std::unique_ptr<Encoder>& encoder)
{
  heif_image_tiling tiling;

  heif_image_handle_get_image_tiling(handle, !decode_options->ignore_transformations, &tiling);
  if (tiling.num_columns == 1 && tiling.num_rows == 1) {
    return decode_single_image(handle, filename_stem, filename_suffix, decode_options, encoder);
  }


  int bit_depth = heif_image_handle_get_luma_bits_per_pixel(handle);
  if (bit_depth < 0) {
    std::cerr << "Input image has undefined bit-depth\n";
    return 1;
  }

  int has_alpha = heif_image_handle_has_alpha_channel(handle);

  int digits_tx = digits_for_integer(tiling.num_columns-1);
  int digits_ty = digits_for_integer(tiling.num_rows-1);

  for (uint32_t ty = 0; ty < tiling.num_rows; ty++)
    for (uint32_t tx = 0; tx < tiling.num_columns; tx++) {
      struct heif_image* image;
      struct heif_error err;
      err = heif_image_handle_decode_image_tile(handle,
                                                &image,
                                                encoder->colorspace(has_alpha),
                                                encoder->chroma(has_alpha, bit_depth),
                                                decode_options, tx, ty);
      if (err.code) {
        std::cerr << "Could not decode image tile: "
                  << err.message << "\n";
        return 1;
      }

      // show decoding warnings

      for (int i = 0;; i++) {
        int n = heif_image_get_decoding_warnings(image, i, &err, 1);
        if (n == 0) {
          break;
        }

        std::cerr << "Warning: " << err.message << "\n";
      }

      if (image) {
        std::stringstream filename_str;
        filename_str << filename_stem << "-"
                     << std::setfill('0') << std::setw(digits_ty) << ty << '-'
                     << std::setfill('0') << std::setw(digits_tx) << tx << "." << filename_suffix;

        std::string filename = filename_str.str();

        bool written = encoder->Encode(handle, image, filename);
        if (!written) {
          fprintf(stderr, "could not write image\n");
        }
        else {
          if (!option_quiet) {
            std::cout << "Written to " << filename << "\n";
          }
        }
        heif_image_release(image);
      }
    }

  return 0;
}

static int max_value_progress = 0;

void start_progress(enum heif_progress_step step, int max_progress, void* progress_user_data)
{
  max_value_progress = max_progress;
}

void on_progress(enum heif_progress_step step, int progress, void* progress_user_data)
{
  std::cout << "decoding image... " << progress * 100 / max_value_progress << "%\r";
  std::cout.flush();
}

void end_progress(enum heif_progress_step step, void* progress_user_data)
{
  std::cout << "\n";
}


class LibHeifInitializer {
public:
  LibHeifInitializer() { heif_init(nullptr); }
  ~LibHeifInitializer() { heif_deinit(); }
};


int main(int argc, char** argv)
{
  // This takes care of initializing libheif and also deinitializing it at the end to free all resources.
  LibHeifInitializer initializer;

  int quality = -1;  // Use default quality.
  bool strict_decoding = false;
  const char* decoder_id = nullptr;

  UNUSED(quality);  // The quality will only be used by encoders that support it.
  //while ((opt = getopt(argc, argv, "q:s")) != -1) {
  while (true) {
    int option_index = 0;
    int c = getopt_long(argc, argv, "hq:sd:C:vo:S", long_options, &option_index);
    if (c == -1) {
      break;
    }

    switch (c) {
      case 'q':
        quality = atoi(optarg);
        break;
      case 'd':
        decoder_id = optarg;
        break;
      case 's':
        strict_decoding = true;
        break;
      case '?':
        std::cerr << "\n";
        [[fallthrough]];
      case 'h':
        show_help(argv[0]);
        return 0;
      case 'C':
        chroma_upsampling = optarg;
        if (chroma_upsampling != "nn" &&
            chroma_upsampling != "nearest-neighbor" &&
            chroma_upsampling != "bilinear") {
          fprintf(stderr, "Undefined chroma upsampling algorithm.\n");
          exit(5);
        }
        if (chroma_upsampling == "nn") { // abbreviation
          chroma_upsampling = "nearest-neighbor";
        }
        break;
      case OPTION_PNG_COMPRESSION_LEVEL:
        if (!is_integer_string(optarg)) {
          show_png_compression_level_usage_warning();
          exit(5);
        }
        option_png_compression_level = std::stoi(optarg);
        if (option_png_compression_level < -1 || option_png_compression_level > 9) {
          show_png_compression_level_usage_warning();
          exit(5);
        }
        break;
      case OPTION_TRANSPARENCY_COMPOSITION_MODE:
        if (strcmp(optarg, "white") == 0 ||
            strcmp(optarg, "black") == 0 ||
            strcmp(optarg, "checkerboard") == 0) {
            transparency_composition_mode = optarg;
          }
        else {
          fprintf(stderr, "Unknown transparency composition mode. Must be one of: white, black, checkerboard.\n");
          exit(5);
        }
        break;
      case 'v':
        heif_examples::show_version();
        return 0;
      case 'o':
        output_filename = optarg;
        break;
      case 'S':
        option_sequence = 1;
        break;
    }
  }

  if (option_list_decoders) {
    heif_examples::list_all_decoders();
    return 0;
  }

  if (optind >= argc || optind + 2 < argc) {
    // Need at least input filename as additional argument, but not more as two filenames.
    show_help(argv[0]);
    return 5;
  }

  std::string input_filename(argv[optind++]);
  std::string output_filename_stem;
  std::string output_filename_suffix;

  if (output_filename.empty()) {
    if (optind == argc) {
      std::string input_stem;
      size_t dot_pos = input_filename.rfind('.');
      if (dot_pos != std::string::npos) {
        input_stem = input_filename.substr(0, dot_pos);
      }
      else {
        input_stem = input_filename;
      }

      output_filename = input_stem + ".jpg";
    }
    else if (optind == argc-1) {
      output_filename = argv[optind];
    }
    else {
      assert(false);
    }
  }

  std::unique_ptr<Encoder> encoder;

  size_t dot_pos = output_filename.rfind('.');
  if (dot_pos != std::string::npos) {
    output_filename_stem = output_filename.substr(0,dot_pos);
    std::string suffix_lowercase = output_filename.substr(dot_pos + 1);

    std::transform(suffix_lowercase.begin(), suffix_lowercase.end(),
                   suffix_lowercase.begin(), ::tolower);

    output_filename_suffix = suffix_lowercase;

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
      auto pngEncoder = new PngEncoder();
      pngEncoder->set_compression_level(option_png_compression_level);
      encoder.reset(pngEncoder);
#else
      fprintf(stderr, "PNG support has not been compiled in.\n");
      return 1;
#endif  // HAVE_LIBPNG
    }

    if (suffix_lowercase == "tif" || suffix_lowercase == "tiff") {
#if HAVE_LIBTIFF
      encoder.reset(new TiffEncoder());
#else
      fprintf(stderr, "TIFF support has not been compiled in.\n");
      return 1;
#endif  // HAVE_LIBTIFF
    }

    if (suffix_lowercase == "y4m") {
      encoder.reset(new Y4MEncoder());
    }
  }
  else {
    output_filename_stem = output_filename;
    output_filename_suffix = "jpg";
  }

  if (!encoder) {
    fprintf(stderr, "Unknown file type in %s\n", output_filename.c_str());
    return 1;
  }


  // --- check whether input is a supported HEIF file

  if (int ret = heif_examples::check_for_valid_input_HEIF_file(input_filename)) {
    return ret;
  }

  // --- read the HEIF file

  struct heif_context* ctx = heif_context_alloc();
  if (!ctx) {
    fprintf(stderr, "Could not create context object\n");
    return 1;
  }

  if (option_disable_limits) {
    heif_context_set_security_limits(ctx, heif_get_disabled_security_limits());
  }

  ContextReleaser cr(ctx);
  struct heif_error err;
  err = heif_context_read_from_file(ctx, input_filename.c_str(), nullptr);
  if (err.code != 0) {
    std::cerr << "Could not read HEIF/AVIF file: " << err.message << "\n";
    return 1;
  }


  if (option_sequence) {
    if (!heif_context_has_sequence(ctx)) {
      std::cerr << "File contains no image sequence\n";
      return 1;
    }

    int nTracks = heif_context_number_of_sequence_tracks(ctx);
    std::cout << "number of tracks: " << nTracks << "\n";

    std::vector<uint32_t> track_ids(nTracks);
    heif_context_get_track_ids(ctx, track_ids.data());

    for (uint32_t id : track_ids) {
      heif_track* track = heif_context_get_track(ctx, id);
      std::cout << "#" << id << " : " << heif_examples::fourcc_to_string(heif_track_get_track_handler_type(track));

      if (heif_track_get_track_handler_type(track) == heif_track_type_image_sequence ||
          heif_track_get_track_handler_type(track) == heif_track_type_video) {
        uint16_t w,h;
        heif_track_get_image_resolution(track, &w, &h);
        std::cout << " " << w << "x" << h;
      }
      else {
        uint32_t sample_entry_type = heif_track_get_sample_entry_type_of_first_cluster(track);
        std::cout << "\n  sample entry type: " << heif_examples::fourcc_to_string(sample_entry_type);
        if (sample_entry_type == heif_fourcc('u', 'r', 'i', 'm')) {
          const char* uri;
          err = heif_track_get_urim_sample_entry_uri_of_first_cluster(track, &uri);
          if (err.code) {
            std::cerr << "Cannot read 'urim'-track URI: " << err.message << "\n";
          }
          std::cout << "\n  URI: " << uri;
          heif_string_release(uri);
        }

        // get metadata track samples

        for (;;) {
          struct heif_raw_sequence_sample* sample;
          err = heif_track_get_next_raw_sequence_sample(track, &sample);
          if (err.code != 0) {
            break;
          }

          size_t dataSize;
          const uint8_t* data = heif_raw_sequence_sample_get_data(sample, &dataSize);

          std::cout << "\n  raw sample: ";
          for (uint32_t i = 0; i < dataSize; i++) {
            std::cout << " " << std::hex << (int)data[i];
          }

          heif_raw_sequence_sample_release(sample);
        }
      }

      std::cout << "\n";
    }

    std::unique_ptr<heif_decoding_options, void(*)(heif_decoding_options*)> decode_options(heif_decoding_options_alloc(), heif_decoding_options_free);
    encoder->UpdateDecodingOptions(nullptr, decode_options.get());

    struct heif_track* track = heif_context_get_track(ctx, 0);

    const char* track_contentId = heif_track_get_gimi_track_content_id(track);
    if (track_contentId) {
      std::cout << "track content ID: " << track_contentId << "\n";
      heif_string_release(track_contentId);
    }

    const heif_tai_clock_info* taic = heif_track_get_tai_clock_info_of_first_cluster(track);
    if (taic) {
      std::cout << "taic: " << taic->time_uncertainty << " / " << taic->clock_resolution << " / "
                << taic->clock_drift_rate << " / " << int(taic->clock_type) << "\n";
    }

    for (int i=0; ;i++) {
      heif_image* out_image = nullptr;
      int bit_depth = 8; // TODO
      err = heif_track_decode_next_image(track, &out_image,
                                         encoder->colorspace(false),
                                         encoder->chroma(false, bit_depth),
                                         decode_options.get());
      if (err.code == heif_error_End_of_sequence) {
        break;
      }
      else if (err.code) {
        std::cerr << err.message << "\n";
        return 1;
      }

      std::cout << "sample duration " << heif_image_get_duration(out_image) << "\n";

      const char* contentID = heif_image_get_gimi_sample_content_id(out_image);
      if (contentID) {
        std::cout << "content ID " << contentID << "\n";
        heif_string_release(contentID);
      }

      heif_tai_timestamp_packet* timestamp;
      err = heif_image_get_tai_timestamp(out_image, &timestamp);
      if (err.code) {
        std::cerr << err.message << "\n";
        return 10;
      }
      else if (timestamp) {
        std::cout << "TAI timestamp: " << timestamp->tai_timestamp << "\n";
        heif_tai_timestamp_packet_release(timestamp);
      }

      std::ostringstream s;
      s << output_filename_stem;
      s << "-" << i+1;
      s << "." << output_filename_suffix;
      std::string numbered_filename = s.str();

      bool written = encoder->Encode(nullptr, out_image, numbered_filename);
      if (!written) {
        fprintf(stderr, "could not write image\n");
      }
      else {
        if (!option_quiet) {
          std::cout << "Written to " << numbered_filename << "\n";
        }
      }
      heif_image_release(out_image);
    }

    heif_track_release(track);

    return 0;
  }


  int num_images = heif_context_get_number_of_top_level_images(ctx);
  if (num_images == 0) {
    fprintf(stderr, "File doesn't contain any images\n");
    return 1;
  }

  if (!option_quiet) {
    std::cout << "File contains " << num_images << " image" << (num_images>1 ? "s" : "") << "\n";
  }

  std::vector<heif_item_id> image_IDs(num_images);
  num_images = heif_context_get_list_of_top_level_image_IDs(ctx, image_IDs.data(), num_images);


  std::string filename;
  size_t image_index = 1;  // Image filenames are "1" based.

  for (int idx = 0; idx < num_images; ++idx) {

    std::string numbered_output_filename_stem;

    if (num_images > 1) {
      std::ostringstream s;
      s << output_filename_stem;
      s << "-" << image_index;
      numbered_output_filename_stem = s.str();

      s << "." << output_filename_suffix;
      filename = s.str();
    }
    else {
      filename = output_filename;
      numbered_output_filename_stem = output_filename_stem;
    }

    struct heif_image_handle* handle;
    err = heif_context_get_image_handle(ctx, image_IDs[idx], &handle);
    if (err.code) {
      std::cerr << "Could not read HEIF/AVIF image " << idx << ": "
                << err.message << "\n";
      return 1;
    }

    std::unique_ptr<heif_decoding_options, void(*)(heif_decoding_options*)> decode_options(heif_decoding_options_alloc(), heif_decoding_options_free);
    encoder->UpdateDecodingOptions(handle, decode_options.get());

    decode_options->strict_decoding = strict_decoding;
    decode_options->decoder_id = decoder_id;

    if (!option_quiet) {
      decode_options->start_progress = start_progress;
      decode_options->on_progress = on_progress;
      decode_options->end_progress = end_progress;
    }

    if (chroma_upsampling=="nearest-neighbor") {
      decode_options->color_conversion_options.preferred_chroma_upsampling_algorithm = heif_chroma_upsampling_nearest_neighbor;
      decode_options->color_conversion_options.only_use_preferred_chroma_algorithm = true;
    }
    else if (chroma_upsampling=="bilinear") {
      decode_options->color_conversion_options.preferred_chroma_upsampling_algorithm = heif_chroma_upsampling_bilinear;
      decode_options->color_conversion_options.only_use_preferred_chroma_algorithm = true;
    }

    int ret;

    auto* color_conversion_options_ext = heif_color_conversion_options_ext_alloc();
    decode_options->color_conversion_options_ext = color_conversion_options_ext;


    // If output file format does not support transparency, render in the selected composition mode.

    if (!encoder->supports_alpha()) {
      if (transparency_composition_mode == "white") {
        color_conversion_options_ext->alpha_composition_mode = heif_alpha_composition_mode_solid_color;
        color_conversion_options_ext->background_red = 0xFFFFU;
        color_conversion_options_ext->background_green = 0xFFFFU;
        color_conversion_options_ext->background_blue = 0xFFFFU;
      }
      else if (transparency_composition_mode == "black") {
        color_conversion_options_ext->alpha_composition_mode = heif_alpha_composition_mode_solid_color;
        color_conversion_options_ext->background_red = 0;
        color_conversion_options_ext->background_green = 0;
        color_conversion_options_ext->background_blue = 0;
      }
      else if (transparency_composition_mode == "checkerboard") {
        color_conversion_options_ext->alpha_composition_mode = heif_alpha_composition_mode_checkerboard;
      }
    }

    if (option_output_tiles) {
      ret = decode_image_tiles(handle, numbered_output_filename_stem, output_filename_suffix, decode_options.get(), encoder);
    }
    else {
      ret = decode_single_image(handle, numbered_output_filename_stem, output_filename_suffix, decode_options.get(), encoder);
    }

    heif_color_conversion_options_ext_free(color_conversion_options_ext);

    if (ret) {
      heif_image_handle_release(handle);
      return ret;
    }

    heif_image_handle_release(handle);

    image_index++;
  }

  return 0;
}
