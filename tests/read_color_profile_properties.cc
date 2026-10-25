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
#include "image-items/iden.h"
#include "image-items/image_item.h"
#include "test-config.h"
#include "libheif/heif.h"
#include "libheif/heif_properties.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>


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


TEST_CASE("setting an item profile does not add a property")
{
  HeifContext ctx;
  auto id = ctx.get_heif_file()->add_new_image(fourcc("iden"));
  REQUIRE(id);

  ImageItem_iden image(&ctx, *id);
  nclx_profile profile;
  profile.set_sRGB_defaults();
  image.set_color_profile(std::make_shared<color_profile_nclx>(profile));
  auto icc = std::make_shared<color_profile_raw>(fourcc("prof"), std::vector<uint8_t>{1, 2, 3, 4});
  image.set_color_profile_icc(icc);

  REQUIRE(image.has_nclx_color_profile());
  REQUIRE(image.get_color_profile_icc() == icc);
  REQUIRE(image.get_all_properties<Box_colr>().empty());

  auto colr = std::make_shared<Box_colr>();
  colr->set_color_profile(std::make_shared<color_profile_nclx>(profile));
  REQUIRE(image.add_color_profile_property(colr) != 0);
  REQUIRE(image.get_all_properties<Box_colr>().size() == 1);

  std::vector<std::shared_ptr<Box>> stored_properties;
  REQUIRE(!ctx.get_heif_file()->get_properties(*id, stored_properties));
  REQUIRE(stored_properties.size() == 1);
  REQUIRE(std::dynamic_pointer_cast<Box_colr>(stored_properties.front()) != nullptr);
}


TEST_CASE("encoding writes an explicit color profile property")
{
  if (!heif_have_encoder_for_format(heif_compression_uncompressed)) {
    SKIP("uncompressed encoder unavailable");
  }

  heif_image* image = nullptr;
  REQUIRE(heif_image_create(8, 8, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &image).code == heif_error_Ok);
  REQUIRE(heif_image_add_plane(image, heif_channel_interleaved, 8, 8, 8).code == heif_error_Ok);
  size_t stride = 0;
  uint8_t* pixels = heif_image_get_plane2(image, heif_channel_interleaved, &stride);
  REQUIRE(pixels != nullptr);
  for (int y = 0; y < 8; ++y) {
    std::memset(pixels + y * stride, 128, 8 * 3);
  }

  const uint8_t icc_data[] = {0x01, 0x02, 0x03, 0x04};
  REQUIRE(heif_image_set_raw_color_profile(image, "prof", icc_data, sizeof(icc_data)).code == heif_error_Ok);

  heif_context* context = heif_context_alloc();
  heif_encoder* encoder = nullptr;
  REQUIRE(heif_context_get_encoder_for_format(context, heif_compression_uncompressed, &encoder).code == heif_error_Ok);
  heif_image_handle* handle = nullptr;
  auto err = heif_context_encode_image(context, image, encoder, nullptr, &handle);
  INFO("encode error: " << err.message);
  REQUIRE(err.code == heif_error_Ok);

  auto colr_type = static_cast<heif_item_property_type>(heif_fourcc('c', 'o', 'l', 'r'));
  REQUIRE(heif_item_get_properties_of_type(context, heif_image_handle_get_item_id(handle), colr_type, nullptr, 0) == 1);

  heif_image_handle_release(handle);
  heif_encoder_release(encoder);
  heif_context_free(context);
  heif_image_release(image);
}
