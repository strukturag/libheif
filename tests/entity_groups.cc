/*
  libheif regression tests for entity groups (grpl) parsing.

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
#include "libheif/heif.h"
#include "test_utils.h"
#include "libheif/heif_entity_groups.h"
#include "libheif/heif_items.h"
#include "libheif/heif_regions.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// Build a minimal HEIF file whose 'grpl' box contains a single child box
// whose four-cc is NOT one of the registered Box_EntityToGroup subclasses
// (pymd / altr / ster). Such children parse to Box_other and used to
// trigger a NULL-pointer dereference in heif_context_get_entity_groups().
std::vector<uint8_t> build_minimal_heif_with_bogus_grpl_child() {
  // ftyp: heic / 0 / mif1 heic
  std::vector<uint8_t> ftyp_payload;
  append_fourcc(ftyp_payload, "heic");
  put_u32_be(ftyp_payload, 0);
  append_fourcc(ftyp_payload, "mif1");
  append_fourcc(ftyp_payload, "heic");
  auto ftyp = make_box("ftyp", ftyp_payload);

  // hdlr: handler type 'null' so HeifFile::has_images() returns false and
  // pitm/iprp/ipco/ipma are not required for the file to parse.
  std::vector<uint8_t> hdlr_payload;
  put_u32_be(hdlr_payload, 0);       // pre_defined
  append_fourcc(hdlr_payload, "null"); // handler_type (NOT 'pict')
  put_u32_be(hdlr_payload, 0);
  put_u32_be(hdlr_payload, 0);
  put_u32_be(hdlr_payload, 0);
  hdlr_payload.push_back(0);         // name (empty, NUL-terminated)
  auto hdlr = make_box("hdlr", hdlr_payload, /*full=*/true);

  // iinf: zero entries
  std::vector<uint8_t> iinf_payload;
  put_u16_be(iinf_payload, 0);
  auto iinf = make_box("iinf", iinf_payload, /*full=*/true);

  // iloc: offset_size=0, length_size=0, base_offset_size=0, reserved=0, item_count=0
  std::vector<uint8_t> iloc_payload;
  iloc_payload.push_back(0);
  iloc_payload.push_back(0);
  put_u16_be(iloc_payload, 0);
  auto iloc = make_box("iloc", iloc_payload, /*full=*/true);

  // grpl with a single 'pict' child — 'pict' is not registered as an
  // entity-to-group type, so it parses to Box_other.
  auto bogus_child = make_box("pict", {});
  auto grpl = make_box("grpl", bogus_child);

  // meta = hdlr + iinf + iloc + grpl
  std::vector<uint8_t> meta_payload;
  meta_payload.insert(meta_payload.end(), hdlr.begin(), hdlr.end());
  meta_payload.insert(meta_payload.end(), iinf.begin(), iinf.end());
  meta_payload.insert(meta_payload.end(), iloc.begin(), iloc.end());
  meta_payload.insert(meta_payload.end(), grpl.begin(), grpl.end());
  auto meta = make_box("meta", meta_payload, /*full=*/true);

  std::vector<uint8_t> file;
  file.insert(file.end(), ftyp.begin(), ftyp.end());
  file.insert(file.end(), meta.begin(), meta.end());
  return file;
}

heif_error memory_writer(heif_context*, const void* data, size_t size, void* userdata) {
  auto* output = static_cast<std::vector<uint8_t>*>(userdata);
  const auto* bytes = static_cast<const uint8_t*>(data);
  output->insert(output->end(), bytes, bytes + size);
  return {heif_error_Ok, heif_suberror_Unspecified, "Success"};
}

heif_item_id add_test_av1_image(heif_context* ctx, heif_encoder* encoder,
                                heif_image_handle** out_handle = nullptr) {
  heif_image* image = nullptr;
  REQUIRE(heif_image_create(2, 2, heif_colorspace_YCbCr,
                            heif_chroma_420, &image).code == heif_error_Ok);
  REQUIRE(heif_image_add_plane(image, heif_channel_Y, 2, 2, 8).code == heif_error_Ok);
  REQUIRE(heif_image_add_plane(image, heif_channel_Cb, 1, 1, 8).code == heif_error_Ok);
  REQUIRE(heif_image_add_plane(image, heif_channel_Cr, 1, 1, 8).code == heif_error_Ok);

  for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr}) {
    int stride = 0;
    uint8_t* plane = heif_image_get_plane(image, channel, &stride);
    REQUIRE(plane != nullptr);
    const int height = channel == heif_channel_Y ? 2 : 1;
    memset(plane, 128, static_cast<size_t>(stride * height));
  }

  heif_image_handle* handle = nullptr;
  REQUIRE(heif_context_encode_image(ctx, image, encoder, nullptr, &handle).code == heif_error_Ok);
  heif_item_id item_id = heif_image_handle_get_item_id(handle);
  if (out_handle) {
    *out_handle = handle;
  }
  else {
    heif_image_handle_release(handle);
  }
  heif_image_release(image);
  return item_id;
}

} // namespace


// Regression for PR #1806 / NULL-deref in heif_context_get_entity_groups
// when 'grpl' contains a child whose four-cc is not a registered
// Box_EntityToGroup subclass.
TEST_CASE("entity_groups: unknown grpl child does not crash") {
  auto data = build_minimal_heif_with_bogus_grpl_child();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_error err = heif_context_read_from_memory_without_copy(
      ctx, data.data(), data.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  int num_groups = -1;
  heif_entity_group* groups =
      heif_context_get_entity_groups(ctx, 0, 0, &num_groups);
  // Unknown four-cc must be silently skipped, not dereferenced.
  REQUIRE(num_groups == 0);

  heif_entity_groups_release(groups, num_groups);
  heif_context_free(ctx);
}


TEST_CASE("entity_groups: item visibility and ordered groups survive a round trip") {
  heif_context* write_ctx = heif_context_alloc();
  REQUIRE(write_ctx != nullptr);
  heif_context_set_major_brand(write_ctx, heif_brand2_mif1);
  heif_context_set_unif(write_ctx, 1);

  const uint8_t data[] = {0x12, 0x34};
  heif_item_id tmap_id = 0;
  heif_item_id base_id = 0;
  REQUIRE(heif_context_add_mime_item(write_ctx, "application/x-tmap",
                                     heif_metadata_compression_off,
                                     data, sizeof(data), &tmap_id).code == heif_error_Ok);
  REQUIRE(heif_context_add_mime_item(write_ctx, "application/x-base",
                                     heif_metadata_compression_off,
                                     data, sizeof(data), &base_id).code == heif_error_Ok);
  REQUIRE(tmap_id != base_id);

  REQUIRE(heif_item_set_item_hidden(write_ctx, tmap_id, 0).code == heif_error_Ok);
  REQUIRE(heif_item_set_item_hidden(write_ctx, base_id, 1).code == heif_error_Ok);
  REQUIRE(heif_item_is_item_hidden(write_ctx, tmap_id) == 0);
  REQUIRE(heif_item_is_item_hidden(write_ctx, base_id) == 1);

  const heif_item_id alternatives[] = {tmap_id, base_id};
  heif_entity_group_id group_id = 0;
  REQUIRE(heif_context_add_alternative_entity_group(
      write_ctx, alternatives, 2, &group_id).code == heif_error_Ok);
  REQUIRE(group_id != 0);
  REQUIRE(group_id != tmap_id);
  REQUIRE(group_id != base_id);

  std::vector<uint8_t> encoded;
  heif_writer writer{};
  writer.writer_api_version = 1;
  writer.write = memory_writer;
  REQUIRE(heif_context_write(write_ctx, &writer, &encoded).code == heif_error_Ok);
  heif_context_free(write_ctx);

  heif_context* read_ctx = heif_context_alloc();
  REQUIRE(read_ctx != nullptr);
  heif_error read_err = heif_context_read_from_memory_without_copy(
      read_ctx, encoded.data(), encoded.size(), nullptr);
  UNSCOPED_INFO(read_err.message);
  REQUIRE(read_err.code == heif_error_Ok);
  REQUIRE(heif_item_is_item_hidden(read_ctx, tmap_id) == 0);
  REQUIRE(heif_item_is_item_hidden(read_ctx, base_id) == 1);

  int num_groups = 0;
  heif_entity_group* groups = heif_context_get_entity_groups(
      read_ctx, heif_fourcc('a', 'l', 't', 'r'), 0, &num_groups);
  REQUIRE(num_groups == 1);
  REQUIRE(groups != nullptr);
  REQUIRE(groups[0].entity_group_id == group_id);
  REQUIRE(groups[0].num_entities == 2);
  REQUIRE(groups[0].entities[0] == tmap_id);
  REQUIRE(groups[0].entities[1] == base_id);

  heif_entity_groups_release(groups, num_groups);
  heif_context_free(read_ctx);
}


TEST_CASE("entity_groups: writer rejects invalid input without creating a group") {
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  const heif_item_id missing_id = 17;
  heif_entity_group_id group_id = 123;
  heif_error err = heif_context_add_alternative_entity_group(
      ctx, &missing_id, 1, &group_id);
  REQUIRE(err.code == heif_error_Input_does_not_exist);
  REQUIRE(group_id == 0);

  int num_groups = -1;
  heif_entity_group* groups = heif_context_get_entity_groups(ctx, 0, 0, &num_groups);
  REQUIRE(num_groups == 0);
  heif_entity_groups_release(groups, num_groups);

  REQUIRE(heif_item_set_item_hidden(ctx, missing_id, 1).code == heif_error_Input_does_not_exist);
  REQUIRE(heif_context_add_alternative_entity_group(ctx, nullptr, 1, nullptr).code ==
          heif_error_Usage_error);

  heif_context_free(ctx);
}


TEST_CASE("entity_groups: specialized writers enforce group semantics") {
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  heif_context_set_major_brand(ctx, heif_brand2_mif1);
  heif_context_set_unif(ctx, 1);

  const uint8_t data[] = {0x12, 0x34};
  heif_item_id item_ids[2] = {};
  REQUIRE(heif_context_add_mime_item(ctx, "application/x-first",
                                     heif_metadata_compression_off,
                                     data, sizeof(data), &item_ids[0]).code == heif_error_Ok);
  REQUIRE(heif_context_add_mime_item(ctx, "application/x-second",
                                     heif_metadata_compression_off,
                                     data, sizeof(data), &item_ids[1]).code == heif_error_Ok);

  heif_entity_group_id group_id = 123;
  heif_error err = heif_context_add_alternative_entity_group(
      ctx, item_ids, 0, &group_id);
  REQUIRE(err.code == heif_error_Usage_error);
  REQUIRE(err.subcode == heif_suberror_Invalid_parameter_value);
  REQUIRE(group_id == 0);

  err = heif_context_add_stereo_pair_entity_group(
      ctx, item_ids[0], item_ids[1], &group_id);
  REQUIRE(err.code == heif_error_Usage_error);
  REQUIRE(err.subcode == heif_suberror_Invalid_parameter_value);
  REQUIRE(group_id == 0);

  const heif_item_id duplicate_items[] = {item_ids[0], item_ids[0]};
  group_id = 123;
  err = heif_context_add_alternative_entity_group(
      ctx, duplicate_items, 2, &group_id);
  REQUIRE(err.code == heif_error_Usage_error);
  REQUIRE(err.subcode == heif_suberror_Invalid_parameter_value);
  REQUIRE(group_id == 0);

  int num_groups = -1;
  heif_entity_group* groups = heif_context_get_entity_groups(ctx, 0, 0, &num_groups);
  REQUIRE(num_groups == 0);
  heif_entity_groups_release(groups, num_groups);

  heif_encoder* encoder = get_encoder_or_skip_test(heif_compression_AV1);
  const heif_item_id image_ids[] = {
      add_test_av1_image(ctx, encoder), add_test_av1_image(ctx, encoder)};
  heif_encoder_release(encoder);

  group_id = 123;
  err = heif_context_add_stereo_pair_entity_group(
      ctx, image_ids[0], image_ids[0], &group_id);
  REQUIRE(err.code == heif_error_Usage_error);
  REQUIRE(err.subcode == heif_suberror_Invalid_parameter_value);
  REQUIRE(group_id == 0);

  REQUIRE(heif_context_add_stereo_pair_entity_group(
      ctx, image_ids[0], image_ids[1], &group_id).code == heif_error_Ok);

  std::vector<uint8_t> encoded;
  heif_writer writer{};
  writer.writer_api_version = 1;
  writer.write = memory_writer;
  REQUIRE(heif_context_write(ctx, &writer, &encoded).code == heif_error_Ok);
  heif_context_free(ctx);

  heif_context* read_ctx = heif_context_alloc();
  REQUIRE(read_ctx != nullptr);
  heif_error read_err = heif_context_read_from_memory_without_copy(
      read_ctx, encoded.data(), encoded.size(), nullptr);
  UNSCOPED_INFO(read_err.message);
  REQUIRE(read_err.code == heif_error_Ok);

  groups = heif_context_get_entity_groups(
      read_ctx, heif_fourcc('s', 't', 'e', 'r'), 0, &num_groups);
  REQUIRE(num_groups == 1);
  REQUIRE(groups != nullptr);
  REQUIRE(groups[0].num_entities == 2);
  REQUIRE(groups[0].entities[0] == image_ids[0]);
  REQUIRE(groups[0].entities[1] == image_ids[1]);
  heif_entity_groups_release(groups, num_groups);
  heif_context_free(read_ctx);
}


TEST_CASE("entity_groups: an item belongs to at most one alternative group") {
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  const uint8_t data[] = {0x12, 0x34};
  heif_item_id item_ids[3] = {};
  for (int i = 0; i < 3; ++i) {
    REQUIRE(heif_context_add_mime_item(ctx, "application/octet-stream",
                                       heif_metadata_compression_off,
                                       data, sizeof(data), &item_ids[i]).code == heif_error_Ok);
  }

  const heif_item_id first_group[] = {item_ids[0], item_ids[1]};
  heif_entity_group_id first_group_id = 0;
  REQUIRE(heif_context_add_alternative_entity_group(
      ctx, first_group, 2, &first_group_id).code == heif_error_Ok);
  REQUIRE(first_group_id != 0);

  const heif_item_id overlapping_group[] = {item_ids[0], item_ids[2]};
  heif_entity_group_id overlapping_group_id = 123;
  heif_error err = heif_context_add_alternative_entity_group(
      ctx, overlapping_group, 2, &overlapping_group_id);
  REQUIRE(err.code == heif_error_Usage_error);
  REQUIRE(err.subcode == heif_suberror_Invalid_parameter_value);
  REQUIRE(overlapping_group_id == 0);

  int num_groups = -1;
  heif_entity_group* groups = heif_context_get_entity_groups(
      ctx, heif_fourcc('a', 'l', 't', 'r'), 0, &num_groups);
  REQUIRE(num_groups == 1);
  REQUIRE(groups != nullptr);
  REQUIRE(groups[0].entity_group_id == first_group_id);
  heif_entity_groups_release(groups, num_groups);
  heif_context_free(ctx);
}


TEST_CASE("entity_groups: unhiding a region-mask image keeps it non-top-level") {
  heif_encoder* encoder = get_encoder_or_skip_test(heif_compression_AV1);
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  heif_image_handle* primary_handle = nullptr;
  const heif_item_id primary_id = add_test_av1_image(ctx, encoder, &primary_handle);
  const heif_item_id mask_id = add_test_av1_image(ctx, encoder);
  heif_encoder_release(encoder);

  heif_region_item* region_item = nullptr;
  REQUIRE(heif_image_handle_add_region_item(primary_handle, 2, 2, &region_item).code ==
          heif_error_Ok);
  REQUIRE(heif_region_item_add_region_referenced_mask(
      region_item, 0, 0, 2, 2, mask_id, nullptr).code == heif_error_Ok);
  heif_region_item_release(region_item);
  heif_image_handle_release(primary_handle);

  std::vector<uint8_t> encoded;
  heif_writer writer{};
  writer.writer_api_version = 1;
  writer.write = memory_writer;
  REQUIRE(heif_context_write(ctx, &writer, &encoded).code == heif_error_Ok);
  heif_context_free(ctx);

  ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  heif_error read_err = heif_context_read_from_memory_without_copy(
      ctx, encoded.data(), encoded.size(), nullptr);
  UNSCOPED_INFO(read_err.message);
  REQUIRE(read_err.code == heif_error_Ok);
  REQUIRE(heif_context_get_number_of_top_level_images(ctx) == 1);
  REQUIRE(heif_context_is_top_level_image_ID(ctx, primary_id) == 1);
  REQUIRE(heif_context_is_top_level_image_ID(ctx, mask_id) == 0);

  REQUIRE(heif_item_set_item_hidden(ctx, mask_id, 0).code == heif_error_Ok);
  REQUIRE(heif_context_get_number_of_top_level_images(ctx) == 1);
  REQUIRE(heif_context_is_top_level_image_ID(ctx, mask_id) == 0);
  heif_context_free(ctx);
}


TEST_CASE("entity_groups: image visibility keeps top-level queries synchronized") {
  heif_encoder* encoder = get_encoder_or_skip_test(heif_compression_AV1);
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  const heif_item_id primary_id = add_test_av1_image(ctx, encoder);
  const heif_item_id secondary_id = add_test_av1_image(ctx, encoder);
  heif_encoder_release(encoder);

  REQUIRE(heif_item_set_item_hidden(ctx, secondary_id, 1).code == heif_error_Ok);

  std::vector<uint8_t> encoded;
  heif_writer writer{};
  writer.writer_api_version = 1;
  writer.write = memory_writer;
  REQUIRE(heif_context_write(ctx, &writer, &encoded).code == heif_error_Ok);
  heif_context_free(ctx);

  ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  heif_error read_err = heif_context_read_from_memory_without_copy(
      ctx, encoded.data(), encoded.size(), nullptr);
  UNSCOPED_INFO(read_err.message);
  REQUIRE(read_err.code == heif_error_Ok);
  REQUIRE(heif_context_get_number_of_top_level_images(ctx) == 1);
  REQUIRE(heif_context_is_top_level_image_ID(ctx, primary_id) == 1);
  REQUIRE(heif_context_is_top_level_image_ID(ctx, secondary_id) == 0);

  REQUIRE(heif_item_set_item_hidden(ctx, secondary_id, 0).code == heif_error_Ok);
  REQUIRE(heif_context_get_number_of_top_level_images(ctx) == 2);
  REQUIRE(heif_context_is_top_level_image_ID(ctx, secondary_id) == 1);

  REQUIRE(heif_item_set_item_hidden(ctx, secondary_id, 1).code == heif_error_Ok);
  REQUIRE(heif_context_get_number_of_top_level_images(ctx) == 1);
  heif_context_free(ctx);
}


TEST_CASE("entity_groups: primary image cannot be hidden") {
  heif_encoder* encoder = get_encoder_or_skip_test(heif_compression_AV1);
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  const heif_item_id primary_id = add_test_av1_image(ctx, encoder);
  heif_encoder_release(encoder);

  heif_error err = heif_item_set_item_hidden(ctx, primary_id, 1);
  REQUIRE(err.code == heif_error_Usage_error);
  REQUIRE(err.subcode == heif_suberror_Invalid_parameter_value);
  REQUIRE(heif_item_is_item_hidden(ctx, primary_id) == 0);

  std::vector<uint8_t> encoded;
  heif_writer writer{};
  writer.writer_api_version = 1;
  writer.write = memory_writer;
  REQUIRE(heif_context_write(ctx, &writer, &encoded).code == heif_error_Ok);
  heif_context_free(ctx);

  ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  heif_error read_err = heif_context_read_from_memory_without_copy(
      ctx, encoded.data(), encoded.size(), nullptr);
  UNSCOPED_INFO(read_err.message);
  REQUIRE(read_err.code == heif_error_Ok);
  REQUIRE(heif_context_get_number_of_top_level_images(ctx) == 1);
  heif_context_free(ctx);
}


TEST_CASE("entity_groups: promoting a hidden image makes it a visible primary") {
  heif_encoder* encoder = get_encoder_or_skip_test(heif_compression_AV1);
  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);

  const heif_item_id original_primary_id = add_test_av1_image(ctx, encoder);
  heif_image_handle* new_primary_handle = nullptr;
  const heif_item_id new_primary_id = add_test_av1_image(ctx, encoder, &new_primary_handle);
  heif_encoder_release(encoder);

  REQUIRE(heif_item_set_item_hidden(ctx, new_primary_id, 1).code == heif_error_Ok);
  REQUIRE(heif_context_set_primary_image(ctx, new_primary_handle).code == heif_error_Ok);
  heif_image_handle_release(new_primary_handle);

  REQUIRE(heif_item_is_item_hidden(ctx, new_primary_id) == 0);
  REQUIRE(heif_context_is_top_level_image_ID(ctx, new_primary_id) == 1);

  std::vector<uint8_t> encoded;
  heif_writer writer{};
  writer.writer_api_version = 1;
  writer.write = memory_writer;
  REQUIRE(heif_context_write(ctx, &writer, &encoded).code == heif_error_Ok);
  heif_context_free(ctx);

  ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  heif_error read_err = heif_context_read_from_memory_without_copy(
      ctx, encoded.data(), encoded.size(), nullptr);
  UNSCOPED_INFO(read_err.message);
  REQUIRE(read_err.code == heif_error_Ok);

  heif_image_handle* read_primary_handle = nullptr;
  REQUIRE(heif_context_get_primary_image_handle(ctx, &read_primary_handle).code == heif_error_Ok);
  REQUIRE(heif_image_handle_get_item_id(read_primary_handle) == new_primary_id);
  REQUIRE(heif_item_is_item_hidden(ctx, new_primary_id) == 0);
  REQUIRE(heif_context_is_top_level_image_ID(ctx, original_primary_id) == 1);
  REQUIRE(heif_context_is_top_level_image_ID(ctx, new_primary_id) == 1);
  heif_image_handle_release(read_primary_handle);
  heif_context_free(ctx);
}
