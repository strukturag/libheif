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

// Regression tests for GHSA-7pwf-qh74-p35w.
//
// FileLayout::read forwarded the caller's security limits to the ftyp, meta
// and moov parsers but parsed a MinimizedImageBox ('mini') file under the
// global default limits, and Box_mini re-parsed the embedded av1C/hvcC blob
// the same way. An application that tightened heif_security_limits on its
// context therefore got no enforcement of its budget on mini-format input.
//
// Box_mini::parse copies the whole box payload into memory through
// MemoryHandle, so a max_memory_block_size (or max_total_memory) below the
// payload size must reject the file, and a value above it must accept it.
// The fixtures' payloads lie between 1.9 KB and 19.3 KB.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "test-config.h"
#include <cstdint>
#include <string>

#define MINI_FILES \
  "lightning_mini.heif", "simple_osm_tile_meta.avif", "simple_osm_tile_alpha.avif", "mini_size_zero.avif"

static const uint64_t kLimitBelowAllPayloads = 1024;
static const uint64_t kBlockLimitAboveAllPayloads = 64 * 1024;
static const uint64_t kTotalLimitAboveAllPayloads = 256 * 1024;


// Reads a fixture on a context whose limits were tightened before the read.
// A zero argument keeps the default for that limit.
static heif_context* read_with_limits(const char* filename,
                                      uint64_t max_memory_block_size,
                                      uint64_t max_total_memory,
                                      heif_error* out_err)
{
  heif_context* ctx = heif_context_alloc();

  heif_security_limits limits = *heif_context_get_security_limits(ctx);
  if (max_memory_block_size) {
    limits.max_memory_block_size = max_memory_block_size;
  }
  if (max_total_memory) {
    limits.max_total_memory = max_total_memory;
  }
  heif_context_set_security_limits(ctx, &limits);

  std::string path = tests_data_directory + "/" + filename;
  *out_err = heif_context_read_from_file(ctx, path.c_str(), nullptr);
  return ctx;
}


// The error must come from the mini payload allocation, not from some other
// limit check that happens to fire first.
static bool mentions_mini_payload(const heif_error& err)
{
  return err.message != nullptr &&
         std::string(err.message).find("MinimizedImageBox") != std::string::npos;
}


TEST_CASE("mini payload is rejected by a tightened max_memory_block_size")
{
  auto file = GENERATE(MINI_FILES);
  INFO("file name: " << file);

  heif_error err;
  heif_context* ctx = read_with_limits(file, kLimitBelowAllPayloads, 0, &err);
  INFO("error: " << (err.message ? err.message : "(null)"));

  REQUIRE(err.code == heif_error_Memory_allocation_error);
  REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);
  REQUIRE(mentions_mini_payload(err));

  heif_context_free(ctx);
}


TEST_CASE("mini payload is rejected by a tightened max_total_memory")
{
  auto file = GENERATE(MINI_FILES);
  INFO("file name: " << file);

  heif_error err;
  heif_context* ctx = read_with_limits(file, 0, kLimitBelowAllPayloads, &err);
  INFO("error: " << (err.message ? err.message : "(null)"));

  REQUIRE(err.code == heif_error_Memory_allocation_error);
  REQUIRE(err.subcode == heif_suberror_Security_limit_exceeded);
  REQUIRE(mentions_mini_payload(err));

  heif_context_free(ctx);
}


TEST_CASE("mini file is accepted when the tightened limits cover the payload")
{
  auto file = GENERATE(MINI_FILES);
  INFO("file name: " << file);

  heif_error err;
  heif_context* ctx = read_with_limits(file, kBlockLimitAboveAllPayloads, kTotalLimitAboveAllPayloads, &err);
  INFO("error: " << (err.message ? err.message : "(null)"));

  REQUIRE(err.code == heif_error_Ok);

  // The expanded boxes must have been built from the mini box.
  heif_image_handle* handle = nullptr;
  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(handle != nullptr);
  heif_image_handle_release(handle);

  heif_context_free(ctx);
}
