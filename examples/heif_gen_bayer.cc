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
#include <filesystem>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <libheif/heif.h>
#include <libheif/heif_sequences.h>
#include <libheif/heif_uncompressed.h>

#include "heifio/decoder_png.h"


struct PatternDefinition
{
  std::string name;
  uint16_t width;
  uint16_t height;
  std::vector<heif_uncompressed_component_type> cpat;
};


static const PatternDefinition patterns[] = {
  // RGGB (standard Bayer)
  //   R G
  //   G B
  {
    "rggb", 2, 2,
    {
      heif_uncompressed_component_type_red,
      heif_uncompressed_component_type_green,
      heif_uncompressed_component_type_green,
      heif_uncompressed_component_type_blue,
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
      heif_uncompressed_component_type_Y,
      heif_uncompressed_component_type_green,
      heif_uncompressed_component_type_Y,
      heif_uncompressed_component_type_red,

      heif_uncompressed_component_type_green,
      heif_uncompressed_component_type_Y,
      heif_uncompressed_component_type_blue,
      heif_uncompressed_component_type_Y,

      heif_uncompressed_component_type_Y,
      heif_uncompressed_component_type_blue,
      heif_uncompressed_component_type_Y,
      heif_uncompressed_component_type_green,

      heif_uncompressed_component_type_red,
      heif_uncompressed_component_type_Y,
      heif_uncompressed_component_type_green,
      heif_uncompressed_component_type_Y,
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
      heif_uncompressed_component_type_green,
      heif_uncompressed_component_type_green,
      heif_uncompressed_component_type_red,
      heif_uncompressed_component_type_red,

      heif_uncompressed_component_type_green,
      heif_uncompressed_component_type_green,
      heif_uncompressed_component_type_red,
      heif_uncompressed_component_type_red,

      heif_uncompressed_component_type_blue,
      heif_uncompressed_component_type_blue,
      heif_uncompressed_component_type_green,
      heif_uncompressed_component_type_green,

      heif_uncompressed_component_type_blue,
      heif_uncompressed_component_type_blue,
      heif_uncompressed_component_type_green,
      heif_uncompressed_component_type_green,
    }
  },
};

static constexpr int num_patterns = sizeof(patterns) / sizeof(patterns[0]);


static const PatternDefinition* find_pattern(const char* name)
{
  for (int i = 0; i < num_patterns; i++) {
    if (strcasecmp(patterns[i].name.c_str(), name) == 0) {
      return &patterns[i];
    }
  }
  return nullptr;
}


static std::optional<PatternDefinition> parse_pattern_string(const char* str)
{
  std::string s(str);
  size_t len = s.size();
  if (len != 4 && len != 16) {
    return {};
  }

  uint16_t dim = (len == 4) ? 2 : 4;
  std::vector<heif_uncompressed_component_type> cpat;
  cpat.reserve(len);

  for (char c : s) {
    switch (std::tolower(c)) {
      case 'r': cpat.push_back(heif_uncompressed_component_type_red); break;
      case 'g': cpat.push_back(heif_uncompressed_component_type_green); break;
      case 'b': cpat.push_back(heif_uncompressed_component_type_blue); break;
      default: return {};
    }
  }

  return PatternDefinition{str, dim, dim, std::move(cpat)};
}


static std::vector<std::string> deflate_input_filenames(const std::string& filename_example)
{
  std::regex pattern(R"((.*\D)?(\d+)(\..+)$)");
  std::smatch match;

  if (!std::regex_match(filename_example, match, pattern)) {
    return {filename_example};
  }

  std::string prefix = match[1];

  auto p = std::filesystem::absolute(std::filesystem::path(prefix));
  std::filesystem::path directory = p.parent_path();
  std::string filename_prefix = p.filename().string();
  std::string number = match[2];
  std::string suffix = match[3];

  std::string patternString = filename_prefix + "(\\d+)" + suffix + "$";
  pattern = patternString;

  uint32_t digits = std::numeric_limits<uint32_t>::max();
  uint32_t start = std::numeric_limits<uint32_t>::max();
  uint32_t end = 0;

  for (const auto& dirEntry : std::filesystem::directory_iterator(directory))
  {
    if (dirEntry.is_regular_file()) {
      std::string s{dirEntry.path().filename().string()};

      if (std::regex_match(s, match, pattern)) {
        digits = std::min(digits, (uint32_t)match[1].length());

        uint32_t number = std::stoi(match[1]);
        start = std::min(start, number);
        end = std::max(end, number);
      }
    }
  }

  std::vector<std::string> files;

  for (uint32_t i = start; i <= end; i++)
  {
    std::stringstream sstr;

    sstr << prefix << std::setw(digits) << std::setfill('0') << i << suffix;

    std::filesystem::path p = directory / sstr.str();
    files.emplace_back(p.string());
  }

  return files;
}


static void print_usage()
{
  std::cerr << "Usage: heif-gen-bayer [options] <input.png> <output.heif>\n"
            << "       heif-gen-bayer -S [options] <frame_NNN.png> <output.mp4>\n\n"
            << "Options:\n"
            << "  -h, --help              show this help\n"
            << "  -b, --bit-depth #       output bit depth (default: 8, range: 8-16)\n"
            << "  -p, --pattern <name>    filter array pattern (default: rggb)\n"
            << "  -S, --sequence          sequence mode (expand numbered PNGs)\n"
            << "  -V, --video             use video track handler (vide) instead of pict\n"
            << "      --fps <N>           frames per second (default: 30)\n\n"
            << "Patterns:\n";
  for (int i = 0; i < num_patterns; i++) {
    std::cerr << "  " << patterns[i].name
              << " (" << patterns[i].width << "x" << patterns[i].height << ")"
              << (i == 0 ? "  [default]" : "")
              << "\n";
  }
  std::cerr << "  Or specify a custom R/G/B string of length 4 (2x2) or 16 (4x4), e.g. -p BGGR\n";
}


static struct option long_options[] = {
    {(char* const) "help",      no_argument,       nullptr, 'h'},
    {(char* const) "bit-depth", required_argument, nullptr, 'b'},
    {(char* const) "pattern",   required_argument, nullptr, 'p'},
    {(char* const) "sequence",  no_argument,       nullptr, 'S'},
    {(char* const) "video",     no_argument,       nullptr, 'V'},
    {(char* const) "fps",       required_argument, nullptr, 'f'},
    {nullptr, 0, nullptr, 0}
};


// Create a bayer image from a PNG file. Returns nullptr on error.
// If expected_width/expected_height are non-zero, the PNG must match those dimensions.
static heif_image* create_bayer_image_from_png(const char* png_filename,
                                               const PatternDefinition* pat,
                                               int output_bit_depth,
                                               int expected_width,
                                               int expected_height)
{
  InputImage input_image;
  heif_error err = loadPNG(png_filename, output_bit_depth, &input_image);
  if (err.code != heif_error_Ok) {
    std::cerr << "Cannot load PNG '" << png_filename << "': " << err.message << "\n";
    return nullptr;
  }

  heif_image* src_img = input_image.image.get();

  int width = heif_image_get_primary_width(src_img);
  int height = heif_image_get_primary_height(src_img);

  if (expected_width != 0 && (width != expected_width || height != expected_height)) {
    std::cerr << "Frame '" << png_filename << "' has dimensions " << width << "x" << height
              << " but expected " << expected_width << "x" << expected_height << "\n";
    return nullptr;
  }

  if (width % pat->width != 0 || height % pat->height != 0) {
    std::cerr << "Image dimensions must be multiples of the pattern size ("
              << pat->width << "x" << pat->height << "). Got "
              << width << "x" << height << "\n";
    return nullptr;
  }

  // Get source RGB data
  int src_stride;
  const uint8_t* src_data = heif_image_get_plane_readonly(src_img, heif_channel_interleaved, &src_stride);
  if (!src_data) {
    std::cerr << "Failed to get interleaved RGB plane from PNG.\n";
    return nullptr;
  }

  // Create Bayer image
  heif_image* bayer_img = nullptr;
  err = heif_image_create(width, height,
                          heif_colorspace_filter_array,
                          heif_chroma_monochrome,
                          &bayer_img);
  if (err.code != heif_error_Ok) {
    std::cerr << "Cannot create image: " << err.message << "\n";
    return nullptr;
  }

  err = heif_image_add_plane(bayer_img, heif_channel_filter_array, width, height, output_bit_depth);
  if (err.code != heif_error_Ok) {
    std::cerr << "Cannot add plane: " << err.message << "\n";
    heif_image_release(bayer_img);
    return nullptr;
  }

  // Convert RGB to filter array using the selected pattern
  if (output_bit_depth == 8) {
    int dst_stride;
    uint8_t* dst_data = heif_image_get_plane(bayer_img, heif_channel_filter_array, &dst_stride);

    for (int y = 0; y < height; y++) {
      const uint8_t* src_row = src_data + y * src_stride;
      uint8_t* dst_row = dst_data + y * dst_stride;

      for (int x = 0; x < width; x++) {
        uint8_t r = src_row[x * 3 + 0];
        uint8_t g = src_row[x * 3 + 1];
        uint8_t b = src_row[x * 3 + 2];

        int px = x % pat->width;
        int py = y % pat->height;
        auto comp_type = pat->cpat[py * pat->width + px];

        switch (comp_type) {
          case heif_uncompressed_component_type_red:   dst_row[x] = r; break;
          case heif_uncompressed_component_type_green: dst_row[x] = g; break;
          case heif_uncompressed_component_type_blue:  dst_row[x] = b; break;
          case heif_uncompressed_component_type_Y: dst_row[x] = static_cast<uint8_t>((r + g + b) / 3); break;
          default:
            assert(false);
        }
      }
    }
  }
  else {
    int dst_stride;
    uint8_t* dst_raw = heif_image_get_plane(bayer_img, heif_channel_filter_array, &dst_stride);
    auto* dst_data = reinterpret_cast<uint16_t*>(dst_raw);
    int dst_stride16 = dst_stride / 2;

    for (int y = 0; y < height; y++) {
      const uint8_t* src_row = src_data + y * src_stride;
      uint16_t* dst_row = dst_data + y * dst_stride16;

      for (int x = 0; x < width; x++) {
        // Source is little-endian uint16_t per component, 3 components per pixel
        uint16_t r = src_row[x * 6 + 0] | (src_row[x * 6 + 1] << 8);
        uint16_t g = src_row[x * 6 + 2] | (src_row[x * 6 + 3] << 8);
        uint16_t b = src_row[x * 6 + 4] | (src_row[x * 6 + 5] << 8);

        int px = x % pat->width;
        int py = y % pat->height;
        auto comp_type = pat->cpat[py * pat->width + px];

        switch (comp_type) {
          case heif_uncompressed_component_type_red:   dst_row[x] = r; break;
          case heif_uncompressed_component_type_green: dst_row[x] = g; break;
          case heif_uncompressed_component_type_blue:  dst_row[x] = b; break;
          case heif_uncompressed_component_type_Y: dst_row[x] = static_cast<uint16_t>((r + g + b) / 3); break;
          default:
            assert(false);
        }
      }
    }
  }

  // Build heif_bayer_pattern_pixel array from component types.
  // The component_index values here are the component types themselves — the encoder
  // will resolve them to proper cmpd indices when writing the cpat box.
  std::vector<heif_bayer_pattern_pixel> bayer_pixels(pat->cpat.size());
  for (size_t i = 0; i < pat->cpat.size(); i++) {
    bayer_pixels[i].component_index = static_cast<uint16_t>(pat->cpat[i]);
    bayer_pixels[i].component_gain = 1.0f;
  }

  // Set Bayer pattern metadata
  err = heif_image_set_bayer_pattern(bayer_img,
                                     pat->width, pat->height,
                                     bayer_pixels.data());
  if (err.code != heif_error_Ok) {
    std::cerr << "Cannot set Bayer pattern: " << err.message << "\n";
    heif_image_release(bayer_img);
    return nullptr;
  }

  return bayer_img;
}


static int encode_sequence(const std::vector<std::string>& filenames,
                           const PatternDefinition* pat,
                           int output_bit_depth,
                           int fps,
                           bool use_video_handler,
                           const char* output_filename)
{
  heif_context* ctx = heif_context_alloc();

  heif_encoder* encoder = nullptr;
  heif_error err = heif_context_get_encoder_for_format(ctx, heif_compression_uncompressed, &encoder);
  if (err.code != heif_error_Ok) {
    std::cerr << "Cannot get uncompressed encoder: " << err.message << "\n";
    heif_context_free(ctx);
    return 1;
  }

  heif_context_set_sequence_timescale(ctx, fps);

  heif_sequence_encoding_options* enc_options = heif_sequence_encoding_options_alloc();
  heif_track* track = nullptr;
  int first_width = 0, first_height = 0;

  for (size_t i = 0; i < filenames.size(); i++) {
    heif_image* bayer_img = create_bayer_image_from_png(filenames[i].c_str(), pat,
                                                        output_bit_depth,
                                                        first_width, first_height);
    if (!bayer_img) {
      heif_sequence_encoding_options_release(enc_options);
      if (track) heif_track_release(track);
      heif_encoder_release(encoder);
      heif_context_free(ctx);
      return 1;
    }

    if (i == 0) {
      first_width = heif_image_get_primary_width(bayer_img);
      first_height = heif_image_get_primary_height(bayer_img);

      heif_track_type track_type = use_video_handler ? heif_track_type_video
                                                     : heif_track_type_image_sequence;

      heif_track_options* track_options = heif_track_options_alloc();
      heif_track_options_set_timescale(track_options, fps);

      err = heif_context_add_visual_sequence_track(ctx,
                                                   static_cast<uint16_t>(first_width),
                                                   static_cast<uint16_t>(first_height),
                                                   track_type,
                                                   track_options,
                                                   enc_options,
                                                   &track);
      heif_track_options_release(track_options);

      if (err.code != heif_error_Ok) {
        std::cerr << "Cannot create sequence track: " << err.message << "\n";
        heif_image_release(bayer_img);
        heif_sequence_encoding_options_release(enc_options);
        heif_encoder_release(encoder);
        heif_context_free(ctx);
        return 1;
      }
    }

    heif_image_set_duration(bayer_img, 1);

    err = heif_track_encode_sequence_image(track, bayer_img, encoder, enc_options);
    heif_image_release(bayer_img);

    if (err.code != heif_error_Ok) {
      std::cerr << "Cannot encode frame " << i << ": " << err.message << "\n";
      heif_sequence_encoding_options_release(enc_options);
      heif_track_release(track);
      heif_encoder_release(encoder);
      heif_context_free(ctx);
      return 1;
    }

    std::cout << "Encoded frame " << (i + 1) << "/" << filenames.size()
              << ": " << filenames[i] << "\n";
  }

  err = heif_track_encode_end_of_sequence(track, encoder);
  if (err.code != heif_error_Ok) {
    std::cerr << "Cannot end sequence: " << err.message << "\n";
  }

  heif_sequence_encoding_options_release(enc_options);
  heif_track_release(track);
  heif_encoder_release(encoder);

  err = heif_context_write_to_file(ctx, output_filename);
  if (err.code != heif_error_Ok) {
    std::cerr << "Cannot write file: " << err.message << "\n";
    heif_context_free(ctx);
    return 1;
  }

  heif_context_free(ctx);

  std::cout << "Wrote " << filenames.size() << " frame(s) to " << output_filename << "\n";
  return 0;
}


int main(int argc, char* argv[])
{
  PatternDefinition custom_pattern;
  const PatternDefinition* pat = &patterns[0]; // default: RGGB
  int output_bit_depth = 8;
  bool sequence_mode = false;
  bool use_video_handler = false;
  int fps = 30;

  while (true) {
    int option_index = 0;
    int c = getopt_long(argc, argv, "hb:p:SV", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
      case 'h':
        print_usage();
        return 0;

      case 'b':
        output_bit_depth = std::atoi(optarg);
        if (output_bit_depth < 8 || output_bit_depth > 16) {
          std::cerr << "Invalid bit depth: " << optarg << " (must be 8-16)\n";
          return 1;
        }
        break;

      case 'p':
        pat = find_pattern(optarg);
        if (!pat) {
          auto custom = parse_pattern_string(optarg);
          if (custom) {
            custom_pattern = std::move(*custom);
            pat = &custom_pattern;
          }
          else {
            std::cerr << "Unknown pattern: " << optarg << "\n";
            print_usage();
            return 1;
          }
        }
        break;

      case 'S':
        sequence_mode = true;
        break;

      case 'V':
        use_video_handler = true;
        break;

      case 'f': // --fps
        fps = std::atoi(optarg);
        if (fps <= 0) {
          std::cerr << "Invalid FPS value: " << optarg << "\n";
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

  if (sequence_mode) {
    // --- Sequence mode: expand numbered filenames and encode as sequence

    std::vector<std::string> filenames = deflate_input_filenames(input_filename);
    if (filenames.empty()) {
      std::cerr << "No input files found matching pattern: " << input_filename << "\n";
      return 1;
    }

    std::cout << "Found " << filenames.size() << " frame(s), encoding at " << fps << " fps\n";
    return encode_sequence(filenames, pat, output_bit_depth, fps, use_video_handler, output_filename);
  }

  // --- Single image mode

  heif_image* bayer_img = create_bayer_image_from_png(input_filename, pat, output_bit_depth, 0, 0);
  if (!bayer_img) {
    return 1;
  }

  // --- Encode

  heif_context* ctx = heif_context_alloc();

  heif_encoder* encoder = nullptr;
  heif_error err = heif_context_get_encoder_for_format(ctx, heif_compression_uncompressed, &encoder);
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
