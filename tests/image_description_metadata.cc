/*
  libheif unit tests for ImageDescription metadata preservation across
  HeifPixelImage transforms (rotation, crop, clone) and sequence decoding.

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
#include "libheif/heif_sequences.h"
#include "sequences/seq_boxes.h"
#include "sequence_file_builder.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>


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


static heif_error sequence_metadata_writer(heif_context*, const void* data, size_t size, void* userdata)
{
  auto& output = *static_cast<std::vector<uint8_t>*>(userdata);
  const auto* bytes = static_cast<const uint8_t*>(data);
  output.insert(output.end(), bytes, bytes + size);
  return {heif_error_Ok, heif_suberror_Unspecified, nullptr};
}

static std::vector<uint8_t> encode_metadata_sequence()
{
  heif_context* context = heif_context_alloc();
  heif_encoder* encoder = nullptr;
  REQUIRE(heif_context_get_encoder_for_format(context, heif_compression_AV1, &encoder).code == heif_error_Ok);
  heif_track* track = nullptr;
  REQUIRE(heif_context_add_visual_sequence_track(context, 64, 64, heif_track_type_image_sequence,
                                                nullptr, nullptr, &track).code == heif_error_Ok);
  heif_image* image = nullptr;
  REQUIRE(heif_image_create(64, 64, heif_colorspace_YCbCr, heif_chroma_444, &image).code == heif_error_Ok);
  for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}) {
    REQUIRE(heif_image_add_plane(image, channel, 64, 64, 8).code == heif_error_Ok);
    size_t stride = 0;
    uint8_t* pixels = heif_image_get_plane2(image, channel, &stride);
    REQUIRE(pixels != nullptr);
    for (size_t y = 0; y < 64; ++y) {
      std::fill(pixels + y * stride, pixels + y * stride + 64, channel == heif_channel_Y ? 64 : 128);
    }
  }
  heif_image_set_duration(image, 1);
  heif_color_profile_nclx* nclx = heif_nclx_color_profile_alloc();
  REQUIRE(nclx != nullptr);
  nclx->full_range_flag = 1;
  REQUIRE(heif_image_set_nclx_color_profile(image, nclx).code == heif_error_Ok);
  heif_sequence_encoding_options* encoding_options = heif_sequence_encoding_options_alloc();
  REQUIRE(encoding_options != nullptr);
  encoding_options->output_nclx_profile = nclx;
  encoding_options->gop_structure = heif_sequence_gop_structure_intra_only;
  for (int frame = 0; frame < 3; ++frame) {
    REQUIRE(heif_track_encode_sequence_image(track, image, encoder, encoding_options).code == heif_error_Ok);
  }
  REQUIRE(heif_track_encode_end_of_sequence(track, encoder).code == heif_error_Ok);
  std::vector<uint8_t> file;
  heif_writer writer{};
  writer.writer_api_version = 1;
  writer.write = sequence_metadata_writer;
  REQUIRE(heif_context_write(context, &writer, &file).code == heif_error_Ok);
  heif_sequence_encoding_options_release(encoding_options);
  heif_nclx_color_profile_free(nclx);
  heif_image_release(image);
  heif_track_release(track);
  heif_encoder_release(encoder);
  heif_context_free(context);
  return file;
}

static void add_sequence_metadata(std::vector<uint8_t>& file, const std::vector<uint8_t>& icc,
                                  bool multiple_descriptions = false)
{
  REQUIRE(seqfile::rewrite_moov_keeping_offsets(file, [&](const std::vector<uint8_t>& encoded_moov) {
    // With two descriptions, the first sample uses the first sample entry and the
    // remaining samples use a copy of it, which gets a different 'clli' below.
    std::vector<uint8_t> moov_data = encoded_moov;
    if (multiple_descriptions) {
      auto split = seqfile::split_first_sample_into_own_chunk(encoded_moov, true);
      REQUIRE(split);
      moov_data = std::move(*split);
    }

    auto reader = std::make_shared<StreamReader_memory>(moov_data.data(), moov_data.size(), false);
    BitstreamRange range(reader, moov_data.size());
    std::shared_ptr<Box> moov;
    REQUIRE(Box::read(range, &moov, heif_get_global_security_limits()) == Error::Ok);
    auto trak = moov->get_child_box<Box_trak>();
    REQUIRE(trak != nullptr);
    auto mdia = trak->get_child_box<Box_mdia>();
    REQUIRE(mdia != nullptr);
    auto minf = mdia->get_child_box<Box_minf>();
    REQUIRE(minf != nullptr);
    auto stbl = minf->get_child_box<Box_stbl>();
    REQUIRE(stbl != nullptr);
    auto stsd = stbl->get_child_box<Box_stsd>();
    REQUIRE(stsd != nullptr);
    REQUIRE(stsd->get_num_sample_entries() == (multiple_descriptions ? 2 : 1));

    for (size_t i = 0; i < stsd->get_num_sample_entries(); i++) {
      auto entry = std::const_pointer_cast<Box>(stsd->get_sample_entry(i));
      REQUIRE(entry != nullptr);
      nclx_profile profile;
      profile.set_colour_primaries(9);
      profile.set_transfer_characteristics(16);
      profile.set_matrix_coefficients(9);
      profile.set_full_range_flag(false);
      auto colr = std::make_shared<Box_colr>();
      colr->set_color_profile(std::make_shared<color_profile_nclx>(profile));
      entry->replace_child_box(colr);
      auto raw_colr = std::make_shared<Box_colr>();
      raw_colr->set_color_profile(std::make_shared<color_profile_raw>(fourcc("prof"), icc));
      entry->append_child_box(raw_colr);
      auto clli = std::make_shared<Box_clli>();
      clli->clli = (i == 0) ? heif_content_light_level{1000, 400} : heif_content_light_level{2000, 800};
      entry->append_child_box(clli);
      auto mdcv = std::make_shared<Box_mdcv>();
      mdcv->mdcv.max_display_mastering_luminance = 10000000;
      mdcv->mdcv.min_display_mastering_luminance = 50;
      entry->append_child_box(mdcv);
      auto pasp = std::make_shared<Box_pasp>();
      pasp->hSpacing = 4;
      pasp->vSpacing = 3;
      entry->append_child_box(pasp);
    }

    StreamWriter output;
    REQUIRE(moov->write(output) == Error::Ok);
    return std::optional<std::vector<uint8_t>>(output.get_data());
  }));
}

TEST_CASE("sequence sample-entry colour metadata is applied to every decoded frame")
{
  if (!heif_have_encoder_for_format(heif_compression_AV1) ||
      !heif_have_decoder_for_format(heif_compression_AV1)) {
    SKIP("No AV1 encoder/decoder pair available");
  }
  enum class SampleEntryMetadata { none, single_description, two_descriptions };
  const auto metadata_mode = GENERATE(SampleEntryMetadata::none,
                                      SampleEntryMetadata::single_description,
                                      SampleEntryMetadata::two_descriptions);
  const bool with_metadata = metadata_mode != SampleEntryMetadata::none;
  const bool multiple_descriptions = metadata_mode == SampleEntryMetadata::two_descriptions;
  const bool convert_to_rgb = GENERATE(false, true);
  const bool autocorrect = GENERATE(false, true);
  INFO("sample-entry metadata: " << with_metadata << ", two sample descriptions: " << multiple_descriptions);
  INFO("RGB conversion: " << convert_to_rgb << ", autocorrect: " << autocorrect);
  auto file = encode_metadata_sequence();
  const std::vector<uint8_t> icc = {1, 2, 3, 4, 5};
  if (with_metadata) {
    add_sequence_metadata(file, icc, multiple_descriptions);
  }
  heif_context* context = heif_context_alloc();
  const auto read_error = heif_context_read_from_memory(context, file.data(), file.size(), nullptr);
  INFO(read_error.message);
  REQUIRE(read_error.code == heif_error_Ok);
  heif_track* track = heif_context_get_track(context, 0);
  REQUIRE(track != nullptr);
  heif_decoding_options* options = heif_decoding_options_alloc();
  options->ignore_sequence_editlist = 1;
  options->output_image_nclx_profile_passthrough = 1;
  options->autocorrect_broken_input = autocorrect;
  for (int frame = 0; frame < 3; ++frame) {
    INFO("frame: " << frame);
    heif_image* image = nullptr;
    REQUIRE(heif_track_decode_next_image(track, &image,
                                         convert_to_rgb ? heif_colorspace_RGB : heif_colorspace_undefined,
                                         convert_to_rgb ? heif_chroma_444 : heif_chroma_undefined,
                                         options).code == heif_error_Ok);
    REQUIRE(image != nullptr);
    // With sample-entry metadata, the colr box disagrees with the bitstream (always reported)
    // and its range is limited while the bitstream says full (autocorrected on request).
    heif_error warnings[3]{};
    const int num_warnings = heif_image_get_decoding_warnings(image, 0, warnings, 3);
    CHECK(num_warnings == (with_metadata ? (autocorrect ? 2 : 1) : 0));
    for (int i = 0; i < num_warnings; i++) {
      CHECK(warnings[i].subcode == heif_suberror_NCLX_colr_VUI_mismatch);
    }
    if (with_metadata && convert_to_rgb) {
      size_t stride = 0;
      const uint8_t* pixels = heif_image_get_plane_readonly2(image, heif_channel_R, &stride);
      REQUIRE(pixels != nullptr);
      CHECK(pixels[0] >= (autocorrect ? 63 : 55));
      CHECK(pixels[0] <= (autocorrect ? 65 : 57));
    }
    if (with_metadata && !convert_to_rgb) {
      heif_color_profile_nclx* nclx = nullptr;
      REQUIRE(heif_image_get_nclx_color_profile(image, &nclx).code == heif_error_Ok);
      CHECK(nclx->color_primaries == 9);
      CHECK(nclx->transfer_characteristics == 16);
      CHECK(nclx->matrix_coefficients == 9);
      CHECK(nclx->full_range_flag == (autocorrect ? 1 : 0));
      heif_nclx_color_profile_free(nclx);
    }
    CHECK(heif_image_get_raw_color_profile_size(image) == (with_metadata ? icc.size() : 0));
    CHECK(heif_image_has_content_light_level(image) == (with_metadata ? 1 : 0));
    CHECK(heif_image_has_mastering_display_colour_volume(image) == (with_metadata ? 1 : 0));
    uint32_t pasp_h = 0, pasp_v = 0;
    heif_image_get_pixel_aspect_ratio(image, &pasp_h, &pasp_v);
    CHECK(pasp_h == (with_metadata ? 4 : 1));
    CHECK(pasp_v == (with_metadata ? 3 : 1));
    if (with_metadata) {
      std::vector<uint8_t> actual(icc.size());
      REQUIRE(heif_image_get_raw_color_profile(image, actual.data()).code == heif_error_Ok);
      CHECK(actual == icc);
      heif_content_light_level clli{};
      heif_image_get_content_light_level(image, &clli);
      CHECK(clli.max_content_light_level == (multiple_descriptions && frame > 0 ? 2000 : 1000));
      CHECK(clli.max_pic_average_light_level == (multiple_descriptions && frame > 0 ? 800 : 400));
      heif_mastering_display_colour_volume mdcv{};
      heif_image_get_mastering_display_colour_volume(image, &mdcv);
      CHECK(mdcv.max_display_mastering_luminance == 10000000);
      CHECK(mdcv.min_display_mastering_luminance == 50);
    }
    heif_image_release(image);
  }
  {
    heif_image* image = nullptr;
    CHECK(heif_track_decode_next_image(track, &image, heif_colorspace_undefined,
                                       heif_chroma_undefined, options).code == heif_error_End_of_sequence);
  }
  heif_decoding_options_free(options);
  heif_track_release(track);
  heif_context_free(context);
}
