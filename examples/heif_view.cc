/*
  libheif example application.

  MIT License

  Copyright (c) 2025 Dirk Farin <dirk.farin@gmail.com>

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
#include "libheif/heif_experimental.h"

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
#include <memory>

#include <libheif/heif.h>

#include "common.h"
#include "sdl.hh"

#if defined(_MSC_VER)
#include "getopt.h"
#endif

#define UNUSED(x) (void)x


static void show_help(const char* argv0)
{
  std::cerr << " " << argv0 << "  libheif version: " << heif_get_version() << "\n"
            << "---------------------------------------\n"
               "Usage: " << argv0 << " [options]  <input-file>\n"
            << "\n"
               "Options:\n"
               "  -h, --help                     show help\n"
               "  -v, --version                  show version\n"
               "      --list-decoders            list all available decoders (built-in and plugins)\n"
               "  -d, --decoder ID               use a specific decoder (see --list-decoders)\n"
               "      --speedup FACTOR           increase playback speed by FACTOR\n"
               "      --show-sai                 show sample auxiliary information\n"
               "      --show-frame-duration      show each frame duration in milliseconds\n"
               "      --show-all                 show all extra information";
}


class ContextReleaser {
public:
  ContextReleaser(struct heif_context* ctx) : ctx_(ctx) {}

  ~ContextReleaser()
  {
    heif_context_free(ctx_);
  }

private:
  struct heif_context* ctx_;
};


int option_list_decoders = 0;
double option_speedup = 1.0;
bool option_show_sai = false;
bool option_show_frame_duration = false;

const int OPTION_SPEEDUP = 1000;
const int OPTION_SHOW_SAI = 1001;
const int OPTION_SHOW_FRAME_DURATION = 1002;
const int OPTION_SHOW_ALL = 1003;

static struct option long_options[] = {
    {(char* const) "decoder",             required_argument, 0,                     'd'},
    {(char* const) "list-decoders",       no_argument,       &option_list_decoders, 1},
    {(char* const) "help",                no_argument,       0,                     'h'},
    {(char* const) "version",             no_argument,       0,                     'v'},
    {(char* const) "speedup",             required_argument, 0,                     OPTION_SPEEDUP},
    {(char* const) "show-sai",            no_argument,       0,                     OPTION_SHOW_SAI},
    {(char* const) "show-frame-duration", no_argument,       0,                     OPTION_SHOW_FRAME_DURATION},
    {(char* const) "show-all",            no_argument,       0,                     OPTION_SHOW_ALL},
    {nullptr,                             no_argument,       nullptr,               0}
};


class LibHeifInitializer {
public:
  LibHeifInitializer() { heif_init(nullptr); }

  ~LibHeifInitializer() { heif_deinit(); }
};


int main(int argc, char** argv)
{
  // This takes care of initializing libheif and also deinitializing it at the end to free all resources.
  LibHeifInitializer initializer;

  const char* decoder_id = nullptr;
  bool show_frame_number = false;

  while (true) {
    int option_index = 0;
    int c = getopt_long(argc, argv, "hvd:", long_options, &option_index);
    if (c == -1) {
      break;
    }

    switch (c) {
      case 'd':
        decoder_id = optarg;
        break;
      case 'h':
        show_help(argv[0]);
        return 0;
      case 'v':
        show_version();
        return 0;
      case OPTION_SPEEDUP:
        option_speedup = atof(optarg);
        if (option_speedup <= 0) {
          std::cerr << "Speedup must be positive.\n";
          return 5;
        }
        break;
      case OPTION_SHOW_SAI:
        option_show_sai = true;
        show_frame_number = true;
        break;
      case OPTION_SHOW_FRAME_DURATION:
        option_show_frame_duration = true;
        show_frame_number = true;
        break;
      case OPTION_SHOW_ALL:
        option_show_sai = true;
        option_show_frame_duration = true;
        show_frame_number = true;
        break;
    }
  }

  if (option_list_decoders) {
    list_all_decoders();
    return 0;
  }

  if (optind + 1 != argc) {
    // Need exactly one input filename as additional argument.
    show_help(argv[0]);
    return 5;
  }

  std::string input_filename(argv[optind++]);


  // --- check whether input is a supported HEIF file

  if (int ret = check_for_valid_input_HEIF_file(input_filename)) {
    return ret;
  }

  // --- open the HEIF file

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


  // --- error if file contains no image sequence

  if (!heif_context_has_sequence(ctx)) {
    std::cerr << "File contains no image sequence\n";
    return 1;
  }


  // --- get visual track

  struct heif_track* track = heif_context_get_track(ctx, 0);

  uint16_t w, h;
  heif_track_get_image_resolution(track, &w, &h);


  // --- open output window

  SDL_YUV_Display sdlWindow;
  bool success = sdlWindow.init(w, h, SDL_YUV_Display::SDL_CHROMA_420);
  if (!success) {
    std::cerr << "Cannot open output window\n";
    return 10;
  }

  std::unique_ptr<heif_decoding_options, void (*)(heif_decoding_options*)> decode_options(heif_decoding_options_alloc(), heif_decoding_options_free);
  decode_options->convert_hdr_to_8bit = true;
  decode_options->decoder_id = decoder_id;


  // --- decoding loop

  for (int frameNr = 1;; frameNr++) {
    heif_image* out_image = nullptr;

    // --- decode next sequence image

    err = heif_track_decode_next_image(track, &out_image,
                                       heif_colorspace_YCbCr, // TODO: find best format
                                       heif_chroma_420,
                                       decode_options.get());
    if (err.code == heif_error_End_of_sequence) {
      break;
    }
    else if (err.code) {
      std::cerr << err.message << "\n";
      return 1;
    }


    // --- display image

    size_t stride_Y, stride_Cb, stride_Cr;
    const uint8_t* p_Y = heif_image_get_plane_readonly2(out_image, heif_channel_Y, &stride_Y);
    const uint8_t* p_Cb = heif_image_get_plane_readonly2(out_image, heif_channel_Cb, &stride_Cb);
    const uint8_t* p_Cr = heif_image_get_plane_readonly2(out_image, heif_channel_Cr, &stride_Cr);

    sdlWindow.display(p_Y, p_Cb, p_Cr, stride_Y, stride_Cb);

    if (show_frame_number) {
      std::cout << "--- frame " << frameNr << "\n";
    }

    if (option_show_sai) {
      const char* contentID = heif_image_get_gimi_sample_content_id(out_image);
      if (contentID) {
        std::cout << "GIMI content id: " << contentID << "\n";
        heif_string_release(contentID);
      }

      if (heif_image_has_tai_timestamp(out_image)) {
        struct heif_tai_timestamp_packet timestamp;
        timestamp.version = 1;
        heif_image_get_tai_timestamp(out_image, &timestamp);
        std::cout << "TAI timestamp: " << timestamp.tai_timestamp << "\n";
      }
    }

    // --- wait for image duration

    uint32_t duration = heif_image_get_sample_duration(out_image);
    uint64_t timescale = heif_track_get_timescale(track);
    uint64_t duration_ms = duration * 1000 / timescale;

    if (option_show_frame_duration) {
      std::cout << "sample duration " << heif_image_get_sample_duration(out_image) << " = " << duration_ms << " ms\n";
    }

    static const uint64_t start_time = SDL_GetTicks64();
    static uint64_t frame_end_time = 0;

    frame_end_time += duration_ms / option_speedup;

    uint64_t now_time = SDL_GetTicks64();
    uint64_t elapsed_time = (now_time - start_time);
    if (elapsed_time < frame_end_time) {
      SDL_Delay(frame_end_time - elapsed_time);
    }

    heif_image_release(out_image);
  }

  sdlWindow.close();

  heif_track_release(track);

  return 0;
}
