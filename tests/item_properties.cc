/*
  libheif integration tests for generic item properties.

  MIT License

  Copyright (c) 2026 Greg Benz Photography LLC

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
#include "libheif/heif_properties.h"
#include "test-config.h"
#include "test_utils.h"

TEST_CASE("item properties: read NCLX from a parsed colr property")
{
  heif_context* ctx = get_context_for_local_file(
      tests_data_directory + "/../../examples/example.avif");
  REQUIRE(ctx != nullptr);

  heif_item_id item_id = 0;
  REQUIRE(heif_context_get_primary_image_ID(ctx, &item_id).code == heif_error_Ok);

  const auto colr_type = static_cast<heif_item_property_type>(heif_fourcc('c', 'o', 'l', 'r'));
  heif_property_id property_id = 0;
  REQUIRE(heif_item_get_properties_of_type(ctx, item_id, colr_type, &property_id, 1) == 1);

  // Recognized properties are parsed into typed boxes and cannot be accessed through the
  // existing Box_other-only raw-property getter.
  size_t raw_size = 0;
  REQUIRE(heif_item_get_property_raw_size(ctx, item_id, property_id, &raw_size).code ==
          heif_error_Usage_error);

  heif_color_profile_nclx* profile = nullptr;
  REQUIRE(heif_item_get_property_nclx_color_profile(ctx, item_id, property_id,
                                                    &profile).code == heif_error_Ok);
  REQUIRE(profile != nullptr);
  REQUIRE(profile->color_primaries == heif_color_primaries_unspecified);
  REQUIRE(profile->transfer_characteristics == heif_transfer_characteristic_unspecified);
  REQUIRE(profile->matrix_coefficients == heif_matrix_coefficients_ITU_R_BT_601_6);
  REQUIRE(profile->full_range_flag);
  heif_nclx_color_profile_free(profile);

  heif_property_id non_colr_property_id = 0;
  REQUIRE(heif_item_get_properties_of_type(ctx, item_id, heif_item_property_type_image_size,
                                           &non_colr_property_id, 1) == 1);

  profile = reinterpret_cast<heif_color_profile_nclx*>(1);
  REQUIRE(heif_item_get_property_nclx_color_profile(ctx, item_id, non_colr_property_id,
                                                    &profile).code == heif_error_Usage_error);
  REQUIRE(profile == nullptr);

  profile = reinterpret_cast<heif_color_profile_nclx*>(1);
  heif_error out_of_range_error =
      heif_item_get_property_nclx_color_profile(ctx, item_id, 999, &profile);
  REQUIRE(out_of_range_error.code == heif_error_Usage_error);
  REQUIRE(out_of_range_error.subcode == heif_suberror_Invalid_property);
  REQUIRE(profile == nullptr);

  heif_context_free(ctx);
}
