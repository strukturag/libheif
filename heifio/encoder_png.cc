/*
  libheif example application.

  MIT License

  Copyright (c) 2017 struktur AG, Joachim Bauch <bauch@struktur.de>

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
#include <cerrno>
#include <png.h>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "encoder_png.h"
#include "exif.h"

PngEncoder::PngEncoder() = default;

bool PngEncoder::Encode(const struct heif_image_handle* handle,
                        const struct heif_image* image, const std::string& filename)
{
  png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr,
                                                nullptr, nullptr);
  if (!png_ptr) {
    fprintf(stderr, "libpng initialization failed (1)\n");
    return false;
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    png_destroy_write_struct(&png_ptr, nullptr);
    fprintf(stderr, "libpng initialization failed (2)\n");
    return false;
  }

  if (m_compression_level != -1) {
    png_set_compression_level(png_ptr, m_compression_level);
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

  bool withAlpha = (heif_image_get_chroma_format(image) == heif_chroma_interleaved_RGBA ||
                    heif_image_get_chroma_format(image) == heif_chroma_interleaved_RRGGBBAA_BE);

  int width = heif_image_get_width(image, heif_channel_interleaved);
  int height = heif_image_get_height(image, heif_channel_interleaved);

  int bitDepth;
  int input_bpp = heif_image_get_bits_per_pixel_range(image, heif_channel_interleaved);
  if (input_bpp > 8) {
    bitDepth = 16;
  }
  else {
    bitDepth = 8;
  }

  const int colorType = withAlpha ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB;

  png_set_IHDR(png_ptr, info_ptr, width, height, bitDepth, colorType,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

  // --- write ICC profile

  if (handle) {
    size_t profile_size = heif_image_handle_get_raw_color_profile_size(handle);
    if (profile_size > 0) {
      uint8_t* profile_data = static_cast<uint8_t*>(malloc(profile_size));
      heif_image_handle_get_raw_color_profile(handle, profile_data);
      char profile_name[] = "unknown";
      png_set_iCCP(png_ptr, info_ptr, profile_name, PNG_COMPRESSION_TYPE_BASE,
#if PNG_LIBPNG_VER < 10500
              (png_charp)profile_data,
#else
                   (png_const_bytep) profile_data,
#endif
                   (png_uint_32) profile_size);
      free(profile_data);
    }
  }

  // --- write EXIF metadata

#ifdef PNG_eXIf_SUPPORTED
  if (handle) {
    size_t exifsize = 0;
    uint8_t* exifdata = GetExifMetaData(handle, &exifsize);
    if (exifdata) {
      if (exifsize > 4) {
        uint32_t skip = (exifdata[0] << 24) | (exifdata[1] << 16) | (exifdata[2] << 8) | exifdata[3];
        if (skip < (exifsize - 4)) {
          skip += 4;
          uint8_t* ptr = exifdata + skip;
          size_t size = exifsize - skip;

          // libheif by default normalizes the image orientation, so that we have to set the EXIF Orientation to "Horizontal (normal)"
          modify_exif_orientation_tag_if_it_exists(ptr, (int) size, 1);
          overwrite_exif_image_size_if_it_exists(ptr, (int) size, width, height);

          png_set_eXIf_1(png_ptr, info_ptr, (png_uint_32) size, ptr);
        }
      }

      free(exifdata);
    }
  }
#endif

  // --- write XMP metadata

#ifdef PNG_iTXt_SUPPORTED
  if (handle) {
    // spec: https://raw.githubusercontent.com/adobe/xmp-docs/master/XMPSpecifications/XMPSpecificationPart3.pdf
    std::vector<uint8_t> xmp = get_xmp_metadata(handle);
    if (!xmp.empty()) {
      // make sure that XMP string is always null terminated.
      if (xmp.back() != 0) {
        xmp.push_back(0);
      }

      // compute XMP string length
      size_t text_length = 0;
      while (xmp[text_length] != 0) {
        text_length++;
      }

      png_text xmp_text{}; // important to zero-initialize the structure so that the remaining fields are NULL !
      xmp_text.compression = PNG_ITXT_COMPRESSION_NONE;
      xmp_text.key = (char*) "XML:com.adobe.xmp";
      xmp_text.text = (char*) xmp.data();
      xmp_text.text_length = 0; // should be 0 for ITXT according the libpng documentation
      xmp_text.itxt_length = text_length;
      png_set_text(png_ptr, info_ptr, &xmp_text, 1);
    }
  }
#endif

  png_write_info(png_ptr, info_ptr);

  uint8_t** row_pointers = new uint8_t* [height];

  size_t stride_rgb;
  const uint8_t* row_rgb = heif_image_get_plane_readonly2(image,
                                                          heif_channel_interleaved, &stride_rgb);

  for (int y = 0; y < height; ++y) {
    row_pointers[y] = const_cast<uint8_t*>(&row_rgb[y * stride_rgb]);
  }

  if (bitDepth == 16) {
    // shift image data to full 16bit range

    int shift = 16 - input_bpp;
    if (shift > 0) {
      for (int y = 0; y < height; ++y) {
        for (size_t x = 0; x < stride_rgb; x += 2) {
          uint8_t* p = (&row_pointers[y][x]);
          int v = (p[0] << 8) | p[1];
          v = (v << shift) | (v >> (16 - shift));
          p[0] = (uint8_t) (v >> 8);
          p[1] = (uint8_t) (v & 0xFF);
        }
      }
    }
  }


  png_write_image(png_ptr, row_pointers);

  png_write_end(png_ptr, nullptr);
  png_destroy_write_struct(&png_ptr, &info_ptr);
  delete[] row_pointers;
  fclose(fp);
  return true;
}
