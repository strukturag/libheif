/*
  libheif integration tests for uncompressed decoder

  MIT License

  Copyright (c) 2024 Dirk Farin <dirk.farin@gmail.com>

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
#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "libheif/heif_sequences.h"
#include "api_structs.h"
#include <cstdint>
#include <stdio.h>
#include "test_utils.h"
#include <string.h>
#include "file_layout.h"
#include "test-config.h"

#include <fstream>


TEST_CASE("parse file layout") {
  auto istr = std::unique_ptr<std::istream>(new std::ifstream(tests_data_directory + "/uncompressed_comp_ABGR.heif", std::ios::binary));
  auto reader = std::make_shared<StreamReader_istream>(std::move(istr));

  FileLayout file;
  Error err = file.read(reader, heif_get_global_security_limits());

  REQUIRE(err.error_code == heif_error_Ok);
}


TEST_CASE("ftyp box parse error is returned, not masked as missing ftyp") {
  // A failure while reading the 'ftyp' box content (here: provoked through a
  // brands security limit smaller than the file's brand list) must be returned
  // from FileLayout::read(). It used to be swallowed, leaving the ftyp box
  // unset, so that the file was later rejected with a misleading
  // "No 'ftyp' box" error (issue #1872).
  auto istr = std::unique_ptr<std::istream>(new std::ifstream(tests_data_directory + "/uncompressed_comp_ABGR.heif", std::ios::binary));
  auto reader = std::make_shared<StreamReader_istream>(std::move(istr));

  heif_security_limits limits = *heif_get_global_security_limits();
  limits.max_number_of_file_brands = 1; // the file has two compatible brands

  FileLayout file;
  Error err = file.read(reader, &limits);

  REQUIRE(err.error_code == heif_error_Memory_allocation_error);
  REQUIRE(err.sub_error_code == heif_suberror_Security_limit_exceeded);
}


TEST_CASE("disabled security limits accept any number of ftyp brands") {
  // With disabled security limits, all limit fields are zero, which means
  // "no limit". In v1.21.x, the zero was taken literally and every file with
  // a non-empty compatible-brands list was rejected (issue #1872).
  auto istr = std::unique_ptr<std::istream>(new std::ifstream(tests_data_directory + "/uncompressed_comp_ABGR.heif", std::ios::binary));
  auto reader = std::make_shared<StreamReader_istream>(std::move(istr));

  FileLayout file;
  Error err = file.read(reader, heif_get_disabled_security_limits());

  REQUIRE(err.error_code == heif_error_Ok);
  REQUIRE(file.get_ftyp_box() != nullptr);
}


TEST_CASE("meta box with size 0 (extends to end of file)") {
  // The 'meta' box uses a box size of 0, which per ISO/IEC 14496-12 clause 4.2
  // means it extends to the end of the file (legal for the last box). In this
  // file 'meta' also appears after the media data rather than first. It must
  // parse successfully (libavif / Chromium read and render this file).
  auto istr = std::unique_ptr<std::istream>(new std::ifstream(tests_data_directory + "/meta_size_zero.avif", std::ios::binary));
  auto reader = std::make_shared<StreamReader_istream>(std::move(istr));

  FileLayout file;
  Error err = file.read(reader, heif_get_global_security_limits());

  REQUIRE(err.error_code == heif_error_Ok);
  REQUIRE(file.get_meta_box() != nullptr);

  // The resolved 'meta' box must yield the correct image metadata. Read the
  // file through the high-level API and check the primary image dimensions.
  // These come from the 'ispe' box, so no AV1 decoder is required.
  heif_context* context = get_context_for_test_file("meta_size_zero.avif");
  heif_image_handle* handle = get_primary_image_handle(context);
  REQUIRE(heif_image_handle_get_ispe_width(handle) == 33);
  REQUIRE(heif_image_handle_get_ispe_height(handle) == 11);
  heif_image_handle_release(handle);
  heif_context_free(context);
}


TEST_CASE("mini box with size 0 (extends to end of file)") {
  // A top-level 'mini' box (MIAF low-overhead AVIF) with a box size of 0, which
  // per ISO/IEC 14496-12 clause 4.2 means it extends to the end of the file.
  auto istr = std::unique_ptr<std::istream>(new std::ifstream(tests_data_directory + "/mini_size_zero.avif", std::ios::binary));
  auto reader = std::make_shared<StreamReader_istream>(std::move(istr));

  FileLayout file;
  Error err = file.read(reader, heif_get_global_security_limits());

  REQUIRE(err.error_code == heif_error_Ok);
  REQUIRE(file.get_mini_box() != nullptr);

  heif_context* context = get_context_for_test_file("mini_size_zero.avif");
  heif_image_handle* handle = get_primary_image_handle(context);
  REQUIRE(heif_image_handle_get_ispe_width(handle) == 256);
  REQUIRE(heif_image_handle_get_ispe_height(handle) == 256);
  heif_image_handle_release(handle);
  heif_context_free(context);
}


TEST_CASE("moov box with size 0 (extends to end of file)") {
  // A sequence file laid out as 'ftyp' + 'mdat' + 'moov', where the trailing
  // 'moov' box uses a box size of 0 (extends to end of file, per ISO/IEC
  // 14496-12 clause 4.2). This is the common non-fast-start ordering.
  auto istr = std::unique_ptr<std::istream>(new std::ifstream(tests_data_directory + "/moov_size_zero.heif", std::ios::binary));
  auto reader = std::make_shared<StreamReader_istream>(std::move(istr));

  FileLayout file;
  Error err = file.read(reader, heif_get_global_security_limits());

  REQUIRE(err.error_code == heif_error_Ok);
  REQUIRE(file.get_moov_box() != nullptr);

  // The resolved 'moov' box must yield a readable sequence track.
  heif_context* context = get_context_for_test_file("moov_size_zero.heif");
  REQUIRE(heif_context_has_sequence(context) == 1);
  REQUIRE(heif_context_number_of_sequence_tracks(context) == 1);
  heif_context_free(context);
}
