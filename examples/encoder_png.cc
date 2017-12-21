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

bool PngEncoder::Encode(const std::shared_ptr<HeifPixelImage>& image,
    const std::string& filename) {
  if (image->get_chroma_format() != heif_chroma_420) {
    fprintf(stderr, "Only YUV420 images supported.\n");
    return false;
  }

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

  int width = image->get_width();
  int height = image->get_height();
  static const int kBitDepth = 8;
  static const int kColorType = PNG_COLOR_TYPE_RGB;
  png_set_IHDR(png_ptr, info_ptr, width, height, kBitDepth, kColorType,
      PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
  png_write_info(png_ptr, info_ptr);

  uint8_t** row_pointers = new uint8_t*[height];
  for (int y = 0; y < height; ++y) {
    row_pointers[y] = new uint8_t[width * 3];
  }

  int stride_y;
  const uint8_t* row_y = image->get_plane(heif_channel_Y, &stride_y);
  int stride_u;
  const uint8_t* row_u = image->get_plane(heif_channel_Cb, &stride_u);
  int stride_v;
  const uint8_t* row_v = image->get_plane(heif_channel_Cr, &stride_v);

  switch (image->get_chroma_format()) {
    case heif_chroma_420:
      // Simple YUV -> RGB conversion:
      // R = 1.164 * (Y - 16) + 1.596 * (V - 128)
      // G = 1.164 * (Y - 16) - 0.813 * (V - 128) - 0.391 * (U - 128)
      // B = 1.164 * (Y - 16) + 2.018 * (U - 128)
      for (int y = 0; y < height; ++y) {
        const uint8_t* start_y = &row_y[y * stride_y];
        const uint8_t* start_u = &row_u[(y / 2) * stride_u];
        const uint8_t* start_v = &row_v[(y / 2) * stride_v];
        for (int x = 0; x < width / 2; ++x) {
          float y_val;
          float u_val = static_cast<float>(start_u[x]) - 128;
          float v_val = static_cast<float>(start_v[x]) - 128;
          y_val = 1.164 * (static_cast<float>(start_y[x*2]) - 16);
          row_pointers[y][x*2*3] = clip(y_val + 1.596 * v_val);
          row_pointers[y][x*2*3+1] =
              clip(y_val - 0.813 * v_val - 0.391 * u_val);
          row_pointers[y][x*2*3+2] = clip(y_val + 2.018 * u_val);

          y_val = 1.164 * (static_cast<float>(start_y[x*2+1]) - 16);
          row_pointers[y][(x*2+1)*3] = clip(y_val + 1.596 * v_val);
          row_pointers[y][(x*2+1)*3+1] =
              clip(y_val - 0.813 * v_val - 0.391 * u_val);
          row_pointers[y][(x*2+1)*3+2] = clip(y_val + 2.018 * u_val);
        }
      }
      break;
    default:
      // Checked above.
      assert(false);
      return false;
  }

  png_write_image(png_ptr, row_pointers);
  png_write_end(png_ptr, nullptr);
  png_destroy_write_struct(&png_ptr, &info_ptr);
  for (int y = 0; y < height; ++y) {
    delete[] row_pointers[y];
  }
  delete[] row_pointers;
  fclose(fp);
  return true;
}
