/*
 * HEIF codec.
 * Copyright (c) 2017-2025 Dirk Farin <dirk.farin@gmail.com>
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

#include "heif_metadata.h"
#include "libheif/heif.h"
#include "libheif/api_structs.h"

#include "box.h"

#include <cassert>
#include <cstring>
#include <memory>
#include <array>


int heif_image_handle_get_number_of_metadata_blocks(const struct heif_image_handle* handle,
                                                    const char* type_filter)
{
  int cnt = 0;
  for (const auto& metadata : handle->image->get_metadata()) {
    if (type_filter == nullptr ||
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
  int cnt = 0;
  for (const auto& metadata : handle->image->get_metadata()) {
    if (type_filter == nullptr ||
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
  for (auto& metadata : handle->image->get_metadata()) {
    if (metadata->item_id == metadata_id) {
      return metadata->item_type.c_str();
    }
  }

  return nullptr;
}


const char* heif_image_handle_get_metadata_content_type(const struct heif_image_handle* handle,
                                                        heif_item_id metadata_id)
{
  for (auto& metadata : handle->image->get_metadata()) {
    if (metadata->item_id == metadata_id) {
      return metadata->content_type.c_str();
    }
  }

  return nullptr;
}


size_t heif_image_handle_get_metadata_size(const struct heif_image_handle* handle,
                                           heif_item_id metadata_id)
{
  for (auto& metadata : handle->image->get_metadata()) {
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
  for (auto& metadata : handle->image->get_metadata()) {
    if (metadata->item_id == metadata_id) {

      if (!metadata->m_data.empty()) {
        if (out_data == nullptr) {
          Error err(heif_error_Usage_error,
                    heif_suberror_Null_pointer_argument);
          return err.error_struct(handle->image.get());
        }

        memcpy(out_data,
               metadata->m_data.data(),
               metadata->m_data.size());
      }

      return Error::Ok.error_struct(handle->image.get());
    }
  }

  Error err(heif_error_Usage_error,
            heif_suberror_Nonexisting_item_referenced);
  return err.error_struct(handle->image.get());
}


const char* heif_image_handle_get_metadata_item_uri_type(const struct heif_image_handle* handle,
                                                         heif_item_id metadata_id)
{
  for (auto& metadata : handle->image->get_metadata()) {
    if (metadata->item_id == metadata_id) {
      return metadata->item_uri_type.c_str();
    }
  }

  return nullptr;
}


// ------------------------- intrinsic and extrinsic matrices -------------------------


int heif_image_handle_has_camera_intrinsic_matrix(const struct heif_image_handle* handle)
{
  if (!handle) {
    return false;
  }

  return handle->image->has_intrinsic_matrix();
}

struct heif_error heif_image_handle_get_camera_intrinsic_matrix(const struct heif_image_handle* handle,
                                                                struct heif_camera_intrinsic_matrix* out_matrix)
{
  if (handle == nullptr || out_matrix == nullptr) {
    return heif_error{heif_error_Usage_error,
                      heif_suberror_Null_pointer_argument};
  }

  if (!handle->image->has_intrinsic_matrix()) {
    Error err(heif_error_Usage_error,
              heif_suberror_Camera_intrinsic_matrix_undefined);
    return err.error_struct(handle->image.get());
  }

  const auto& m = handle->image->get_intrinsic_matrix();
  out_matrix->focal_length_x = m.focal_length_x;
  out_matrix->focal_length_y = m.focal_length_y;
  out_matrix->principal_point_x = m.principal_point_x;
  out_matrix->principal_point_y = m.principal_point_y;
  out_matrix->skew = m.skew;

  return heif_error_success;
}

int heif_image_handle_has_camera_extrinsic_matrix(const struct heif_image_handle* handle)
{
  if (!handle) {
    return false;
  }

  return handle->image->has_extrinsic_matrix();
}

struct heif_camera_extrinsic_matrix
{
  Box_cmex::ExtrinsicMatrix matrix;
};

struct heif_error heif_image_handle_get_camera_extrinsic_matrix(const struct heif_image_handle* handle,
                                                                struct heif_camera_extrinsic_matrix** out_matrix)
{
  if (handle == nullptr || out_matrix == nullptr) {
    return heif_error{heif_error_Usage_error,
                      heif_suberror_Null_pointer_argument};
  }

  if (!handle->image->has_extrinsic_matrix()) {
    Error err(heif_error_Usage_error,
              heif_suberror_Camera_extrinsic_matrix_undefined);
    return err.error_struct(handle->image.get());
  }

  *out_matrix = new heif_camera_extrinsic_matrix;
  (*out_matrix)->matrix = handle->image->get_extrinsic_matrix();

  return heif_error_success;
}

void heif_camera_extrinsic_matrix_release(struct heif_camera_extrinsic_matrix* matrix)
{
  delete matrix;
}

struct heif_error heif_camera_extrinsic_matrix_get_rotation_matrix(const struct heif_camera_extrinsic_matrix* matrix,
                                                                   double* out_matrix_row_major)
{
  if (matrix == nullptr || out_matrix_row_major == nullptr) {
    return heif_error{heif_error_Usage_error,
                      heif_suberror_Null_pointer_argument};
  }

  auto m3x3 = matrix->matrix.calculate_rotation_matrix();

  for (int i=0;i<9;i++) {
    out_matrix_row_major[i] = m3x3[i];
  }

  return heif_error_success;
}

