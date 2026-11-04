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
#include <libheif/heif_items.h>
#include <libheif/heif_sequences.h>
#include <libheif/heif_tai_timestamps.h>
#include <libheif/heif_uncompressed.h>
#include <random> // For generating Content IDs

// Global Variables
heif_error err;

std::string security_markings = R"(
<?xml version="1.0" encoding="utf-8"?>
<?xml-model href="../ISM/Schematron/ISMCAT/ISMCAT_XML.sch" type="application/xml"
schematypens="http://purl.oclc.org/dsdl/schematron"?>
<?xml-model href="../ISM/Schematron/ISM/ISM_XML.sch" type="application/xml"
schematypens="http://purl.oclc.org/dsdl/schematron"?>
<GIMISecurity xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns="urn:us:mil:nga:stnd:0076:ism" xmlns:ism="urn:us:gov:ic:ism" xmlns:gimi="urn:us:mil:nga:stnd:0076:ism" xmlns:arh="urn:us:gov:ic:arh" xsi:schemaLocation="urn:us:mil:nga:stnd:0076:ism NGA.STND.0076_Security-V1.xsd" ism:DESVersion="202405" ism:ISMCATCESVersion="202405" GIMISecVer="1">
	<File>
		<arh:Security ism:compliesWith="USGov USIC" ism:resourceElement="true" ism:createDate="2006-05-04" ism:classification="U7" ism:ownerProducer="USA" >
			<ism:NoticeList ism:classification="U1" ism:ownerProducer="USA">
				<ism:Notice ism:classification="U1" ism:ownerProducer="USA" ism:NoticeType="DS">
					<ism:NoticeText ism:classification="U1" ism:ownerProducer="USA">Distribution authorized to DoD, IAW 10 U.S.C. §§130 &amp; 455. Release authorized to U.S. DoD contractors IAW 48 C.F.R. §252.245-7000. Refer other requests to: Headquarters, NGA, ATTN: Disclosure and Release Office, Mail Stop S81-IA, 7500 GEOINT Drive, Springfield, VA 22150 or by email at NDRO@nga.mil. Destroy IAW DoDD 5200.01 Vol 4. Removal of this caveat is prohibited; the caveat must be retained regardless of classification. Sharing LIMDIS material beyond USG-authorized users requires NGA-originator approval.</ism:NoticeText>
				</ism:Notice>
			</ism:NoticeList>
		</arh:Security>
		<Content id="guide://999999/ee4ea7a9-27a1-522f-9e6d-182618b2609e" ism:classification="U1" ism:ownerProducer="USA" ism:nonICmarkings="DS"/>
		<Content id=" guide://999999/e03d1743-5664-586e-916f-9fa731d0315f" ism:classification="U3" ism:ownerProducer="USA" />
		<Content id=" guide://999999/60f00d81-52c3-5c3c-9258-5f0e07a63076" ism:classification="U1" ism:ownerProducer="USA" />
		<Content id=" guide://999999/8b40ebf8-c278-5d7a-88c6-95aa89b61deb" ism:classification="U7" ism:ownerProducer="USA" />
		<Content id=" guide://999999/d83b7576-4fc0-59e3-9303-9d1a9830c0ed" ism:classification="U5" ism:ownerProducer="USA" />
		<Content id=" guide://999999/2245b213-9d44-5c8f-8124-645df7a65110" ism:classification="U7" ism:ownerProducer="USA" />
	</File>
</GIMISecurity>
<!--All classification marks in this example are for illustrative purposes only,-->
<!--there are no actual classified data contained in this example-->
)";

static void add_brands(heif_context* ctx) {
  heif_context_add_compatible_brand(ctx, heif_fourcc_to_brand("geo1"));
  heif_context_add_compatible_brand(ctx, heif_fourcc_to_brand("sm01"));
  heif_context_add_compatible_brand(ctx, heif_fourcc_to_brand("unif"));
}

static heif_item_id add_security_markings(heif_context* ctx) {
  heif_item_id security_markings_id;
  const void* data = (const void*)security_markings.c_str();
  int size = static_cast<int>(security_markings.length());
  heif_context_add_mime_item(ctx,
                             "application/x.fake-dni-arh+xml",
                             heif_metadata_compression_off,
                             data,
                             size,
                             &security_markings_id);

  // Optional
  heif_item_set_item_name(ctx, security_markings_id, "Fake Security Markings");

  return security_markings_id;
}

static heif_context* build_heif_context() {
  heif_context* ctx = heif_context_alloc();
  return ctx;
}

static heif_track* build_heif_track(heif_context* ctx, heif_track_options* track_options, const uint32_t num_frames) {

  heif_error err;
  const uint16_t width = 640;
  const uint16_t height = 512;
  heif_compression_format compression_format = heif_compression_uncompressed;
  const int bit_depth = 16;
  const float frame_rate = 30.0;
  const uint32_t track_timescale = 90000;
  heif_track* track = nullptr;
  heif_sequence_encoding_options* encoding_options = heif_sequence_encoding_options_alloc();

  // Verify Codec Support
  if (!heif_have_encoder_for_format(compression_format)) {
    std::string codec_name = "TODO";
    std::cerr << "Error: Support for the " << codec_name << " codec is not available."
              << std::endl;
    return nullptr;
  }

  // Get Encoder
  heif_encoder* encoder = nullptr;
  err = heif_context_get_encoder_for_format(ctx, compression_format, &encoder);
  if (err.code != heif_error_Ok || !encoder) {
    std::cerr << "Error: Failed to get encoder for format." << std::endl;
    heif_context_free(ctx);
    return nullptr;
  }
  heif_encoder_set_lossless(encoder, true);

  err = heif_context_add_visual_sequence_track(ctx, width, height, heif_track_type_video, track_options, encoding_options, &track);
  if (err.code != heif_error_Ok || !track) {
    std::cerr << "Error adding visual sequence track: " << err.message
              << std::endl;
    heif_track_options_release(track_options);
    heif_sequence_encoding_options_release(encoding_options);
    heif_encoder_release(encoder);
    heif_context_free(ctx);
    return nullptr;
  }

  heif_track_options_set_timescale(track_options, track_timescale);

  // Loop through and write frames to the file
  for (uint32_t i = 0; i < num_frames; i++) {
    heif_image* image;
    heif_image_create(width, height, heif_colorspace_monochrome, heif_chroma_monochrome, &image);
    heif_image_add_plane(image, heif_channel_Y, width, height, bit_depth);
    uint32_t duration = static_cast<uint32_t>(std::round(static_cast<double>(track_timescale) / frame_rate));
    heif_image_set_duration(image, duration);

    // Fill 16-bit Plane Data
    size_t stride;
    uint8_t* plane_ptr = heif_image_get_plane2(image, heif_channel_Y, &stride);
    uint16_t* pixels = reinterpret_cast<uint16_t*>(plane_ptr);

    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        pixels[y * (static_cast<uint16_t>(stride) / 2) + x] = 0x8899;
      }
    }

    // Sample Content ID
    const char* content_id = "TODO";
    heif_image_set_gimi_sample_content_id(image, content_id);

    // Sample Timestamp
    heif_tai_timestamp_packet* tai = heif_tai_timestamp_packet_alloc();
    {
      tai->version = 1;
      tai->tai_timestamp = 2346923647982 + (i * 33333);
      tai->synchronization_state = 1;
      tai->timestamp_generation_failure = 0;
      tai->timestamp_is_modified = 0;
    }
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

  return track;
}

static heif_item_id add_image(heif_context* ctx) {
  heif_error err;
  heif_image* img;
  heif_image_handle* handle;
  heif_encoder* encoder;

  // Create Image
  const int width = 64;
  const int height = 32;
  const int bit_depth = 8;
  heif_compression_format compression = heif_compression_HEVC;
  heif_colorspace colorspace = heif_colorspace_RGB;
  heif_channel channel = heif_channel_interleaved;
  heif_chroma chroma = heif_chroma_interleaved_RGB;
  heif_image_create(width, height, colorspace, chroma, &img);
  heif_image_add_plane(img, channel, width, height, bit_depth);

  // Set Pixels
  int stride;
  uint8_t* data = heif_image_get_plane(img, channel, &stride);
  int vertical_middle = width / 2;
  int horizontal_middle = height / 2;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      if (x < vertical_middle && y < horizontal_middle) {
        // Top Left
        data[y * stride + x * 3 + 0] = 0xFF; // R
        data[y * stride + x * 3 + 1] = 0x01; // G
        data[y * stride + x * 3 + 2] = 0x02; // B
      }
      else if (x >= vertical_middle && y < horizontal_middle) {
        // Top Right
        data[y * stride + x * 3 + 0] = 0x03; // R
        data[y * stride + x * 3 + 1] = 0xFF; // G
        data[y * stride + x * 3 + 2] = 0x04; // B
      }
      else if (x < vertical_middle && y >= horizontal_middle) {
        // Bottom Left
        data[y * stride + x * 3 + 0] = 0x05; // R
        data[y * stride + x * 3 + 1] = 0x06; // G
        data[y * stride + x * 3 + 2] = 0xFF; // B
      }
      else {
        // Bottom Right
        data[y * stride + x * 3 + 0] = 0x88; // R
        data[y * stride + x * 3 + 1] = 0x88; // G
        data[y * stride + x * 3 + 2] = 0x88; // B
      }
    }
  }

  err = heif_context_get_encoder_for_format(ctx, compression, &encoder);
  err = heif_context_encode_image(ctx, img, encoder, nullptr, &handle);

  heif_item_id image_id = heif_image_handle_get_item_id(handle);
  return image_id;
}

void add_timestamp(heif_context* ctx, heif_item_id id) {

  // Clock Info
  heif_tai_clock_info* clock = heif_tai_clock_info_alloc();
  heif_item_set_property_tai_clock_info(ctx, id, clock, nullptr);

  // TAI Timestamp
  heif_tai_timestamp_packet* timestamp = heif_tai_timestamp_packet_alloc();
  heif_item_set_property_tai_timestamp(ctx, id, timestamp, nullptr);

  heif_tai_clock_info_release(clock);
  heif_tai_timestamp_packet_release(timestamp);
}

std::string uuid_v4() {
  // This function was generated by AI
  // Returns a random (version 4, RFC 4122 variant) UUID, e.g.
  // "3b241101-e2bb-4255-8caf-4136c566a962". Thread-safe.
  static thread_local std::mt19937_64 gen = [] {
    std::random_device rd;
    std::seed_seq seq{rd(), rd(), rd(), rd()};
    return std::mt19937_64(seq);
  }();
  std::uniform_int_distribution<std::uint64_t> dist;

  std::uint64_t hi = dist(gen);
  std::uint64_t lo = dist(gen);

  hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL; // version = 4
  lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL; // variant = 10xx

  char buf[37];
  std::snprintf(buf, sizeof buf, "%08x-%04x-%04x-%04x-%012llx",
                static_cast<unsigned>(hi >> 32),
                static_cast<unsigned>((hi >> 16) & 0xFFFF),
                static_cast<unsigned>(hi & 0xFFFF),
                static_cast<unsigned>(lo >> 48),
                static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFULL));
  return buf;
}

void add_item_content_id(heif_context* ctx, heif_item_id id) {

  // The Item Content ID Property 4cc is 'uuid' with the extended type 0x261ef3741d975bbaacbd9d2c8ea73522
  uint8_t extended_type_item_content_id[16] = {0x26, 0x1e, 0xf3, 0x74, 0x1d, 0x97, 0x5b, 0xba, 0xac, 0xbd, 0x9d, 0x2c, 0x8e, 0xa7, 0x35, 0x22};

  // Generate GIMI Content ID
  std::string content_id = "urn:uuid:" + uuid_v4();

  // Required by GIMI, but not essential for viewing
  bool is_essential = false;

  // Content Id
  heif_item_add_raw_property(
      ctx,
      id,
      heif_fourcc('u', 'u', 'i', 'd'),
      (const uint8_t*)extended_type_item_content_id,
      (const uint8_t*)content_id.c_str(),
      content_id.length(),
      is_essential,
      nullptr);
}

static void write_gimi_image(const char* output_name) {
  heif_context* ctx = heif_context_alloc();

  // Add Brands
  add_brands(ctx);

  // Add Image
  heif_item_id image_id = add_image(ctx);

  // Security Markings
  heif_item_id security_markings_id = add_security_markings(ctx);

  // Timestamp
  add_timestamp(ctx, image_id);

  // Content ID
  add_item_content_id(ctx, image_id);
  add_item_content_id(ctx, security_markings_id);

  // Write to File
  err = heif_context_write_to_file(ctx, output_name);

  heif_context_free(ctx);
}

static void read_gimi_image(const char* input_name) {
  // TODO
}

static void write_gimi_video(const char* output_name) {
  heif_context* ctx = build_heif_context();

  // Add Brands
  add_brands(ctx);

  // Security Markings
  add_security_markings(ctx);

  // Setup Track/Track Options
  heif_track_options* track_options = heif_track_options_alloc();
  // heif_track_options_set_interleaved_sample_aux_infos(track_options,1);

  const heif_sample_aux_info_presence aux_info_presence = heif_sample_aux_info_presence_mandatory;

  // Timestamp
  heif_tai_clock_info* tai_clock_info = heif_tai_clock_info_alloc();
  heif_track_options_enable_sample_tai_timestamps(track_options, tai_clock_info, aux_info_presence);

  // Content IDs
  const char* gimi_track_id = "TODO";
  heif_track_options_enable_sample_gimi_content_ids(track_options, aux_info_presence);
  heif_track_options_set_gimi_track_id(track_options, gimi_track_id);

  heif_error err;

  // Build Track
  const int num_frames = 10;
  heif_track* track = build_heif_track(ctx, track_options, num_frames);
  if (!track) {
    std::cerr << "Error: Failed to build HEIF track." << std::endl;
    heif_track_options_release(track_options);
    heif_context_free(ctx);
    return;
  }

  // Iterate Through Frames
  for (uint32_t i = 0; i < num_frames; i++) {
    // TODO
  }

  // Finalize
  err = heif_context_write_to_file(ctx, output_name);
  if (err.code != heif_error_Ok) {
    std::cerr << "Write failed: " << err.message << std::endl;
  }
  else {
    std::cout << "Successfully wrote " << output_name << std::endl;
  }

  heif_track_options_release(track_options);
  heif_context_free(ctx);
  return;
}

static void read_gimi_video(const char* input_name) {
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