/*
  libheif integration tests for regions

  MIT License

  Copyright (c) 2023 Brad Hards <bradh@frogmouth.net>

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
#include "api_structs.h"
#include "libheif/heif.h"
#include "test-config.h"
#include "test_utils.h"
#include <cstddef>
#include <cstdint>
#include <cstring>


TEST_CASE("no regions") {
  // skip test if we do not have the uncompressed codec
  if (!heif_have_decoder_for_format(heif_compression_uncompressed)) {
    SKIP("Skipping test because uncompressed codec is not compiled.");
  }

  auto context = get_context_for_test_file("uncompressed_comp_RGB.heif");
  heif_image_handle *handle = get_primary_image_handle(context);
  int num_region_items = heif_image_handle_get_number_of_region_items(handle);
  REQUIRE(num_region_items == 0);
  heif_image_handle_release(handle);
  heif_context_free(context);
}

TEST_CASE("create regions") {
  struct heif_error err;

  heif_image* img;
  uint32_t input_width = 1280;
  uint32_t input_height = 1024;
  heif_image_create(input_width,input_height, heif_colorspace_YCbCr, heif_chroma_420, &img);
  fill_new_plane(img, heif_channel_Y, input_width, input_height);
  fill_new_plane(img, heif_channel_Cb, (input_width+1)/2, (input_height+1)/2);
  fill_new_plane(img, heif_channel_Cr, (input_width+1)/2, (input_height+1)/2);

  heif_context* ctx = heif_context_alloc();
  heif_encoder* enc;
  err = heif_context_get_encoder_for_format(ctx, heif_compression_AV1, &enc);
  REQUIRE(err.code == heif_error_Ok);

  struct heif_encoding_options* options;
  options = heif_encoding_options_alloc();
  options->macOS_compatibility_workaround = false;
  options->macOS_compatibility_workaround_no_nclx_profile = false;
  options->image_orientation = heif_orientation_normal;

  heif_image_handle* handle;
  err = heif_context_encode_image(ctx, img, enc, options, &handle);
  REQUIRE(err.code == heif_error_Ok);

  struct heif_region_item* region_item1;
  err = heif_image_handle_add_region_item(handle, input_width, input_height, &region_item1);
  REQUIRE(err.code == heif_error_Ok);
  struct heif_region *regionA;
  err = heif_region_item_add_region_point(region_item1, 100, 200, &regionA);
  REQUIRE(err.code == heif_error_Ok);
  struct heif_region *regionB;
  err = heif_region_item_add_region_rectangle(region_item1, 150, 250, 30, 50, &regionB);
  REQUIRE(err.code == heif_error_Ok);
  int32_t polylinePoints[6] = {10, 20, 15, 20, 15, 50};
  int nPolylinePoints = (sizeof(polylinePoints)/sizeof(polylinePoints[0])) / 2;
  err = heif_region_item_add_region_polyline(region_item1, polylinePoints, nPolylinePoints, NULL);
  REQUIRE(err.code == heif_error_Ok);

  struct heif_region_item* region_item2;
  err = heif_image_handle_add_region_item(handle, input_width, input_height, &region_item2);
  REQUIRE(err.code == heif_error_Ok);
  struct heif_region *regionC;
  err = heif_region_item_add_region_ellipse(region_item2, 350, 450, 60, 80, &regionC);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_region_item_add_region_point(region_item2, 360, 460, NULL);
  REQUIRE(err.code == heif_error_Ok);
  err = heif_region_item_add_region_rectangle(region_item2, 370, 420, 10, 16, NULL);
  REQUIRE(err.code == heif_error_Ok);
  int32_t polygonPoints[6] = {100, 120, 115, 120, 125, 150};
  int nPolygonPoints = (sizeof(polygonPoints)/sizeof(polygonPoints[0])) / 2;
  err = heif_region_item_add_region_polygon(region_item2, polygonPoints, nPolygonPoints, NULL);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_write_to_file(ctx, "regions.heif");
  REQUIRE(err.code == heif_error_Ok);

  heif_region_release(regionA);
  heif_region_release(regionB);
  heif_region_release(regionC);
  heif_region_item_release(region_item1);
  heif_region_item_release(region_item2);

  heif_image_handle_release(handle);
  heif_encoder_release(enc);
  heif_encoding_options_free(options);
  heif_context_free(ctx);
  heif_image_release(img);

  heif_context *readbackCtx = get_context_for_local_file("regions.heif");
  heif_image_handle *readbackHandle = get_primary_image_handle(readbackCtx);
  int num_region_items = heif_image_handle_get_number_of_region_items(readbackHandle);
  REQUIRE(num_region_items == 2);
  std::vector<heif_item_id> region_item_ids_minus(1);
  int num_returned = heif_image_handle_get_list_of_region_item_ids(readbackHandle, region_item_ids_minus.data(), 1);
  REQUIRE(num_returned == 1);
  std::vector<heif_item_id> region_item_ids_extra(3);
  num_returned = heif_image_handle_get_list_of_region_item_ids(readbackHandle, region_item_ids_extra.data(), 3);
  REQUIRE(num_returned == num_region_items);
  std::vector<heif_item_id> region_item_ids(num_region_items);
  num_returned = heif_image_handle_get_list_of_region_item_ids(readbackHandle, region_item_ids.data(), num_region_items);
  REQUIRE(num_returned == 2);
  heif_region_item* in_region_1;
  err = heif_context_get_region_item(readbackCtx, region_item_ids[0], &in_region_1);
  heif_item_id id1 = heif_region_item_get_id(in_region_1);
  REQUIRE(id1 == region_item_ids[0]);
  uint32_t width1;
  uint32_t height1;
  heif_region_item_get_reference_size(in_region_1, &width1, &height1);
  REQUIRE(width1 == input_width);
  REQUIRE(height1 == input_height);
  int num_regions_region_item1 = heif_region_item_get_number_of_regions(in_region_1);
  REQUIRE(num_regions_region_item1 == 3);
  std::vector<heif_region*> out_regions_1(num_regions_region_item1);
  int num_regions1 = heif_region_item_get_list_of_regions(in_region_1, out_regions_1.data(), (int)(out_regions_1.size()));
  REQUIRE(num_regions1 == num_regions_region_item1);
  REQUIRE(heif_region_get_type(out_regions_1[0]) == heif_region_type_point);
  int32_t x, y;
  err = heif_region_get_point(out_regions_1[0], &x, &y);
  REQUIRE (err.code == heif_error_Ok);
  REQUIRE(x == 100);
  REQUIRE(y == 200);
  REQUIRE(heif_region_get_type(out_regions_1[1]) == heif_region_type_rectangle);
  uint32_t width, height;
  err = heif_region_get_rectangle(out_regions_1[1], &x, &y, &width, &height);
  REQUIRE (err.code == heif_error_Ok);
  REQUIRE(x == 150);
  REQUIRE(y == 250);
  REQUIRE(width == 30);
  REQUIRE(height == 50);
  REQUIRE(heif_region_get_type(out_regions_1[2]) == heif_region_type_polyline);
  int num_polyline_points_out = heif_region_get_polyline_num_points(out_regions_1[2]);
  REQUIRE(num_polyline_points_out == 3);
  std::vector<int32_t> polyline(num_polyline_points_out * 2);
  err = heif_region_get_polyline_points(out_regions_1[2], polyline.data());
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(polyline[0] == 10);
  REQUIRE(polyline[1] == 20);
  REQUIRE(polyline[2] == 15);
  REQUIRE(polyline[3] == 20);
  REQUIRE(polyline[4] == 15);
  REQUIRE(polyline[5] == 50);
  heif_region_release_many(out_regions_1.data(), (int)(out_regions_1.size()));

  heif_region_item* in_region_2;
  err = heif_context_get_region_item(readbackCtx, region_item_ids[1], &in_region_2);
  heif_item_id id2 = heif_region_item_get_id(in_region_2);
  REQUIRE(id2 == region_item_ids[1]);
  uint32_t width2;
  uint32_t height2;
  heif_region_item_get_reference_size(in_region_2, &width2, &height2);
  REQUIRE(width2 == input_width);
  REQUIRE(height2 == input_height);
  int num_regions_region_item2 = heif_region_item_get_number_of_regions(in_region_2);
  REQUIRE(num_regions_region_item2 == 4);
  std::vector<heif_region*> out_regions_2(num_regions_region_item2);
  int num_regions2 = heif_region_item_get_list_of_regions(in_region_2, out_regions_2.data(), (int)(out_regions_2.size()));
  REQUIRE(num_regions2 == num_regions_region_item2);
  REQUIRE(heif_region_get_type(out_regions_2[0]) == heif_region_type_ellipse);
  uint32_t radius_x, radius_y;
  err = heif_region_get_ellipse(out_regions_2[0], &x, &y, &radius_x, &radius_y);
  REQUIRE (err.code == heif_error_Ok);
  REQUIRE(x == 350);
  REQUIRE(y == 450);
  REQUIRE(radius_x == 60);
  REQUIRE(radius_y == 80);
  heif_region_release(out_regions_2[0]);
  REQUIRE(heif_region_get_type(out_regions_2[1]) == heif_region_type_point);
  err = heif_region_get_point(out_regions_2[1], &x, &y);
  REQUIRE (err.code == heif_error_Ok);
  REQUIRE(x == 360);
  REQUIRE(y == 460);
  heif_region_release(out_regions_2[1]);
  REQUIRE(heif_region_get_type(out_regions_2[2]) == heif_region_type_rectangle);
  err = heif_region_get_rectangle(out_regions_2[2], &x, &y, &width, &height);
  REQUIRE (err.code == heif_error_Ok);
  REQUIRE(x == 370);
  REQUIRE(y == 420);
  REQUIRE(width == 10);
  REQUIRE(height == 16);
  heif_region_release(out_regions_2[2]);
  REQUIRE(heif_region_get_type(out_regions_2[3]) == heif_region_type_polygon);
  int num_polygon_points_out = heif_region_get_polygon_num_points(out_regions_2[3]);
  REQUIRE(num_polygon_points_out == 3);
  std::vector<int32_t> polygon(num_polygon_points_out * 2);
  err = heif_region_get_polygon_points(out_regions_2[3], polygon.data());
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(polygon[0] == 100);
  REQUIRE(polygon[1] == 120);
  REQUIRE(polygon[2] == 115);
  REQUIRE(polygon[3] == 120);
  REQUIRE(polygon[4] == 125);
  REQUIRE(polygon[5] == 150);
  heif_region_release(out_regions_2[3]);

  heif_region_item_release(in_region_1);
  heif_region_item_release(in_region_2);
  heif_image_handle_release(readbackHandle);
  heif_context_free(readbackCtx);
}

TEST_CASE("create mask region") {
  struct heif_error err;

  heif_image* img;
  uint32_t input_width = 1280;
  uint32_t input_height = 1024;
  heif_image_create(input_width, input_height, heif_colorspace_YCbCr,
                    heif_chroma_420, &img);
  fill_new_plane(img, heif_channel_Y, input_width, input_height);
  fill_new_plane(img, heif_channel_Cb, (input_width + 1) / 2,
                 (input_height + 1) / 2);
  fill_new_plane(img, heif_channel_Cr, (input_width + 1) / 2,
                 (input_height + 1) / 2);

  heif_context *ctx = heif_context_alloc();
  heif_encoder *enc;
  err = heif_context_get_encoder_for_format(ctx, heif_compression_AV1, &enc);
  REQUIRE(err.code == heif_error_Ok);

  struct heif_encoding_options *options;
  options = heif_encoding_options_alloc();
  options->macOS_compatibility_workaround = false;
  options->macOS_compatibility_workaround_no_nclx_profile = false;
  options->image_orientation = heif_orientation_normal;

  heif_image_handle *handle;
  err = heif_context_encode_image(ctx, img, enc, options, &handle);
  REQUIRE(err.code == heif_error_Ok);

  heif_image *mask;
  heif_image_create(128, 64, heif_colorspace_monochrome,
                    heif_chroma_monochrome, &mask);
  fill_new_plane(mask, heif_channel_Y, 128, 64);
  int mask_stride;
  uint8_t* p = heif_image_get_plane(mask, heif_channel_Y, &mask_stride);
  p[0] = 0xff;
  p[127] = 0x00;
  p[128*64 - 1] = 0xfe;

  heif_encoder *mask_enc;
  err = heif_context_get_encoder_for_format(ctx, heif_compression_mask,
                                            &mask_enc);
  REQUIRE(err.code == heif_error_Ok);
  heif_image_handle *mask_handle;
  err = heif_context_encode_image(ctx, mask, mask_enc, options, &mask_handle);
  REQUIRE(err.code == heif_error_Ok);

  struct heif_region_item *region_item1;
  err = heif_image_handle_add_region_item(handle, input_width, input_height,
                                          &region_item1);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_region_item_add_region_referenced_mask(
      region_item1, 200, 140, 128, 64,
      heif_image_handle_get_item_id(mask_handle), NULL);
  REQUIRE(err.code == heif_error_Ok);
  // TODO: maybe another mask or two?

  err = heif_context_write_to_file(ctx, "regions_mask.heif");
  REQUIRE(err.code == heif_error_Ok);

  heif_region_item_release(region_item1);

  heif_image_handle_release(handle);
  heif_encoder_release(enc);
  heif_image_handle_release(mask_handle);
  heif_encoder_release(mask_enc);
  heif_encoding_options_free(options);
  heif_context_free(ctx);
  heif_image_release(img);
  heif_image_release(mask);

  heif_context *readbackCtx = get_context_for_local_file("regions_mask.heif");
  heif_image_handle *readbackHandle = get_primary_image_handle(readbackCtx);
  int num_region_items = heif_image_handle_get_number_of_region_items(readbackHandle);
  REQUIRE(num_region_items == 1);
  std::vector<heif_item_id> region_item_ids(num_region_items);
  int num_returned = heif_image_handle_get_list_of_region_item_ids(readbackHandle, region_item_ids.data(), num_region_items);
  REQUIRE(num_returned == 1);
  heif_region_item* in_region_item;
  err = heif_context_get_region_item(readbackCtx, region_item_ids[0], &in_region_item);
  heif_item_id id1 = heif_region_item_get_id(in_region_item);
  REQUIRE(id1 == region_item_ids[0]);
  int num_regions = heif_region_item_get_number_of_regions(in_region_item);
  REQUIRE(num_regions == 1);
  std::vector<heif_region*> regions(num_regions);
  int num_regions_returned = heif_region_item_get_list_of_regions(in_region_item, regions.data(), (int)(regions.size()));
  REQUIRE(num_regions_returned == num_regions);
  REQUIRE(heif_region_get_type(regions[0]) == heif_region_type_referenced_mask);
  int32_t x, y;
  uint32_t width, height;
  heif_item_id referenced_item_id;
  err = heif_region_get_referenced_mask_ID(regions[0], &x, &y, &width, &height, &referenced_item_id);
  REQUIRE (err.code == heif_error_Ok);
  REQUIRE(x == 200);
  REQUIRE(y == 140);
  REQUIRE(width == 128);
  REQUIRE(height == 64);
  // This is kind of an implementation detail, but checks the iref is in the right direction
  REQUIRE(referenced_item_id == 2);
  heif_image_handle* mski_handle_in;
  err = heif_context_get_image_handle(readbackCtx, referenced_item_id, &mski_handle_in);
  REQUIRE(err.code == heif_error_Ok);
  heif_image* mski_in;
  err = heif_decode_image(mski_handle_in, &mski_in, heif_colorspace_monochrome, heif_chroma_monochrome, NULL);
  REQUIRE(err.code == heif_error_Ok);
  int mski_in_width = heif_image_get_width(mski_in, heif_channel_Y);
  int mski_in_height = heif_image_get_height(mski_in, heif_channel_Y);
  REQUIRE(mski_in_width == 128);
  REQUIRE(mski_in_height == 64);
  int mask_stride_in;
  uint8_t* plane_in = heif_image_get_plane(mski_in, heif_channel_Y, &mask_stride_in);
  REQUIRE(plane_in[0] == 0xff);
  REQUIRE(plane_in[mski_in_width - 1] == 0x00);
  REQUIRE(plane_in[mski_in_width*mski_in_height - 1] == 0xfe);

  heif_region_release(regions[0]);
  heif_image_release(mski_in);
  heif_image_handle_release(mski_handle_in);
  heif_region_item_release(in_region_item);
  heif_image_handle_release(readbackHandle);
  heif_context_free(readbackCtx);
}


TEST_CASE("create inline mask region from data") {
  struct heif_error err;

  heif_image* img;
  uint32_t input_width = 1280;
  uint32_t input_height = 1024;
  heif_image_create(input_width, input_height, heif_colorspace_YCbCr,
                    heif_chroma_420, &img);
  fill_new_plane(img, heif_channel_Y, input_width, input_height);
  fill_new_plane(img, heif_channel_Cb, (input_width + 1) / 2,
                 (input_height + 1) / 2);
  fill_new_plane(img, heif_channel_Cr, (input_width + 1) / 2,
                 (input_height + 1) / 2);

  heif_context *ctx = heif_context_alloc();
  heif_encoder *enc;
  err = heif_context_get_encoder_for_format(ctx, heif_compression_AV1, &enc);
  REQUIRE(err.code == heif_error_Ok);

  struct heif_encoding_options *options;
  options = heif_encoding_options_alloc();
  options->macOS_compatibility_workaround = false;
  options->macOS_compatibility_workaround_no_nclx_profile = false;
  options->image_orientation = heif_orientation_normal;

  heif_image_handle *handle;
  err = heif_context_encode_image(ctx, img, enc, options, &handle);
  REQUIRE(err.code == heif_error_Ok);

  struct heif_region_item *region_item1;
  err = heif_image_handle_add_region_item(handle, input_width, input_height,
                                          &region_item1);
  REQUIRE(err.code == heif_error_Ok);

  std::vector<uint8_t> mask_data((64 * 3) / 8);
  mask_data[0] = 0x80;
  mask_data[2] = 0x7f;
  mask_data[10] = 0x3e;
  mask_data[18] = 0x1d;
  mask_data[23] = 0x01; 
  err = heif_region_item_add_region_inline_mask_data(region_item1, 20, 50, 64, 3, mask_data.data(), mask_data.size(), NULL);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_write_to_file(ctx, "regions_mask_inline_data.heif");
  REQUIRE(err.code == heif_error_Ok);

  heif_region_item_release(region_item1);

  heif_image_handle_release(handle);
  heif_encoder_release(enc);
  heif_encoding_options_free(options);
  heif_context_free(ctx);
  heif_image_release(img);;

  heif_context *readbackCtx = get_context_for_local_file("regions_mask_inline_data.heif");
  heif_image_handle *readbackHandle = get_primary_image_handle(readbackCtx);
  int num_region_items = heif_image_handle_get_number_of_region_items(readbackHandle);
  REQUIRE(num_region_items == 1);
  std::vector<heif_item_id> region_item_ids(num_region_items);
  int num_returned = heif_image_handle_get_list_of_region_item_ids(readbackHandle, region_item_ids.data(), num_region_items);
  REQUIRE(num_returned == 1);
  heif_region_item* in_region_item;
  err = heif_context_get_region_item(readbackCtx, region_item_ids[0], &in_region_item);
  heif_item_id id1 = heif_region_item_get_id(in_region_item);
  REQUIRE(id1 == region_item_ids[0]);
  int num_regions = heif_region_item_get_number_of_regions(in_region_item);
  REQUIRE(num_regions == 1);
  std::vector<heif_region*> regions(num_regions);
  int num_regions_returned = heif_region_item_get_list_of_regions(in_region_item, regions.data(), (int)(regions.size()));
  REQUIRE(num_regions_returned == num_regions);
  REQUIRE(heif_region_get_type(regions[0]) == heif_region_type_inline_mask);
  size_t data_len = heif_region_get_inline_mask_data_len(regions[0]);
  int32_t x, y;
  uint32_t width, height;
  std::vector<uint8_t> mask_data_in(data_len);
  err = heif_region_get_inline_mask_data(regions[0], &x, &y, &width, &height, mask_data_in.data());
  REQUIRE (err.code == heif_error_Ok);
  REQUIRE(x == 20);
  REQUIRE(y == 50);
  REQUIRE(width == 64);
  REQUIRE(height == 3);
  REQUIRE(mask_data_in[0] == 0x80);
  REQUIRE(mask_data_in[1] == 0x00);
  REQUIRE(mask_data_in[2] == 0x7f);
  REQUIRE(mask_data_in[10] == 0x3e);
  REQUIRE(mask_data_in[18] == 0x1d);
  REQUIRE(mask_data_in[23] == 0x01);
  heif_region_release(regions[0]);

  heif_region_item_release(in_region_item);
  heif_image_handle_release(readbackHandle);
  heif_context_free(readbackCtx);
}


TEST_CASE("create inline mask region from image") {
  struct heif_error err;

  heif_image* img;
  uint32_t input_width = 1280;
  uint32_t input_height = 1024;
  heif_image_create(input_width, input_height, heif_colorspace_YCbCr,
                    heif_chroma_420, &img);
  fill_new_plane(img, heif_channel_Y, input_width, input_height);
  fill_new_plane(img, heif_channel_Cb, (input_width + 1) / 2,
                 (input_height + 1) / 2);
  fill_new_plane(img, heif_channel_Cr, (input_width + 1) / 2,
                 (input_height + 1) / 2);

  heif_context *ctx = heif_context_alloc();
  heif_encoder *enc;
  err = heif_context_get_encoder_for_format(ctx, heif_compression_AV1, &enc);
  REQUIRE(err.code == heif_error_Ok);

  struct heif_encoding_options *options;
  options = heif_encoding_options_alloc();
  options->macOS_compatibility_workaround = false;
  options->macOS_compatibility_workaround_no_nclx_profile = false;
  options->image_orientation = heif_orientation_normal;

  heif_image_handle *handle;
  err = heif_context_encode_image(ctx, img, enc, options, &handle);
  REQUIRE(err.code == heif_error_Ok);

  struct heif_region_item *region_item1;
  err = heif_image_handle_add_region_item(handle, input_width, input_height,
                                          &region_item1);
  REQUIRE(err.code == heif_error_Ok);

  heif_image* mask_image;
  heif_image_create(64, 3, heif_colorspace_monochrome,
                    heif_chroma_monochrome, &mask_image);
  err = heif_image_add_plane(mask_image, heif_channel_Y, 64, 3, 8);
  REQUIRE(err.code == heif_error_Ok);
  int stride;
  uint8_t* p = heif_image_get_plane(mask_image, heif_channel_Y, &stride);
  memset(p, 0, 3 * stride);
  p[0] = 0x82;
  p[17] = 0x81;
  p[18] = 0x81;
  p[19] = 0x81;
  p[20] = 0x81;
  p[21] = 0xff;
  p[22] = 0xff;
  p[23] = 0xff;
  p[stride + 18] = 0x86;
  p[stride + 19] = 0x86;
  p[stride + 20] = 0x87;
  p[stride + 21] = 0x87;
  p[stride + 22] = 0x87;
  p[2*stride + 19] = 0x81;
  p[2*stride + 20] = 0x81;
  p[2*stride + 21] = 0x81;
  p[2*stride + 23] = 0x81;
  p[2*stride + 63] = 0x81;
  err = heif_region_item_add_region_inline_mask(region_item1, 20, 50, 64, 3, mask_image, NULL);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_write_to_file(ctx, "regions_mask_inline_image.heif");
  REQUIRE(err.code == heif_error_Ok);

  heif_image_release(mask_image);
  heif_region_item_release(region_item1);

  heif_image_handle_release(handle);
  heif_encoder_release(enc);
  heif_encoding_options_free(options);
  heif_context_free(ctx);
  heif_image_release(img);;

  heif_context *readbackCtx = get_context_for_local_file("regions_mask_inline_image.heif");
  heif_image_handle *readbackHandle = get_primary_image_handle(readbackCtx);
  int num_region_items = heif_image_handle_get_number_of_region_items(readbackHandle);
  REQUIRE(num_region_items == 1);
  std::vector<heif_item_id> region_item_ids(num_region_items);
  int num_returned = heif_image_handle_get_list_of_region_item_ids(readbackHandle, region_item_ids.data(), num_region_items);
  REQUIRE(num_returned == 1);
  heif_region_item* in_region_item;
  err = heif_context_get_region_item(readbackCtx, region_item_ids[0], &in_region_item);
  heif_item_id id1 = heif_region_item_get_id(in_region_item);
  REQUIRE(id1 == region_item_ids[0]);
  int num_regions = heif_region_item_get_number_of_regions(in_region_item);
  REQUIRE(num_regions == 1);
  std::vector<heif_region*> regions(num_regions);
  int num_regions_returned = heif_region_item_get_list_of_regions(in_region_item, regions.data(), (int)(regions.size()));
  REQUIRE(num_regions_returned == num_regions);
  REQUIRE(heif_region_get_type(regions[0]) == heif_region_type_inline_mask);
  heif_image * mask_image_in;
  int32_t x, y;
  uint32_t width, height;
  err = heif_region_get_mask_image(regions[0], &x, &y, &width, &height, &mask_image_in);
  REQUIRE (err.code == heif_error_Ok);
  REQUIRE(x == 20);
  REQUIRE(y == 50);
  REQUIRE(width == 64);
  REQUIRE(height == 3);
  uint8_t* p_in = heif_image_get_plane(mask_image_in, heif_channel_Y, &stride);
  REQUIRE(p_in[0] == 0xff);
  REQUIRE(p_in[1] == 0x00);
  REQUIRE(p_in[8] == 0x00);
  REQUIRE(p_in[16] == 0x00);
  REQUIRE(p_in[17] == 0xff);
  REQUIRE(p_in[18] == 0xff);
  REQUIRE(p_in[19] == 0xff);
  REQUIRE(p_in[20] == 0xff);
  REQUIRE(p_in[21] == 0xff);
  REQUIRE(p_in[22] == 0xff);
  REQUIRE(p_in[23] == 0xff);
  REQUIRE(p_in[24] == 0x00);
  REQUIRE(p_in[81] == 0x00);
  REQUIRE(p_in[82] == 0xff);
  REQUIRE(p_in[83] == 0xff);
  REQUIRE(p_in[84] == 0xff);
  REQUIRE(p_in[85] == 0xff);
  REQUIRE(p_in[86] == 0xff);
  REQUIRE(p_in[87] == 0x00);
  REQUIRE(p_in[146] == 0x00);
  REQUIRE(p_in[147] == 0xff);
  REQUIRE(p_in[148] == 0xff);
  REQUIRE(p_in[149] == 0xff);
  REQUIRE(p_in[150] == 0x00);
  REQUIRE(p_in[151] == 0xff);
  REQUIRE(p_in[190] == 0x00);
  REQUIRE(p_in[191] == 0xff);
  heif_image_release(mask_image_in);
  heif_region_release(regions[0]);

  heif_region_item_release(in_region_item);
  heif_image_handle_release(readbackHandle);
  heif_context_free(readbackCtx);
}
