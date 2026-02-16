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

#ifndef LIBHEIF_DECODER_TIFF_H
#define LIBHEIF_DECODER_TIFF_H

#include "decoder.h"
#include "libheif/heif.h"
#include <memory>
#include <cstdint>

LIBHEIF_API
heif_error loadTIFF(const char *filename, InputImage *input_image);

class LIBHEIF_API TiledTiffReader {
public:
  ~TiledTiffReader();

  // Returns a reader if the file is a tiled TIFF. If the TIFF is not tiled,
  // returns nullptr with heif_error_Ok (caller should fall back to loadTIFF).
  static std::unique_ptr<TiledTiffReader> open(const char* filename, heif_error* out_err);

  uint32_t imageWidth() const { return m_image_width; }
  uint32_t imageHeight() const { return m_image_height; }
  uint32_t tileWidth() const { return m_tile_width; }
  uint32_t tileHeight() const { return m_tile_height; }
  uint32_t nColumns() const { return m_n_columns; }
  uint32_t nRows() const { return m_n_rows; }

  heif_error readTile(uint32_t tx, uint32_t ty, heif_image** out_image);
  void readExif(InputImage* input_image);

private:
  TiledTiffReader() = default;

  struct TiffCloser { void operator()(void* tif) const; };
  std::unique_ptr<void, TiffCloser> m_tif;

  uint32_t m_image_width = 0, m_image_height = 0;
  uint32_t m_tile_width = 0, m_tile_height = 0;
  uint32_t m_n_columns = 0, m_n_rows = 0;
  uint16_t m_samples_per_pixel = 0;
  uint16_t m_bits_per_sample = 0;
  uint16_t m_planar_config = 0;
  bool m_has_alpha = false;
};

#endif // LIBHEIF_DECODER_TIFF_H
