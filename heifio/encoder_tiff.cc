/*
  libheif example application.

  MIT License

  Copyright (c) 2024 Brad Hards <bradh@frogmouth.net>

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
#include <tiff.h>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "encoder_tiff.h"
#include <tiffio.h>

TiffEncoder::TiffEncoder() = default;

bool TiffEncoder::Encode(const struct heif_image_handle *handle,
                         const struct heif_image *image, const std::string &filename)
{
    TIFF *tif = TIFFOpen(filename.c_str(), "w");

    // For now we write interleaved
    int width = heif_image_get_width(image, heif_channel_interleaved);
    int height = heif_image_get_height(image, heif_channel_interleaved);
    bool hasAlpha = ((heif_image_get_chroma_format(image) == heif_chroma_interleaved_RGBA) ||
                     (heif_image_get_chroma_format(image) == heif_chroma_interleaved_RRGGBBAA_BE));
    int input_bpp = heif_image_get_bits_per_pixel_range(image, heif_channel_interleaved);

    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, height);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, input_bpp);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, hasAlpha ? 4 : 3);
    if (hasAlpha)
    {
        // TODO: is alpha premultiplied?
        uint16_t extra_samples[1] = {EXTRASAMPLE_UNASSALPHA};
        TIFFSetField(tif, TIFFTAG_EXTRASAMPLES, 1, &extra_samples);
    }
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);
    TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);

    size_t stride_rgb;
    const uint8_t *row_rgb = heif_image_get_plane_readonly2(image,
                                                            heif_channel_interleaved, &stride_rgb);

    for (int i = 0; i < height; i++)
    {
        // memcpy(scan_line, &buffer[i * width], width * sizeof(uint32));
        TIFFWriteScanline(tif, (void *)(&(row_rgb[i * stride_rgb])), i, 0);
    }
    TIFFClose(tif);
    return true;
}
