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

  // TODO: read file where 'meta' box is not the first one after 'ftyp'
}
