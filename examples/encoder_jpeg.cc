/*
 * libheif example application "convert".
 * Copyright (c) 2017 struktur AG, Joachim Bauch <bauch@struktur.de>
 *
 * This file is part of convert, an example application using libheif.
 *
 * convert is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * convert is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with convert.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <assert.h>
#include <errno.h>
#include <string.h>

#include <iostream>

#include "encoder_jpeg.h"

JpegEncoder::JpegEncoder(int quality) : quality_(quality) {
  if (quality_ < 0 || quality_ > 100) {
    quality_ = kDefaultQuality;
  }
}

// static
void JpegEncoder::OnJpegError(j_common_ptr cinfo) {
  ErrorHandler* handler = reinterpret_cast<ErrorHandler*>(cinfo->err);
  longjmp(handler->setjmp_buffer, 1);
}

bool JpegEncoder::Encode(const struct heif_image* image,
    const std::string& filename) {
  FILE* fp = fopen(filename.c_str(), "wb");
  if (!fp) {
    fprintf(stderr, "Can't open %s: %s\n", filename.c_str(), strerror(errno));
    return false;
  }

  struct jpeg_compress_struct cinfo;
  struct ErrorHandler jerr;
  cinfo.err = jpeg_std_error(reinterpret_cast<struct jpeg_error_mgr*>(&jerr));
  jerr.pub.error_exit = &JpegEncoder::OnJpegError;
  if (setjmp(jerr.setjmp_buffer)) {
    cinfo.err->output_message(reinterpret_cast<j_common_ptr>(&cinfo));
    jpeg_destroy_compress(&cinfo);
    fclose(fp);
    return false;
  }

  jpeg_create_compress(&cinfo);
  jpeg_stdio_dest(&cinfo, fp);

  cinfo.image_width = heif_image_get_width(image, heif_channel_Y);
  cinfo.image_height = heif_image_get_height(image, heif_channel_Y);
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_YCbCr;
  jpeg_set_defaults(&cinfo);
  static const bool kForceBaseline = true;
  jpeg_set_quality(&cinfo, quality_, kForceBaseline);
  static const bool kWriteAllTables = true;
  jpeg_start_compress(&cinfo, kWriteAllTables);

  int stride_y;
  const uint8_t* row_y = heif_image_get_plane_readonly(image, heif_channel_Y,
      &stride_y);
  int stride_u;
  const uint8_t* row_u = heif_image_get_plane_readonly(image, heif_channel_Cb,
      &stride_u);
  int stride_v;
  const uint8_t* row_v = heif_image_get_plane_readonly(image, heif_channel_Cr,
      &stride_v);

  JSAMPARRAY buffer = cinfo.mem->alloc_sarray(
      reinterpret_cast<j_common_ptr>(&cinfo), JPOOL_IMAGE,
      cinfo.image_width * cinfo.input_components, 1);
  JSAMPROW row[1] = { buffer[0] };

  while (cinfo.next_scanline < cinfo.image_height) {
    size_t offset_y = cinfo.next_scanline * stride_y;
    const uint8_t* start_y = &row_y[offset_y];
    size_t offset_u = (cinfo.next_scanline / 2) * stride_u;
    const uint8_t* start_u = &row_u[offset_u];
    size_t offset_v = (cinfo.next_scanline / 2) * stride_v;
    const uint8_t* start_v = &row_v[offset_v];

    JOCTET* bufp = buffer[0];
    for (JDIMENSION x = 0; x < cinfo.image_width; ++x) {
      *bufp++ = start_y[x];
      *bufp++ = start_u[x / 2];
      *bufp++ = start_v[x / 2];
    }
    jpeg_write_scanlines(&cinfo, row, 1);
  }
  jpeg_finish_compress(&cinfo);
  fclose(fp);
  jpeg_destroy_compress(&cinfo);
  return true;
}
