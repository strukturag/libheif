/*
 * libheif example application "heif".
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of heif, an example application using libheif.
 *
 * heif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * heif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with heif.  If not, see <http://www.gnu.org/licenses/>.
 */
#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <errno.h>
#include <string.h>
#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#else
#define STDOUT_FILENO 1
#endif

#include <libheif/heif.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <getopt.h>
#include <assert.h>


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

static struct option long_options[] = {
  //{"write-raw", required_argument, 0, 'w' },
  //{"output",    required_argument, 0, 'o' },
  {"dump-boxes", no_argument,      0, 'd' },
  {"help",       no_argument,      0, 'h' },
  {0,         0,                 0,  0 }
};

const char* fourcc_to_string(uint32_t fourcc) {
  static char fcc[5];
  fcc[0] = (char)((fourcc>>24) & 0xFF);
  fcc[1] = (char)((fourcc>>16) & 0xFF);
  fcc[2] = (char)((fourcc>> 8) & 0xFF);
  fcc[3] = (char)((fourcc>> 0) & 0xFF);
  fcc[4] = 0;
  return fcc;
}

void show_help(const char* argv0)
{
    fprintf(stderr," heif-info  libheif version: %s\n",heif_get_version());
    fprintf(stderr,"------------------------------------\n");
    fprintf(stderr,"usage: heif-info [options] image.heic\n");
    fprintf(stderr,"\n");
    fprintf(stderr,"options:\n");
    //fprintf(stderr,"  -w, --write-raw ID   write raw compressed data of image 'ID'\n");
    //fprintf(stderr,"  -o, --output NAME    output file name for image selected by -w\n");
    fprintf(stderr,"  -d, --dump-boxes     show a low-level dump of all MP4 file boxes\n");
    fprintf(stderr,"  -h, --help           show help\n");
}

int main(int argc, char** argv)
{
  bool dump_boxes = false;

  bool write_raw_image = false;
  heif_item_id raw_image_id;
  std::string output_filename = "output.265";

  while (true) {
    int option_index = 0;
    int c = getopt_long(argc, argv, "dh", long_options, &option_index);
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
    }
  }

  if (optind != argc-1) {
    show_help(argv[0]);
    return 0;
  }


  (void)raw_image_id;
  (void)write_raw_image;

  const char* input_filename = argv[optind];

  // ==============================================================================

  std::shared_ptr<heif_context> ctx(heif_context_alloc(),
                                    [] (heif_context* c) { heif_context_free(c); });
  if (!ctx) {
    fprintf(stderr, "Could not create HEIF context\n");
    return 1;
  }

  struct heif_error err;
  err = heif_context_read_from_file(ctx.get(), input_filename, nullptr);

  if (dump_boxes) {
    heif_context_debug_dump_boxes_to_file(ctx.get(), STDOUT_FILENO); // dump to stdout
    return 0;
  }

  if (err.code != 0) {
    std::cerr << "Could not read HEIF file: " << err.message << "\n";
    return 1;
  }


  // ==============================================================================


  int numImages = heif_context_get_number_of_top_level_images(ctx.get());
  heif_item_id* IDs = (heif_item_id*)alloca(numImages*sizeof(heif_item_id));
  heif_context_get_list_of_top_level_image_IDs(ctx.get(), IDs, numImages);

  for (int i=0;i<numImages;i++) {
    struct heif_image_handle* handle;
    struct heif_error err = heif_context_get_image_handle(ctx.get(), IDs[i], &handle);
    if (err.code) {
      std::cerr << err.message << "\n";
      return 10;
    }

    int width = heif_image_handle_get_width(handle);
    int height = heif_image_handle_get_height(handle);

    int primary = heif_image_handle_is_primary_image(handle);

    printf("image: %dx%d (id=%d)%s\n",width,height,IDs[i], primary ? ", primary" : "");


    // --- thumbnails

    int nThumbnails = heif_image_handle_get_number_of_thumbnails(handle);
    heif_item_id* thumbnailIDs = (heif_item_id*)alloca(nThumbnails*sizeof(heif_item_id));

    heif_image_handle* thumbnail_handle;
    nThumbnails = heif_image_handle_get_list_of_thumbnail_IDs(handle,thumbnailIDs, nThumbnails);

    for (int thumbnailIdx=0 ; thumbnailIdx<nThumbnails ; thumbnailIdx++) {
      err = heif_image_handle_get_thumbnail(handle, thumbnailIDs[thumbnailIdx], &thumbnail_handle);
      if (err.code) {
        std::cerr << err.message << "\n";
        return 10;
      }

      int th_width = heif_image_handle_get_width(thumbnail_handle);
      int th_height = heif_image_handle_get_height(thumbnail_handle);

      printf("  thumbnail: %dx%d\n",th_width,th_height);

      heif_image_handle_release(thumbnail_handle);
    }


    // --- color profile

    uint32_t profileType = heif_image_handle_get_color_profile_type(handle);
    printf("  color profile: %s\n", profileType ? fourcc_to_string(profileType) : "no");


    // --- depth information

    bool has_depth = heif_image_handle_has_depth_image(handle);

    printf("  alpha channel: %s\n", heif_image_handle_has_alpha_channel(handle) ? "yes":"no");
    printf("  depth channel: %s", has_depth ? "yes":"no\n");

    heif_item_id depth_id;
    int nDepthImages = heif_image_handle_get_list_of_depth_image_IDs(handle, &depth_id, 1);
    if (has_depth) { assert(nDepthImages==1); }
    else { assert(nDepthImages==0); }

    if (has_depth) {
      struct heif_image_handle* depth_handle;
      err = heif_image_handle_get_depth_image_handle(handle, depth_id, &depth_handle);
      if (err.code) {
        fprintf(stderr,"cannot get depth image: %s\n",err.message);
        return 1;
      }

      printf(" (%dx%d)\n",
             heif_image_handle_get_width(depth_handle),
             heif_image_handle_get_height(depth_handle));

      const struct heif_depth_representation_info* depth_info;
      if (heif_image_handle_get_depth_image_representation_info(depth_handle, depth_id, &depth_info)) {

        printf("    z-near: ");
        if (depth_info->has_z_near) printf("%f\n",depth_info->z_near); else printf("undefined\n");
        printf("    z-far:  ");
        if (depth_info->has_z_far) printf("%f\n",depth_info->z_far); else printf("undefined\n");
        printf("    d-min:  ");
        if (depth_info->has_d_min) printf("%f\n",depth_info->d_min); else printf("undefined\n");
        printf("    d-max:  ");
        if (depth_info->has_d_max) printf("%f\n",depth_info->d_max); else printf("undefined\n");

        printf("    representation: ");
        switch (depth_info->depth_representation_type) {
        case heif_depth_representation_type_uniform_inverse_Z: printf("inverse Z\n"); break;
        case heif_depth_representation_type_uniform_disparity: printf("uniform disparity\n"); break;
        case heif_depth_representation_type_uniform_Z: printf("uniform Z\n"); break;
        case heif_depth_representation_type_nonuniform_disparity: printf("non-uniform disparity\n"); break;
        default: printf("unknown\n");
        }

        if (depth_info->has_d_min || depth_info->has_d_max) {
          printf("    disparity_reference_view: %d\n", depth_info->disparity_reference_view);
        }

        heif_depth_representation_info_free(depth_info);
      }

      heif_image_handle_release(depth_handle);
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

  return 0;
}
