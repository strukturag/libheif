/*
  libheif example application "heif".

  MIT License

  Copyright (c) 2024 Joachim Bauch <bauch@struktur.de>
  Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>

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

#include <cstring>
#include <memory>
#include <utility>
#include <vector>
#include <algorithm>
#include <limits>

extern "C" {
#include <tiff.h>
#include <tiffio.h>
}

#if HAVE_GEOTIFF
#include <geotiff/geotiff.h>
#include <geotiff/geotiffio.h>
#include <geotiff/geo_normalize.h>
#include <geotiff/xtiffio.h>
#endif

#include "decoder_tiff.h"
#include "libheif/heif_uncompressed.h"

static heif_error heif_error_ok = {heif_error_Ok, heif_suberror_Unspecified, "Success"};

// Forward declarations for YCbCr helpers (defined after validateTiffFormat)
static YCbCrInfo getYCbCrInfo(TIFF* tif);
static heif_chroma ycbcrChroma(const YCbCrInfo& ycbcr);
static void deinterleaveYCbCr(const uint8_t* src, uint32_t block_w, uint32_t block_h,
                              uint32_t actual_w, uint32_t actual_h,
                              uint16_t horiz_sub, uint16_t vert_sub,
                              uint8_t* y_plane, size_t y_stride,
                              uint8_t* cb_plane, size_t cb_stride,
                              uint8_t* cr_plane, size_t cr_stride);

// Forward declarations for YCbCr helpers (defined after validateTiffFormat)
static YCbCrInfo getYCbCrInfo(TIFF* tif);
static heif_chroma ycbcrChroma(const YCbCrInfo& ycbcr);
static void deinterleaveYCbCr(const uint8_t* src, uint32_t block_w, uint32_t block_h,
                              uint32_t actual_w, uint32_t actual_h,
                              uint16_t horiz_sub, uint16_t vert_sub,
                              uint8_t* y_plane, size_t y_stride,
                              uint8_t* cb_plane, size_t cb_stride,
                              uint8_t* cr_plane, size_t cr_stride);

static bool seekTIFF(TIFF* tif, toff_t offset, int whence) {
  TIFFSeekProc seekProc = TIFFGetSeekProc(tif);
  if (!seekProc) {
    return false;
  }

  thandle_t handle = TIFFClientdata(tif);
  if (!handle) {
    return false;
  }

  return seekProc(handle, offset, whence) != static_cast<toff_t>(-1);
}

static bool readTIFF(TIFF* tif, void* dest, size_t size) {
  TIFFReadWriteProc readProc = TIFFGetReadProc(tif);
  if (!readProc) {
    return false;
  }

  thandle_t handle = TIFFClientdata(tif);
  if (!handle) {
    return false;
  }

  tmsize_t result = readProc(handle, dest, size);
  if (result < 0 || static_cast<size_t>(result) != size) {
    return false;
  }

  return true;
}

static bool readTIFFUint16(TIFF* tif, uint16_t* dest) {
  if (!readTIFF(tif, dest, 2)) {
    return false;
  }

  if (TIFFIsByteSwapped(tif)) {
    TIFFSwabShort(dest);
  }
  return true;
}

static bool readTIFFUint32(TIFF* tif, uint32_t* dest) {
  if (!readTIFF(tif, dest, 4)) {
    return false;
  }

  if (TIFFIsByteSwapped(tif)) {
    TIFFSwabLong(dest);
  }
  return true;
}

class ExifTags {
 public:
  ~ExifTags() = default;

  void Encode(std::vector<uint8_t>* dest);

  static std::unique_ptr<ExifTags> Parse(TIFF* tif);

 private:
  class Tag {
   public:
    uint16_t tag;
    uint16_t type;
    uint32_t len;

    uint32_t offset;
    std::vector<uint8_t> data;
  };

  ExifTags(uint16_t count);
  void writeUint16(std::vector<uint8_t>* dest, uint16_t value);
  void writeUint32(std::vector<uint8_t>* dest, uint32_t value);
  void writeUint32(std::vector<uint8_t>* dest, size_t pos, uint32_t value);
  void writeData(std::vector<uint8_t>* dest, const std::vector<uint8_t>& value);

  std::vector<std::unique_ptr<Tag>> tags_;
};

ExifTags::ExifTags(uint16_t count) {
  tags_.reserve(count);
}

// static
std::unique_ptr<ExifTags> ExifTags::Parse(TIFF* tif) {
  toff_t exif_offset;
  if (!TIFFGetField(tif, TIFFTAG_EXIFIFD, &exif_offset)) {
    // Image doesn't contain EXIF data.
    return nullptr;
  }

  if (!seekTIFF(tif, exif_offset, SEEK_SET)) {
    return nullptr;
  }

  uint16_t count;
  if (!readTIFFUint16(tif, &count)) {
    return nullptr;
  }

  if (count == 0) {
    return nullptr;
  }

  std::unique_ptr<ExifTags> tags(new ExifTags(count));
  for (uint16_t i = 0; i < count; i++) {
    std::unique_ptr<Tag> tag(new Tag());
    if (!readTIFFUint16(tif, &tag->tag)) {
      return nullptr;
    }

    if (!readTIFFUint16(tif, &tag->type) || tag->type > TIFF_IFD8) {
      return nullptr;
    }

    if (TIFFDataWidth(static_cast<TIFFDataType>(tag->type)) == 0) {
      return nullptr;
    }

    if (!readTIFFUint32(tif, &tag->len)) {
      return nullptr;
    }

    if (!readTIFFUint32(tif, &tag->offset)) {
      return nullptr;
    }

    tags->tags_.push_back(std::move(tag));
  }

  for (const auto& tag : tags->tags_) {
    size_t size = tag->len * TIFFDataWidth(static_cast<TIFFDataType>(tag->type));
    if (size <= 4) {
      continue;
    }

    if (!seekTIFF(tif, tag->offset, SEEK_SET)) {
      return nullptr;
    }

    tag->data.resize(size);
    if (!readTIFF(tif, tag->data.data(), size)) {
      return nullptr;
    }
  }

  return tags;
}

void ExifTags::writeUint16(std::vector<uint8_t>* dest, uint16_t value) {
  dest->resize(dest->size() + sizeof(value));
  void* d = dest->data() + dest->size() - sizeof(value);
  memcpy(d, &value, sizeof(value));
}

void ExifTags::writeUint32(std::vector<uint8_t>* dest, uint32_t value) {
  dest->resize(dest->size() + sizeof(value));
  writeUint32(dest, dest->size() - sizeof(value), value);
}

void ExifTags::writeUint32(std::vector<uint8_t>* dest, size_t pos, uint32_t value) {
  void* d = dest->data() + pos;
  memcpy(d, &value, sizeof(value));
}

void ExifTags::writeData(std::vector<uint8_t>* dest, const std::vector<uint8_t>& value) {
  dest->resize(dest->size() + value.size());
  void* d = dest->data() + dest->size() - value.size();
  memcpy(d, value.data(), value.size());
}

void ExifTags::Encode(std::vector<uint8_t>* dest) {
  if (tags_.empty()) {
    return;
  }

#if HOST_BIGENDIAN
  dest->push_back('M');
  dest->push_back('M');
#else
  dest->push_back('I');
  dest->push_back('I');
#endif
  writeUint16(dest, 42);
  // Offset of IFD0.
  writeUint32(dest, 8);

  writeUint16(dest, static_cast<uint16_t>(tags_.size()));
  for (const auto& tag : tags_) {
    writeUint16(dest, tag->tag);
    writeUint16(dest, tag->type);
    writeUint32(dest, tag->len);
    writeUint32(dest, tag->offset);
  }
  // No IFD1 dictionary.
  writeUint32(dest, 0);

  // Update offsets of tags that have their data stored separately.
  for (size_t i = 0; i < tags_.size(); i++) {
    const auto& tag = tags_[i];
    size_t size = tag->data.size();
    if (size <= 4) {
      continue;
    }

    // StartOfTags + (TagIndex * sizeof(Tag)) + OffsetOfTagData
    size_t pos = 10 + (i * 12) + 8;
    size_t offset = dest->size();
    writeUint32(dest, pos, static_cast<uint32_t>(offset));
    writeData(dest, tag->data);
  }
}

static heif_chroma get_heif_chroma(uint16_t outSpp, int output_bit_depth)
{
  if (outSpp == 1) {
    return heif_chroma_monochrome;
  }
  if (output_bit_depth > 8) {
#if IS_BIG_ENDIAN
    return (outSpp == 4) ? heif_chroma_interleaved_RRGGBBAA_BE : heif_chroma_interleaved_RRGGBB_BE;
#else
    return (outSpp == 4) ? heif_chroma_interleaved_RRGGBBAA_LE : heif_chroma_interleaved_RRGGBB_LE;
#endif
  }
  return (outSpp == 4) ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB;
}


heif_error getImageWidthAndHeight(TIFF *tif, uint32_t &width, uint32_t &height)
{
  if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width) ||
      !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height))
  {
    struct heif_error err = {
      .code = heif_error_Invalid_input,
      .subcode = heif_suberror_Unspecified,
      .message = "Can not read width and/or height from TIFF image."};
    return err;
  }

  if (width == 0 || height == 0) {
    return {heif_error_Invalid_input, heif_suberror_Unspecified, "Zero TIFF image size is invalid."};
  }

  return heif_error_ok;
}

heif_error readMono(TIFF *tif, uint16_t bps, int output_bit_depth, heif_image **image)
{
  uint32_t width, height;
  heif_error err = getImageWidthAndHeight(tif, width, height);
  if (err.code != heif_error_Ok) {
    return err;
  }
  err = heif_image_create((int) width, (int) height, heif_colorspace_monochrome, heif_chroma_monochrome, image);
  if (err.code != heif_error_Ok) {
    return err;
  }

  if (bps <= 8) {
    heif_image_add_plane(*image, heif_channel_Y, (int)width, (int)height, 8);

    size_t y_stride;
    uint8_t *py = heif_image_get_plane2(*image, heif_channel_Y, &y_stride);
    for (uint32_t row = 0; row < height; row++) {
      if (TIFFReadScanline(tif, py, row, 0) < 0) {
        heif_image_release(*image);
        *image = nullptr;
        return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF scanline"};
      }
      py += y_stride;
    }
  }
  else {
    heif_image_add_plane(*image, heif_channel_Y, (int)width, (int)height, output_bit_depth);

    size_t y_stride;
    uint8_t *py = heif_image_get_plane2(*image, heif_channel_Y, &y_stride);
    int bdShift = bps - output_bit_depth;
    tdata_t buf = _TIFFmalloc(TIFFScanlineSize(tif));

    if (output_bit_depth <= 8) {
      for (uint32_t row = 0; row < height; row++) {
        if (TIFFReadScanline(tif, buf, row, 0) < 0) {
          _TIFFfree(buf);
          heif_image_release(*image);
          *image = nullptr;
          return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF scanline"};
        }
        uint16_t* src = static_cast<uint16_t*>(buf);
        uint8_t* dst = py + row * y_stride;
        for (uint32_t x = 0; x < width; x++) {
          dst[x] = static_cast<uint8_t>(src[x] >> (bps - 8));
        }
      }
    }
    else {
      for (uint32_t row = 0; row < height; row++) {
        if (TIFFReadScanline(tif, buf, row, 0) < 0) {
          _TIFFfree(buf);
          heif_image_release(*image);
          *image = nullptr;
          return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF scanline"};
        }
        uint16_t* src = static_cast<uint16_t*>(buf);
        uint16_t* dst = reinterpret_cast<uint16_t*>(py + row * y_stride);
        for (uint32_t x = 0; x < width; x++) {
          dst[x] = static_cast<uint16_t>(src[x] >> bdShift);
        }
      }
    }
    _TIFFfree(buf);
  }
  return heif_error_ok;
}

heif_error readPixelInterleaveRGB(TIFF *tif, uint16_t samplesPerPixel, bool hasAlpha, uint16_t bps, int output_bit_depth, heif_image **image)
{
  uint32_t width, height;
  heif_error err = getImageWidthAndHeight(tif, width, height);
  if (err.code != heif_error_Ok) {
    return err;
  }

  // --- YCbCr strip path: TIFFReadScanline doesn't work with packed YCbCr ---
  YCbCrInfo ycbcr = getYCbCrInfo(tif);
  if (ycbcr.is_ycbcr) {
    if (bps != 8) {
      return {heif_error_Unsupported_feature, heif_suberror_Unspecified,
              "Only 8-bit YCbCr TIFF is supported."};
    }

    heif_chroma chroma = ycbcrChroma(ycbcr);
    err = heif_image_create((int)width, (int)height, heif_colorspace_YCbCr, chroma, image);
    if (err.code != heif_error_Ok) return err;

    heif_image_add_plane(*image, heif_channel_Y, (int)width, (int)height, 8);
    uint32_t chroma_w = (width + ycbcr.horiz_sub - 1) / ycbcr.horiz_sub;
    uint32_t chroma_h = (height + ycbcr.vert_sub - 1) / ycbcr.vert_sub;
    heif_image_add_plane(*image, heif_channel_Cb, (int)chroma_w, (int)chroma_h, 8);
    heif_image_add_plane(*image, heif_channel_Cr, (int)chroma_w, (int)chroma_h, 8);

    size_t y_stride, cb_stride, cr_stride;
    uint8_t* y_plane = heif_image_get_plane2(*image, heif_channel_Y, &y_stride);
    uint8_t* cb_plane = heif_image_get_plane2(*image, heif_channel_Cb, &cb_stride);
    uint8_t* cr_plane = heif_image_get_plane2(*image, heif_channel_Cr, &cr_stride);

    uint32_t rows_per_strip = 0;
    TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &rows_per_strip);
    if (rows_per_strip == 0) rows_per_strip = height;

    tmsize_t strip_size = TIFFStripSize(tif);
    std::vector<uint8_t> strip_buf(strip_size);

    uint32_t n_strips = TIFFNumberOfStrips(tif);
    for (uint32_t s = 0; s < n_strips; s++) {
      tmsize_t read = TIFFReadEncodedStrip(tif, s, strip_buf.data(), strip_size);
      if (read < 0) {
        heif_image_release(*image);
        *image = nullptr;
        return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF strip"};
      }

      uint32_t strip_y = s * rows_per_strip;
      uint32_t actual_h = std::min(rows_per_strip, height - strip_y);
      // Packed YCbCr block dimensions must be multiples of subsampling factors
      uint32_t block_w = ((width + ycbcr.horiz_sub - 1) / ycbcr.horiz_sub) * ycbcr.horiz_sub;
      uint32_t block_h = ((actual_h + ycbcr.vert_sub - 1) / ycbcr.vert_sub) * ycbcr.vert_sub;

      deinterleaveYCbCr(strip_buf.data(), block_w, block_h, width, actual_h,
                        ycbcr.horiz_sub, ycbcr.vert_sub,
                        y_plane + strip_y * y_stride, y_stride,
                        cb_plane + (strip_y / ycbcr.vert_sub) * cb_stride, cb_stride,
                        cr_plane + (strip_y / ycbcr.vert_sub) * cr_stride, cr_stride);
    }

    return heif_error_ok;
  }

  uint16_t outSpp = (samplesPerPixel == 4 && !hasAlpha) ? 3 : samplesPerPixel;

  if (bps <= 8) {
    heif_chroma chroma = get_heif_chroma(outSpp, 8);
    err = heif_image_create((int)width, (int)height, heif_colorspace_RGB, chroma, image);
    if (err.code != heif_error_Ok) {
      return err;
    }
    heif_channel channel = heif_channel_interleaved;
    heif_image_add_plane(*image, channel, (int)width, (int)height, outSpp * 8);

    size_t y_stride;
    uint8_t *py = heif_image_get_plane2(*image, channel, &y_stride);
    tdata_t buf = _TIFFmalloc(TIFFScanlineSize(tif));
    for (uint32_t row = 0; row < height; row++) {
      if (TIFFReadScanline(tif, buf, row, 0) < 0) {
        _TIFFfree(buf);
        heif_image_release(*image);
        *image = nullptr;
        return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF scanline"};
      }
      uint8_t* src = static_cast<uint8_t*>(buf);
      if (outSpp == samplesPerPixel) {
        memcpy(py, src, width * outSpp);
      }
      else {
        for (uint32_t x = 0; x < width; x++) {
          memcpy(py + x * outSpp, src + x * samplesPerPixel, outSpp);
        }
      }
      py += y_stride;
    }
    _TIFFfree(buf);
  }
  else {
    heif_chroma chroma = get_heif_chroma(outSpp, output_bit_depth);
    err = heif_image_create((int)width, (int)height, heif_colorspace_RGB, chroma, image);
    if (err.code != heif_error_Ok) {
      return err;
    }
    heif_channel channel = heif_channel_interleaved;
    int planeBitDepth = (output_bit_depth <= 8) ? outSpp * 8 : output_bit_depth;
    heif_image_add_plane(*image, channel, (int)width, (int)height, planeBitDepth);

    size_t y_stride;
    uint8_t *py = heif_image_get_plane2(*image, channel, &y_stride);
    int bdShift = bps - output_bit_depth;
    tdata_t buf = _TIFFmalloc(TIFFScanlineSize(tif));

    if (output_bit_depth <= 8) {
      for (uint32_t row = 0; row < height; row++) {
        if (TIFFReadScanline(tif, buf, row, 0) < 0) {
          _TIFFfree(buf);
          heif_image_release(*image);
          *image = nullptr;
          return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF scanline"};
        }
        uint16_t* src = static_cast<uint16_t*>(buf);
        uint8_t* dst = py + row * y_stride;
        if (outSpp == samplesPerPixel) {
          for (uint32_t x = 0; x < width * outSpp; x++) {
            dst[x] = static_cast<uint8_t>(src[x] >> (bps - 8));
          }
        }
        else {
          for (uint32_t x = 0; x < width; x++) {
            for (uint16_t c = 0; c < outSpp; c++) {
              dst[x * outSpp + c] = static_cast<uint8_t>(src[x * samplesPerPixel + c] >> (bps - 8));
            }
          }
        }
      }
    }
    else {
      for (uint32_t row = 0; row < height; row++) {
        if (TIFFReadScanline(tif, buf, row, 0) < 0) {
          _TIFFfree(buf);
          heif_image_release(*image);
          *image = nullptr;
          return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF scanline"};
        }
        uint16_t* src = static_cast<uint16_t*>(buf);
        uint16_t* dst = reinterpret_cast<uint16_t*>(py + row * y_stride);
        if (outSpp == samplesPerPixel) {
          for (uint32_t x = 0; x < width * outSpp; x++) {
            dst[x] = static_cast<uint16_t>(src[x] >> bdShift);
          }
        }
        else {
          for (uint32_t x = 0; x < width; x++) {
            for (uint16_t c = 0; c < outSpp; c++) {
              dst[x * outSpp + c] = static_cast<uint16_t>(src[x * samplesPerPixel + c] >> bdShift);
            }
          }
        }
      }
    }
    _TIFFfree(buf);
  }
  return heif_error_ok;
}

heif_error readPixelInterleave(TIFF *tif, uint16_t samplesPerPixel, bool hasAlpha, uint16_t bps, int output_bit_depth, heif_image **image)
{
  if (samplesPerPixel == 1) {
    return readMono(tif, bps, output_bit_depth, image);
  } else {
    return readPixelInterleaveRGB(tif, samplesPerPixel, hasAlpha, bps, output_bit_depth, image);
  }
}

heif_error readBandInterleaveRGB(TIFF *tif, uint16_t samplesPerPixel, bool hasAlpha, uint16_t bps, int output_bit_depth, heif_image **image)
{
  uint32_t width, height;
  heif_error err = getImageWidthAndHeight(tif, width, height);
  if (err.code != heif_error_Ok) {
    return err;
  }

  uint16_t outSpp = (samplesPerPixel == 4 && !hasAlpha) ? 3 : samplesPerPixel;

  if (bps <= 8) {
    heif_chroma chroma = get_heif_chroma(outSpp, 8);
    err = heif_image_create((int)width, (int)height, heif_colorspace_RGB, chroma, image);
    if (err.code != heif_error_Ok) {
      return err;
    }
    heif_channel channel = heif_channel_interleaved;
    heif_image_add_plane(*image, channel, (int)width, (int)height, outSpp * 8);

    size_t y_stride;
    uint8_t *py = heif_image_get_plane2(*image, channel, &y_stride);

    uint8_t *buf = static_cast<uint8_t *>(_TIFFmalloc(TIFFScanlineSize(tif)));
    for (uint16_t i = 0; i < outSpp; i++) {
      uint8_t *dest = py + i;
      for (uint32_t row = 0; row < height; row++) {
        if (TIFFReadScanline(tif, buf, row, i) < 0) {
          _TIFFfree(buf);
          heif_image_release(*image);
          *image = nullptr;
          return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF scanline"};
        }
        for (uint32_t x = 0; x < width; x++, dest += outSpp) {
          *dest = buf[x];
        }
        dest += (y_stride - width * outSpp);
      }
    }
    _TIFFfree(buf);
  }
  else {
    heif_chroma chroma = get_heif_chroma(outSpp, output_bit_depth);
    err = heif_image_create((int)width, (int)height, heif_colorspace_RGB, chroma, image);
    if (err.code != heif_error_Ok) {
      return err;
    }
    heif_channel channel = heif_channel_interleaved;
    int planeBitDepth = (output_bit_depth <= 8) ? outSpp * 8 : output_bit_depth;
    heif_image_add_plane(*image, channel, (int)width, (int)height, planeBitDepth);

    size_t y_stride;
    uint8_t *py = heif_image_get_plane2(*image, channel, &y_stride);
    int bdShift = bps - output_bit_depth;
    uint8_t *buf = static_cast<uint8_t *>(_TIFFmalloc(TIFFScanlineSize(tif)));

    if (output_bit_depth <= 8) {
      for (uint16_t i = 0; i < outSpp; i++) {
        for (uint32_t row = 0; row < height; row++) {
          if (TIFFReadScanline(tif, buf, row, i) < 0) {
            _TIFFfree(buf);
            heif_image_release(*image);
            *image = nullptr;
            return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF scanline"};
          }
          uint16_t* src = reinterpret_cast<uint16_t*>(buf);
          uint8_t* dst = py + row * y_stride + i;
          for (uint32_t x = 0; x < width; x++) {
            dst[x * outSpp] = static_cast<uint8_t>(src[x] >> (bps - 8));
          }
        }
      }
    }
    else {
      for (uint16_t i = 0; i < outSpp; i++) {
        for (uint32_t row = 0; row < height; row++) {
          if (TIFFReadScanline(tif, buf, row, i) < 0) {
            _TIFFfree(buf);
            heif_image_release(*image);
            *image = nullptr;
            return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF scanline"};
          }
          uint16_t* src = reinterpret_cast<uint16_t*>(buf);
          uint16_t* dst = reinterpret_cast<uint16_t*>(py + row * y_stride);
          for (uint32_t x = 0; x < width; x++) {
            dst[x * outSpp + i] = static_cast<uint16_t>(src[x] >> bdShift);
          }
        }
      }
    }
    _TIFFfree(buf);
  }
  return heif_error_ok;
}


heif_error readBandInterleave(TIFF *tif, uint16_t samplesPerPixel, bool hasAlpha, uint16_t bps, int output_bit_depth, heif_image **image)
{
  if (samplesPerPixel == 1) {
    return readMono(tif, bps, output_bit_depth, image);
  } else if (samplesPerPixel == 3) {
    return readBandInterleaveRGB(tif, samplesPerPixel, hasAlpha, bps, output_bit_depth, image);
  } else if (samplesPerPixel == 4) {
    return readBandInterleaveRGB(tif, samplesPerPixel, hasAlpha, bps, output_bit_depth, image);
  } else {
    struct heif_error err = {
      .code = heif_error_Unsupported_feature,
      .subcode = heif_suberror_Unspecified,
      .message = "Only 1, 3 and 4 bands are supported"};
    return err;
  }
}


#if WITH_UNCOMPRESSED_CODEC
static heif_error readMonoFloat(TIFF* tif, heif_image** image)
{
  uint32_t width, height;
  heif_error err = getImageWidthAndHeight(tif, width, height);
  if (err.code != heif_error_Ok) {
    return err;
  }

  err = heif_image_create((int)width, (int)height, heif_colorspace_custom, heif_chroma_planar, image);
  if (err.code != heif_error_Ok) {
    return err;
  }

  uint32_t component_idx;
  err = heif_image_add_component(*image, (int)width, (int)height,
                                 heif_unci_component_type_monochrome,
                                 heif_component_datatype_floating_point, 32, &component_idx);
  if (err.code != heif_error_Ok) {
    heif_image_release(*image);
    *image = nullptr;
    return err;
  }

  size_t stride;
  float* plane = heif_image_get_component_float32(*image, component_idx, &stride);
  if (!plane) {
    heif_image_release(*image);
    *image = nullptr;
    return {heif_error_Memory_allocation_error, heif_suberror_Unspecified, "Failed to get float plane"};
  }

  tdata_t buf = _TIFFmalloc(TIFFScanlineSize(tif));
  for (uint32_t row = 0; row < height; row++) {
    if (TIFFReadScanline(tif, buf, row, 0) < 0) {
      _TIFFfree(buf);
      heif_image_release(*image);
      *image = nullptr;
      return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF scanline"};
    }
    memcpy(plane + row * stride, buf, width * sizeof(float));
  }
  _TIFFfree(buf);

  return heif_error_ok;
}


static heif_error readMonoSignedInt(TIFF* tif, uint16_t bps, heif_image** image)
{
  uint32_t width, height;
  heif_error err = getImageWidthAndHeight(tif, width, height);
  if (err.code != heif_error_Ok) {
    return err;
  }

  err = heif_image_create((int)width, (int)height, heif_colorspace_custom, heif_chroma_planar, image);
  if (err.code != heif_error_Ok) {
    return err;
  }

  uint32_t component_idx;
  err = heif_image_add_component(*image, (int)width, (int)height,
                                 heif_unci_component_type_monochrome,
                                 heif_component_datatype_signed_integer, bps, &component_idx);
  if (err.code != heif_error_Ok) {
    heif_image_release(*image);
    *image = nullptr;
    return err;
  }

  size_t row_elements;  // int8 or int16 elements per row, depending on bps
  uint8_t* plane;
  if (bps == 8) {
    plane = reinterpret_cast<uint8_t*>(heif_image_get_component_int8(*image, component_idx, &row_elements));
  }
  else {
    plane = reinterpret_cast<uint8_t*>(heif_image_get_component_int16(*image, component_idx, &row_elements));
  }
  if (!plane) {
    heif_image_release(*image);
    *image = nullptr;
    return {heif_error_Memory_allocation_error, heif_suberror_Unspecified, "Failed to get signed int plane"};
  }

  int bytesPerSample = bps > 8 ? 2 : 1;
  size_t row_bytes = row_elements * bytesPerSample;
  tdata_t buf = _TIFFmalloc(TIFFScanlineSize(tif));
  for (uint32_t row = 0; row < height; row++) {
    if (TIFFReadScanline(tif, buf, row, 0) < 0) {
      _TIFFfree(buf);
      heif_image_release(*image);
      *image = nullptr;
      return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF scanline"};
    }
    memcpy(plane + row * row_bytes, buf, width * bytesPerSample);
  }
  _TIFFfree(buf);

  return heif_error_ok;
}
#endif


static void suppress_warnings(const char* module, const char* fmt, va_list ap) {
  // Do nothing
}


static heif_error validateTiffFormat(TIFF* tif, uint16_t& samplesPerPixel, uint16_t& bps,
                                     uint16_t& config, bool& hasAlpha, uint16_t& sampleFormat)
{
  uint16_t shortv;
  if (TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &shortv) && shortv == PHOTOMETRIC_PALETTE) {
    return {heif_error_Unsupported_feature, heif_suberror_Unspecified,
            "Palette TIFF images are not supported yet"};
  }

  TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &config);
  TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
  if (samplesPerPixel != 1 && samplesPerPixel != 3 && samplesPerPixel != 4) {
    return {heif_error_Invalid_input, heif_suberror_Unspecified,
            "Only 1, 3 and 4 samples per pixel are supported."};
  }

  // Determine whether the 4th sample is true alpha or an unrelated extra sample
  hasAlpha = false;
  if (samplesPerPixel == 4) {
    uint16_t extraCount = 0;
    uint16_t* extraTypes = nullptr;
    if (TIFFGetField(tif, TIFFTAG_EXTRASAMPLES, &extraCount, &extraTypes) && extraCount > 0) {
      hasAlpha = (extraTypes[0] == EXTRASAMPLE_ASSOCALPHA || extraTypes[0] == EXTRASAMPLE_UNASSALPHA);
    }
    else {
      // No EXTRASAMPLES tag with 4 spp — assume the extra sample is not alpha
      hasAlpha = false;
    }
  }

  TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bps);

  if (!TIFFGetField(tif, TIFFTAG_SAMPLEFORMAT, &sampleFormat)) {
    sampleFormat = SAMPLEFORMAT_UINT;
  }

  if (sampleFormat == SAMPLEFORMAT_IEEEFP) {
    if (bps != 32) {
      return {heif_error_Unsupported_feature, heif_suberror_Unspecified,
              "Only 32-bit floating point TIFF is supported."};
    }
    if (samplesPerPixel != 1) {
      return {heif_error_Unsupported_feature, heif_suberror_Unspecified,
              "Only monochrome floating point TIFF is supported."};
    }
  }
  else if (sampleFormat == SAMPLEFORMAT_INT) {
    if (bps < 8 || bps > 16) {
      return {heif_error_Invalid_input, heif_suberror_Unspecified,
              "Only 8 to 16 bits per sample are supported for signed integer TIFF."};
    }
    if (samplesPerPixel != 1) {
      return {heif_error_Unsupported_feature, heif_suberror_Unspecified,
              "Only monochrome signed integer TIFF is supported."};
    }
  }
  else if (sampleFormat == SAMPLEFORMAT_UINT) {
    if (bps < 8 || bps > 16) {
      return {heif_error_Invalid_input, heif_suberror_Unspecified,
              "Only 8 to 16 bits per sample are supported."};
    }
  }
  else {
    return {heif_error_Unsupported_feature, heif_suberror_Unspecified,
            "Unsupported TIFF sample format."};
  }

  return heif_error_ok;
}


static YCbCrInfo getYCbCrInfo(TIFF* tif)
{
  YCbCrInfo info;
  uint16_t photometric;
  if (TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric) && photometric == PHOTOMETRIC_YCBCR) {
    info.is_ycbcr = true;
    TIFFGetFieldDefaulted(tif, TIFFTAG_YCBCRSUBSAMPLING, &info.horiz_sub, &info.vert_sub);
  }
  return info;
}


static heif_chroma ycbcrChroma(const YCbCrInfo& ycbcr)
{
  if (ycbcr.horiz_sub == 2 && ycbcr.vert_sub == 2) return heif_chroma_420;
  if (ycbcr.horiz_sub == 2 && ycbcr.vert_sub == 1) return heif_chroma_422;
  return heif_chroma_444;
}


// Deinterleave libtiff's packed YCbCr format into separate Y/Cb/Cr planes.
// Packed format: MCUs of (horiz_sub * vert_sub) Y samples + 1 Cb + 1 Cr.
// block_w/block_h must be multiples of horiz_sub/vert_sub respectively.
// actual_w/actual_h are the clipped dimensions for edge tiles/strips.
static void deinterleaveYCbCr(
    const uint8_t* src,
    uint32_t block_w, uint32_t block_h,
    uint32_t actual_w, uint32_t actual_h,
    uint16_t horiz_sub, uint16_t vert_sub,
    uint8_t* y_plane, size_t y_stride,
    uint8_t* cb_plane, size_t cb_stride,
    uint8_t* cr_plane, size_t cr_stride)
{
  uint32_t mcus_h = block_w / horiz_sub;
  uint32_t mcu_rows = block_h / vert_sub;
  uint32_t mcu_size = horiz_sub * vert_sub + 2;

  uint32_t chroma_w = (actual_w + horiz_sub - 1) / horiz_sub;
  uint32_t chroma_h = (actual_h + vert_sub - 1) / vert_sub;

  for (uint32_t mcu_y = 0; mcu_y < mcu_rows; mcu_y++) {
    for (uint32_t mcu_x = 0; mcu_x < mcus_h; mcu_x++) {
      const uint8_t* mcu = src + (mcu_y * mcus_h + mcu_x) * mcu_size;

      // Scatter Y samples
      for (uint32_t vy = 0; vy < vert_sub; vy++) {
        uint32_t py = mcu_y * vert_sub + vy;
        if (py >= actual_h) break;
        for (uint32_t hx = 0; hx < horiz_sub; hx++) {
          uint32_t px = mcu_x * horiz_sub + hx;
          if (px >= actual_w) break;
          y_plane[py * y_stride + px] = mcu[vy * horiz_sub + hx];
        }
      }

      // Scatter Cb/Cr
      if (mcu_x < chroma_w && mcu_y < chroma_h) {
        cb_plane[mcu_y * cb_stride + mcu_x] = mcu[horiz_sub * vert_sub];
        cr_plane[mcu_y * cr_stride + mcu_x] = mcu[horiz_sub * vert_sub + 1];
      }
    }
  }
}


// Create a YCbCr heif_image from a single packed YCbCr tile/block.
static heif_error readYCbCrBlock(
    const uint8_t* tile_buf,
    uint32_t block_w, uint32_t block_h,
    uint32_t actual_w, uint32_t actual_h,
    const YCbCrInfo& ycbcr,
    heif_image** out_image)
{
  heif_chroma chroma = ycbcrChroma(ycbcr);

  heif_error err = heif_image_create((int)actual_w, (int)actual_h, heif_colorspace_YCbCr, chroma, out_image);
  if (err.code != heif_error_Ok) return err;

  heif_image_add_plane(*out_image, heif_channel_Y, (int)actual_w, (int)actual_h, 8);
  uint32_t chroma_w = (actual_w + ycbcr.horiz_sub - 1) / ycbcr.horiz_sub;
  uint32_t chroma_h = (actual_h + ycbcr.vert_sub - 1) / ycbcr.vert_sub;
  heif_image_add_plane(*out_image, heif_channel_Cb, (int)chroma_w, (int)chroma_h, 8);
  heif_image_add_plane(*out_image, heif_channel_Cr, (int)chroma_w, (int)chroma_h, 8);

  size_t y_stride, cb_stride, cr_stride;
  uint8_t* y_plane = heif_image_get_plane2(*out_image, heif_channel_Y, &y_stride);
  uint8_t* cb_plane = heif_image_get_plane2(*out_image, heif_channel_Cb, &cb_stride);
  uint8_t* cr_plane = heif_image_get_plane2(*out_image, heif_channel_Cr, &cr_stride);

  deinterleaveYCbCr(tile_buf, block_w, block_h, actual_w, actual_h,
                    ycbcr.horiz_sub, ycbcr.vert_sub,
                    y_plane, y_stride, cb_plane, cb_stride, cr_plane, cr_stride);

  return heif_error_ok;
}


static heif_error readTiledContiguous(TIFF* tif, uint32_t width, uint32_t height,
                                  uint32_t tile_width, uint32_t tile_height,
                                  uint16_t samplesPerPixel, bool hasAlpha,
                                  uint16_t bps, int output_bit_depth,
                                  uint16_t sampleFormat, heif_image** out_image)
{
  // --- YCbCr path: deinterleave packed MCU data into planar Y/Cb/Cr ---
  YCbCrInfo ycbcr = getYCbCrInfo(tif);
  if (ycbcr.is_ycbcr) {
    if (bps != 8) {
      return {heif_error_Unsupported_feature, heif_suberror_Unspecified,
              "Only 8-bit YCbCr TIFF is supported."};
    }

    if (width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max()) {
      return {heif_error_Invalid_input, heif_suberror_Unspecified, "TIFF image size exceeds maximum supported by libheif."};
    }


    heif_chroma chroma = ycbcrChroma(ycbcr);
    heif_error err = heif_image_create((int)width, (int)height, heif_colorspace_YCbCr, chroma, out_image);
    if (err.code != heif_error_Ok) return err;

    heif_image_add_plane(*out_image, heif_channel_Y, (int)width, (int)height, 8);
    uint32_t chroma_w = (width + ycbcr.horiz_sub - 1) / ycbcr.horiz_sub;
    uint32_t chroma_h = (height + ycbcr.vert_sub - 1) / ycbcr.vert_sub;
    heif_image_add_plane(*out_image, heif_channel_Cb, (int)chroma_w, (int)chroma_h, 8);
    heif_image_add_plane(*out_image, heif_channel_Cr, (int)chroma_w, (int)chroma_h, 8);

    size_t y_stride, cb_stride, cr_stride;
    uint8_t* y_plane = heif_image_get_plane2(*out_image, heif_channel_Y, &y_stride);
    uint8_t* cb_plane = heif_image_get_plane2(*out_image, heif_channel_Cb, &cb_stride);
    uint8_t* cr_plane = heif_image_get_plane2(*out_image, heif_channel_Cr, &cr_stride);

    tmsize_t tile_buf_size = TIFFTileSize(tif);
    std::vector<uint8_t> tile_buf(tile_buf_size);

    uint32_t n_cols = (width - 1) / tile_width + 1;
    uint32_t n_rows = (height - 1) / tile_height + 1;

    for (uint32_t ty = 0; ty < n_rows; ty++) {
      for (uint32_t tx = 0; tx < n_cols; tx++) {
        tmsize_t read = TIFFReadEncodedTile(tif, TIFFComputeTile(tif, tx * tile_width, ty * tile_height, 0, 0),
                                            tile_buf.data(), tile_buf_size);
        if (read < 0) {
          heif_image_release(*out_image);
          *out_image = nullptr;
          return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF tile"};
        }

        uint32_t actual_w = std::min(tile_width, width - tx * tile_width);
        uint32_t actual_h = std::min(tile_height, height - ty * tile_height);

        uint32_t y_offset = ty * tile_height;
        uint32_t x_offset = tx * tile_width;
        uint32_t chroma_x_offset = x_offset / ycbcr.horiz_sub;
        uint32_t chroma_y_offset = y_offset / ycbcr.vert_sub;

        deinterleaveYCbCr(tile_buf.data(), tile_width, tile_height, actual_w, actual_h,
                          ycbcr.horiz_sub, ycbcr.vert_sub,
                          y_plane + y_offset * y_stride + x_offset, y_stride,
                          cb_plane + chroma_y_offset * cb_stride + chroma_x_offset, cb_stride,
                          cr_plane + chroma_y_offset * cr_stride + chroma_x_offset, cr_stride);
      }
    }

    return heif_error_ok;
  }

  bool isFloat = (sampleFormat == SAMPLEFORMAT_IEEEFP);

  if (isFloat) {
#if WITH_UNCOMPRESSED_CODEC
    heif_error err = heif_image_create((int)width, (int)height, heif_colorspace_custom, heif_chroma_planar, out_image);
    if (err.code != heif_error_Ok) return err;

    uint32_t component_idx;
    err = heif_image_add_component(*out_image, (int)width, (int)height,
                                   heif_unci_component_type_monochrome,
                                   heif_component_datatype_floating_point, 32, &component_idx);
    if (err.code != heif_error_Ok) {
      heif_image_release(*out_image);
      *out_image = nullptr;
      return err;
    }

    size_t out_stride;
    float* out_plane = heif_image_get_component_float32(*out_image, component_idx, &out_stride);
    if (!out_plane) {
      heif_image_release(*out_image);
      *out_image = nullptr;
      return {heif_error_Memory_allocation_error, heif_suberror_Unspecified, "Failed to get float plane"};
    }

    tmsize_t tile_buf_size = TIFFTileSize(tif);
    std::vector<uint8_t> tile_buf(tile_buf_size);

    uint32_t n_cols = (width - 1) / tile_width + 1;
    uint32_t n_rows = (height - 1) / tile_height + 1;

    for (uint32_t ty = 0; ty < n_rows; ty++) {
      for (uint32_t tx = 0; tx < n_cols; tx++) {
        tmsize_t read = TIFFReadEncodedTile(tif, TIFFComputeTile(tif, tx * tile_width, ty * tile_height, 0, 0),
                                            tile_buf.data(), tile_buf_size);
        if (read < 0) {
          heif_image_release(*out_image);
          *out_image = nullptr;
          return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF tile"};
        }

        uint32_t actual_w = std::min(tile_width, width - tx * tile_width);
        uint32_t actual_h = std::min(tile_height, height - ty * tile_height);

        for (uint32_t row = 0; row < actual_h; row++) {
          float* dst = out_plane + (size_t)(ty * tile_height + row) * out_stride
                       + (size_t)tx * tile_width;
          float* src = reinterpret_cast<float*>(tile_buf.data() + (size_t)row * tile_width * sizeof(float));
          memcpy(dst, src, actual_w * sizeof(float));
        }
      }
    }

    return heif_error_ok;
#else
    return {heif_error_Unsupported_feature, heif_suberror_Unspecified,
            "Floating point TIFF requires uncompressed codec support (WITH_UNCOMPRESSED_CODEC)."};
#endif
  }

  bool isSignedInt = (sampleFormat == SAMPLEFORMAT_INT);

  if (isSignedInt) {
#if WITH_UNCOMPRESSED_CODEC
    heif_error err = heif_image_create((int)width, (int)height, heif_colorspace_custom, heif_chroma_planar, out_image);
    if (err.code != heif_error_Ok) return err;

    uint32_t component_idx;
    err = heif_image_add_component(*out_image, (int)width, (int)height,
                                   heif_unci_component_type_monochrome,
                                   heif_component_datatype_signed_integer, bps, &component_idx);
    if (err.code != heif_error_Ok) {
      heif_image_release(*out_image);
      *out_image = nullptr;
      return err;
    }

    size_t row_elements;  // int8 or int16 elements per row, depending on bps
    uint8_t* out_plane;
    if (bps == 8) {
      out_plane = reinterpret_cast<uint8_t*>(heif_image_get_component_int8(*out_image, component_idx, &row_elements));
    }
    else {
      out_plane = reinterpret_cast<uint8_t*>(heif_image_get_component_int16(*out_image, component_idx, &row_elements));
    }
    if (!out_plane) {
      heif_image_release(*out_image);
      *out_image = nullptr;
      return {heif_error_Memory_allocation_error, heif_suberror_Unspecified, "Failed to get signed int plane"};
    }

    int bytesPerSample = bps > 8 ? 2 : 1;
    size_t row_bytes = row_elements * bytesPerSample;
    tmsize_t tile_buf_size = TIFFTileSize(tif);
    std::vector<uint8_t> tile_buf(tile_buf_size);

    uint32_t n_cols = (width - 1) / tile_width + 1;
    uint32_t n_rows = (height - 1) / tile_height + 1;

    for (uint32_t ty = 0; ty < n_rows; ty++) {
      for (uint32_t tx = 0; tx < n_cols; tx++) {
        tmsize_t read = TIFFReadEncodedTile(tif, TIFFComputeTile(tif, tx * tile_width, ty * tile_height, 0, 0),
                                            tile_buf.data(), tile_buf_size);
        if (read < 0) {
          heif_image_release(*out_image);
          *out_image = nullptr;
          return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF tile"};
        }

        uint32_t actual_w = std::min(tile_width, width - tx * tile_width);
        uint32_t actual_h = std::min(tile_height, height - ty * tile_height);

        for (uint32_t row = 0; row < actual_h; row++) {
          uint8_t* dst = out_plane + ((size_t)ty * tile_height + row) * row_bytes
                         + (size_t)tx * tile_width * bytesPerSample;
          uint8_t* src = tile_buf.data() + (size_t)row * tile_width * bytesPerSample;
          memcpy(dst, src, actual_w * bytesPerSample);
        }
      }
    }

    return heif_error_ok;
#else
    return {heif_error_Unsupported_feature, heif_suberror_Unspecified,
            "Signed integer TIFF requires uncompressed codec support (WITH_UNCOMPRESSED_CODEC)."};
#endif
  }

  uint16_t outSpp = (samplesPerPixel == 4 && !hasAlpha) ? 3 : samplesPerPixel;
  int effectiveBitDepth = (bps <= 8) ? 8 : output_bit_depth;
  heif_chroma chroma = get_heif_chroma(outSpp, effectiveBitDepth);
  heif_colorspace colorspace = (outSpp == 1) ? heif_colorspace_monochrome : heif_colorspace_RGB;
  heif_channel channel = (outSpp == 1) ? heif_channel_Y : heif_channel_interleaved;

  heif_error err = heif_image_create((int)width, (int)height, colorspace, chroma, out_image);
  if (err.code != heif_error_Ok) return err;

  int planeBitDepth = (effectiveBitDepth <= 8) ? outSpp * 8 : effectiveBitDepth;
  heif_image_add_plane(*out_image, channel, (int)width, (int)height, planeBitDepth);

  size_t out_stride;
  uint8_t* out_plane = heif_image_get_plane2(*out_image, channel, &out_stride);

  tmsize_t tile_buf_size = TIFFTileSize(tif);
  std::vector<uint8_t> tile_buf(tile_buf_size);

  uint32_t n_cols = (width - 1) / tile_width + 1;
  uint32_t n_rows = (height - 1) / tile_height + 1;

  for (uint32_t ty = 0; ty < n_rows; ty++) {
    for (uint32_t tx = 0; tx < n_cols; tx++) {
      tmsize_t read = TIFFReadEncodedTile(tif, TIFFComputeTile(tif, tx * tile_width, ty * tile_height, 0, 0),
                                          tile_buf.data(), tile_buf_size);
      if (read < 0) {
        heif_image_release(*out_image);
        *out_image = nullptr;
        return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF tile"};
      }

      uint32_t actual_w = std::min(tile_width, width - tx * tile_width);
      uint32_t actual_h = std::min(tile_height, height - ty * tile_height);

      if (bps <= 8) {
        for (uint32_t row = 0; row < actual_h; row++) {
          uint8_t* dst = out_plane + ((size_t)ty * tile_height + row) * out_stride + (size_t)tx * tile_width * outSpp;
          uint8_t* src = tile_buf.data() + (size_t)row * tile_width * samplesPerPixel;
          if (outSpp == samplesPerPixel) {
            memcpy(dst, src, actual_w * outSpp);
          }
          else {
            for (uint32_t x = 0; x < actual_w; x++) {
              memcpy(dst + x * outSpp, src + x * samplesPerPixel, outSpp);
            }
          }
        }
      }
      else if (output_bit_depth <= 8) {
        for (uint32_t row = 0; row < actual_h; row++) {
          uint8_t* dst = out_plane + ((size_t)ty * tile_height + row) * out_stride + (size_t)tx * tile_width * outSpp;
          uint16_t* src = reinterpret_cast<uint16_t*>(tile_buf.data() + (size_t)row * tile_width * samplesPerPixel * 2);
          if (outSpp == samplesPerPixel) {
            for (uint32_t x = 0; x < actual_w * outSpp; x++) {
              dst[x] = static_cast<uint8_t>(src[x] >> (bps - 8));
            }
          }
          else {
            for (uint32_t x = 0; x < actual_w; x++) {
              for (uint16_t c = 0; c < outSpp; c++) {
                dst[x * outSpp + c] = static_cast<uint8_t>(src[x * samplesPerPixel + c] >> (bps - 8));
              }
            }
          }
        }
      }
      else {
        int bdShift = bps - output_bit_depth;
        for (uint32_t row = 0; row < actual_h; row++) {
          uint16_t* dst = reinterpret_cast<uint16_t*>(out_plane + ((size_t)ty * tile_height + row) * out_stride
                                                      + (size_t)tx * tile_width * outSpp * 2);
          uint16_t* src = reinterpret_cast<uint16_t*>(tile_buf.data() + (size_t)row * tile_width * samplesPerPixel * 2);
          if (outSpp == samplesPerPixel) {
            if (bdShift == 0) {
              memcpy(dst, src, actual_w * outSpp * 2);
            }
            else {
              for (uint32_t x = 0; x < actual_w * outSpp; x++) {
                dst[x] = static_cast<uint16_t>(src[x] >> bdShift);
              }
            }
          }
          else {
            for (uint32_t x = 0; x < actual_w; x++) {
              for (uint16_t c = 0; c < outSpp; c++) {
                dst[x * outSpp + c] = static_cast<uint16_t>(src[x * samplesPerPixel + c] >> bdShift);
              }
            }
          }
        }
      }
    }
  }

  return heif_error_ok;
}


static heif_error readTiledSeparate(TIFF* tif, uint32_t width, uint32_t height,
                                    uint32_t tile_width, uint32_t tile_height,
                                    uint16_t samplesPerPixel, bool hasAlpha,
                                    uint16_t bps, int output_bit_depth,
                                    uint16_t sampleFormat, heif_image** out_image)
{
  // For mono float/signed int, separate layout is the same as contiguous (1 sample)
  if (sampleFormat == SAMPLEFORMAT_IEEEFP || sampleFormat == SAMPLEFORMAT_INT) {
#if WITH_UNCOMPRESSED_CODEC
    return readTiledContiguous(tif, width, height, tile_width, tile_height,
                               samplesPerPixel, hasAlpha, bps, output_bit_depth,
                               sampleFormat, out_image);
#else
    return {heif_error_Unsupported_feature, heif_suberror_Unspecified,
            "Float/signed integer TIFF requires uncompressed codec support (WITH_UNCOMPRESSED_CODEC)."};
#endif
  }

  uint16_t outSpp = (samplesPerPixel == 4 && !hasAlpha) ? 3 : samplesPerPixel;
  int effectiveBitDepth = (bps <= 8) ? 8 : output_bit_depth;
  heif_chroma chroma = get_heif_chroma(outSpp, effectiveBitDepth);
  heif_colorspace colorspace = (outSpp == 1) ? heif_colorspace_monochrome : heif_colorspace_RGB;
  heif_channel channel = (outSpp == 1) ? heif_channel_Y : heif_channel_interleaved;

  heif_error err = heif_image_create((int)width, (int)height, colorspace, chroma, out_image);
  if (err.code != heif_error_Ok) return err;

  int planeBitDepth = (effectiveBitDepth <= 8) ? outSpp * 8 : effectiveBitDepth;
  heif_image_add_plane(*out_image, channel, (int)width, (int)height, planeBitDepth);

  size_t out_stride;
  uint8_t* out_plane = heif_image_get_plane2(*out_image, channel, &out_stride);

  tmsize_t tile_buf_size = TIFFTileSize(tif);
  std::vector<uint8_t> tile_buf(tile_buf_size);

  uint32_t n_cols = (width - 1) / tile_width + 1;
  uint32_t n_rows = (height - 1) / tile_height + 1;

  for (uint16_t s = 0; s < outSpp; s++) {
    for (uint32_t ty = 0; ty < n_rows; ty++) {
      for (uint32_t tx = 0; tx < n_cols; tx++) {
        tmsize_t read = TIFFReadEncodedTile(tif, TIFFComputeTile(tif, tx * tile_width, ty * tile_height, 0, s),
                                            tile_buf.data(), tile_buf_size);
        if (read < 0) {
          heif_image_release(*out_image);
          *out_image = nullptr;
          return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF tile"};
        }

        uint32_t actual_w = std::min(tile_width, width - tx * tile_width);
        uint32_t actual_h = std::min(tile_height, height - ty * tile_height);

        if (bps <= 8) {
          for (uint32_t row = 0; row < actual_h; row++) {
            uint8_t* dst = out_plane + ((size_t)ty * tile_height + row) * out_stride + (size_t)tx * tile_width * outSpp + s;
            uint8_t* src = tile_buf.data() + (size_t)row * tile_width;
            for (uint32_t x = 0; x < actual_w; x++) {
              dst[x * outSpp] = src[x];
            }
          }
        }
        else if (output_bit_depth <= 8) {
          for (uint32_t row = 0; row < actual_h; row++) {
            uint8_t* dst = out_plane + ((size_t)ty * tile_height + row) * out_stride + (size_t)tx * tile_width * outSpp + s;
            uint16_t* src = reinterpret_cast<uint16_t*>(tile_buf.data() + (size_t)row * tile_width * 2);
            for (uint32_t x = 0; x < actual_w; x++) {
              dst[x * outSpp] = static_cast<uint8_t>(src[x] >> (bps - 8));
            }
          }
        }
        else {
          int bdShift = bps - output_bit_depth;
          for (uint32_t row = 0; row < actual_h; row++) {
            uint16_t* dst = reinterpret_cast<uint16_t*>(out_plane + ((size_t)ty * tile_height + row) * out_stride) + (size_t)tx * tile_width * outSpp + s;
            uint16_t* src = reinterpret_cast<uint16_t*>(tile_buf.data() + (size_t)row * tile_width * 2);
            for (uint32_t x = 0; x < actual_w; x++) {
              dst[x * outSpp] = static_cast<uint16_t>(src[x] >> bdShift);
            }
          }
        }
      }
    }
  }

  return heif_error_ok;
}


heif_error loadTIFF(const char* filename, int output_bit_depth, InputImage *input_image) {
  TIFFSetWarningHandler(suppress_warnings);

  std::unique_ptr<TIFF, void(*)(TIFF*)> tifPtr(TIFFOpen(filename, "r"), [](TIFF* tif) { TIFFClose(tif); });
  if (!tifPtr) {
    return {heif_error_Invalid_input, heif_suberror_Unspecified, "Cannot open TIFF file"};
  }

  TIFF* tif = tifPtr.get();

  uint16_t samplesPerPixel, bps, config, sampleFormat;
  bool hasAlpha;
  heif_error err = validateTiffFormat(tif, samplesPerPixel, bps, config, hasAlpha, sampleFormat);
  if (err.code != heif_error_Ok) return err;

  // For PLANARCONFIG_SEPARATE + YCbCr, tell libtiff to convert to RGB on the fly
  YCbCrInfo ycbcr = getYCbCrInfo(tif);
  if (ycbcr.is_ycbcr && config == PLANARCONFIG_SEPARATE) {
    TIFFSetField(tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);
  }

  bool isFloat = (sampleFormat == SAMPLEFORMAT_IEEEFP);
  bool isSignedInt = (sampleFormat == SAMPLEFORMAT_INT);

  // For 8-bit source, always produce 8-bit output (ignore output_bit_depth).
  // For float, use 32-bit. For signed int, preserve original bit depth.
  int effectiveOutputBitDepth = isFloat ? 32 : (isSignedInt ? bps : ((bps <= 8) ? 8 : ((bps < 16) ? bps : output_bit_depth)));

  struct heif_image* image = nullptr;

  if (TIFFIsTiled(tif)) {
    uint32_t width, height, tile_width, tile_height;
    err = getImageWidthAndHeight(tif, width, height);
    if (err.code != heif_error_Ok) return err;

    if (!TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tile_width) ||
        !TIFFGetField(tif, TIFFTAG_TILELENGTH, &tile_height)) {
      return {heif_error_Invalid_input, heif_suberror_Unspecified, "Cannot read TIFF tile dimensions"};
    }

    if (tile_width == 0 || tile_height == 0) {
      return {heif_error_Invalid_input, heif_suberror_Unspecified, "Invalid TIFF tile dimensions"};
    }

    switch (config) {
      case PLANARCONFIG_CONTIG:
        err = readTiledContiguous(tif, width, height, tile_width, tile_height, samplesPerPixel, hasAlpha, bps, effectiveOutputBitDepth, sampleFormat, &image);
        break;
      case PLANARCONFIG_SEPARATE:
        err = readTiledSeparate(tif, width, height, tile_width, tile_height, samplesPerPixel, hasAlpha, bps, effectiveOutputBitDepth, sampleFormat, &image);
        break;
      default:
        return {heif_error_Invalid_input, heif_suberror_Unspecified, "Unsupported planar configuration"};
    }
  }
  else {
    if (isFloat) {
#if WITH_UNCOMPRESSED_CODEC
      err = readMonoFloat(tif, &image);
#else
      return {heif_error_Unsupported_feature, heif_suberror_Unspecified,
              "Floating point TIFF requires uncompressed codec support (WITH_UNCOMPRESSED_CODEC)."};
#endif
    }
    else if (isSignedInt) {
#if WITH_UNCOMPRESSED_CODEC
      err = readMonoSignedInt(tif, bps, &image);
#else
      return {heif_error_Unsupported_feature, heif_suberror_Unspecified,
              "Signed integer TIFF requires uncompressed codec support (WITH_UNCOMPRESSED_CODEC)."};
#endif
    }
    else {
      switch (config) {
        case PLANARCONFIG_CONTIG:
          err = readPixelInterleave(tif, samplesPerPixel, hasAlpha, bps, effectiveOutputBitDepth, &image);
          break;
        case PLANARCONFIG_SEPARATE:
          err = readBandInterleave(tif, samplesPerPixel, hasAlpha, bps, effectiveOutputBitDepth, &image);
          break;
        default:
          return {heif_error_Invalid_input, heif_suberror_Unspecified, "Unsupported planar configuration"};
      }
    }
  }

  if (err.code != heif_error_Ok) {
    return err;
  }

  input_image->image = std::shared_ptr<heif_image>(image,
                                          [](heif_image* img) { heif_image_release(img); });

  // Unfortunately libtiff doesn't provide a way to read a raw dictionary.
  // Therefore we manually parse the EXIF data, extract the tags and encode
  // them for use in the HEIF image.
  std::unique_ptr<ExifTags> tags = ExifTags::Parse(tif);
  if (tags) {
    tags->Encode(&(input_image->exif));
  }
  return heif_error_ok;
}


// --- TiledTiffReader ---

void TiledTiffReader::TiffCloser::operator()(void* tif) const {
  if (tif) {
    TIFFClose(static_cast<TIFF*>(tif));
  }
}


std::unique_ptr<TiledTiffReader> TiledTiffReader::open(const char* filename, heif_error* out_err)
{
  TIFFSetWarningHandler(suppress_warnings);

  TIFF* tif = TIFFOpen(filename, "r");
  if (!tif) {
    *out_err = {heif_error_Invalid_input, heif_suberror_Unspecified, "Cannot open TIFF file"};
    return nullptr;
  }

  if (!TIFFIsTiled(tif)) {
    TIFFClose(tif);
    *out_err = heif_error_ok;
    return nullptr;
  }

  auto reader = std::unique_ptr<TiledTiffReader>(new TiledTiffReader());
  reader->m_tif.reset(tif);

  uint16_t bps;
  heif_error err = validateTiffFormat(tif, reader->m_samples_per_pixel, bps, reader->m_planar_config, reader->m_has_alpha, reader->m_sample_format);
  if (err.code != heif_error_Ok) {
    *out_err = err;
    return nullptr;
  }
  reader->m_bits_per_sample = bps;

  reader->m_ycbcr = getYCbCrInfo(tif);
  if (reader->m_ycbcr.is_ycbcr && reader->m_planar_config == PLANARCONFIG_SEPARATE) {
    TIFFSetField(tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);
    reader->m_ycbcr.is_ycbcr = false;  // data comes out as RGB now
  }

  if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &reader->m_image_width) ||
      !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &reader->m_image_height)) {
    *out_err = {heif_error_Invalid_input, heif_suberror_Unspecified, "Cannot read TIFF image dimensions"};
    return nullptr;
  }

  if (!TIFFGetField(tif, TIFFTAG_TILEWIDTH, &reader->m_tile_width) ||
      !TIFFGetField(tif, TIFFTAG_TILELENGTH, &reader->m_tile_height)) {
    *out_err = {heif_error_Invalid_input, heif_suberror_Unspecified, "Cannot read TIFF tile dimensions"};
    return nullptr;
  }

  if (reader->m_tile_width == 0 || reader->m_tile_height == 0) {
    *out_err = {heif_error_Invalid_input, heif_suberror_Unspecified, "Invalid TIFF tile dimensions"};
    return nullptr;
  }

  reader->m_n_columns = (reader->m_image_width - 1) / reader->m_tile_width + 1;
  reader->m_n_rows = (reader->m_image_height - 1) / reader->m_tile_height + 1;

  // Detect overview directories (reduced-resolution images)
  tdir_t n_dirs = TIFFNumberOfDirectories(tif);
  for (uint16_t d = 1; d < n_dirs; d++) {
    if (!TIFFSetDirectory(tif, d)) {
      continue;
    }

    uint32_t subfiletype = 0;
    TIFFGetField(tif, TIFFTAG_SUBFILETYPE, &subfiletype);
    if (!(subfiletype & FILETYPE_REDUCEDIMAGE)) {
      continue;
    }

    if (!TIFFIsTiled(tif)) {
      continue;
    }

    uint16_t spp, bps_ov, config_ov, sampleFormat_ov;
    bool hasAlpha_ov;
    heif_error valErr = validateTiffFormat(tif, spp, bps_ov, config_ov, hasAlpha_ov, sampleFormat_ov);
    if (valErr.code != heif_error_Ok) {
      continue;
    }

    uint32_t ov_width, ov_height, ov_tw, ov_th;
    if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &ov_width) ||
        !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &ov_height)) {
      continue;
    }
    if (!TIFFGetField(tif, TIFFTAG_TILEWIDTH, &ov_tw) ||
        !TIFFGetField(tif, TIFFTAG_TILELENGTH, &ov_th)) {
      continue;
    }

    if (ov_width == 0 || ov_height == 0) {
      continue;
    }

    if (ov_tw == 0 || ov_th == 0) {
      continue;
    }

    reader->m_overviews.push_back({d, ov_width, ov_height, ov_tw, ov_th});
  }

  // Switch back to directory 0
  TIFFSetDirectory(tif, 0);

  *out_err = heif_error_ok;
  return reader;
}


TiledTiffReader::~TiledTiffReader() = default;


bool TiledTiffReader::setDirectory(uint32_t dir_index)
{
  TIFF* tif = static_cast<TIFF*>(m_tif.get());

  if (!TIFFSetDirectory(tif, static_cast<uint16_t>(dir_index))) {
    return false;
  }

  if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &m_image_width) ||
      !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &m_image_height)) {
    return false;
  }

  if (!TIFFGetField(tif, TIFFTAG_TILEWIDTH, &m_tile_width) ||
      !TIFFGetField(tif, TIFFTAG_TILELENGTH, &m_tile_height)) {
    return false;
  }

  if (m_image_width == 0 || m_image_height == 0) {
    return false;
  }

  if (m_tile_width == 0 || m_tile_height == 0) {
    return false;
  }

  uint16_t bps;
  heif_error err = validateTiffFormat(tif, m_samples_per_pixel, bps, m_planar_config, m_has_alpha, m_sample_format);
  if (err.code != heif_error_Ok) {
    return false;
  }
  m_bits_per_sample = bps;

  m_ycbcr = getYCbCrInfo(tif);
  if (m_ycbcr.is_ycbcr && m_planar_config == PLANARCONFIG_SEPARATE) {
    TIFFSetField(tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);
    m_ycbcr.is_ycbcr = false;
  }

  m_n_columns = (m_image_width - 1) / m_tile_width + 1;
  m_n_rows = (m_image_height - 1) / m_tile_height + 1;

  return true;
}


heif_error TiledTiffReader::readTile(uint32_t tx, uint32_t ty, int output_bit_depth, heif_image** out_image)
{
  TIFF* tif = static_cast<TIFF*>(m_tif.get());

  uint32_t actual_w = std::min(m_tile_width, m_image_width - tx * m_tile_width);
  uint32_t actual_h = std::min(m_tile_height, m_image_height - ty * m_tile_height);

  // --- YCbCr path (PLANARCONFIG_CONTIG only; SEPARATE was converted to RGB in open/setDirectory) ---
  if (m_ycbcr.is_ycbcr && m_planar_config == PLANARCONFIG_CONTIG) {
    if (m_bits_per_sample != 8) {
      return {heif_error_Unsupported_feature, heif_suberror_Unspecified,
              "Only 8-bit YCbCr TIFF is supported."};
    }

    tmsize_t tile_buf_size = TIFFTileSize(tif);
    std::vector<uint8_t> tile_buf(tile_buf_size);

    tmsize_t read = TIFFReadEncodedTile(tif, TIFFComputeTile(tif, tx * m_tile_width, ty * m_tile_height, 0, 0),
                                        tile_buf.data(), tile_buf_size);
    if (read < 0) {
      return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF tile"};
    }

    return readYCbCrBlock(tile_buf.data(), m_tile_width, m_tile_height, actual_w, actual_h, m_ycbcr, out_image);
  }

  if (m_sample_format == SAMPLEFORMAT_IEEEFP) {
#if WITH_UNCOMPRESSED_CODEC
    heif_error err = heif_image_create((int)actual_w, (int)actual_h, heif_colorspace_custom, heif_chroma_planar, out_image);
    if (err.code != heif_error_Ok) return err;

    uint32_t component_idx;
    err = heif_image_add_component(*out_image, (int)actual_w, (int)actual_h,
                                   heif_unci_component_type_monochrome,
                                   heif_component_datatype_floating_point, 32, &component_idx);
    if (err.code != heif_error_Ok) {
      heif_image_release(*out_image);
      *out_image = nullptr;
      return err;
    }

    size_t out_stride;
    float* out_plane = heif_image_get_component_float32(*out_image, component_idx, &out_stride);
    if (!out_plane) {
      heif_image_release(*out_image);
      *out_image = nullptr;
      return {heif_error_Memory_allocation_error, heif_suberror_Unspecified, "Failed to get float plane"};
    }

    tmsize_t tile_buf_size = TIFFTileSize(tif);
    std::vector<uint8_t> tile_buf(tile_buf_size);

    tmsize_t read = TIFFReadEncodedTile(tif, TIFFComputeTile(tif, tx * m_tile_width, ty * m_tile_height, 0, 0),
                                        tile_buf.data(), tile_buf_size);
    if (read < 0) {
      heif_image_release(*out_image);
      *out_image = nullptr;
      return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF tile"};
    }

    for (uint32_t row = 0; row < actual_h; row++) {
      float* dst = out_plane + row * out_stride;
      float* src = reinterpret_cast<float*>(tile_buf.data() + row * m_tile_width * sizeof(float));
      memcpy(dst, src, actual_w * sizeof(float));
    }

    return heif_error_ok;
#else
    return {heif_error_Unsupported_feature, heif_suberror_Unspecified,
            "Floating point TIFF requires uncompressed codec support (WITH_UNCOMPRESSED_CODEC)."};
#endif
  }

  if (m_sample_format == SAMPLEFORMAT_INT) {
#if WITH_UNCOMPRESSED_CODEC
    heif_error err = heif_image_create((int)actual_w, (int)actual_h, heif_colorspace_custom, heif_chroma_planar, out_image);
    if (err.code != heif_error_Ok) return err;

    uint32_t component_idx;
    err = heif_image_add_component(*out_image, (int)actual_w, (int)actual_h,
                                   heif_unci_component_type_monochrome,
                                   heif_component_datatype_signed_integer, m_bits_per_sample, &component_idx);
    if (err.code != heif_error_Ok) {
      heif_image_release(*out_image);
      *out_image = nullptr;
      return err;
    }

    size_t row_elements;  // int8 or int16 elements per row, depending on bps
    uint8_t* out_plane;
    if (m_bits_per_sample == 8) {
      out_plane = reinterpret_cast<uint8_t*>(heif_image_get_component_int8(*out_image, component_idx, &row_elements));
    }
    else {
      out_plane = reinterpret_cast<uint8_t*>(heif_image_get_component_int16(*out_image, component_idx, &row_elements));
    }
    if (!out_plane) {
      heif_image_release(*out_image);
      *out_image = nullptr;
      return {heif_error_Memory_allocation_error, heif_suberror_Unspecified, "Failed to get signed int plane"};
    }

    int bytesPerSample = m_bits_per_sample > 8 ? 2 : 1;
    size_t row_bytes = row_elements * bytesPerSample;
    tmsize_t tile_buf_size = TIFFTileSize(tif);
    std::vector<uint8_t> tile_buf(tile_buf_size);

    tmsize_t read = TIFFReadEncodedTile(tif, TIFFComputeTile(tif, tx * m_tile_width, ty * m_tile_height, 0, 0),
                                        tile_buf.data(), tile_buf_size);
    if (read < 0) {
      heif_image_release(*out_image);
      *out_image = nullptr;
      return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF tile"};
    }

    for (uint32_t row = 0; row < actual_h; row++) {
      uint8_t* dst = out_plane + row * row_bytes;
      uint8_t* src = tile_buf.data() + row * m_tile_width * bytesPerSample;
      memcpy(dst, src, actual_w * bytesPerSample);
    }

    return heif_error_ok;
#else
    return {heif_error_Unsupported_feature, heif_suberror_Unspecified,
            "Signed integer TIFF requires uncompressed codec support (WITH_UNCOMPRESSED_CODEC)."};
#endif
  }

  int effectiveBitDepth = (m_bits_per_sample <= 8) ? 8 : output_bit_depth;
  uint16_t outSpp = (m_samples_per_pixel == 4 && !m_has_alpha) ? 3 : m_samples_per_pixel;
  heif_chroma chroma = get_heif_chroma(outSpp, effectiveBitDepth);
  heif_colorspace colorspace = (outSpp == 1) ? heif_colorspace_monochrome : heif_colorspace_RGB;
  heif_channel channel = (outSpp == 1) ? heif_channel_Y : heif_channel_interleaved;

  heif_error err = heif_image_create((int)actual_w, (int)actual_h, colorspace, chroma, out_image);
  if (err.code != heif_error_Ok) return err;

  int planeBitDepth = (effectiveBitDepth <= 8) ? outSpp * 8 : effectiveBitDepth;
  heif_image_add_plane(*out_image, channel, (int)actual_w, (int)actual_h, planeBitDepth);

  size_t out_stride;
  uint8_t* out_plane = heif_image_get_plane2(*out_image, channel, &out_stride);

  tmsize_t tile_buf_size = TIFFTileSize(tif);
  std::vector<uint8_t> tile_buf(tile_buf_size);

  if (m_planar_config == PLANARCONFIG_CONTIG) {
    tmsize_t read = TIFFReadEncodedTile(tif, TIFFComputeTile(tif, tx * m_tile_width, ty * m_tile_height, 0, 0),
                                        tile_buf.data(), tile_buf_size);
    if (read < 0) {
      heif_image_release(*out_image);
      *out_image = nullptr;
      return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF tile"};
    }

    if (m_bits_per_sample <= 8) {
      for (uint32_t row = 0; row < actual_h; row++) {
        uint8_t* dst = out_plane + row * out_stride;
        uint8_t* src = tile_buf.data() + row * m_tile_width * m_samples_per_pixel;
        if (outSpp == m_samples_per_pixel) {
          memcpy(dst, src, actual_w * outSpp);
        }
        else {
          for (uint32_t x = 0; x < actual_w; x++) {
            memcpy(dst + x * outSpp, src + x * m_samples_per_pixel, outSpp);
          }
        }
      }
    }
    else if (output_bit_depth <= 8) {
      for (uint32_t row = 0; row < actual_h; row++) {
        uint8_t* dst = out_plane + row * out_stride;
        uint16_t* src = reinterpret_cast<uint16_t*>(tile_buf.data() + row * m_tile_width * m_samples_per_pixel * 2);
        if (outSpp == m_samples_per_pixel) {
          for (uint32_t x = 0; x < actual_w * outSpp; x++) {
            dst[x] = static_cast<uint8_t>(src[x] >> (m_bits_per_sample - 8));
          }
        }
        else {
          for (uint32_t x = 0; x < actual_w; x++) {
            for (uint16_t c = 0; c < outSpp; c++) {
              dst[x * outSpp + c] = static_cast<uint8_t>(src[x * m_samples_per_pixel + c] >> (m_bits_per_sample - 8));
            }
          }
        }
      }
    }
    else {
      int bdShift = m_bits_per_sample - output_bit_depth;
      for (uint32_t row = 0; row < actual_h; row++) {
        uint16_t* dst = reinterpret_cast<uint16_t*>(out_plane + row * out_stride);
        uint16_t* src = reinterpret_cast<uint16_t*>(tile_buf.data() + row * m_tile_width * m_samples_per_pixel * 2);
        if (outSpp == m_samples_per_pixel) {
          if (bdShift == 0) {
            memcpy(dst, src, actual_w * outSpp * 2);
          }
          else {
            for (uint32_t x = 0; x < actual_w * outSpp; x++) {
              dst[x] = static_cast<uint16_t>(src[x] >> bdShift);
            }
          }
        }
        else {
          for (uint32_t x = 0; x < actual_w; x++) {
            for (uint16_t c = 0; c < outSpp; c++) {
              dst[x * outSpp + c] = static_cast<uint16_t>(src[x * m_samples_per_pixel + c] >> bdShift);
            }
          }
        }
      }
    }
  }
  else {
    // PLANARCONFIG_SEPARATE
    for (uint16_t s = 0; s < outSpp; s++) {
      tmsize_t read = TIFFReadEncodedTile(tif, TIFFComputeTile(tif, tx * m_tile_width, ty * m_tile_height, 0, s),
                                          tile_buf.data(), tile_buf_size);
      if (read < 0) {
        heif_image_release(*out_image);
        *out_image = nullptr;
        return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF tile"};
      }

      if (m_bits_per_sample <= 8) {
        for (uint32_t row = 0; row < actual_h; row++) {
          uint8_t* dst = out_plane + row * out_stride + s;
          uint8_t* src = tile_buf.data() + row * m_tile_width;
          for (uint32_t x = 0; x < actual_w; x++) {
            dst[x * outSpp] = src[x];
          }
        }
      }
      else if (output_bit_depth <= 8) {
        for (uint32_t row = 0; row < actual_h; row++) {
          uint8_t* dst = out_plane + row * out_stride + s;
          uint16_t* src = reinterpret_cast<uint16_t*>(tile_buf.data() + row * m_tile_width * 2);
          for (uint32_t x = 0; x < actual_w; x++) {
            dst[x * outSpp] = static_cast<uint8_t>(src[x] >> (m_bits_per_sample - 8));
          }
        }
      }
      else {
        int bdShift = m_bits_per_sample - output_bit_depth;
        for (uint32_t row = 0; row < actual_h; row++) {
          uint16_t* dst = reinterpret_cast<uint16_t*>(out_plane + row * out_stride) + s;
          uint16_t* src = reinterpret_cast<uint16_t*>(tile_buf.data() + row * m_tile_width * 2);
          for (uint32_t x = 0; x < actual_w; x++) {
            dst[x * outSpp] = static_cast<uint16_t>(src[x] >> bdShift);
          }
        }
      }
    }
  }

  return heif_error_ok;
}


void TiledTiffReader::readExif(InputImage* input_image)
{
  TIFF* tif = static_cast<TIFF*>(m_tif.get());
  std::unique_ptr<ExifTags> tags = ExifTags::Parse(tif);
  if (tags) {
    tags->Encode(&(input_image->exif));
  }
}

/*
void TiledTiffReader::printGeoInfo(const char* filename) const
{
#if HAVE_GEOTIFF
  TIFF* tif = XTIFFOpen(filename, "r");
  if (!tif) {
    return;
  }

  GTIF* gtif = GTIFNew(tif);
  if (!gtif) {
    XTIFFClose(tif);
    return;
  }

  // Get EPSG code
  unsigned short epsg = 0;
  GTIFKeyGetSHORT(gtif, ProjectedCSTypeGeoKey, &epsg, 0, 1);

  // Get CRS citation string
  char citation[256] = {};
  GTIFKeyGetASCII(gtif, GTCitationGeoKey, citation, sizeof(citation));
  if (citation[0] == '\0') {
    GTIFKeyGetASCII(gtif, GeogCitationGeoKey, citation, sizeof(citation));
  }

  if (epsg > 0 || citation[0] != '\0') {
    std::cout << "GeoTIFF: ";
    if (epsg > 0) {
      std::cout << "EPSG:" << epsg;
    }
    if (citation[0] != '\0') {
      if (epsg > 0) {
        std::cout << " (" << citation << ")";
      }
      else {
        std::cout << citation;
      }
    }
    std::cout << "\n";
  }

  // Get pixel scale
  uint16_t scale_count = 0;
  double* scale = nullptr;
  if (TIFFGetField(tif, TIFFTAG_GEOPIXELSCALE, &scale_count, &scale) && scale_count >= 2) {
    std::cout << std::fixed << std::setprecision(3)
              << "  pixel scale: " << scale[0] << " x " << scale[1] << "\n";
  }

  // Get tiepoint (origin)
  uint16_t tp_count = 0;
  double* tiepoints = nullptr;
  if (TIFFGetField(tif, TIFFTAG_GEOTIEPOINTS, &tp_count, &tiepoints) && tp_count >= 6) {
    std::cout << std::fixed << std::setprecision(3)
              << "  origin: (" << tiepoints[3] << ", " << tiepoints[4] << ")\n";
  }

  GTIFFree(gtif);
  XTIFFClose(tif);
#endif

  if (!m_overviews.empty()) {
    std::cout << "  overviews: ";
    for (size_t i = 0; i < m_overviews.size(); i++) {
      if (i > 0) std::cout << ", ";
      std::cout << m_overviews[i].image_width << "x" << m_overviews[i].image_height;
    }
    std::cout << "\n";
  }
}
*/