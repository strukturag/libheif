/*
 * HEIF codec.
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of libheif.
 *
 * libheif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libheif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libheif.  If not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include "heif.h"
#include "heif_image.h"
#include "heif_api_structs.h"
#include "heif_context.h"
#include "heif_plugin_registry.h"
#include "error.h"
#include "bitstream.h"

#if defined(__EMSCRIPTEN__)
#include "heif_emscripten.h"
#endif

#include <algorithm>
#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <string.h>
#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif

#if defined(_MSC_VER)
// for _write
#include <io.h>
#endif

using namespace heif;

static struct heif_error error_Ok = { heif_error_Ok, heif_suberror_Unspecified, kSuccess };
static struct heif_error error_unsupported_parameter = { heif_error_Usage_error,
                                                         heif_suberror_Unsupported_parameter,
                                                         "Unsupported encoder parameter" };
static struct heif_error error_unsupported_plugin_version = { heif_error_Usage_error,
                                                              heif_suberror_Unsupported_plugin_version,
                                                              "Unsupported plugin version" };
static struct heif_error error_null_parameter = { heif_error_Usage_error,
                                                  heif_suberror_Null_pointer_argument,
                                                  "NULL passed" };

const char *heif_get_version(void) {
  return (LIBHEIF_VERSION);
}

uint32_t heif_get_version_number(void) {
  return (LIBHEIF_NUMERIC_VERSION);
}

int heif_get_version_number_major(void) {
  return ((LIBHEIF_NUMERIC_VERSION)>>24) & 0xFF;
}

int heif_get_version_number_minor(void) {
  return ((LIBHEIF_NUMERIC_VERSION)>>16) & 0xFF;
}

int heif_get_version_number_maintenance(void) {
  return ((LIBHEIF_NUMERIC_VERSION)>>8) & 0xFF;
}


heif_filetype_result heif_check_filetype(const uint8_t* data, int len)
{
  if (len<8) {
    return heif_filetype_maybe;
  }

  if (data[4] != 'f' ||
      data[5] != 't' ||
      data[6] != 'y' ||
      data[7] != 'p') {
    return heif_filetype_no;
  }

  if (len>=12) {
    heif_brand brand = heif_main_brand(data, len);

    if (brand == heif_heic) {
      return heif_filetype_yes_supported;
    }
    else if (brand == heif_unknown_brand) {
      return heif_filetype_no;
    }
    else if (brand == heif_mif1) {
      return heif_filetype_maybe;
    }
    else {
      return heif_filetype_yes_unsupported;
    }
  }

  return heif_filetype_maybe;
}


heif_brand heif_main_brand(const uint8_t* data, int len)
{
  if (len<12) {
    return heif_unknown_brand;
  }

  char brand[5];
  brand[0]=data[8];
  brand[1]=data[9];
  brand[2]=data[10];
  brand[3]=data[11];
  brand[4]=0;

  if (strcmp(brand, "heic")==0) {
    return heif_heic;
  }
  else if (strcmp(brand, "heix")==0) {
    return heif_heix;
  }
  else if (strcmp(brand, "hevc")==0) {
    return heif_hevc;
  }
  else if (strcmp(brand, "hevx")==0) {
    return heif_hevx;
  }
  else if (strcmp(brand, "heim")==0) {
    return heif_heim;
  }
  else if (strcmp(brand, "heis")==0) {
    return heif_heis;
  }
  else if (strcmp(brand, "hevm")==0) {
    return heif_hevm;
  }
  else if (strcmp(brand, "hevs")==0) {
    return heif_hevs;
  }
  else if (strcmp(brand, "mif1")==0) {
    return heif_mif1;
  }
  else if (strcmp(brand, "msf1")==0) {
    return heif_msf1;
  }
  else {
    return heif_unknown_brand;
  }
}


enum class TriBool {
  No, Yes, Unknown
};

TriBool is_jpeg(const uint8_t* data, int len) {
  if (len < 12) {
    return TriBool::Unknown;
  }

  if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF && data[3] == 0xE0 &&
      data[4] == 0x00 && data[5] == 0x10 && data[6] == 0x4A && data[7] == 0x46 &&
      data[8] == 0x49 && data[9] == 0x46 && data[10] == 0x00 && data[11] == 0x01) {
    return TriBool::Yes;
  }
  if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF && data[3] == 0xE1 &&
      data[6] == 0x45 && data[7] == 0x78 && data[8] == 0x69 && data[9] == 0x66 &&
      data[10] == 0x00 && data[11] == 0x00) {
    return TriBool::Yes;
  }
  else {
    return TriBool::No;
  }
}


TriBool is_png(const uint8_t* data, int len) {
  if (len < 8) {
    return TriBool::Unknown;
  }

  if (data[0] == 0x89 && data[1]==0x50 && data[2]==0x4E && data[3]==0x47 &&
      data[4] == 0x0D && data[5]==0x0A && data[6]==0x1A && data[7]==0x0A) {
    return TriBool::Yes;
  }
  else {
    return TriBool::No;
  }
}


const char* heif_get_file_mime_type(const uint8_t* data, int len)
{
  heif_brand mainBrand = heif_main_brand(data,len);

  if (mainBrand == heif_heic ||
      mainBrand == heif_heix ||
      mainBrand == heif_heim ||
      mainBrand == heif_heis) {
    return "image/heic";
  }
  else if (mainBrand == heif_mif1) {
    return "image/heif";
  }
  if (mainBrand == heif_hevc ||
      mainBrand == heif_hevx ||
      mainBrand == heif_hevm ||
      mainBrand == heif_hevs) {
    return "image/heic-sequence";
  }
  else if (mainBrand == heif_msf1) {
    return "image/heif-sequence";
  }
  else if (is_jpeg(data,len)==TriBool::Yes) {
    return "image/jpeg";
  }
  else if (is_png(data,len)==TriBool::Yes) {
    return "image/png";
  }
  else {
    return "";
  }
}


heif_context* heif_context_alloc()
{
  struct heif_context* ctx = new heif_context;
  ctx->context = std::make_shared<HeifContext>();

  return ctx;
}

void heif_context_free(heif_context* ctx)
{
  delete ctx;
}

heif_error heif_context_read_from_file(heif_context* ctx, const char* filename,
                                       const struct heif_reading_options*)
{
  Error err = ctx->context->read_from_file(filename);
  return err.error_struct(ctx->context.get());
}

heif_error heif_context_read_from_memory(heif_context* ctx, const void* mem, size_t size,
                                         const struct heif_reading_options*)
{
  Error err = ctx->context->read_from_memory(mem, size, true);
  return err.error_struct(ctx->context.get());
}

heif_error heif_context_read_from_memory_without_copy(heif_context* ctx, const void* mem, size_t size,
                                                      const struct heif_reading_options*)
{
  Error err = ctx->context->read_from_memory(mem, size, false);
  return err.error_struct(ctx->context.get());
}

heif_error heif_context_read_from_reader(struct heif_context* ctx,
                                         const struct heif_reader* reader_func_table,
                                         void* userdata,
                                         const struct heif_reading_options*)
{
  auto reader = std::make_shared<StreamReader_CApi>(reader_func_table, userdata);

  Error err = ctx->context->read(reader);
  return err.error_struct(ctx->context.get());
}

// TODO: heif_error heif_context_read_from_file_descriptor(heif_context*, int fd);

void heif_context_debug_dump_boxes_to_file(struct heif_context* ctx, int fd) {
  if (!ctx) {
    return;
  }

  std::string dump = ctx->context->debug_dump_boxes();
  // TODO(fancycode): Should we return an error if writing fails?
#if defined(_MSC_VER)
  auto written = _write(fd, dump.c_str(), dump.size());
#else
  auto written = write(fd, dump.c_str(), dump.size());
#endif
  (void) written;
}

heif_error heif_context_get_primary_image_handle(heif_context* ctx, heif_image_handle** img)
{
  if (!img) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(ctx->context.get());
  }

  std::shared_ptr<HeifContext::Image> primary_image = ctx->context->get_primary_image();

  // It is a requirement of an HEIF file there is always a primary image.
  // If there is none, an error is generated when loading the file.
  if (!primary_image) {
    Error err(heif_error_Invalid_input,
              heif_suberror_No_or_invalid_primary_item);
    return err.error_struct(ctx->context.get());
  }

  *img = new heif_image_handle();
  (*img)->image = std::move(primary_image);
  (*img)->context = ctx->context;

  return Error::Ok.error_struct(ctx->context.get());
}


struct heif_error heif_context_get_primary_image_ID(struct heif_context* ctx, heif_item_id* id)
{
  if (!id) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(ctx->context.get());
  }

  std::shared_ptr<HeifContext::Image> primary = ctx->context->get_primary_image();
  if (!primary) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_or_invalid_primary_item).error_struct(ctx->context.get());
  }

  *id = primary->get_id();

  return Error::Ok.error_struct(ctx->context.get());
}


int heif_context_is_top_level_image_ID(struct heif_context* ctx, heif_item_id id)
{
  const std::vector<std::shared_ptr<HeifContext::Image>> images = ctx->context->get_top_level_images();

  for (const auto& img : images) {
    if (img->get_id() == id) {
      return true;
    }
  }

  return false;
}


int heif_context_get_number_of_top_level_images(heif_context* ctx)
{
  return (int)ctx->context->get_top_level_images().size();
}


int heif_context_get_list_of_top_level_image_IDs(struct heif_context* ctx,
                                                 heif_item_id* ID_array,
                                                 int count)
{
  if (ID_array == nullptr || count==0 || ctx==nullptr) {
    return 0;
  }


  // fill in ID values into output array

  const std::vector<std::shared_ptr<HeifContext::Image>> imgs = ctx->context->get_top_level_images();
  int n = (int)std::min(count,(int)imgs.size());
  for (int i=0;i<n;i++) {
    ID_array[i] = imgs[i]->get_id();
  }

  return n;
}


struct heif_error heif_context_get_image_handle(struct heif_context* ctx,
                                                heif_item_id id,
                                                struct heif_image_handle** imgHdl)
{
  if (!imgHdl) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(ctx->context.get());
  }

  const std::vector<std::shared_ptr<HeifContext::Image>> images = ctx->context->get_top_level_images();

  std::shared_ptr<HeifContext::Image> image;
  for (auto& img : images) {
    if (img->get_id() == id) {
      image = img;
      break;
    }
  }

  if (!image) {
    Error err(heif_error_Usage_error, heif_suberror_Nonexisting_item_referenced);
    return err.error_struct(ctx->context.get());
  }

  *imgHdl = new heif_image_handle();
  (*imgHdl)->image = image;
  (*imgHdl)->context = ctx->context;

  return Error::Ok.error_struct(ctx->context.get());
}


int heif_image_handle_is_primary_image(const struct heif_image_handle* handle)
{
  return handle->image->is_primary();
}


int heif_image_handle_get_number_of_thumbnails(const struct heif_image_handle* handle)
{
  return (int)handle->image->get_thumbnails().size();
}


int heif_image_handle_get_list_of_thumbnail_IDs(const struct heif_image_handle* handle,
                                                heif_item_id* ids, int count)
{
  if (ids==nullptr) {
    return 0;
  }

  auto thumbnails = handle->image->get_thumbnails();
  int n = (int)std::min(count, (int)thumbnails.size());

  for (int i=0;i<n;i++) {
    ids[i] = thumbnails[i]->get_id();
  }

  return n;
}


heif_error heif_image_handle_get_thumbnail(const struct heif_image_handle* handle,
                                           heif_item_id thumbnail_id,
                                           struct heif_image_handle** out_thumbnail_handle)
{
  if (!out_thumbnail_handle) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(handle->image.get());
  }

  auto thumbnails = handle->image->get_thumbnails();
  for (auto thumb : thumbnails) {
    if (thumb->get_id() == thumbnail_id) {
      *out_thumbnail_handle = new heif_image_handle();
      (*out_thumbnail_handle)->image = thumb;
      (*out_thumbnail_handle)->context = handle->context;

      return Error::Ok.error_struct(handle->image.get());
    }
  }

  Error err(heif_error_Usage_error, heif_suberror_Nonexisting_item_referenced);
  return err.error_struct(handle->image.get());
}


int heif_image_handle_get_width(const struct heif_image_handle* handle)
{
  if (handle && handle->image) {
    return handle->image->get_width();
  }
  else {
    return 0;
  }
}


int heif_image_handle_get_height(const struct heif_image_handle* handle)
{
  if (handle && handle->image) {
    return handle->image->get_height();
  }
  else {
    return 0;
  }
}


int heif_image_handle_get_ispe_width(const struct heif_image_handle* handle)
{
  if (handle && handle->image) {
    return handle->image->get_ispe_width();
  }
  else {
    return 0;
  }
}


int heif_image_handle_get_ispe_height(const struct heif_image_handle* handle)
{
  if (handle && handle->image) {
    return handle->image->get_ispe_height();
  }
  else {
    return 0;
  }
}


int heif_image_handle_has_alpha_channel(const struct heif_image_handle* handle)
{
  return handle->image->get_alpha_channel() != nullptr;
}


int heif_image_handle_get_luma_bits_per_pixel(const struct heif_image_handle* handle)
{
  return handle->image->get_luma_bits_per_pixel();
}


int heif_image_handle_get_chroma_bits_per_pixel(const struct heif_image_handle* handle)
{
  return handle->image->get_chroma_bits_per_pixel();
}


int heif_image_handle_has_depth_image(const struct heif_image_handle* handle)
{
  return handle->image->get_depth_channel() != nullptr;
}

void heif_depth_representation_info_free(const struct heif_depth_representation_info* info)
{
  delete info;
}

int heif_image_handle_get_depth_image_representation_info(const struct heif_image_handle* handle,
                                                          heif_item_id depth_image_id,
                                                          const struct heif_depth_representation_info** out)
{
  if (out) {
    if (handle->image->has_depth_representation_info()) {
      auto info = new heif_depth_representation_info;
      *info = handle->image->get_depth_representation_info();
      *out = info;
      return true;
    }
    else {
      *out = nullptr;
    }
  }

  return false;
}


int heif_image_handle_get_number_of_depth_images(const struct heif_image_handle* handle)
{
  auto depth_image = handle->image->get_depth_channel();

  if (depth_image) {
    return 1;
  }
  else {
    return 0;
  }
}


int heif_image_handle_get_list_of_depth_image_IDs(const struct heif_image_handle* handle,
                                                  heif_item_id* ids, int count)
{
  auto depth_image = handle->image->get_depth_channel();

  if (count==0) {
    return 0;
  }

  if (depth_image) {
    ids[0] = depth_image->get_id();
    return 1;
  }
  else {
    return 0;
  }
}


struct heif_error heif_image_handle_get_depth_image_handle(const struct heif_image_handle* handle,
                                                           heif_item_id depth_id,
                                                           struct heif_image_handle** out_depth_handle)
{
  auto depth_image = handle->image->get_depth_channel();

  if (depth_image->get_id() != depth_id) {
    *out_depth_handle = nullptr;

    Error err(heif_error_Usage_error, heif_suberror_Nonexisting_item_referenced);
    return err.error_struct(handle->image.get());
  }

  *out_depth_handle = new heif_image_handle();
  (*out_depth_handle)->image = depth_image;
  (*out_depth_handle)->context = handle->context;

  return Error::Ok.error_struct(handle->image.get());
}


heif_decoding_options* heif_decoding_options_alloc()
{
  auto options = new heif_decoding_options;

  options->version = 1;

  options->ignore_transformations = false;

  options->start_progress = NULL;
  options->on_progress = NULL;
  options->end_progress = NULL;
  options->progress_user_data = NULL;

  return options;
}


void heif_decoding_options_free(heif_decoding_options* options)
{
  delete options;
}


struct heif_error heif_decode_image(const struct heif_image_handle* in_handle,
                                    struct heif_image** out_img,
                                    heif_colorspace colorspace,
                                    heif_chroma chroma,
                                    const struct heif_decoding_options* options)
{
  std::shared_ptr<HeifPixelImage> img;

  Error err = in_handle->image->decode_image(img,
                                             colorspace,
                                             chroma,
                                             options);
  if (err.error_code != heif_error_Ok) {
    return err.error_struct(in_handle->image.get());
  }

  *out_img = new heif_image();
  (*out_img)->image = std::move(img);

  return Error::Ok.error_struct(in_handle->image.get());
}


struct heif_error heif_image_create(int width, int height,
                                    heif_colorspace colorspace,
                                    heif_chroma chroma,
                                    struct heif_image** image)
{
  struct heif_image* img = new heif_image;
  img->image = std::make_shared<HeifPixelImage>();

  img->image->create(width, height, colorspace, chroma);

  *image = img;

  struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, Error::kSuccess };
  return err;
}

void heif_image_release(const struct heif_image* img)
{
  delete img;
}

void heif_image_handle_release(const struct heif_image_handle* handle)
{
  delete handle;
}


enum heif_colorspace heif_image_get_colorspace(const struct heif_image* img)
{
  return img->image->get_colorspace();
}

enum heif_chroma heif_image_get_chroma_format(const struct heif_image* img)
{
  return img->image->get_chroma_format();
}


int heif_image_get_width(const struct heif_image* img,enum heif_channel channel)
{
  return img->image->get_width(channel);
}

int heif_image_get_height(const struct heif_image* img,enum heif_channel channel)
{
  return img->image->get_height(channel);
}


int heif_image_get_bits_per_pixel(const struct heif_image* img,enum heif_channel channel)
{
  return img->image->get_storage_bits_per_pixel(channel);
}


int heif_image_get_bits_per_pixel_range(const struct heif_image* img,enum heif_channel channel)
{
  return img->image->get_bits_per_pixel(channel);
}


int heif_image_has_channel(const struct heif_image* img, enum heif_channel channel)
{
  return img->image->has_channel(channel);
}


struct heif_error heif_image_add_plane(struct heif_image* image,
                                       heif_channel channel, int width, int height, int bit_depth)
{
  if (!image->image->add_plane(channel, width, height, bit_depth)) {
    struct heif_error err = { heif_error_Memory_allocation_error,
                              heif_suberror_Unspecified,
                              "Cannot allocate memory for image plane" };
    return err;
  }
  else {
    struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, Error::kSuccess };
    return err;
  }
}



const uint8_t* heif_image_get_plane_readonly(const struct heif_image* image,
                                             enum heif_channel channel,
                                             int* out_stride)
{
  if (!image || !image->image) {
    *out_stride = 0;
    return nullptr;
  }

  return image->image->get_plane(channel, out_stride);
}


uint8_t* heif_image_get_plane(struct heif_image* image,
                              enum heif_channel channel,
                              int* out_stride)
{
  if (!image || !image->image) {
    *out_stride = 0;
    return nullptr;
  }

  return image->image->get_plane(channel, out_stride);
}


struct heif_error heif_image_scale_image(const struct heif_image* input,
                                         struct heif_image** output,
                                         int width, int height,
                                         const struct heif_scaling_options* options)
{
  std::shared_ptr<HeifPixelImage> out_img;

  Error err = input->image->scale_nearest_neighbor(out_img, width, height);
  if (err) {
    return err.error_struct(input->image.get());
  }

  *output = new heif_image;
  (*output)->image = out_img;

  return Error::Ok.error_struct(input->image.get());
}

struct heif_error heif_image_set_raw_color_profile(struct heif_image* image,
                                                   const char* color_profile_type_fourcc,
                                                   const void* profile_data,
                                                   const size_t profile_size)
{
  if (strlen(color_profile_type_fourcc) != 4) {
    heif_error err = { heif_error_Usage_error,
                       heif_suberror_Unspecified,
                       "Invalid color_profile_type (must be 4 characters)" };
    return err;
  }

  uint32_t color_profile_type = fourcc(color_profile_type_fourcc);

  std::vector<uint8_t> data;
  data.insert(data.end(),
              (const uint8_t*)profile_data,
              (const uint8_t*)profile_data + profile_size);

  auto color_profile = std::make_shared<color_profile_raw>(color_profile_type, data);

  image->image->set_color_profile(color_profile);

  struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, Error::kSuccess };
  return err;
}


struct heif_error heif_image_set_nclx_color_profile(struct heif_image* image,
                                                    const struct heif_color_profile_nclx* color_profile)
{
  auto nclx = std::make_shared<color_profile_nclx>();

  nclx->set_colour_primaries(color_profile->color_primaries);
  nclx->set_transfer_characteristics(color_profile->transfer_characteristics);
  nclx->set_matrix_coefficients(color_profile->matrix_coefficients);
  nclx->set_full_range_flag(color_profile->full_range_flag);

  image->image->set_color_profile(nclx);

  return error_Ok;
}


/*
void heif_image_remove_color_profile(struct heif_image* image)
{
  image->image->set_color_profile(nullptr);
}
*/


int heif_image_handle_get_number_of_metadata_blocks(const struct heif_image_handle* handle,
                                                    const char* type_filter)
{
  auto metadata_list = handle->image->get_metadata();

  int cnt=0;
  for (const auto& metadata : metadata_list) {
    if (type_filter==nullptr ||
        metadata->item_type == type_filter) {
      cnt++;
    }
  }

  return cnt;
}


int heif_image_handle_get_list_of_metadata_block_IDs(const struct heif_image_handle* handle,
                                                     const char* type_filter,
                                                     heif_item_id* ids, int count)
{
  auto metadata_list = handle->image->get_metadata();

  int cnt=0;
  for (const auto& metadata : metadata_list) {
    if (type_filter==nullptr ||
        metadata->item_type == type_filter) {
      if (cnt < count) {
        ids[cnt] = metadata->item_id;
        cnt++;
      }
      else {
        break;
      }
    }
  }

  return cnt;
}


const char* heif_image_handle_get_metadata_type(const struct heif_image_handle* handle,
                                                heif_item_id metadata_id)
{
  auto metadata_list = handle->image->get_metadata();

  for (auto metadata : metadata_list) {
    if (metadata->item_id == metadata_id) {
      return metadata->item_type.c_str();
    }
  }

  return NULL;
}


const char* heif_image_handle_get_metadata_content_type(const struct heif_image_handle* handle,
                                                        heif_item_id metadata_id)
{
  auto metadata_list = handle->image->get_metadata();

  for (auto metadata : metadata_list) {
    if (metadata->item_id == metadata_id) {
      return metadata->content_type.c_str();
    }
  }

  return NULL;
}


size_t heif_image_handle_get_metadata_size(const struct heif_image_handle* handle,
                                           heif_item_id metadata_id)
{
  auto metadata_list = handle->image->get_metadata();

  for (auto metadata : metadata_list) {
    if (metadata->item_id == metadata_id) {
      return metadata->m_data.size();
    }
  }

  return 0;
}


struct heif_error heif_image_handle_get_metadata(const struct heif_image_handle* handle,
                                                 heif_item_id metadata_id,
                                                 void* out_data)
{
  if (out_data==nullptr) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(handle->image.get());
  }

  auto metadata_list = handle->image->get_metadata();

  for (auto metadata : metadata_list) {
    if (metadata->item_id == metadata_id) {
      memcpy(out_data,
             metadata->m_data.data(),
             metadata->m_data.size());

      return Error::Ok.error_struct(handle->image.get());
    }
  }

  Error err(heif_error_Usage_error,
            heif_suberror_Nonexisting_item_referenced);
  return err.error_struct(handle->image.get());
}

heif_color_profile_type heif_image_handle_get_color_profile_type(const struct heif_image_handle* handle)
{
  auto profile = handle->image->get_color_profile();
  if (!profile) {
    return heif_color_profile_type_not_present;
  }
  else {
    return (heif_color_profile_type)profile->get_type();
  }
}

size_t heif_image_handle_get_raw_color_profile_size(const struct heif_image_handle* handle)
{
  auto profile = handle->image->get_color_profile();
  auto raw_profile = std::dynamic_pointer_cast<const color_profile_raw>(profile);
  if (raw_profile) {
    return raw_profile->get_data().size();
  }
  else {
    return 0;
  }
}

static struct color_primaries_table_entry {
  int id;
  float gx,gy, bx,by, rx,ry, wx,wy;
} color_primaries_table[] = {
  { 0,  0.000f,0.000f,  0.000f,0.000f,  0.000f,0.000f,  0.0000f,0.0000f },
  { 1,  0.300f,0.600f,  0.150f,0.060f,  0.640f,0.330f,  0.3127f,0.3290f },
  { 4,  0.210f,0.710f,  0.140f,0.080f,  0.670f,0.330f,  0.3100f,0.3160f },
  { 5,  0.290f,0.600f,  0.150f,0.060f,  0.640f,0.330f,  0.3127f,0.3290f },
  { 6,  0.310f,0.595f,  0.155f,0.070f,  0.630f,0.340f,  0.3127f,0.3290f },
  { 7,  0.310f,0.595f,  0.155f,0.707f,  0.630f,0.340f,  0.3127f,0.3290f },
  { -1 }
};


static Error get_nclx_color_profile(std::shared_ptr<const color_profile_nclx> nclx_profile,
                                    struct heif_color_profile_nclx** out_data)
{
  if (nclx_profile) {
    *out_data = (struct heif_color_profile_nclx*)malloc(sizeof(struct heif_color_profile_nclx));

    struct heif_color_profile_nclx* nclx = *out_data;

    nclx->version = 1;
    nclx->color_primaries = (enum heif_color_primaries)nclx_profile->get_colour_primaries();
    nclx->transfer_characteristics = (enum heif_transfer_characteristics)nclx_profile->get_transfer_characteristics();
    nclx->matrix_coefficients = (enum heif_matrix_coefficients)nclx_profile->get_matrix_coefficients();
    nclx->full_range_flag = nclx_profile->get_full_range_flag();

    // fill color primaries

    int tableIdx = 0;
    for (int i=0; color_primaries_table[i].id >= 0; i++) {
      const auto& c = color_primaries_table[i];
      if (c.id == (*out_data)->color_primaries) {
        tableIdx = i;
        break;
      }
    }

    const auto& c = color_primaries_table[tableIdx];
    nclx->color_primary_red_x = c.rx;
    nclx->color_primary_red_y = c.ry;
    nclx->color_primary_green_x = c.gx;
    nclx->color_primary_green_y = c.gy;
    nclx->color_primary_blue_x = c.bx;
    nclx->color_primary_blue_y = c.by;
    nclx->color_primary_white_x = c.wx;
    nclx->color_primary_white_y = c.wy;

    return Error::Ok;
  }
  else {
    return Error(heif_error_Usage_error,
                 heif_suberror_Unspecified);
  }
}

struct heif_error heif_image_handle_get_nclx_color_profile(const struct heif_image_handle* handle,
                                                           struct heif_color_profile_nclx** out_data)
{
  if (!out_data) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(handle->image.get());
  }

  auto profile = handle->image->get_color_profile();
  auto nclx_profile = std::dynamic_pointer_cast<const color_profile_nclx>(profile);
  Error err = get_nclx_color_profile(nclx_profile, out_data);

  return err.error_struct(handle->image.get());
}


struct heif_error heif_image_handle_get_raw_color_profile(const struct heif_image_handle* handle,
                                                          void* out_data)
{
  if (out_data==nullptr) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(handle->image.get());
  }

  auto profile = handle->image->get_color_profile();
  auto raw_profile = std::dynamic_pointer_cast<const color_profile_raw>(profile);
  if (raw_profile) {
    memcpy(out_data,
           raw_profile->get_data().data(),
           raw_profile->get_data().size());
  }

  return Error::Ok.error_struct(handle->image.get());
}



enum heif_color_profile_type heif_image_get_color_profile_type(const struct heif_image* image)
{
  auto profile = image->image->get_color_profile();
  if (!profile) {
    return heif_color_profile_type_not_present;
  }
  else {
    return (heif_color_profile_type)profile->get_type();
  }
}


size_t heif_image_get_raw_color_profile_size(const struct heif_image* image)
{
  auto profile = image->image->get_color_profile();
  auto raw_profile = std::dynamic_pointer_cast<const color_profile_raw>(profile);
  if (raw_profile) {
    return raw_profile->get_data().size();
  }
  else {
    return 0;
  }
}


struct heif_error heif_image_get_raw_color_profile(const struct heif_image* image,
                                                   void* out_data)
{
  if (out_data==nullptr) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(image->image.get());
  }

  auto profile = image->image->get_color_profile();
  auto raw_profile = std::dynamic_pointer_cast<const color_profile_raw>(profile);
  if (raw_profile) {
    memcpy(out_data,
           raw_profile->get_data().data(),
           raw_profile->get_data().size());
  }

  return Error::Ok.error_struct(image->image.get());
}


struct heif_error heif_image_get_nclx_color_profile(const struct heif_image* image,
                                                    struct heif_color_profile_nclx** out_data)
{
  if (!out_data) {
    Error err(heif_error_Usage_error,
              heif_suberror_Null_pointer_argument);
    return err.error_struct(image->image.get());
  }

  auto profile = image->image->get_color_profile();
  auto nclx_profile = std::dynamic_pointer_cast<const color_profile_nclx>(profile);
  Error err = get_nclx_color_profile(nclx_profile, out_data);

  return err.error_struct(image->image.get());
}





// DEPRECATED
struct heif_error heif_register_decoder(heif_context* heif, const heif_decoder_plugin* decoder_plugin)
{
  if (!decoder_plugin) {
    return error_null_parameter;
  } else if (decoder_plugin->plugin_api_version != 1) {
    return error_unsupported_plugin_version;
  }

  heif->context->register_decoder(decoder_plugin);
  return Error::Ok.error_struct(heif->context.get());
}


struct heif_error heif_register_decoder_plugin(const heif_decoder_plugin* decoder_plugin)
{
  if (!decoder_plugin) {
    return error_null_parameter;
  } else if (decoder_plugin->plugin_api_version != 1) {
    return error_unsupported_plugin_version;
  }

  register_decoder(decoder_plugin);
  return error_Ok;
}


struct heif_error heif_register_encoder_plugin(const heif_encoder_plugin* encoder_plugin)
{
  if (!encoder_plugin) {
    return error_null_parameter;
  } else if (encoder_plugin->plugin_api_version != 1) {
    return error_unsupported_plugin_version;
  }

  register_encoder(encoder_plugin);
  return error_Ok;
}




/*
int  heif_image_get_number_of_data_chunks(heif_image* img);

void heif_image_get_data_chunk(heif_image* img, int chunk_index,
                               uint8_t const*const* dataptr,
                               int const* data_size);

void heif_image_free_data_chunk(heif_image* img, int chunk_index);
*/


/*
void heif_context_reset(struct heif_context* ctx)
{
  ctx->context->reset_to_empty_heif();
}
*/

static struct heif_error heif_file_writer_write(struct heif_context* ctx,
    const void* data, size_t size, void* userdata) {
  const char* filename = static_cast<const char*>(userdata);

  std::ofstream ostr(filename, std::ios_base::binary);
  ostr.write(static_cast<const char*>(data), size);
  // TODO: handle write errors
  return Error::Ok.error_struct(ctx->context.get());
}


struct heif_error heif_context_write_to_file(struct heif_context* ctx,
                                             const char* filename)
{
  heif_writer writer;
  writer.writer_api_version = 1;
  writer.write = heif_file_writer_write;
  return heif_context_write(ctx, &writer, (void*)filename);
}


struct heif_error heif_context_write(struct heif_context* ctx,
                                     struct heif_writer* writer,
                                     void* userdata)
{
  if (!writer) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(ctx->context.get());
  } else if (writer->writer_api_version != 1) {
    Error err(heif_error_Usage_error, heif_suberror_Unsupported_writer_version);
    return err.error_struct(ctx->context.get());
  }

  StreamWriter swriter;
  ctx->context->write(swriter);

  const auto& data = swriter.get_data();
  return writer->write(ctx, data.data(), data.size(), userdata);
}


int heif_context_get_encoder_descriptors(struct heif_context* ctx,
                                         enum heif_compression_format format,
                                         const char* name,
                                         const struct heif_encoder_descriptor** out_encoder_descriptors,
                                         int count)
{
  if (out_encoder_descriptors == nullptr || count <= 0) {
    return 0;
  }

  std::vector<const struct heif_encoder_descriptor*> descriptors;
  descriptors = get_filtered_encoder_descriptors(format, name);

  int i;
  for (i=0 ; i < count && static_cast<size_t>(i) < descriptors.size() ; i++) {
    out_encoder_descriptors[i] = descriptors[i];
  }

  return i;
}


const char* heif_encoder_descriptor_get_name(const struct heif_encoder_descriptor* descriptor)
{
  return descriptor->plugin->get_plugin_name();
}


const char* heif_encoder_descriptor_get_id_name(const struct heif_encoder_descriptor* descriptor)
{
  return descriptor->plugin->id_name;
}


int heif_encoder_descriptor_supportes_lossy_compression(const struct heif_encoder_descriptor* descriptor)
{
  return descriptor->plugin->supports_lossy_compression;
}


int heif_encoder_descriptor_supportes_lossless_compression(const struct heif_encoder_descriptor* descriptor)
{
  return descriptor->plugin->supports_lossless_compression;
}



const char* heif_encoder_get_name(const struct heif_encoder* encoder)
{
  return encoder->plugin->get_plugin_name();
}


struct heif_error heif_context_get_encoder(struct heif_context* context,
                                           const struct heif_encoder_descriptor* descriptor,
                                           struct heif_encoder** encoder)
{
  if (!descriptor || !encoder) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(nullptr);
  }

  if (context==nullptr) {
    *encoder = new struct heif_encoder(nullptr, descriptor->plugin);
  }
  else {
    // DEPRECATED. We do not need the context anywhere.
    *encoder = new struct heif_encoder(context->context, descriptor->plugin);
  }

  (*encoder)->alloc();

  struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, kSuccess };
  return err;
}


int heif_have_decoder_for_format(enum heif_compression_format format)
{
  auto plugin = heif::get_decoder(format);
  return plugin != nullptr;
}


int heif_have_encoder_for_format(enum heif_compression_format format)
{
  auto plugin = heif::get_encoder(format);
  return plugin != nullptr;
}


struct heif_error heif_context_get_encoder_for_format(struct heif_context* context,
                                                      enum heif_compression_format format,
                                                      struct heif_encoder** encoder)
{
  if (!encoder) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(nullptr);
  }

  std::vector<const struct heif_encoder_descriptor*> descriptors;
  descriptors = get_filtered_encoder_descriptors(format, nullptr);

  if (descriptors.size()>0) {
    if (context==nullptr) {
      *encoder = new struct heif_encoder(nullptr, descriptors[0]->plugin);
    }
    else {
      // DEPRECATED. We do not need the context anywhere.
      *encoder = new struct heif_encoder(context->context, descriptors[0]->plugin);
    }

    (*encoder)->alloc();

    struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, kSuccess };
    return err;
  }
  else {
    struct heif_error err = { heif_error_Unsupported_filetype, // TODO: is this the right error code?
                              heif_suberror_Unspecified, kSuccess };
    return err;
  }
}


void heif_encoder_release(struct heif_encoder* encoder)
{
  if (encoder) {
    delete encoder;
  }
}


//struct heif_encoder_param* heif_encoder_get_param(struct heif_encoder* encoder)
//{
//  return nullptr;
//}


//void heif_encoder_release_param(struct heif_encoder_param* param)
//{
//}


// Set a 'quality' factor (0-100). How this is mapped to actual encoding parameters is
// encoder dependent.
struct heif_error heif_encoder_set_lossy_quality(struct heif_encoder* encoder,
                                                int quality)
{
  if (!encoder) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(nullptr);
  }

  return encoder->plugin->set_parameter_quality(encoder->encoder, quality);
}


struct heif_error heif_encoder_set_lossless(struct heif_encoder* encoder, int enable)
{
  if (!encoder) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(nullptr);
  }

  return encoder->plugin->set_parameter_lossless(encoder->encoder, enable);
}


struct heif_error heif_encoder_set_logging_level(struct heif_encoder* encoder, int level)
{
  if (!encoder) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(nullptr);
  }

  if (encoder->plugin->set_parameter_logging_level) {
    return encoder->plugin->set_parameter_logging_level(encoder->encoder, level);
  }

  struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, kSuccess };
  return err;
}


const struct heif_encoder_parameter*const* heif_encoder_list_parameters(struct heif_encoder* encoder)
{
  return encoder->plugin->list_parameters(encoder->encoder);
}


const char* heif_encoder_parameter_get_name(const struct heif_encoder_parameter* param)
{
  return param->name;
}

enum heif_encoder_parameter_type
heif_encoder_parameter_get_type(const struct heif_encoder_parameter* param)
{
  return param->type;
}


struct heif_error heif_encoder_set_parameter_integer(struct heif_encoder* encoder,
                                                     const char* parameter_name,
                                                     int value)
{
  return encoder->plugin->set_parameter_integer(encoder->encoder, parameter_name, value);
}

struct heif_error heif_encoder_get_parameter_integer(struct heif_encoder* encoder,
                                                     const char* parameter_name,
                                                     int* value_ptr)
{
  return encoder->plugin->get_parameter_integer(encoder->encoder, parameter_name, value_ptr);
}

struct heif_error
heif_encoder_parameter_get_valid_integer_range(const struct heif_encoder_parameter* param,
                                               int* have_minimum_maximum,
                                               int* minimum, int* maximum)
{
  if (param->type != heif_encoder_parameter_type_integer) {
    return error_unsupported_parameter; // TODO: correct error ?
  }

  if (param->integer.have_minimum_maximum) {
    if (minimum) {
      *minimum = param->integer.minimum;
    }

    if (maximum) {
      *maximum = param->integer.maximum;
    }
  }

  if (have_minimum_maximum) {
    *have_minimum_maximum = param->integer.have_minimum_maximum;
  }

  return error_Ok;
}

struct heif_error
heif_encoder_parameter_get_valid_string_values(const struct heif_encoder_parameter* param,
                                               const char*const** out_stringarray)
{
  if (param->type != heif_encoder_parameter_type_string) {
    return error_unsupported_parameter; // TODO: correct error ?
  }

  if (out_stringarray) {
    *out_stringarray = param->string.valid_values;
  }

  return error_Ok;
}

struct heif_error heif_encoder_parameter_integer_valid_range(struct heif_encoder* encoder,
                                                             const char* parameter_name,
                                                             int* have_minimum_maximum,
                                                             int* minimum, int* maximum)
{
  for (const struct heif_encoder_parameter*const* params = heif_encoder_list_parameters(encoder);
       *params;
       params++) {
    if (strcmp((*params)->name, parameter_name)==0) {
      return heif_encoder_parameter_get_valid_integer_range(*params, have_minimum_maximum,
                                                            minimum, maximum);
    }
  }

  return error_unsupported_parameter;
}

struct heif_error heif_encoder_set_parameter_boolean(struct heif_encoder* encoder,
                                                     const char* parameter_name,
                                                     int value)
{
  return encoder->plugin->set_parameter_boolean(encoder->encoder, parameter_name, value);
}

struct heif_error heif_encoder_get_parameter_boolean(struct heif_encoder* encoder,
                                                     const char* parameter_name,
                                                     int* value_ptr)
{
  return encoder->plugin->get_parameter_boolean(encoder->encoder, parameter_name, value_ptr);
}

struct heif_error heif_encoder_set_parameter_string(struct heif_encoder* encoder,
                                                    const char* parameter_name,
                                                    const char* value)
{
  return encoder->plugin->set_parameter_string(encoder->encoder, parameter_name, value);
}

struct heif_error heif_encoder_get_parameter_string(struct heif_encoder* encoder,
                                                    const char* parameter_name,
                                                    char* value_ptr, int value_size)
{
  return encoder->plugin->get_parameter_string(encoder->encoder, parameter_name,
                                               value_ptr, value_size);
}

struct heif_error heif_encoder_parameter_string_valid_values(struct heif_encoder* encoder,
                                                             const char* parameter_name,
                                                             const char*const** out_stringarray)
{
  for (const struct heif_encoder_parameter*const* params = heif_encoder_list_parameters(encoder);
       *params;
       params++) {
    if (strcmp((*params)->name, parameter_name)==0) {
      return heif_encoder_parameter_get_valid_string_values(*params, out_stringarray);
    }
  }

  return error_unsupported_parameter;
}



bool parse_boolean(const char* value)
{
  if (strcmp(value,"true")==0) {
    return true;
  }
  else if (strcmp(value,"false")==0) {
    return false;
  }
  else if (strcmp(value,"1")==0) {
    return true;
  }
  else if (strcmp(value,"0")==0) {
    return false;
  }

  return false;
}


struct heif_error heif_encoder_set_parameter(struct heif_encoder* encoder,
                                             const char* parameter_name,
                                             const char* value)
{
  for (const struct heif_encoder_parameter*const* params = heif_encoder_list_parameters(encoder);
       *params;
       params++) {
    if (strcmp((*params)->name, parameter_name)==0) {
      switch ((*params)->type) {
      case heif_encoder_parameter_type_integer:
        return heif_encoder_set_parameter_integer(encoder, parameter_name, atoi(value));

      case heif_encoder_parameter_type_boolean:
        return heif_encoder_set_parameter_boolean(encoder, parameter_name, parse_boolean(value));

      case heif_encoder_parameter_type_string:
        return heif_encoder_set_parameter_string(encoder, parameter_name, value);
        break;
      }

      return error_Ok;
    }
  }

  return heif_encoder_set_parameter_string(encoder, parameter_name, value);

  //return error_unsupported_parameter;
}


struct heif_error heif_encoder_get_parameter(struct heif_encoder* encoder,
                                             const char* parameter_name,
                                             char* value_ptr, int value_size)
{
  for (const struct heif_encoder_parameter*const* params = heif_encoder_list_parameters(encoder);
       *params;
       params++) {
    if (strcmp((*params)->name, parameter_name)==0) {
      switch ((*params)->type) {
      case heif_encoder_parameter_type_integer:
        {
          int value;
          struct heif_error error = heif_encoder_get_parameter_integer(encoder, parameter_name, &value);
          if (error.code) {
            return error;
          }
          else {
            snprintf(value_ptr, value_size, "%d",value);
          }
        }
        break;

      case heif_encoder_parameter_type_boolean:
        {
          int value;
          struct heif_error error = heif_encoder_get_parameter_boolean(encoder, parameter_name, &value);
          if (error.code) {
            return error;
          }
          else {
            snprintf(value_ptr, value_size, "%d",value);
          }
        }
        break;

      case heif_encoder_parameter_type_string:
        {
          struct heif_error error = heif_encoder_get_parameter_string(encoder, parameter_name,
                                                                      value_ptr, value_size);
          if (error.code) {
            return error;
          }
        }
        break;
      }

      return error_Ok;
    }
  }

  return error_unsupported_parameter;
}


int heif_encoder_has_default(struct heif_encoder* encoder,
                             const char* parameter_name)
{
  for (const struct heif_encoder_parameter*const* params = heif_encoder_list_parameters(encoder);
       *params;
       params++) {
    if (strcmp((*params)->name, parameter_name)==0) {

      if ((*params)->version >= 2) {
        return (*params)->has_default;
      }
      else {
        return true;
      }
    }
  }

  return false;
}


static void set_default_options(heif_encoding_options& options)
{
  options.version = 1;

  options.save_alpha_channel = true;
}


heif_encoding_options* heif_encoding_options_alloc()
{
  auto options = new heif_encoding_options;

  set_default_options(*options);

  return options;
}


void heif_encoding_options_free(heif_encoding_options* options)
{
  delete options;
}

struct heif_error heif_context_encode_image(struct heif_context* ctx,
                                            const struct heif_image* input_image,
                                            struct heif_encoder* encoder,
                                            const struct heif_encoding_options* options,
                                            struct heif_image_handle** out_image_handle)
{
  if (!encoder) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument).error_struct(ctx->context.get());
  }

  heif_encoding_options default_options;
  if (options==nullptr) {
    set_default_options(default_options);
    options = &default_options;
  }

  std::shared_ptr<HeifContext::Image> image;
  Error error;

  error = ctx->context->encode_image(input_image->image,
                                     encoder,
                                     options,
                                     heif_image_input_class_normal,
                                     image);
  if (error != Error::Ok) {
    return error.error_struct(ctx->context.get());
  }

  // mark the new image as primary image

  if (ctx->context->is_primary_image_set() == false) {
    ctx->context->set_primary_image(image);
  }

  if (out_image_handle) {
    *out_image_handle = new heif_image_handle;
    (*out_image_handle)->image = image;
    (*out_image_handle)->context = ctx->context;
  }

  return error_Ok;
}


struct heif_error heif_context_assign_thumbnail(struct heif_context* ctx,
                                                const struct heif_image_handle* master_image,
                                                const struct heif_image_handle* thumbnail_image)
{
  Error error = ctx->context->assign_thumbnail(thumbnail_image->image, master_image->image);
  return error.error_struct(ctx->context.get());
}


struct heif_error heif_context_encode_thumbnail(struct heif_context* ctx,
                                                const struct heif_image* image,
                                                const struct heif_image_handle* image_handle,
                                                struct heif_encoder* encoder,
                                                const struct heif_encoding_options* options,
                                                int bbox_size,
                                                struct heif_image_handle** out_image_handle)
{
  std::shared_ptr<HeifContext::Image> thumbnail_image;

  heif_encoding_options default_options;
  if (options==nullptr) {
    set_default_options(default_options);
    options = &default_options;
  }

  Error error = ctx->context->encode_thumbnail(image->image,
                                               encoder,
                                               options,
                                               bbox_size,
                                               thumbnail_image);
  if (error != Error::Ok) {
    return error.error_struct(ctx->context.get());
  } else if (!thumbnail_image) {
    Error err(heif_error_Usage_error,
              heif_suberror_Invalid_parameter_value,
              "Thumbnail images must be smaller than the original image.");
    return err.error_struct(ctx->context.get());
  }

  error = ctx->context->assign_thumbnail(image_handle->image, thumbnail_image);
  if (error != Error::Ok) {
    return error.error_struct(ctx->context.get());
  }


  if (out_image_handle) {
    if (thumbnail_image) {
      *out_image_handle = new heif_image_handle;
      (*out_image_handle)->image = thumbnail_image;
      (*out_image_handle)->context = ctx->context;
    }
    else {
      *out_image_handle = nullptr;
    }
  }

  return error_Ok;
}


struct heif_error heif_context_set_primary_image(struct heif_context* ctx,
                                                 struct heif_image_handle* image_handle)
{
  ctx->context->set_primary_image(image_handle->image);

  return error_Ok;
}


struct heif_error heif_context_add_exif_metadata(struct heif_context* ctx,
                                                 const struct heif_image_handle* image_handle,
                                                 const void* data, int size)
{
  Error error = ctx->context->add_exif_metadata(image_handle->image, data, size);
  if (error != Error::Ok) {
    return error.error_struct(ctx->context.get());
  }
  else {
    return error_Ok;
  }
}


struct heif_error heif_context_add_XMP_metadata(struct heif_context* ctx,
                                                const struct heif_image_handle* image_handle,
                                                const void* data, int size)
{
  Error error = ctx->context->add_XMP_metadata(image_handle->image, data, size);
  if (error != Error::Ok) {
    return error.error_struct(ctx->context.get());
  }
  else {
    return error_Ok;
  }
}


struct heif_error heif_context_add_generic_metadata(struct heif_context* ctx,
                                                    const struct heif_image_handle* image_handle,
                                                    const void* data, int size,
                                                    const char* item_type, const char* content_type)
{
  Error error = ctx->context->add_generic_metadata(image_handle->image, data, size,
                                                   item_type, content_type);
  if (error != Error::Ok) {
    return error.error_struct(ctx->context.get());
  }
  else {
    return error_Ok;
  }
}


void heif_context_set_maximum_image_size_limit(struct heif_context* ctx, int maximum_width)
{
  ctx->context->set_maximum_image_size_limit(maximum_width);
}
