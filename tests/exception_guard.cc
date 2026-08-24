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

// Tests for exception_guard(), the translation layer that keeps C++ exceptions
// from crossing the C API boundary. It converts an out-of-memory condition into
// a clean heif_error instead of letting std::bad_alloc abort the process
// (hardening for GHSA-7p2q-crf9-xm46 / GHSA-24wx-9w62-c96w).

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "api_structs.h"

#include <new>
#include <stdexcept>
#include <cstring>

TEST_CASE("exception_guard passes through the body result unchanged") {
  heif_error ok = exception_guard([]() -> heif_error {
    return {heif_error_Ok, heif_suberror_Unspecified, "fine"};
  });
  REQUIRE(ok.code == heif_error_Ok);

  heif_error custom = exception_guard([]() -> heif_error {
    return {heif_error_Invalid_input, heif_suberror_End_of_data, "boom"};
  });
  REQUIRE(custom.code == heif_error_Invalid_input);
  REQUIRE(custom.subcode == heif_suberror_End_of_data);
}

TEST_CASE("exception_guard converts std::bad_alloc to a memory-allocation error") {
  heif_error err = exception_guard([]() -> heif_error {
    throw std::bad_alloc();
  });
  REQUIRE(err.code == heif_error_Memory_allocation_error);
  REQUIRE(err.message != nullptr);
  // The message must be a static literal (returning it must not allocate).
  REQUIRE(strlen(err.message) > 0);
}

TEST_CASE("exception_guard converts std::length_error to a memory-allocation error") {
  // A std::vector/std::string asked to exceed max_size() throws std::length_error;
  // treat it as an allocation failure rather than an abort.
  heif_error err = exception_guard([]() -> heif_error {
    throw std::length_error("too large");
  });
  REQUIRE(err.code == heif_error_Memory_allocation_error);
}

TEST_CASE("exception_guard converts other std::exceptions to an internal error") {
  heif_error err = exception_guard([]() -> heif_error {
    throw std::runtime_error("unexpected");
  });
  REQUIRE(err.code == heif_error_Usage_error);
  REQUIRE(err.message != nullptr);
}

TEST_CASE("exception_guard converts non-std exceptions to an internal error") {
  heif_error err = exception_guard([]() -> heif_error {
    throw 42;
  });
  REQUIRE(err.code == heif_error_Usage_error);
}
