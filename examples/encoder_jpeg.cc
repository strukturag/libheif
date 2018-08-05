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

void JpegEncoder::UpdateDecodingOptions(const struct heif_image_handle* handle,
    struct heif_decoding_options *options) const {
  if (HasExifMetaData(handle)) {
    options->ignore_transformations = 1;
  }
}

// static
void JpegEncoder::OnJpegError(j_common_ptr cinfo) {
  ErrorHandler* handler = reinterpret_cast<ErrorHandler*>(cinfo->err);
  longjmp(handler->setjmp_buffer, 1);
}



#define ICC_MARKER  (JPEG_APP0 + 2)     /* JPEG marker code for ICC */
#define ICC_OVERHEAD_LEN  14            /* size of non-profile data in APP2 */
#define MAX_BYTES_IN_MARKER  65533      /* maximum data len of a JPEG marker */
#define MAX_DATA_BYTES_IN_MARKER (MAX_BYTES_IN_MARKER - ICC_OVERHEAD_LEN)

 /*
 * This routine writes the given ICC profile data into a JPEG file.  It *must*
 * be called AFTER calling jpeg_start_compress() and BEFORE the first call to
 * jpeg_write_scanlines().  (This ordering ensures that the APP2 marker(s) will
 * appear after the SOI and JFIF or Adobe markers, but before all else.)
 */

 /* This function is copied almost as is from libjpeg-turbo */

static
void jpeg_write_icc_profile(j_compress_ptr cinfo, const JOCTET *icc_data_ptr,
                            unsigned int icc_data_len)
{
  unsigned int num_markers;     /* total number of markers we'll write */
  int cur_marker = 1;           /* per spec, counting starts at 1 */
  unsigned int length;          /* number of bytes to write in this marker */

  /* Calculate the number of markers we'll need, rounding up of course */
  num_markers = icc_data_len / MAX_DATA_BYTES_IN_MARKER;
  if (num_markers * MAX_DATA_BYTES_IN_MARKER != icc_data_len)
    num_markers++;

  while (icc_data_len > 0) {
    /* length of profile to put in this marker */
    length = icc_data_len;
    if (length > MAX_DATA_BYTES_IN_MARKER)
      length = MAX_DATA_BYTES_IN_MARKER;
    icc_data_len -= length;

    /* Write the JPEG marker header (APP2 code and marker length) */
    jpeg_write_m_header(cinfo, ICC_MARKER,
                        (unsigned int)(length + ICC_OVERHEAD_LEN));

    /* Write the marker identifying string "ICC_PROFILE" (null-terminated).  We
     * code it in this less-than-transparent way so that the code works even if
     * the local character set is not ASCII.
     */
    jpeg_write_m_byte(cinfo, 0x49);
    jpeg_write_m_byte(cinfo, 0x43);
    jpeg_write_m_byte(cinfo, 0x43);
    jpeg_write_m_byte(cinfo, 0x5F);
    jpeg_write_m_byte(cinfo, 0x50);
    jpeg_write_m_byte(cinfo, 0x52);
    jpeg_write_m_byte(cinfo, 0x4F);
    jpeg_write_m_byte(cinfo, 0x46);
    jpeg_write_m_byte(cinfo, 0x49);
    jpeg_write_m_byte(cinfo, 0x4C);
    jpeg_write_m_byte(cinfo, 0x45);
    jpeg_write_m_byte(cinfo, 0x0);

    /* Add the sequencing info */
    jpeg_write_m_byte(cinfo, cur_marker);
    jpeg_write_m_byte(cinfo, (int)num_markers);

    /* Add the profile data */
    while (length--) {
      jpeg_write_m_byte(cinfo, *icc_data_ptr);
      icc_data_ptr++;
    }
    cur_marker++;
  }
}

bool JpegEncoder::Encode(const struct heif_image_handle* handle,
    const struct heif_image* image, const std::string& filename) {
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
  static const boolean kForceBaseline = TRUE;
  jpeg_set_quality(&cinfo, quality_, kForceBaseline);
  static const boolean kWriteAllTables = TRUE;
  jpeg_start_compress(&cinfo, kWriteAllTables);

  size_t exifsize = 0;
  uint8_t* exifdata = GetExifMetaData(handle, &exifsize);
  if (exifdata && exifsize > 4) {
    static const uint8_t kExifMarker = JPEG_APP0 + 1;
    jpeg_write_marker(&cinfo, kExifMarker, exifdata + 4,
        static_cast<unsigned int>(exifsize - 4));
    free(exifdata);
  }

  size_t profile_size = heif_image_handle_get_color_profile_size(handle);
  if (profile_size > 0){
    uint8_t* profile_data = static_cast<uint8_t*>(malloc(profile_size));
    heif_image_handle_get_color_profile(handle, profile_data);
    jpeg_write_icc_profile(&cinfo, profile_data, (unsigned int)profile_size);
    free(profile_data);
  }

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
