/*
  libheif example application "heif-info".

  MIT License

  Copyright (c) 2017 Dirk Farin <dirk.farin@gmail.com>

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
#include <cstdint>

#include <errno.h>
#include <string.h>
#include <vector>

#if defined(HAVE_UNISTD_H)

#include <unistd.h>

#else
#define STDOUT_FILENO 1
#endif

#include <libheif/heif.h>
#include <libheif/heif_regions.h>
#include <libheif/heif_properties.h>
#include <libheif/heif_experimental.h>
#include "libheif/heif_sequences.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <memory>
#include <getopt.h>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include "common.h"


/*
  image: 20005 (1920x1080), primary
    thumbnail: 20010 (320x240)
    alpha channel: 20012 (1920x1080)
    metadata: Exif

  image: 1920x1080 (20005), primary
    thumbnail: 320x240 (20010)
    alpha channel: 1920x1080 (20012)

info *file
info -w 20012 -o out.265 *file
info -d // dump
 */

int option_disable_limits = 0;

static struct option long_options[] = {
    //{"write-raw", required_argument, 0, 'w' },
    //{"output",    required_argument, 0, 'o' },
    {(char* const) "dump-boxes", no_argument, 0, 'd'},
    {(char* const) "disable-limits", no_argument, &option_disable_limits, 1},
    {(char* const) "help",       no_argument, 0, 'h'},
    {(char* const) "version",    no_argument, 0, 'v'},
    {0, 0,                                    0, 0}
};


void show_help(const char* argv0)
{
  std::filesystem::path p(argv0);
  std::string filename = p.filename().string();

  std::stringstream sstr;
  sstr << " " << filename << "  libheif version: " << heif_get_version();

  std::string title = sstr.str();

  std::cerr << title << "\n"
            << std::string(title.length() + 1, '-') << "\n"
            << "Usage: " << filename << " [options] <HEIF-image>\n"
            << "\n"
               "options:\n"
               //fprintf(stderr,"  -w, --write-raw ID   write raw compressed data of image 'ID'\n");
               //fprintf(stderr,"  -o, --output NAME    output file name for image selected by -w\n");
               "  -d, --dump-boxes     show a low-level dump of all MP4 file boxes\n"
               "      --disable-limits disable all security limits (do not use in production environment)\n"
               "  -h, --help           show help\n"
               "  -v, --version        show version\n";
}


class LibHeifInitializer
{
public:
  LibHeifInitializer() { heif_init(nullptr); }

  ~LibHeifInitializer() { heif_deinit(); }
};


int main(int argc, char** argv)
{
  // This takes care of initializing libheif and also deinitializing it at the end to free all resources.
  LibHeifInitializer initializer;

  bool dump_boxes = false;

  bool write_raw_image = false;
  heif_item_id raw_image_id;
  std::string output_filename = "output.265";

  while (true) {
    int option_index = 0;
    int c = getopt_long(argc, argv, "dhv", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
      case 'd':
        dump_boxes = true;
        break;
      case 'h':
        show_help(argv[0]);
        return 0;
      case 'w':
        write_raw_image = true;
        raw_image_id = atoi(optarg);
        break;
      case 'o':
        output_filename = optarg;
        break;
      case 'v':
        heif_examples::show_version();
        return 0;
    }
  }

  if (optind != argc - 1) {
    show_help(argv[0]);
    return 0;
  }


  (void) raw_image_id;
  (void) write_raw_image;

  const char* input_filename = argv[optind];

  // ==============================================================================
  //   show MIME type

  {
    const static int bufSize = 50;

    uint8_t buf[bufSize];
    FILE* fh = fopen(input_filename, "rb");
    if (fh) {
      std::cout << "MIME type: ";
      int n = (int) fread(buf, 1, bufSize, fh);
      const char* mime_type = heif_get_file_mime_type(buf, n);
      if (*mime_type == 0) {
        std::cout << "unknown\n";
      }
      else {
        std::cout << mime_type << "\n";
      }

      fclose(fh);

      char fourcc[5];
      fourcc[4] = 0;
      heif_brand_to_fourcc(heif_read_main_brand(buf, bufSize), fourcc);
      std::cout << "main brand: " << fourcc << "\n";

      heif_brand2* brands = nullptr;
      int nBrands = 0;
      struct heif_error err = heif_list_compatible_brands(buf, n, &brands, &nBrands);
      if (err.code) {
        std::cerr << "error reading brands: " << err.message << "\n";
      }
      else {
        std::cout << "compatible brands: ";
        for (int i = 0; i < nBrands; i++) {
          heif_brand_to_fourcc(brands[i], fourcc);
          if (i > 0) {
            std::cout << ", ";
          }
          std::cout << fourcc;
        }

        std::cout << "\n";

        heif_free_list_of_compatible_brands(brands);
      }
    }
    else {
      if (errno == ENOENT) {
        std::cerr << "Input file does not exist.\n";
        exit(10);
      }
    }
  }

  // ==============================================================================

  std::shared_ptr<heif_context> ctx(heif_context_alloc(),
                                    [](heif_context* c) { heif_context_free(c); });
  if (!ctx) {
    fprintf(stderr, "Could not create context object\n");
    return 1;
  }

  if (option_disable_limits) {
    heif_context_set_security_limits(ctx.get(), heif_get_disabled_security_limits());
  }

  struct heif_error err;
  err = heif_context_read_from_file(ctx.get(), input_filename, nullptr);

  if (dump_boxes) {
    heif_context_debug_dump_boxes_to_file(ctx.get(), STDOUT_FILENO); // dump to stdout
    return 0;
  }

  if (err.code != 0) {
    std::cerr << "Could not read HEIF/AVIF file: " << err.message << "\n";
    return 1;
  }


  // ==============================================================================


  int numImages = heif_context_get_number_of_top_level_images(ctx.get());
  std::vector<heif_item_id> IDs(numImages);
  heif_context_get_list_of_top_level_image_IDs(ctx.get(), IDs.data(), numImages);

  for (int i = 0; i < numImages; i++) {
    std::cout << "\n";

    struct heif_image_handle* handle;
    struct heif_error err = heif_context_get_image_handle(ctx.get(), IDs[i], &handle);
    if (err.code) {
      std::cerr << err.message << "\n";
      return 10;
    }

    int width = heif_image_handle_get_width(handle);
    int height = heif_image_handle_get_height(handle);

    int primary = heif_image_handle_is_primary_image(handle);

    printf("image: %dx%d (id=%d)%s\n", width, height, IDs[i], primary ? ", primary" : "");

    heif_image_tiling tiling;
    err = heif_image_handle_get_image_tiling(handle, true, &tiling);
    if (err.code) {
      std::cerr << "Error while trying to get image tiling information: " << err.message << "\n";
    }
    else if (tiling.num_columns != 1 || tiling.num_rows != 1) {
      std::cout << "  tiles: " << tiling.num_columns << "x" << tiling.num_rows
                << ", tile size: " << tiling.tile_width << "x" << tiling.tile_height << "\n";
    }

    heif_colorspace colorspace;
    heif_chroma chroma;
    err = heif_image_handle_get_preferred_decoding_colorspace(handle, &colorspace, &chroma);
    if (err.code) {
      std::cerr << err.message << "\n";
      return 10;
    }

    printf("  colorspace: ");
    switch (colorspace) {
      case heif_colorspace_YCbCr:
        printf("YCbCr, ");
        break;
      case heif_colorspace_RGB:
        printf("RGB");
        break;
      case heif_colorspace_monochrome:
        printf("monochrome");
        break;
      case heif_colorspace_nonvisual:
        printf("non-visual");
        break;
      default:
        printf("unknown");
        break;
    }

    if (colorspace==heif_colorspace_YCbCr) {
      switch (chroma) {
        case heif_chroma_420:
          printf("4:2:0");
          break;
        case heif_chroma_422:
          printf("4:2:2");
          break;
        case heif_chroma_444:
          printf("4:4:4");
          break;
        default:
          printf("unknown");
          break;
      }
    }

    printf("\n");

    // --- bit depth

    int luma_depth = heif_image_handle_get_luma_bits_per_pixel(handle);
    int chroma_depth = heif_image_handle_get_chroma_bits_per_pixel(handle);

    printf("  bit depth: ");
    if (chroma == heif_chroma_monochrome || luma_depth==chroma_depth) {
      printf("%d\n", luma_depth);
    }
    else {
      printf("%d,%d\n", luma_depth, chroma_depth);
    }


    // --- thumbnails

    int nThumbnails = heif_image_handle_get_number_of_thumbnails(handle);
    std::vector<heif_item_id> thumbnailIDs(nThumbnails);

    nThumbnails = heif_image_handle_get_list_of_thumbnail_IDs(handle, thumbnailIDs.data(), nThumbnails);

    for (int thumbnailIdx = 0; thumbnailIdx < nThumbnails; thumbnailIdx++) {
      heif_image_handle* thumbnail_handle;
      err = heif_image_handle_get_thumbnail(handle, thumbnailIDs[thumbnailIdx], &thumbnail_handle);
      if (err.code) {
        std::cerr << err.message << "\n";
        return 10;
      }

      int th_width = heif_image_handle_get_width(thumbnail_handle);
      int th_height = heif_image_handle_get_height(thumbnail_handle);

      printf("  thumbnail: %dx%d\n", th_width, th_height);

      heif_image_handle_release(thumbnail_handle);
    }


    // --- color profile

    uint32_t profileType = heif_image_handle_get_color_profile_type(handle);
    printf("  color profile: %s\n", profileType ? heif_examples::fourcc_to_string(profileType).c_str() : "no");


    // --- depth information

    bool has_depth = heif_image_handle_has_depth_image(handle);
    bool has_alpha = heif_image_handle_has_alpha_channel(handle);
    bool premultiplied_alpha = false;

    if (has_alpha) {
      premultiplied_alpha = heif_image_handle_is_premultiplied_alpha(handle);
    }

    printf("  alpha channel: %s %s\n", has_alpha ? "yes" : "no",
           premultiplied_alpha ? "(premultiplied)" : "");
    printf("  depth channel: %s\n", has_depth ? "yes" : "no");

    heif_item_id depth_id;
    int nDepthImages = heif_image_handle_get_list_of_depth_image_IDs(handle, &depth_id, 1);
    if (has_depth) { assert(nDepthImages == 1); }
    else { assert(nDepthImages == 0); }
    (void) nDepthImages;

    if (has_depth) {
      struct heif_image_handle* depth_handle;
      err = heif_image_handle_get_depth_image_handle(handle, depth_id, &depth_handle);
      if (err.code) {
        fprintf(stderr, "cannot get depth image: %s\n", err.message);
        return 1;
      }

      printf("    size: %dx%d\n",
             heif_image_handle_get_width(depth_handle),
             heif_image_handle_get_height(depth_handle));

      int depth_luma_bpp = heif_image_handle_get_luma_bits_per_pixel(depth_handle);
      printf("    bits per pixel: %d\n", depth_luma_bpp);


      const struct heif_depth_representation_info* depth_info;
      if (heif_image_handle_get_depth_image_representation_info(handle, depth_id, &depth_info)) {

        printf("    z-near: ");
        if (depth_info->has_z_near) printf("%f\n", depth_info->z_near); else printf("undefined\n");
        printf("    z-far:  ");
        if (depth_info->has_z_far) printf("%f\n", depth_info->z_far); else printf("undefined\n");
        printf("    d-min:  ");
        if (depth_info->has_d_min) printf("%f\n", depth_info->d_min); else printf("undefined\n");
        printf("    d-max:  ");
        if (depth_info->has_d_max) printf("%f\n", depth_info->d_max); else printf("undefined\n");

        printf("    representation: ");
        switch (depth_info->depth_representation_type) {
          case heif_depth_representation_type_uniform_inverse_Z:
            printf("inverse Z\n");
            break;
          case heif_depth_representation_type_uniform_disparity:
            printf("uniform disparity\n");
            break;
          case heif_depth_representation_type_uniform_Z:
            printf("uniform Z\n");
            break;
          case heif_depth_representation_type_nonuniform_disparity:
            printf("non-uniform disparity\n");
            break;
          default:
            printf("unknown\n");
        }

        if (depth_info->has_d_min || depth_info->has_d_max) {
          printf("    disparity_reference_view: %d\n", depth_info->disparity_reference_view);
        }

        heif_depth_representation_info_free(depth_info);
      }

      heif_image_handle_release(depth_handle);
    }

    // --- metadata

    int numMetadata = heif_image_handle_get_number_of_metadata_blocks(handle, nullptr);
    printf("metadata:\n");
    if (numMetadata > 0) {
      std::vector<heif_item_id> ids(numMetadata);
      heif_image_handle_get_list_of_metadata_block_IDs(handle, nullptr, ids.data(), numMetadata);

      for (int n = 0; n < numMetadata; n++) {
        std::string itemtype = heif_image_handle_get_metadata_type(handle, ids[n]);
        std::string contenttype = heif_image_handle_get_metadata_content_type(handle, ids[n]);
        std::string item_uri_type = heif_image_handle_get_metadata_item_uri_type(handle, ids[n]);
        std::string ID{"unknown"};
        if (itemtype == "Exif") {
          ID = itemtype;
        }
        else if (itemtype == "uri ") {
          ID = itemtype + "/" + item_uri_type;
        }
        else if (contenttype == "application/rdf+xml") {
          ID = "XMP";
        }
        else {
          ID = itemtype + "/" + contenttype;
        }

        printf("  %s: %zu bytes\n", ID.c_str(), heif_image_handle_get_metadata_size(handle, ids[n]));
      }
    }
    else {
      printf("  none\n");
    }

    // --- transforms

#define MAX_PROPERTIES 50
    heif_property_id transforms[MAX_PROPERTIES];
    int nTransforms = heif_item_get_transformation_properties(ctx.get(),
                                                              IDs[i],
                                                              transforms,
                                                              MAX_PROPERTIES);
    printf("transformations:\n");
    int image_width = heif_image_handle_get_ispe_width(handle);
    int image_height = heif_image_handle_get_ispe_height(handle);

    if (nTransforms) {
      for (int k = 0; k < nTransforms; k++) {
        switch (heif_item_get_property_type(ctx.get(), IDs[i], transforms[k])) {
          case heif_item_property_type_transform_mirror:
            printf("  mirror: %s\n", heif_item_get_property_transform_mirror(ctx.get(), IDs[i], transforms[k]) == heif_transform_mirror_direction_horizontal ? "horizontal" : "vertical");
            break;
          case heif_item_property_type_transform_rotation: {
            int angle = heif_item_get_property_transform_rotation_ccw(ctx.get(), IDs[i], transforms[k]);
            printf("  angle (ccw): %d\n", angle);
            if (angle==90 || angle==270) {
              std::swap(image_width, image_height);
            }
            break;
          }
          case heif_item_property_type_transform_crop: {
            int left,top,right,bottom;
            heif_item_get_property_transform_crop_borders(ctx.get(), IDs[i], transforms[k], image_width, image_height,
                                                          &left, &top, &right, &bottom);
            printf("  crop: left=%d top=%d right=%d bottom=%d\n", left,top,right,bottom);
            break;
          }
          default:
            assert(false);
        }
      }
    }
    else {
      printf("  none\n");
    }

    // --- regions

    int numRegionItems = heif_image_handle_get_number_of_region_items(handle);
    printf("region annotations:\n");
    if (numRegionItems > 0) {
      std::vector<heif_item_id> region_items(numRegionItems);
      heif_image_handle_get_list_of_region_item_ids(handle, region_items.data(), numRegionItems);
      for (heif_item_id region_item_id : region_items) {
        struct heif_region_item* region_item;
        err = heif_context_get_region_item(ctx.get(), region_item_id, &region_item);

        uint32_t reference_width, reference_height;
        heif_region_item_get_reference_size(region_item, &reference_width, &reference_height);
        int numRegions = heif_region_item_get_number_of_regions(region_item);
        printf("  id: %u, reference_width: %u, reference_height: %u, %d regions\n",
               region_item_id,
               reference_width,
               reference_height,
               numRegions);

        std::vector<heif_region*> regions(numRegions);
        numRegions = heif_region_item_get_list_of_regions(region_item, regions.data(), numRegions);
        for (int j = 0; j < numRegions; j++) {
          printf("    region %d\n", j);
          heif_region_type type = heif_region_get_type(regions[j]);
          if (type == heif_region_type_point) {
            int32_t x;
            int32_t y;
            heif_region_get_point(regions[j], &x, &y);
            printf("      point [x=%i, y=%i]\n", x, y);
          }
          else if (type == heif_region_type_rectangle) {
            int32_t x;
            int32_t y;
            uint32_t w;
            uint32_t h;
            heif_region_get_rectangle(regions[j], &x, &y, &w, &h);
            printf("      rectangle [x=%i, y=%i, w=%u, h=%u]\n", x, y, w, h);
          }
          else if (type == heif_region_type_ellipse) {
            int32_t x;
            int32_t y;
            uint32_t rx;
            uint32_t ry;
            heif_region_get_ellipse(regions[j], &x, &y, &rx, &ry);
            printf("      ellipse [x=%i, y=%i, r_x=%u, r_y=%u]\n", x, y, rx, ry);
          }
          else if (type == heif_region_type_polygon) {
            int32_t numPoints = heif_region_get_polygon_num_points(regions[j]);
            std::vector<int32_t> pts(numPoints*2);
            heif_region_get_polygon_points(regions[j], pts.data());
            printf("      polygon [");
            for (int p=0;p<numPoints;p++) {
              printf("(%d;%d)", pts[2*p+0], pts[2*p+1]);
            }
            printf("]\n");
          }
          else if (type == heif_region_type_referenced_mask) {
            int32_t x;
            int32_t y;
            uint32_t w;
            uint32_t h;
            heif_item_id referenced_item;
            heif_region_get_referenced_mask_ID(regions[j], &x, &y, &w, &h, &referenced_item);
            printf("      referenced mask [x=%i, y=%i, w=%u, h=%u, item=%u]\n", x, y, w, h, referenced_item);
          }
          else if (type == heif_region_type_polyline) {
            int32_t numPoints = heif_region_get_polyline_num_points(regions[j]);
            std::vector<int32_t> pts(numPoints*2);
            heif_region_get_polyline_points(regions[j], pts.data());
            printf("      polyline [");
            for (int p=0;p<numPoints;p++) {
              printf("(%d;%d)", pts[2*p+0], pts[2*p+1]);
            }
            printf("]\n");
          }
          else if (type == heif_region_type_inline_mask) {
            int32_t x;
            int32_t y;
            uint32_t w;
            uint32_t h;
            size_t data_len = heif_region_get_inline_mask_data_len(regions[j]);
            std::vector<uint8_t> mask_data(data_len);
            heif_region_get_inline_mask_data(regions[j], &x, &y, &w, &h, mask_data.data());
            printf("      inline mask [x=%i, y=%i, w=%u, h=%u, data len=%zu]\n", x, y, w, h, mask_data.size());
          }
      }

        heif_region_release_many(regions.data(), numRegions);
        heif_region_item_release(region_item);

        heif_property_id properties[MAX_PROPERTIES];
        int nDescr = heif_item_get_properties_of_type(ctx.get(),
                                                      region_item_id,
                                                      heif_item_property_type_user_description,
                                                      properties,
                                                      MAX_PROPERTIES);

        for (int k = 0; k < nDescr; k++) {
          heif_property_user_description* udes;
          err = heif_item_get_property_user_description(ctx.get(),
                                                        region_item_id,
                                                        properties[k],
                                                        &udes);
          if (err.code == 0) {
            printf("    user description:\n");
            printf("      lang: %s\n", udes->lang);
            printf("      name: %s\n", udes->name);
            printf("      description: %s\n", udes->description);
            printf("      tags: %s\n", udes->tags);
            heif_property_user_description_release(udes);
          }
        }
      }
    }
    else {
      printf("  none\n");
    }

    // --- properties

    printf("properties:\n");

    // user descriptions

    heif_property_id propertyIds[MAX_PROPERTIES];
    int count;
    count = heif_item_get_properties_of_type(ctx.get(), IDs[i], heif_item_property_type_user_description,
                                             propertyIds, MAX_PROPERTIES);

    if (count > 0) {
      for (int p = 0; p < count; p++) {
        struct heif_property_user_description* udes;
        err = heif_item_get_property_user_description(ctx.get(), IDs[i], propertyIds[p], &udes);
        if (err.code) {
          std::cerr << "Error reading udes " << IDs[i] << "/" << propertyIds[p] << "\n";
        }
        else {
          printf("  user description:\n");
          printf("    lang: %s\n", udes->lang);
          printf("    name: %s\n", udes->name);
          printf("    description: %s\n", udes->description);
          printf("    tags: %s\n", udes->tags);

          heif_property_user_description_release(udes);
        }
      }
    }

    // --- camera intrinsic and extrinsic parameters

    if (heif_image_handle_has_camera_intrinsic_matrix(handle)) {
      heif_camera_intrinsic_matrix matrix{};
      heif_image_handle_get_camera_intrinsic_matrix(handle, &matrix);
      printf("  camera intrinsic matrix:\n");
      printf("    focal length: %f; %f\n", matrix.focal_length_x, matrix.focal_length_y);
      printf("    principal point: %f; %f\n", matrix.principal_point_x, matrix.principal_point_y);
      printf("    skew: %f\n", matrix.skew);
    }

    if (heif_image_handle_has_camera_extrinsic_matrix(handle)) {
      heif_camera_extrinsic_matrix* matrix;
      heif_image_handle_get_camera_extrinsic_matrix(handle, &matrix);
      double rot[9];
      heif_camera_extrinsic_matrix_get_rotation_matrix(matrix, rot);
      printf("  camera extrinsic matrix:\n");
      printf("    rotation matrix:\n");
      printf("      %6.3f %6.3f %6.3f\n", rot[0], rot[1], rot[2]);
      printf("      %6.3f %6.3f %6.3f\n", rot[3], rot[4], rot[5]);
      printf("      %6.3f %6.3f %6.3f\n", rot[6], rot[7], rot[8]);
      heif_camera_extrinsic_matrix_release(matrix);
    }


    uint32_t aspect_h, aspect_v;
    int has_pasp = heif_image_handle_get_pixel_aspect_ratio(handle, &aspect_h, &aspect_v);
    if (has_pasp) {
      std::cout << "pixel aspect ratio: " << aspect_h << "/" << aspect_v << "\n";
    }

    struct heif_content_light_level clli{};
    if (heif_image_handle_get_content_light_level(handle, &clli)) {
      std::cout << "content light level (clli):\n"
                << "  max content light level: " << clli.max_content_light_level << "\n"
                << "  max pic average light level: " << clli.max_pic_average_light_level << "\n";
    }

    struct heif_mastering_display_colour_volume mdcv;
    if (heif_image_handle_get_mastering_display_colour_volume(handle, &mdcv)) {

      struct heif_decoded_mastering_display_colour_volume decoded_mdcv;
      err = heif_mastering_display_colour_volume_decode(&mdcv, &decoded_mdcv);

      std::cout << "mastering display color volume:\n"
                << "  display_primaries (x,y): "
                << "(" << decoded_mdcv.display_primaries_x[0] << ";" << decoded_mdcv.display_primaries_y[0] << "), "
                << "(" << decoded_mdcv.display_primaries_x[1] << ";" << decoded_mdcv.display_primaries_y[1] << "), "
                << "(" << decoded_mdcv.display_primaries_x[2] << ";" << decoded_mdcv.display_primaries_y[2] << ")\n";

      std::cout << "  white point (x,y): (" << decoded_mdcv.white_point_x << ";" << decoded_mdcv.white_point_y << ")\n";
      std::cout << "  max display mastering luminance: " << decoded_mdcv.max_display_mastering_luminance << "\n";
      std::cout << "  min display mastering luminance: " << decoded_mdcv.min_display_mastering_luminance << "\n";
    }

    heif_image_handle_release(handle);
  }

#if 0
  std::cout << "num images: " << heif_context_get_number_of_top_level_images(ctx.get()) << "\n";

  struct heif_image_handle* handle;
  err = heif_context_get_primary_image_handle(ctx.get(), &handle);
  if (err.code != 0) {
    std::cerr << "Could not get primage image handle: " << err.message << "\n";
    return 1;
  }

  struct heif_image* image;
  err = heif_decode_image(handle, &image, heif_colorspace_undefined, heif_chroma_undefined, NULL);
  if (err.code != 0) {
    heif_image_handle_release(handle);
    std::cerr << "Could not decode primage image: " << err.message << "\n";
    return 1;
  }

  heif_image_release(image);
  heif_image_handle_release(handle);
#endif

  // ==============================================================================

  heif_context* context = ctx.get();

  uint32_t nTracks = heif_context_number_of_sequence_tracks(context);

  if (nTracks > 0) {
    std::cout << "\n";

    uint64_t timescale = heif_context_get_sequence_timescale(context);
    std::cout << "sequence time scale: " << timescale << " Hz\n";

    uint64_t duration = heif_context_get_sequence_duration(context);
    std::cout << "sequence duration: " << ((double)duration)/(double)timescale << " seconds\n";

              //    TrackOptions

    std::vector<uint32_t> track_ids(nTracks);

    heif_context_get_track_ids(context, track_ids.data());

    for (uint32_t id : track_ids) {
      heif_track* track = heif_context_get_track(context, id);

      heif_track_type handler = heif_track_get_track_handler_type(track);
      std::cout << "track " << id << "\n";
      std::cout << "  handler: '" << heif_examples::fourcc_to_string(handler) << "' = ";

      switch (handler) {
        case heif_track_type_image_sequence:
          std::cout << "image sequence\n";
          break;
        case heif_track_type_video:
          std::cout << "video\n";
          break;
        case heif_track_type_metadata:
          std::cout << "metadata\n";
          break;
        default:
          std::cout << "unknown\n";
          break;
      }

      if (handler == heif_track_type_video ||
          handler == heif_track_type_image_sequence) {
        uint16_t w, h;
        heif_track_get_image_resolution(track, &w, &h);
        std::cout << "  resolution: " << w << "x" << h << "\n";
      }

      uint32_t sampleEntryType = heif_track_get_sample_entry_type_of_first_cluster(track);
      std::cout << "  sample entry type: " << heif_examples::fourcc_to_string(sampleEntryType) << "\n";

      if (sampleEntryType == heif_fourcc('u', 'r', 'i', 'm')) {
        const char* uri;
        err = heif_track_get_urim_sample_entry_uri_of_first_cluster(track, &uri);
        if (err.code) {
          std::cerr << "error reading urim-track uri: " << err.message << "\n";
        }
        std::cout << "  uri: " << uri << "\n";
        heif_string_release(uri);
      }

      std::cout << "  sample auxiliary information: ";
      int nSampleAuxTypes = heif_track_get_number_of_sample_aux_infos(track);

      std::vector<heif_sample_aux_info_type> aux_types(nSampleAuxTypes);
      heif_track_get_sample_aux_info_types(track, aux_types.data());

      for (size_t i=0;i<aux_types.size();i++) {
        if (i) { std::cout << ", "; }
        std::cout << heif_examples::fourcc_to_string(aux_types[i].type);
      }

      if (nSampleAuxTypes==0) {
        std::cout << "---";
      }
      std::cout << "\n";


      size_t nRefTypes = heif_track_get_number_of_track_reference_types(track);

      if (nRefTypes > 0) {
        std::cout << "  references:\n";
        std::vector<uint32_t> refTypes(nRefTypes);
        heif_track_get_track_reference_types(track, refTypes.data());

        for (uint32_t refType : refTypes) {
          std::cout << "    " << heif_examples::fourcc_to_string(refType) << ": ";

          size_t n = heif_track_get_number_of_track_reference_of_type(track, refType);
          std::vector<uint32_t> track_ids(n);
          heif_track_get_references_from_track(track, refType, track_ids.data());
          for (size_t i=0;i<n;i++) {
            if (i>0) std::cout << ", ";
            std::cout << "track#" << track_ids[i];
          }
          std::cout << "\n";
        }
      }

      heif_track_release(track);
    }
  }

  return 0;
}
