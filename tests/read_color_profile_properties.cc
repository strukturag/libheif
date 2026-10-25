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

#include "catch_amalgamated.hpp"
#include "context.h"
#include "image-items/image_item.h"
#include "test-config.h"


TEST_CASE("reading a file does not add color profile properties")
{
  // This file associates one nclx 'colr' property with its primary item.
  HeifContext ctx;
  Error err = ctx.read_from_file((tests_data_directory + "/clap_cropped.avif").c_str());
  INFO("read error: " << err.message);
  REQUIRE(!err);

  auto image = ctx.get_primary_image(false);
  REQUIRE(image);
  REQUIRE(image->get_all_properties<Box_colr>().size() == 1);

  // Reading still assigns the profile to the image description.
  REQUIRE(image->has_nclx_color_profile());
  REQUIRE(image->get_color_profile_icc() == nullptr);
}
