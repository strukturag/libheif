/*
  libheif example of GIMI support.

  MIT License

  Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>
  Copyright (c) 2026 Devon Sookhoo <devonsookhoo14@gmail.com>

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

#include "common.h"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <libheif/heif.h>
#include <libheif/heif_sequences.h>
#include <libheif/heif_tai_timestamps.h>

heif_context* build_heif_context() {
  heif_context* ctx = heif_context_alloc();
  return ctx;
}

static void write_gimi_image(const char* output_name) {
  // TODO
}

static void read_gimi_image(const char* input_name) {
  // TODO
}

void write_gimi_video(const char* output_name) {
  heif_context* ctx = build_heif_context();
  const uint16_t width = 640;
  const uint16_t height = 512;
  heif_compression_format compression_format = heif_compression_uncompressed;
  const int bit_depth = 16;
  const int num_frames = 10;
  const float frame_rate = 30.0;
  const uint32_t track_timescale = 90000;

  heif_encoder* encoder = nullptr;
  heif_error err;

  // Verify Codec Support
  if (!heif_have_encoder_for_format(compression_format)) {
    std::string codec_name = "TODO";
    std::cerr << "Error: Support for the " << codec_name << " codec is not available."
              << std::endl;
    return;
  }

  // Get Encoder
  err = heif_context_get_encoder_for_format(ctx, compression_format, &encoder);
  if (err.code != heif_error_Ok || !encoder) {
    std::cerr << "Error: Failed to get encoder for format." << std::endl;
    heif_context_free(ctx);
    return;
  }
  heif_encoder_set_lossless(encoder, true);

  // Security Markings
  // TODO

  // Add Brands
  heif_context_add_compatible_brand(ctx, heif_fourcc_to_brand("geo1"));
  heif_context_add_compatible_brand(ctx, heif_fourcc_to_brand("sm01"));

  // Setup Track/Track Options
  heif_track_options* track_options = heif_track_options_alloc();
  // heif_track_options_set_interleaved_sample_aux_infos(track_options,1);

  const heif_sample_aux_info_presence aux_info_presence = heif_sample_aux_info_presence_mandatory;

  // Timestamp
  heif_tai_clock_info* tai_clock_info = heif_tai_clock_info_alloc();
  heif_track_options_enable_sample_tai_timestamps(track_options, tai_clock_info, aux_info_presence);
  heif_track_options_set_timescale(track_options, track_timescale);

  // Content IDs
  heif_track_options_enable_sample_gimi_content_ids(track_options, aux_info_presence);
  const char* gimi_track_id = "TODO";
  heif_track_options_set_gimi_track_id(track_options, gimi_track_id);

  // Add video track to the HEIF context
  heif_sequence_encoding_options* encoding_options = heif_sequence_encoding_options_alloc();
  heif_track* track = nullptr;
  err = heif_context_add_visual_sequence_track(
      ctx, width, height, heif_track_type_video, track_options,
      encoding_options, &track);
  if (err.code != heif_error_Ok || !track) {
    std::cerr << "Error adding visual sequence track: " << err.message
              << std::endl;
    heif_track_options_release(track_options);
    heif_sequence_encoding_options_release(encoding_options);
    heif_encoder_release(encoder);
    heif_context_free(ctx);
    return;
  }

  // Loop through and write frames to the file
  for (int i = 0; i < num_frames; i++) {
    heif_image* image;
    heif_image_create(width, height, heif_colorspace_monochrome,
                      heif_chroma_monochrome, &image);
    heif_image_add_plane(image, heif_channel_Y, width, height, bit_depth);
    uint32_t duration = static_cast<uint32_t>(std::round(static_cast<double>(track_timescale) / frame_rate));
    heif_image_set_duration(image, duration);

    // Fill 16-bit Plane Data
    size_t stride;
    uint8_t* plane_ptr = heif_image_get_plane2(image, heif_channel_Y, &stride);
    uint16_t* pixels = reinterpret_cast<uint16_t*>(plane_ptr);

    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        pixels[y * (static_cast<uint16_t>(stride) / 2) + x] = 0x88;
      }
    }

    // 4. Add SAI Metadata
    std::string content_id = "frame_" + std::to_string(i);

    heif_image_set_gimi_sample_content_id(image, content_id.c_str());

    heif_tai_timestamp_packet* tai = heif_tai_timestamp_packet_alloc();
    tai->tai_timestamp = 2346923647982 + (i * 33333);
    tai->synchronization_state = 1;
    heif_image_set_tai_timestamp(image, tai);

    // Encode Frame
    err = heif_track_encode_sequence_image(track, image, encoder,
                                           encoding_options);
    if (err.code != heif_error_Ok) {
      std::cerr << "Error encoding sequence image " << i << ": " << err.message
                << std::endl;
      heif_image_release(image);
      break; // Exit loop on error
    }
    heif_image_release(image);
  }
  heif_track_encode_end_of_sequence(track, encoder);

  // Finalize
  err = heif_context_write_to_file(ctx, output_name);
  if (err.code != heif_error_Ok) {
    std::cerr << "Write failed: " << err.message << std::endl;
  }
  else {
    std::cout << "Successfully wrote " << output_name << std::endl;
  }

  heif_track_options_release(track_options);
  heif_sequence_encoding_options_release(encoding_options);
  heif_encoder_release(encoder);
  heif_context_free(ctx);
  return;
}

void read_gimi_video(const char* input_name) {
  heif_error err;
  heif_context* ctx = heif_context_alloc();
  err = heif_context_read_from_file(ctx, input_name, nullptr);
  if (err.code != heif_error_Ok) {
    std::cerr << "Reader Error: Could not read file: " << err.message
              << std::endl;
    heif_context_free(ctx);
    return;
  }

  int has_sequence = heif_context_has_sequence(ctx);
  if (has_sequence != 1) {
    std::cerr << "Reader Error: File does not contain image sequence."
              << err.message << std::endl;
    heif_context_free(ctx);
    return;
  }

  int num_tracks = heif_context_number_of_sequence_tracks(ctx);
  std::cout << "Reader detected " << num_tracks << " sequence tracks in file."
            << std::endl;

  uint32_t track_id_array[num_tracks];
  heif_context_get_track_ids(ctx, track_id_array);

  std::cout << "Reader detected track id's: ";
  for (uint32_t value : track_id_array) {
    std::cout << value << ", ";
  }
  std::cout << std::endl;

  heif_track* track = heif_context_get_track(ctx, track_id_array[0]);
  heif_track_type track_type = heif_track_get_track_handler_type(track);

  std::cout << "Reader detected track type " << heif_examples::fourcc_to_string(track_type)
            << " for track " << track_id_array[0] << std::endl;

  uint16_t width, height;
  err = heif_track_get_image_resolution(track, &width, &height);
  std::cout << "Reader detected track resolution " << width << "x" << height
            << " for track " << track_id_array[0] << std::endl;

  heif_image* image = nullptr;
  heif_decoding_options* decoding_options = heif_decoding_options_alloc();
  err = heif_track_decode_next_image(track, &image, heif_colorspace_monochrome,
                                     heif_chroma_monochrome, decoding_options);

  if (err.code != heif_error_Ok || !image) {
    std::cerr << "Reader Error: Could not decode the next image sample: "
              << err.message << std::endl;
    heif_decoding_options_free(decoding_options);
    heif_context_free(ctx);
    return;
  }

  std::cout << "Reader detected sample duration "
            << heif_image_get_duration(image) << std::endl;

  std::cout << "Reader detected sample bit depth of "
            << heif_image_get_bits_per_pixel(image, heif_channel_Y)
            << std::endl;

  const char* content_id = heif_image_get_gimi_sample_content_id(image);
  if (content_id) {
    std::cout << "Reader detected GIMI content ID: " << content_id << std::endl;
  }
  else {
    std::cout << "Reader did not detect GIMI content ID." << std::endl;
  }

  heif_tai_timestamp_packet* timestamp;
  err = heif_image_get_tai_timestamp(image, &timestamp);
  if (err.code != heif_error_Ok) {
    std::cerr << "Reader Error: Could not read TAI Timetamp: " << err.message
              << std::endl;
    heif_context_free(ctx);
    return;
  }

  if (timestamp) {
    std::cout << "Reader detected TAI timestamp metadata:" << std::endl;
    std::cout << "  TAI timestamp: " << timestamp->tai_timestamp << std::endl;
    std::cout << "  synchronization state: "
              << (int)timestamp->synchronization_state << std::endl;
    std::cout << "  timestamp generation failure: "
              << (int)timestamp->timestamp_generation_failure << std::endl;
    std::cout << "  timestamp is modified: "
              << (int)timestamp->timestamp_is_modified << std::endl;
  }
  else {
    std::cout << "  no TAI timestamp found" << std::endl;
  }

  heif_decoding_options_free(decoding_options);
  heif_image_release(image);
  heif_context_free(ctx);
}

int main(int argc, char* argv[]) {

  // Image
  const char* image_name = "gimi_example.heif";
  write_gimi_image(image_name);
  read_gimi_image(image_name);

  // Video
  const char* video_name = "gimi_example.mp4";
  write_gimi_video(video_name);
  read_gimi_video(video_name);
}