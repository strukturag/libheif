/*
  libheif unit tests

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

// Regression test for GHSA-q492-cfcm-895h: the OpenJPEG decoder plugin's
// pre-decode resource-limit gate only bounded the JPEG 2000 *window* span
// (x1-x0) * (y1-y0), not the absolute SIZ coordinates. A crafted codestream
// can declare a tiny window (e.g. 17 pixels) anchored at coordinates close to
// the 32-bit boundary; OpenJPEG's tile/coefficient arithmetic operates over
// the full reference grid (Xsiz=x1, Ysiz=y1), not just the window, so such a
// file passed libheif's span-based checks and reached opj_decode() with
// pathological geometry (observed as a heap-buffer-overflow write inside
// OpenJPEG 2.3.1; corresponds to the sink class of CVE-2020-6851).
//
// The fix additionally bounds the reference-grid area x1*y1 against
// max_image_size_pixels, so the file below must now be rejected with a
// Security_limit_exceeded error *before* opj_decode() is ever called.
//
// The test file bytes below are exactly the trigger.heif PoC from the
// advisory (ISO base media container bytes + a real JPEG 2000 codestream
// with SIZ: x0=2147483631, x1=2147483648, y0=0, y1=1 -> window span = 17
// pixels, reference grid area = 2147483648 pixels).

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

// Minimal base64 decoder (no external dependency needed for this test).
std::vector<uint8_t> base64_decode(const std::string& in) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };

  std::vector<uint8_t> out;
  int bits = 0;
  uint32_t acc = 0;
  for (char c : in) {
    if (c == '=' || c == '\n' || c == '\r') {
      continue;
    }
    int v = val(c);
    if (v < 0) {
      continue;
    }
    acc = (acc << 6) | uint32_t(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(uint8_t((acc >> bits) & 0xFF));
    }
  }
  return out;
}


// ftyp + meta for a single j2k1 item; iloc extent starts right after this
// header, at file offset 284. Taken verbatim from the advisory's PoC.
const char* kHeaderB64 =
    "AAAAHGZ0eXBqMmtpAAAAAG1pZjFqMmtpbWlhZgAAAQBtZXRhAAAAAAAAACFoZGxyAAAAAAAAAABw"
    "aWN0AAAAAAAAAAAAAAAAAAAAACJpbG9jAAAAAERAAAEAAQAAAAABJAABAAAAAAAABCkAAAAjaWlu"
    "ZgAAAAAAAQAAABVpbmZlAgAAAAABAABqMmsxAAAAAA5waXRtAAAAAAABAAAAgGlwcnAAAABhaXBj"
    "bwAAACRqMmtIAAAAHGNkZWYAAwAAAAAAAQABAAAAAgACAAAAAwAAABNjb2xybmNseAABAA0ABoAA"
    "AAAUaXNwZQAAAAAAAAAgAAAAIAAAAA5waXhpAAAAAAEIAAAAF2lwbWEAAAAAAAAAAQABBIECAwQ=";

// Raw JPEG 2000 codestream (1065 bytes). SIZ: huge absolute offsets, window
// span == 17 pixels, reference grid area == 2147483648 pixels.
const char* kJ2kB64 =
    "/0//UQApAACAAAAAAAAAAX///+8AAAAAAAAAEAAAAAF////vAAAAAAABBwEB/1IADAAAAAEAAAQE"
    "AAH/XAAEQED/ZAAlAAFDcmVhdGVkIGJ5IE9wZW5KUEVHIHZlcnNpb24gMi4zLjH/kAAKAAAAAAAf"
    "AAH/k9+AcAc2AD6Ey0mHIVs2Icqr/5AACgABAAAAHgAB/5PPtDQJXfai5z0KYbomZ1SF/5AACgAC"
    "AAAAHgAB/5PPtDQJWaoxlpMOQrZsQ5VX/5AACgADAAAAHgAB/5PPtDQJY0vTznoUw3RMzqkL/5AA"
    "CgAEAAAAHwAB/5PPtDgHNgA+enwM/2bxxDlVf/+QAAoABQAAAB0AAf+Tx9QYCVmqHPgZ/03jiHKq"
    "/5AACgAGAAAAHAAB/5PH1BYHNgA6dOTCJ3lUzP+QAAoABwAAABoAAf+Tw+cSBzXV2/Un/O3v/5AA"
    "CgAIAAAAGQAB/5PB8ggZze1KnSkB0f+QAAoACQAAABwAAf+Tw+cWAqnKfq5737yqZj//kAAKAAoA"
    "AAAdAAH/k8fUGAKq6UAZ/03jiHKqf/+QAAoACwAAAB0AAf+Tx9QYAqWeshn/TeOIcqp//5AACgAM"
    "AAAAHQAB/5PPtDACqwf7AF0LWbEOVU//kAAKAA0AAAAeAAH/k8+0NAKrjMbgakU4qm2dUhP/kAAK"
    "AA4AAAAdAAH/k8+0MAKlvW0AXQtZsQ5VT/+QAAoADwAAAB4AAf+Tz7Q0AqY5a42auO6JmdUhP/+Q"
    "AAoAEAAAAB8AAf+T34BwBzYAPoTLSYchWzYhyqv/kAAKABEAAAAeAAH/k8+0NAld9qLnPQphuiZn"
    "VIX/kAAKABIAAAAeAAH/k8+0NAlZqjGWkw5CtmxDlVf/kAAKABMAAAAeAAH/k8+0NAljS9POehTD"
    "dEzOqQv/kAAKABQAAAAfAAH/k8+0OAc2AD56fAz/ZvHEOVV//5AACgAVAAAAHQAB/5PH1BgJWaoc"
    "+Bn/TeOIcqr/kAAKABYAAAAcAAH/k8fUFgc2ADp05MIneVTM/5AACgAXAAAAGgAB/5PD5xIHNdXb"
    "9Sf87e//kAAKABgAAAAZAAH/k8HyCBnN7UqdKQHR/5AACgAZAAAAHAAB/5PD5xYCqcp+rnvfvKpm"
    "P/+QAAoAGgAAAB0AAf+Tx9QYAqrpQBn/TeOIcqp//5AACgAbAAAAHQAB/5PH1BgCpZ6yGf9N44hy"
    "qn//kAAKABwAAAAdAAH/k8+0MAKrB/sAXQtZsQ5VT/+QAAoAHQAAAB4AAf+Tz7Q0AquMxuBqRTiq"
    "bZ1SE/+QAAoAHgAAAB0AAf+Tz7QwAqW9bQBdC1mxDlVP/5AACgAfAAAAHgAB/5PPtDQCpjlrjZq4"
    "7omZ1SE//5AACgAgAAAAHwAB/5PfgHAHPcWfV5c9In/nmcSVV//Z";


// Appends the codestream as 'mdat' to the header above. The item location in the header
// is fixed, hence the codestream has to have the size of the advisory's PoC.
std::vector<uint8_t> wrap_codestream_in_heif(const std::vector<uint8_t>& j2k) {
  std::vector<uint8_t> header = base64_decode(kHeaderB64);
  REQUIRE(header.size() == 284);
  REQUIRE(j2k.size() == 1065);

  std::vector<uint8_t> file = header;

  uint32_t mdat_size = uint32_t(8 + j2k.size());
  file.push_back(uint8_t((mdat_size >> 24) & 0xFF));
  file.push_back(uint8_t((mdat_size >> 16) & 0xFF));
  file.push_back(uint8_t((mdat_size >> 8) & 0xFF));
  file.push_back(uint8_t(mdat_size & 0xFF));
  file.push_back('m');
  file.push_back('d');
  file.push_back('a');
  file.push_back('t');
  file.insert(file.end(), j2k.begin(), j2k.end());

  return file;
}


std::vector<uint8_t> build_trigger_heif() {
  return wrap_codestream_in_heif(base64_decode(kJ2kB64));
}


void append_be(std::vector<uint8_t>& data, uint32_t value, int num_bytes) {
  for (int i = num_bytes - 1; i >= 0; i--) {
    data.push_back(uint8_t((value >> (i * 8)) & 0xFF));
  }
}


// A codestream that consists of the SOC marker, optionally an unknown marker segment, and
// an SIZ marker segment for three 8-bit components. It is padded with zeros to the size
// of the item. Nothing more is needed, because the codestream has to be rejected based on
// its SIZ marker segment.
std::vector<uint8_t> build_codestream(uint32_t width, uint32_t height,
                                      uint32_t tile_width, uint32_t tile_height,
                                      bool data_before_siz = false) {
  const uint16_t num_components = 3;

  std::vector<uint8_t> j2k = {0xFF, 0x4F}; // SOC

  if (data_before_siz) {
    // A marker that does not exist in JPEG 2000, with a segment length of 4 and two bytes
    // of data. OpenJPEG and FFmpeg both skip it and continue with the SIZ marker segment.
    j2k.insert(j2k.end(), {0xFF, 0xD8, 0x00, 0x04, 0x12, 0x34});
  }

  j2k.insert(j2k.end(), {0xFF, 0x51}); // SIZ
  append_be(j2k, 38 + 3 * num_components, 2); // Lsiz
  append_be(j2k, 0, 2); // Rsiz
  append_be(j2k, width, 4); // Xsiz
  append_be(j2k, height, 4); // Ysiz
  append_be(j2k, 0, 4); // XOsiz
  append_be(j2k, 0, 4); // YOsiz
  append_be(j2k, tile_width, 4); // XTsiz
  append_be(j2k, tile_height, 4); // YTsiz
  append_be(j2k, 0, 4); // XTOsiz
  append_be(j2k, 0, 4); // YTOsiz
  append_be(j2k, num_components, 2); // Csiz
  for (int c = 0; c < num_components; c++) {
    j2k.insert(j2k.end(), {0x07, 0x01, 0x01}); // Ssiz, XRsiz, YRsiz
  }

  j2k.resize(1065);
  return j2k;
}


std::vector<std::string> jpeg2000_decoder_ids() {
  const heif_decoder_descriptor* descriptors[10];
  int n = heif_get_decoder_descriptors(heif_compression_JPEG2000, descriptors, 10);

  std::vector<std::string> ids;
  for (int i = 0; i < n && i < 10; i++) {
    ids.emplace_back(heif_decoder_descriptor_get_id_name(descriptors[i]));
  }
  return ids;
}


bool have_openjpeg_decoder() {
  for (const std::string& id : jpeg2000_decoder_ids()) {
    if (id == "openjpeg") {
      return true;
    }
  }
  return false;
}


struct DecodeResult {
  heif_error_code code;
  heif_suberror_code subcode;
};

// Decodes the codestream as the primary image of a HEIF file. When 'decoder_id' is not
// NULL, this decoder is used. The limits are the defaults, optionally with a lower
// maximum number of tiles and maximum memory block size.
DecodeResult decode_codestream(const std::vector<uint8_t>& j2k, const char* decoder_id,
                               uint64_t max_number_of_tiles = 0, uint64_t max_memory_block_size = 0) {
  std::vector<uint8_t> data = wrap_codestream_in_heif(j2k);

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_security_limits* limits = heif_context_get_security_limits(ctx);
  if (max_number_of_tiles) {
    limits->max_number_of_tiles = max_number_of_tiles;
  }
  if (max_memory_block_size) {
    limits->max_memory_block_size = max_memory_block_size;
  }

  heif_error err = heif_context_read_from_memory_without_copy(ctx, data.data(), data.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  heif_decoding_options* options = heif_decoding_options_alloc();
  options->decoder_id = decoder_id;

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, options);
  DecodeResult result{err.code, err.subcode};

  if (img) {
    heif_image_release(img);
  }
  heif_decoding_options_free(options);
  heif_image_handle_release(handle);
  heif_context_free(ctx);

  return result;
}

} // namespace


TEST_CASE("jpeg2000: OpenJPEG plugin rejects huge reference-grid coordinates with a tiny window")
{
  if (!heif_have_decoder_for_format(heif_compression_JPEG2000)) {
    SKIP("JPEG 2000 (OpenJPEG) decoder not available, skipping test");
  }

  std::vector<uint8_t> data = build_trigger_heif();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory_without_copy(ctx, data.data(), data.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);

  heif_image* img = nullptr;
  err = heif_decode_image(handle, &img, heif_colorspace_undefined, heif_chroma_undefined, nullptr);

  // Before the fix, this pathological geometry passed the span-based
  // pre-decode gate (window span = 17 pixels) and reached opj_decode(),
  // which either crashes the underlying codec (OpenJPEG <= 2.3.x) or fails
  // with a generic decoder error (OpenJPEG >= 2.4, observed as
  // heif_error_Decoder_plugin_error / "opj_decode()" on this build). Either
  // way, opj_decode() was called on the pathological input.
  //
  // After the fix, the reference-grid-area check must reject the input
  // before opj_decode() is invoked, surfacing as a security-limit error.
  REQUIRE(err.code == heif_error_Memory_allocation_error);
  REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);

  if (img) {
    heif_image_release(img);
  }
  heif_image_handle_release(handle);
  heif_context_free(ctx);
}


// Regression test for GHSA-h4h8-qgvc-m7r2 (found by OSS-Fuzz as an out-of-memory error
// of the sequence_fuzzer in opj_j2k_read_siz()).
//
// OpenJPEG allocates its coding parameters for each tile and for each component of each
// tile while it reads the SIZ marker segment, i.e. within opj_read_header(), before the
// plugin could check anything. A codestream of some hundred bytes made it allocate 2 GB
// (65388 tiles with 24 components) and it could have been much more. The plugin now reads
// the SIZ marker segment itself and checks it before it calls OpenJPEG.

TEST_CASE("jpeg2000: OpenJPEG plugin checks the number of tiles before it reads the header")
{
  if (!have_openjpeg_decoder()) {
    SKIP("OpenJPEG decoder not available, skipping test");
  }

  // In all cases, we got heif_error_Decoder_plugin_error before the fix, because
  // opj_read_header() had read the SIZ marker segment and failed on the data after it.

  SECTION("more tiles than the codestream can hold") {
    // 65025 tiles of one pixel. The reference grid is small enough to pass the image size
    // limit. OpenJPEG allocated 500 MB for it (2 GB with 24 components).
    DecodeResult result = decode_codestream(build_codestream(255, 255, 1, 1), "openjpeg");
    REQUIRE(result.code == heif_error_Invalid_input);
    REQUIRE(result.subcode == heif_suberror_Invalid_J2K_codestream);
  }

  SECTION("more tiles than the security limit allows") {
    DecodeResult result = decode_codestream(build_codestream(4, 2, 1, 1), "openjpeg", 4);
    REQUIRE(result.code == heif_error_Memory_allocation_error);
    REQUIRE(result.subcode == heif_suberror_Security_limit_exceeded);
  }

  SECTION("tiles need more memory than the security limit allows") {
    // OpenJPEG allocates about 11 kB for each tile with three components.
    DecodeResult result = decode_codestream(build_codestream(4, 2, 1, 1), "openjpeg", 0, 64 * 1024);
    REQUIRE(result.code == heif_error_Memory_allocation_error);
    REQUIRE(result.subcode == heif_suberror_Security_limit_exceeded);
  }

  SECTION("the limits do not get in the way of the same tiles with the default limits") {
    // The 8 tiles pass our checks. OpenJPEG fails later, as the codestream ends after
    // the SIZ marker segment.
    DecodeResult result = decode_codestream(build_codestream(4, 2, 1, 1), "openjpeg");
    REQUIRE(result.code == heif_error_Decoder_plugin_error);
  }
}



// libheif checks the reference grid of a JPEG 2000 codestream against the image size limit
// before it calls a decoder plugin. This check was skipped when libheif could not read the
// SIZ marker segment, which it expects directly after the SOC marker. The decoder
// libraries are more lenient. They skip what comes between the SOC marker and the SIZ
// marker segment, so they got to work with a reference grid that libheif had not checked.
// The file that OSS-Fuzz found for GHSA-h4h8-qgvc-m7r2 came through this way.

TEST_CASE("jpeg2000: codestream with data between SOC and SIZ is not passed to the decoder")
{
  std::vector<std::string> decoder_ids = jpeg2000_decoder_ids();
  if (decoder_ids.empty()) {
    SKIP("JPEG 2000 decoder not available, skipping test");
  }

  // The reference grid is far above the image size limit.
  std::vector<uint8_t> j2k = build_codestream(12000, 12000, 12000, 12000, true);

  for (const std::string& id : decoder_ids) {
    INFO("decoder: " << id);
    DecodeResult result = decode_codestream(j2k, id.c_str());

    // This is the error of libheif. Before the fix, the plugin got the codestream and
    // the error was the one of the plugin.
    REQUIRE(result.code == heif_error_Invalid_input);
    REQUIRE(result.subcode == heif_suberror_Invalid_J2K_codestream);
  }
}
