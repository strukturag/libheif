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
#include <math.h>
#include <png.h>

#include "encoder_png.h"

PngEncoder::PngEncoder() {}

inline uint8_t clip(float value) {
  if (value < 0) {
    return 0x00;
  } else if (value >= 255) {
    return 0xff;
  } else {
    return static_cast<uint8_t>(round(value));
  }
}

bool PngEncoder::Encode(const struct heif_image* image,
    const std::string& filename) {
  png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr,
      nullptr, nullptr);
  if (!png_ptr) {
    fprintf(stderr, "libpng initialization failed (1)\n");
    return false;
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!png_ptr) {
    png_destroy_write_struct(&png_ptr, nullptr);
    fprintf(stderr, "libpng initialization failed (2)\n");
    return false;
  }

  FILE* fp = fopen(filename.c_str(), "wb");
  if (!fp) {
    fprintf(stderr, "Can't open %s: %s\n", filename.c_str(), strerror(errno));
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return false;
  }

  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    fprintf(stderr, "Error while encoding image\n");
    return false;
  }

  png_init_io(png_ptr, fp);

  bool withAlpha = (heif_image_get_chroma_format(image) == heif_chroma_interleaved_32bit);

  int width = heif_image_get_width(image, heif_channel_interleaved);
  int height = heif_image_get_height(image, heif_channel_interleaved);
  static const int kBitDepth = 8;
  static const int kColorType = withAlpha ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB;
  png_set_IHDR(png_ptr, info_ptr, width, height, kBitDepth, kColorType,
      PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
  png_write_info(png_ptr, info_ptr);

  uint8_t** row_pointers = new uint8_t*[height];

  int stride_rgb;
  const uint8_t* row_rgb = heif_image_get_plane_readonly(image,
      heif_channel_interleaved, &stride_rgb);

  for (int y = 0; y < height; ++y) {
    row_pointers[y] = const_cast<uint8_t*>(&row_rgb[y * stride_rgb]);
  }

  png_write_image(png_ptr, row_pointers);
  png_write_end(png_ptr, nullptr);
  png_destroy_write_struct(&png_ptr, &info_ptr);
  delete[] row_pointers;
  fclose(fp);
  return true;
}
