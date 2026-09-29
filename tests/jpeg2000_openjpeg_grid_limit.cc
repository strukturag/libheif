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


std::vector<uint8_t> build_trigger_heif() {
  std::vector<uint8_t> header = base64_decode(kHeaderB64);
  std::vector<uint8_t> j2k = base64_decode(kJ2kB64);
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
