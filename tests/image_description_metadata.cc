/*
  libheif unit tests for ImageDescription metadata preservation across
  HeifPixelImage transforms (rotation, crop, clone).

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
#include "image/pixelimage.h"
#include "image/image_description.h"
#include "libheif/heif.h"
#include "libheif/heif_uncompressed.h"
#include <cstring>
#include <memory>


// Holds the non-default values for every ImageDescription metadata field so
// that the same values can be applied to a fresh image and then checked on
// transform outputs.
struct MetadataFixture
{
  nclx_profile nclx;
  uint32_t pixel_ratio_h = 16;
  uint32_t pixel_ratio_v = 9;
  heif_content_light_level clli{1000, 400};
  heif_mastering_display_colour_volume mdcv{};
  heif_tai_timestamp_packet tai{};
  std::string sample_gimi = "urn:uuid:sample-id";
  std::string comp1_gimi  = "urn:uuid:comp1";
  std::string comp2_gimi  = "urn:uuid:comp2";
  BayerPattern bayer;
  PolarizationPattern polar;
  SensorBadPixelsMap badpix;
  SensorNonUniformityCorrection nuc;
  uint8_t chroma_location = 2;
  uint32_t sample_duration = 33; // sequence frame duration
  heif_omaf_image_projection omaf = heif_omaf_image_projection_equirectangular;

  MetadataFixture()
  {
    nclx.set_colour_primaries(heif_color_primaries_ITU_R_BT_2020_2_and_2100_0);
    nclx.set_transfer_characteristics(heif_transfer_characteristic_ITU_R_BT_2100_0_PQ);
    nclx.set_matrix_coefficients(heif_matrix_coefficients_ITU_R_BT_2020_2_non_constant_luminance);
    nclx.set_full_range_flag(false);

    mdcv.display_primaries_x[0] = 100;  mdcv.display_primaries_y[0] = 200;
    mdcv.display_primaries_x[1] = 300;  mdcv.display_primaries_y[1] = 400;
    mdcv.display_primaries_x[2] = 500;  mdcv.display_primaries_y[2] = 600;
    mdcv.white_point_x = 700;           mdcv.white_point_y = 800;
    mdcv.max_display_mastering_luminance = 90000;
    mdcv.min_display_mastering_luminance = 100;

    tai.version = 1;
    tai.tai_timestamp = 1234567890ULL;
    tai.synchronization_state = 1;
    tai.timestamp_generation_failure = 0;
    tai.timestamp_is_modified = 0;

    polar.pattern_width = 2;
    polar.pattern_height = 2;
    polar.polarization_angles = {0.0f, 45.0f, 90.0f, 135.0f};

    badpix.correction_applied = false;
    badpix.bad_rows = {2};
    badpix.bad_columns = {3};
    badpix.bad_pixels.push_back({1, 1});

    nuc.nuc_is_applied = true;
    nuc.image_width = 4;
    nuc.image_height = 2;
    nuc.nuc_gains.assign(8, 1.5f);
    nuc.nuc_offsets.assign(8, 0.25f);
  }
};


// Builds an 8x4 monochrome image with two components added via
// add_component(). Sets every available ImageDescription metadata field.
// Returns the image and the two minted component ids.
static std::shared_ptr<HeifPixelImage>
build_image(MetadataFixture& fix, uint32_t& comp1, uint32_t& comp2)
{
  auto img = std::make_shared<HeifPixelImage>();
  img->create(8, 4, heif_colorspace_monochrome, heif_chroma_planar);

  const heif_security_limits* limits = heif_get_global_security_limits();

  auto r1 = img->add_component(8, 4, heif_cmpd_component_type_monochrome,
                               heif_component_datatype_unsigned_integer, 8, limits);
  REQUIRE(r1);
  comp1 = *r1;

  auto r2 = img->add_component(8, 4, heif_cmpd_component_type_monochrome,
                               heif_component_datatype_unsigned_integer, 8, limits);
  REQUIRE(r2);
  comp2 = *r2;

  // Per-component gimi content IDs
  img->find_component_description(comp1)->gimi_content_id = fix.comp1_gimi;
  img->find_component_description(comp2)->gimi_content_id = fix.comp2_gimi;

  // Image-level metadata
  img->set_premultiplied_alpha(true);
  img->set_color_profile_nclx(fix.nclx);
  img->set_pixel_ratio(fix.pixel_ratio_h, fix.pixel_ratio_v);
  img->set_clli(fix.clli);
  img->set_mdcv(fix.mdcv);
  img->set_tai_timestamp(&fix.tai);
  img->set_gimi_sample_content_id(fix.sample_gimi);
  img->set_sample_duration(fix.sample_duration);

  // Bayer pattern referencing the two component IDs (2x2 GBRG-style).
  fix.bayer.pattern_width = 2;
  fix.bayer.pattern_height = 2;
  fix.bayer.pixels.resize(4);
  fix.bayer.pixels[0].component_id = comp1;  fix.bayer.pixels[0].component_gain = 1.0f;
  fix.bayer.pixels[1].component_id = comp2;  fix.bayer.pixels[1].component_gain = 1.0f;
  fix.bayer.pixels[2].component_id = comp2;  fix.bayer.pixels[2].component_gain = 1.0f;
  fix.bayer.pixels[3].component_id = comp1;  fix.bayer.pixels[3].component_gain = 1.0f;
  img->set_bayer_pattern(fix.bayer);

  fix.polar.component_ids = {comp1};
  img->add_polarization_pattern(fix.polar);

  fix.badpix.component_ids = {comp1};
  img->add_sensor_bad_pixels_map(fix.badpix);

  fix.nuc.component_ids = {comp2};
  img->add_sensor_nuc(fix.nuc);

  img->set_chroma_location(fix.chroma_location);

  img->set_omaf_image_projection(fix.omaf);

  return img;
}


// Asserts that every metadata field from `fix` is present on `img` and that
// the two monochrome components (with their gimi content IDs) survived.
static void check_metadata(const std::shared_ptr<HeifPixelImage>& img,
                           const MetadataFixture& fix)
{
  REQUIRE(img->is_premultiplied_alpha() == true);
  REQUIRE(img->get_color_profile_nclx() == fix.nclx);

  uint32_t h = 0, v = 0;
  img->get_pixel_ratio(&h, &v);
  REQUIRE(h == fix.pixel_ratio_h);
  REQUIRE(v == fix.pixel_ratio_v);

  REQUIRE(img->has_clli());
  REQUIRE(img->get_clli().max_content_light_level == fix.clli.max_content_light_level);
  REQUIRE(img->get_clli().max_pic_average_light_level == fix.clli.max_pic_average_light_level);

  REQUIRE(img->has_mdcv());
  const auto& m = img->get_mdcv();
  REQUIRE(m.display_primaries_x[0] == fix.mdcv.display_primaries_x[0]);
  REQUIRE(m.display_primaries_y[2] == fix.mdcv.display_primaries_y[2]);
  REQUIRE(m.white_point_x == fix.mdcv.white_point_x);
  REQUIRE(m.max_display_mastering_luminance == fix.mdcv.max_display_mastering_luminance);

  const heif_tai_timestamp_packet* tai = img->get_tai_timestamp();
  REQUIRE(tai != nullptr);
  REQUIRE(tai->tai_timestamp == fix.tai.tai_timestamp);
  REQUIRE(tai->synchronization_state == fix.tai.synchronization_state);

  REQUIRE(img->has_gimi_sample_content_id());
  REQUIRE(img->get_gimi_sample_content_id() == fix.sample_gimi);

  REQUIRE(img->has_any_bayer_pattern());
  const BayerPattern& bp = img->get_any_bayer_pattern();
  REQUIRE(bp.pattern_width == fix.bayer.pattern_width);
  REQUIRE(bp.pattern_height == fix.bayer.pattern_height);
  REQUIRE(bp.pixels.size() == fix.bayer.pixels.size());

  REQUIRE(img->has_polarization_patterns());
  REQUIRE(img->get_polarization_patterns().size() == 1);
  REQUIRE(img->get_polarization_patterns()[0].pattern_width == fix.polar.pattern_width);
  REQUIRE(img->get_polarization_patterns()[0].polarization_angles == fix.polar.polarization_angles);

  REQUIRE(img->has_sensor_bad_pixels_maps());
  REQUIRE(img->get_sensor_bad_pixels_maps().size() == 1);
  REQUIRE(img->get_sensor_bad_pixels_maps()[0].bad_rows == fix.badpix.bad_rows);
  REQUIRE(img->get_sensor_bad_pixels_maps()[0].bad_columns == fix.badpix.bad_columns);

  REQUIRE(img->has_sensor_nuc());
  REQUIRE(img->get_sensor_nuc().size() == 1);
  REQUIRE(img->get_sensor_nuc()[0].image_width == fix.nuc.image_width);
  REQUIRE(img->get_sensor_nuc()[0].nuc_gains == fix.nuc.nuc_gains);

  REQUIRE(img->has_chroma_location());
  REQUIRE(img->get_chroma_location() == fix.chroma_location);

  REQUIRE(img->get_sample_duration() == fix.sample_duration);

  REQUIRE(img->get_omaf_image_projection() == fix.omaf);

  // Two monochrome components must still be there, with their gimi content IDs.
  // Rotate / crop re-mint component IDs; create_clone_image_at_new_size reuses
  // them. In both cases the order in m_components is preserved.
  std::vector<std::string> mono_gimi_ids;
  for (const auto& c : img->get_component_descriptions()) {
    if (c.component_type == heif_cmpd_component_type_monochrome) {
      mono_gimi_ids.push_back(c.gimi_content_id);
    }
  }
  REQUIRE(mono_gimi_ids.size() == 2);
  REQUIRE(mono_gimi_ids[0] == fix.comp1_gimi);
  REQUIRE(mono_gimi_ids[1] == fix.comp2_gimi);
}


TEST_CASE("rotate_ccw 90 preserves metadata and components", "[image_description_metadata]")
{
  MetadataFixture fix;
  uint32_t c1, c2;
  auto src = build_image(fix, c1, c2);

  auto rotated_r = src->rotate_ccw(90, heif_get_global_security_limits());
  REQUIRE(rotated_r);
  auto rotated = *rotated_r;

  REQUIRE(rotated->get_width() == 4);
  REQUIRE(rotated->get_height() == 8);

  check_metadata(rotated, fix);
}


TEST_CASE("crop preserves metadata and components", "[image_description_metadata]")
{
  MetadataFixture fix;
  uint32_t c1, c2;
  auto src = build_image(fix, c1, c2);

  // crop to a 4x2 region (left=0, right=3, top=0, bottom=1)
  auto cropped_r = src->crop(0, 3, 0, 1, heif_get_global_security_limits());
  REQUIRE(cropped_r);
  auto cropped = *cropped_r;

  REQUIRE(cropped->get_width() == 4);
  REQUIRE(cropped->get_height() == 2);

  check_metadata(cropped, fix);
}


TEST_CASE("create_clone_image_at_new_size preserves metadata and components",
          "[image_description_metadata]")
{
  MetadataFixture fix;
  uint32_t c1, c2;
  auto src = build_image(fix, c1, c2);

  auto clone = std::make_shared<HeifPixelImage>();
  Error err = clone->create_clone_image_at_new_size(src, 16, 8,
                                                    heif_get_global_security_limits());
  REQUIRE(!err);

  REQUIRE(clone->get_width() == 16);
  REQUIRE(clone->get_height() == 8);

  check_metadata(clone, fix);
}
