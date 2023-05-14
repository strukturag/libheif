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

#include <cstring>
#include <getopt.h>

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
#if HAVE_LIBPNG

#include "encoder_png.h"

#endif

#include "encoder_y4m.h"

#if defined(_MSC_VER)
#include "getopt.h"
#endif

#define UNUSED(x) (void)x

static void show_help(const char* argv0)
{
  std::cerr << " heif-convert  libheif version: " << heif_get_version() << "\n"
            << "-------------------------------------------\n"
               "Usage: heif-convert [options]  <input-image> <output-image>\n"
               "\n"
               "The program determines the output file format from the output filename suffix.\n"
               "These suffices are recognized: jpg, jpeg, png, y4m."
               "\n"
               "Options:\n"
               "  -h, --help                     show help\n"
               "  -q, --quality                  quality (for JPEG output)\n"
               "  -d, --decoder ID               use a specific decoder (see --list-decoders)\n"
               "      --with-aux                 also write auxiliary images (e.g. depth images)\n"
               "      --with-xmp                 write XMP metadata to file (output filename with .xmp suffix)\n"
               "      --with-exif                write EXIF metadata to file (output filename with .exif suffix)\n"
               "      --skip-exif-offset         skip EXIF metadata offset bytes\n"
               "      --no-colons                replace ':' characters in auxiliary image filenames with '_'\n"
               "      --list-decoders            list all available decoders (built-in and plugins)\n"
               "      --quiet                    do not output status messages to console\n"
               "  -C, --chroma-upsampling ALGO   Force chroma upsampling algorithm (nn = nearest-neighbor / bilinear)\n"
               "      --png-compression-level #  Set to integer between 0 (fastest) and 9 (best). Use -1 for default.\n";
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

std::string chroma_upsampling;

#define OPTION_PNG_COMPRESSION_LEVEL 1000


static struct option long_options[] = {
    {(char* const) "quality",          required_argument, 0,                        'q'},
    {(char* const) "strict",           no_argument,       0,                        's'},
    {(char* const) "decoder",          required_argument, 0,                        'd'},
    {(char* const) "quiet",            no_argument,       &option_quiet,            1},
    {(char* const) "with-aux",         no_argument,       &option_aux,              1},
    {(char* const) "with-xmp",         no_argument,       &option_with_xmp,         1},
    {(char* const) "with-exif",        no_argument,       &option_with_exif,        1},
    {(char* const) "skip-exif-offset", no_argument,       &option_skip_exif_offset, 1},
    {(char* const) "no-colons",        no_argument,       &option_no_colons,        1},
    {(char* const) "list-decoders",    no_argument,       &option_list_decoders,    1},
    {(char* const) "help",             no_argument,       0,                        'h'},
    {(char* const) "chroma-upsampling", required_argument, 0,                     'C'},
    {(char* const) "png-compression-level", required_argument, 0,  OPTION_PNG_COMPRESSION_LEVEL}
};


#define MAX_DECODERS 20

void list_decoders(heif_compression_format format)
{
  const heif_decoder_descriptor* decoders[MAX_DECODERS];
  int n = heif_get_decoder_descriptors(format, decoders, MAX_DECODERS);

  for (int i=0;i<n;i++) {
    const char* id = heif_decoder_descriptor_get_id_name(decoders[i]);
    if (id==nullptr) {
      id = "---";
    }

    std::cout << "- " << id << " = " << heif_decoder_descriptor_get_name(decoders[i]) << "\n";
  }
}


void list_all_decoders()
{
  std::cout << "HEIC decoders:\n";
  list_decoders(heif_compression_HEVC);

  std::cout << "AVIF decoders:\n";
  list_decoders(heif_compression_AV1);

#if WITH_UNCOMPRESSED_CODEC
  std::cout << "uncompressed: yes\n";
#else
  std::cout << "uncompressed: no\n";
#endif
}


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
    int c = getopt_long(argc, argv, "hq:sd:C:", long_options, &option_index);
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
        // fallthrough
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
    }
  }

  if (option_list_decoders) {
    list_all_decoders();
    return 0;
  }

  if (optind + 2 > argc) {
    // Need input and output filenames as additional arguments.
    show_help(argv[0]);
    return 5;
  }

  std::string input_filename(argv[optind++]);
  std::string output_filename(argv[optind++]);
  std::string output_filename_stem;
  std::string output_filename_suffix;

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

  // TODO: when we are reading from named pipes, we probably should not consume any bytes
  // just for file-type checking.
  // TODO: check, whether reading from named pipes works at all.

  std::ifstream istr(input_filename.c_str(), std::ios_base::binary);
  uint8_t magic[12];
  istr.read((char*) magic, 12);

  if (heif_check_jpeg_filetype(magic, 12)) {
    fprintf(stderr, "Input file '%s' is a JPEG image\n", input_filename.c_str());
    return 1;
  }

  enum heif_filetype_result filetype_check = heif_check_filetype(magic, 12);
  if (filetype_check == heif_filetype_no) {
    fprintf(stderr, "Input file is not an HEIF/AVIF file\n");
    return 1;
  }

  if (filetype_check == heif_filetype_yes_unsupported) {
    fprintf(stderr, "Input file is an unsupported HEIF/AVIF file type\n");
    return 1;
  }



  // --- read the HEIF file

  struct heif_context* ctx = heif_context_alloc();
  if (!ctx) {
    fprintf(stderr, "Could not create context object\n");
    return 1;
  }

  ContextReleaser cr(ctx);
  struct heif_error err;
  err = heif_context_read_from_file(ctx, input_filename.c_str(), nullptr);
  if (err.code != 0) {
    std::cerr << "Could not read HEIF/AVIF file: " << err.message << "\n";
    return 1;
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
      filename.assign(s.str());
    }
    else {
      filename.assign(output_filename);
      numbered_output_filename_stem = output_filename_stem;
    }

    struct heif_image_handle* handle;
    err = heif_context_get_image_handle(ctx, image_IDs[idx], &handle);
    if (err.code) {
      std::cerr << "Could not read HEIF/AVIF image " << idx << ": "
                << err.message << "\n";
      return 1;
    }

    int has_alpha = heif_image_handle_has_alpha_channel(handle);
    struct heif_decoding_options* decode_options = heif_decoding_options_alloc();
    encoder->UpdateDecodingOptions(handle, decode_options);

    decode_options->strict_decoding = strict_decoding;
    decode_options->decoder_id = decoder_id;

    if (chroma_upsampling=="nearest-neighbor") {
      decode_options->color_conversion_options.preferred_chroma_upsampling_algorithm = heif_chroma_upsampling_nearest_neighbor;
      decode_options->color_conversion_options.only_use_preferred_chroma_algorithm = true;
    }
    else if (chroma_upsampling=="bilinear") {
      decode_options->color_conversion_options.preferred_chroma_upsampling_algorithm = heif_chroma_upsampling_bilinear;
      decode_options->color_conversion_options.only_use_preferred_chroma_algorithm = true;
    }

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
      std::cerr << "Could not decode image: " << idx << ": "
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

          struct heif_image_handle* depth_handle;
          err = heif_image_handle_get_depth_image_handle(handle, depth_id, &depth_handle);
          if (err.code) {
            heif_image_handle_release(handle);
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
            heif_image_handle_release(handle);
            std::cerr << "Could not decode depth image: " << err.message << "\n";
            return 1;
          }

          std::ostringstream s;
          s << numbered_output_filename_stem;
          s << "-depth.";
          s << output_filename_suffix;

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

          for (heif_item_id auxId: auxIDs) {

            struct heif_image_handle* aux_handle;
            err = heif_image_handle_get_auxiliary_image_handle(handle, auxId, &aux_handle);
            if (err.code) {
              heif_image_handle_release(handle);
              std::cerr << "Could not read auxiliary image\n";
              return 1;
            }

            int aux_bit_depth = heif_image_handle_get_luma_bits_per_pixel(aux_handle);

            struct heif_image* aux_image;
            err = heif_decode_image(aux_handle,
                                    &aux_image,
                                    encoder->colorspace(false),
                                    encoder->chroma(false, aux_bit_depth),
                                    nullptr);
            if (err.code) {
              heif_image_handle_release(aux_handle);
              heif_image_handle_release(handle);
              std::cerr << "Could not decode auxiliary image: " << err.message << "\n";
              return 1;
            }

            const char* auxTypeC = nullptr;
            err = heif_image_handle_get_auxiliary_type(aux_handle, &auxTypeC);
            if (err.code) {
              heif_image_handle_release(aux_handle);
              heif_image_handle_release(handle);
              std::cerr << "Could not get type of auxiliary image: " << err.message << "\n";
              return 1;
            }

            std::string auxType = std::string(auxTypeC);

            heif_image_handle_release_auxiliary_type(aux_handle, &auxTypeC);

            std::ostringstream s;
            s << numbered_output_filename_stem;
            s << "-" + auxType + ".";
            s << output_filename_suffix;

            std::string auxFilename = s.str();

            if (option_no_colons) {
              std::replace(auxFilename.begin(), auxFilename.end(), ':', '_');
            }

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
        if (numMetadata>0) {
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
                heif_image_handle_release(handle);
                std::cerr << "Could not read XMP metadata: " << err.message << "\n";
                return 1;
              }

              // write XMP data to file

              std::string xmp_filename = numbered_output_filename_stem + ".xmp";
              std::ofstream ostr(xmp_filename.c_str());
              ostr.write((char*)xmp.data(), xmpSize);
            }
            else if (option_with_exif && itemtype == "Exif") {
              // read EXIF data to memory array

              size_t exifSize = heif_image_handle_get_metadata_size(handle, ids[n]);
              std::vector<uint8_t> exif(exifSize);
              err = heif_image_handle_get_metadata(handle, ids[n], exif.data());
              if (err.code) {
                heif_image_handle_release(handle);
                std::cerr << "Could not read EXIF metadata: " << err.message << "\n";
                return 1;
              }

              uint32_t offset = 0;
              if (option_skip_exif_offset) {
                if (exifSize<4) {
                  heif_image_handle_release(handle);
                  std::cerr << "Invalid EXIF metadata, it is too small.\n";
                  return 1;
                }

                offset = (exif[0]<<24) | (exif[1]<<16) | (exif[2]<<8) | exif[3];
                offset += 4;
                
                if (offset >= exifSize) {
                  heif_image_handle_release(handle);
                  std::cerr << "Invalid EXIF metadata, offset out of range.\n";
                  return 1;
                }
              }

              // write EXIF data to file

              std::string exif_filename = numbered_output_filename_stem + ".exif";
              std::ofstream ostr(exif_filename.c_str());
              ostr.write((char*)exif.data() + offset, exifSize - offset);
            }
          }
        }
      }

      heif_image_handle_release(handle);
    }

    image_index++;
  }

  return 0;
}
