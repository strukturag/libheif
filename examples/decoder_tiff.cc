/*
  libheif example application "heif".

  MIT License

  Copyright (c) 2024 Joachim Bauch <bauch@struktur.de>

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

#include <iostream>
#include <memory>

#include <string.h>

extern "C" {
#include <tiff.h>
#include <tiffio.h>
}

#include "decoder_tiff.h"

InputImage loadTIFF(const char* filename) {
  std::unique_ptr<TIFF, void(*)(TIFF*)> tifPtr(TIFFOpen(filename, "r"), [](TIFF* tif) { TIFFClose(tif); });
  if (!tifPtr) {
    std::cerr << "Can't open " << filename << "\n";
    exit(1);
  }

  TIFF* tif = tifPtr.get();
  if (TIFFIsTiled(tif)) {
    // TODO: Implement this.
    std::cerr << "Tiled TIFF images are not supported.\n";
    exit(1);
  }

  InputImage input_image;

  uint16_t shortv, bpp, bps, config, format;
  uint32_t width, height;
  uint32_t row;
  if (TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &shortv) && shortv == PHOTOMETRIC_PALETTE) {
    std::cerr << "Palette TIFF images are not supported.\n";
    exit(1);
  }

  if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width) ||
      !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height)) {
    std::cerr << "Can't read width and/or height from TIFF image.\n";
    exit(1);
  }

  TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &config);
  TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &bpp);
  if (bpp != 1 && bpp != 3 && bpp != 4) {
    std::cerr << "Unsupported TIFF samples per pixel: " << bpp << "\n";
    exit(1);
  }

  TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bps);
  if (bps != 8) {
    std::cerr << "Unsupported TIFF bits per sample: " << bps << "\n";
    exit(1);
  }

  if (TIFFGetField(tif, TIFFTAG_SAMPLEFORMAT, &format) && format != SAMPLEFORMAT_UINT) {
    std::cerr << "Unsupported TIFF sample format: " << format << "\n";
    exit(1);
  }

  struct heif_error err;
  struct heif_image* image = nullptr;
  heif_colorspace colorspace = bpp == 1 ? heif_colorspace_monochrome : heif_colorspace_RGB;
  heif_chroma chroma = bpp == 1 ? heif_chroma_monochrome : heif_chroma_interleaved_RGB;
  if (bpp == 4) {
    chroma = heif_chroma_interleaved_RGBA;
  }

  err = heif_image_create((int) width, (int) height, colorspace, chroma, &image);
  (void) err;
  // TODO: handle error

  switch (config) {
    case PLANARCONFIG_CONTIG:
      {
        heif_channel channel = heif_channel_interleaved;
        heif_image_add_plane(image, channel, (int) width, (int) height, bpp*8);

        int y_stride;
        uint8_t* py = heif_image_get_plane(image, channel, &y_stride);

        tdata_t buf = _TIFFmalloc(TIFFScanlineSize(tif));
        for (row = 0; row < height; row++) {
          TIFFReadScanline(tif, buf, row, 0);
          memcpy(py, buf, width*bpp);
          py += y_stride;
        }
        _TIFFfree(buf);
      }
      break;
    case PLANARCONFIG_SEPARATE:
      {
        heif_channel channel = heif_channel_interleaved;
        heif_image_add_plane(image, channel, (int) width, (int) height, bpp*8);

        int y_stride;
        uint8_t* py = heif_image_get_plane(image, channel, &y_stride);

        if (bpp == 4) {
          TIFFRGBAImage img;
          char emsg[1024] = { 0 };
          if (!TIFFRGBAImageBegin(&img, tif, 1, emsg)) {
            heif_image_release(image);
            std::cerr << "Could not get RGBA image: " << emsg << "\n";
            exit(1);
          }

          uint32_t* buf = static_cast<uint32_t*>(_TIFFmalloc(width*bpp));
          for (row = 0; row < height; row++) {
            TIFFReadRGBAStrip(tif, row, buf);
            memcpy(py, buf, width*bpp);
            py += y_stride;
          }
          _TIFFfree(buf);
          TIFFRGBAImageEnd(&img);
        } else {
          uint8_t* buf = static_cast<uint8_t*>(_TIFFmalloc(TIFFScanlineSize(tif)));
          for (uint16_t i = 0; i < bpp; i++) {
            uint8_t* dest = py+i;
            for (row = 0; row < height; row++) {
              TIFFReadScanline(tif, buf, row, i);
              for (uint32_t x = 0; x < width; x++, dest+=bpp) {
                *dest = buf[x];
              }
              dest += (y_stride - width*bpp);
            }
          }
          _TIFFfree(buf);
        }
      }
      break;
    default:
      heif_image_release(image);
      std::cerr << "Unsupported planar config: " << config << "\n";
      exit(1);
  }

  input_image.image = std::shared_ptr<heif_image>(image,
                                          [](heif_image* img) { heif_image_release(img); });
  return input_image;
}
