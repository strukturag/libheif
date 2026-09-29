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

// Regression tests for the in-band coded-size security gate.
//
// The container 'ispe' can declare a small image while the actual coded size
// lives in the bitstream: an AV1 sequence header OBU, an SPS NAL for AVC/HEVC/VVC,
// or the SOF marker for JPEG. That coded size is what the decoder allocates. For the NAL codecs the
// SPS may sit in the item data rather than only in the avcC/hvcC/vvcC record,
// and for JPEG the SOF is always in the bitstream. libheif now scans the whole
// combined config+bitstream buffer for the largest coded size and rejects it
// against the (ispe-tightened) security limits before any decoder plugin runs.
//
// Each file below carries a 64x64 (HEVC: 320x240) 'ispe' but an oversized coded
// size in the bitstream, and must be rejected with a security-limit error.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

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
    if (c == '=' || c == '\n' || c == '\r') continue;
    int v = val(c);
    if (v < 0) continue;
    acc = (acc << 6) | uint32_t(v);
    bits += 6;
    if (bits >= 8) { bits -= 8; out.push_back(uint8_t((acc >> bits) & 0xFF)); }
  }
  return out;
}

// Decode `b64` and assert it is rejected with a security-limit error before the
// decoder allocates. Returns without asserting if no decoder for `format` is built.
void expect_security_reject(heif_compression_format format, const char* b64) {
  if (!heif_have_decoder_for_format(format)) {
    SKIP("no decoder for this format built, skipping");
  }

  std::vector<uint8_t> data = base64_decode(b64);

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

  REQUIRE(err.code == heif_error_Memory_allocation_error);
  REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);

  if (img) heif_image_release(img);
  heif_image_handle_release(handle);
  heif_context_free(ctx);
}

// HEVC item, 320x240 ispe, with an extra SPS (declaring 2000x2000) injected into
// the item data. The hvcC SPS (320x240) passes the tightened limit, so only the
// in-band 2000x2000 SPS can trip the gate.
const char* kHevcInbandB64 =
  "AAAAHGZ0eXBoZWljAAAAAG1pZjFoZWljbWlhZgAAAVZtZXRhAAAAAAAAACFoZGxyAAAAAAAAAABwaWN0AAAAAAAAAAAAAAAAAAAA"
  "ACJpbG9jAAAAAERAAAEAAQAAAAABegABAAAAAAAAAKQAAAAjaWluZgAAAAAAAQAAABVpbmZlAgAAAAABAABodmMxAAAAAA5waXRt"
  "AAAAAAABAAAA1mlwcnAAAAC3aXBjbwAAAHhodmNDAQNwAAAAAAAAAAAAPPAA/P34+AAADwNgAAEAGEABDAH//wNwAAADAJAAAAMA"
  "AAMAPLoCQGEAAQArQgEBA3AAAAMAkAAAAwAAAwA8oAoIDxZbqSSmubgIaDAgAAADAyAAAAMAIWIAAQAHRAHBcrBiQAAAABNjb2xy"
  "bmNseAABAA0ABoAAAAAUaXNwZQAAAAAAAAFAAAAA8AAAABBwaXhpAAAAAAMICAgAAAAXaXBtYQAAAAAAAAABAAEEgQIDBAAAAKxt"
  "ZGF0AAAALUIBAQNwAAADAJAAAAMAAAMAlqAD6IAfRZbqSSmubgIaDAgAAAMAyAAAAwAIQAAAAG8oAa8E8hpVEoBBMmnb/xh///+y"
  "jH//RNiT0cYM7GDOTinQihDqlAAAAwAAAwB5QABlUvVKAAADAAADAAADAAADAAAacAAAAwAAAwAAAwAAAwAAAwAAX0AAAAMAAAMA"
  "AAMAAAMAAAMAAAMAAAMAAx4=";

// AVC item, 64x64 ispe, avcC carrying no SPS (only a PPS); the SPS (declaring
// 1024x512) is in the item data. The old config-record-only check saw no SPS.
const char* kAvcInbandB64 =
  "AAAAGGZ0eXBtaWYxAAAAAG1pZjFoZWljAAAA0W1ldGEAAAAAAAAAIWhkbHIAAAAAAAAAAHBpY3QAAAAAAAAAAAAAAAAAAAAADnBp"
  "dG0AAAAAAAEAAAAjaWluZgAAAAAAAQAAABVpbmZlAgAAAAABAABhdmMxAAAAAFFpcHJwAAAANGlwY28AAAAYYXZjQwFkACj/4AEA"
  "B2joQ4OSyLAAAAAUaXNwZQAAAAAAAABAAAAAQAAAABVpcG1hAAAAAAAAAAEAAQKBAgAAACJpbG9jAAAAAERAAAEAAQAAAAAA8QAB"
  "AAAAAAAAAB4AAAAmbWRhdAAAABpnZAAorHIEQEAEGhAAAAMAEAAAAwMg8YMYRg==";

// JPEG item, 64x64 ispe, with a SOF marker declaring 8000x8000.
const char* kJpegSofB64 =
  "AAAAGGZ0eXBtaWYxAAAAAG1pZjFoZWljAAAAuG1ldGEAAAAAAAAAIWhkbHIAAAAAAAAAAHBpY3QAAAAAAAAAAAAAAAAAAAAADnBp"
  "dG0AAAAAAAEAAAAjaWluZgAAAAAAAQAAABVpbmZlAgAAAAABAABqcGVnAAAAADhpcHJwAAAAHGlwY28AAAAUaXNwZQAAAAAAAABA"
  "AAAAQAAAABRpcG1hAAAAAAAAAAEAAQGBAAAAImlsb2MAAAAAREAAAQABAAAAAADYAAEAAAAAAAAChAAAAoxtZGF0/9j/4AAQSkZJ"
  "RgABAQAAAQABAAD/2wBDAAYEBQYFBAYGBQYHBwYIChAKCgkJChQODwwQFxQYGBcUFhYaHSUfGhsjHBYWICwgIyYnKSopGR8tMC0o"
  "MCUoKSj/2wBDAQcHBwoIChMKChMoGhYaKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCj/"
  "wAARCB9AH0ADASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQID"
  "AAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZn"
  "aGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx"
  "8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJB"
  "UQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3"
  "eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6"
  "/9oADAMBAAIRAxEAPwCKiiivjz74KKKKACiiigAooooA/9k=";


// AVIF item, 64x64 ispe, with an in-band AV1 Sequence Header OBU coding 8192x8192.
// This is the original advisory reproducer (GHSA-v8qw-hwjv-44hw,
// poc_av1_huge-frame-8192.avif).
const char* kAvifPocB64 =
  "AAAAHGZ0eXBhdmlmAAAAAG1pZjFhdmlmbWlhZgAAAM9tZXRhAAAAAAAAACFoZGxyAAAAAAAAAABwaWN0AAAAAAAAAAAAAAAA"
  "AAAAAA5waXRtAAAAAAABAAAAI2lpbmYAAAAAAAEAAAAVaW5mZQIAAAAAAQAAYXYwMQAAAABRaXBycAAAADRpcGNvAAAAFGlz"
  "cGUAAAAAAAAAQAAAAEAAAAAYYXYxQ4ERDAAKCgAAAAKv/4lfIAgAAAAVaXBtYQAAAAAAAAABAAECgYIAAAAgaWxvYwEAAABE"
  "AAABAAEAAAAAAAEAAADzAAAAbAAAAHRtZGF0EgAKDQAAAIP8f/x//Er5AEAyWRAAkLQAggQQAACAACUAKPPXDGWYHeUNJr+m"
  "mLGOwbZyMlmA0easG5s7ljg1k8Hjl6lwngAo89cMZZgd5Q0mv6aYsY7BtnIyWYDR5qwbmzuWODWTweOXqXCe"
;

} // namespace


TEST_CASE("avif: reject in-band AV1 sequence header exceeding the security limit") {
  expect_security_reject(heif_compression_AV1, kAvifPocB64);
}

TEST_CASE("hevc: reject in-band SPS exceeding the security limit") {
  expect_security_reject(heif_compression_HEVC, kHevcInbandB64);
}

TEST_CASE("avc: reject in-band SPS exceeding the security limit") {
  expect_security_reject(heif_compression_AVC, kAvcInbandB64);
}

TEST_CASE("jpeg: reject oversized SOF exceeding the security limit") {
  expect_security_reject(heif_compression_JPEG, kJpegSofB64);
}
