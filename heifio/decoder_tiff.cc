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
#include <iomanip>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>
#include <algorithm>

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

static struct heif_error heif_error_ok = {heif_error_Ok, heif_suberror_Unspecified, "Success"};

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
  if (!readTIFF(tif, &dest, 2)) {
    return false;
  }

  if (TIFFIsByteSwapped(tif)) {
    TIFFSwabShort(dest);
  }
  return true;
}

static bool readTIFFUint32(TIFF* tif, uint32_t* dest) {
  if (!readTIFF(tif, &dest, 4)) {
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
  return heif_error_ok;
}

heif_error readMono(TIFF *tif, heif_image **image)
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
  heif_image_add_plane(*image, heif_channel_Y, (int)width, (int)height, 8);

  size_t y_stride;
  uint8_t *py = heif_image_get_plane2(*image, heif_channel_Y, &y_stride);
  for (uint32_t row = 0; row < height; row++)
  {
    TIFFReadScanline(tif, py, row, 0);
    py += y_stride;
  }
  return heif_error_ok;
}

heif_error readPixelInterleaveRGB(TIFF *tif, uint16_t samplesPerPixel, heif_image **image)
{
  uint32_t width, height;
  heif_error err = getImageWidthAndHeight(tif, width, height);
  if (err.code != heif_error_Ok) {
    return err;
  }
  heif_chroma chroma = heif_chroma_interleaved_RGB;
  if (samplesPerPixel == 4) {
    chroma = heif_chroma_interleaved_RGBA;
  }

  err = heif_image_create((int)width, (int)height, heif_colorspace_RGB, chroma, image);
  if (err.code != heif_error_Ok)
  {
    return err;
  }
  heif_channel channel = heif_channel_interleaved;
  heif_image_add_plane(*image, channel, (int)width, (int)height, samplesPerPixel * 8);

  size_t y_stride;
  uint8_t *py = heif_image_get_plane2(*image, channel, &y_stride);

  tdata_t buf = _TIFFmalloc(TIFFScanlineSize(tif));
  for (uint32_t row = 0; row < height; row++)
  {
    TIFFReadScanline(tif, buf, row, 0);
    memcpy(py, buf, width * samplesPerPixel);
    py += y_stride;
  }
  _TIFFfree(buf);
  return heif_error_ok;
}

heif_error readPixelInterleave(TIFF *tif,  uint16_t samplesPerPixel, heif_image **image)
{
  if (samplesPerPixel == 1) {
    return readMono(tif, image);
  } else {
    return readPixelInterleaveRGB(tif, samplesPerPixel, image);
  }
}

heif_error readBandInterleaveRGB(TIFF *tif, uint16_t samplesPerPixel, heif_image **image)
{
  uint32_t width, height;
  heif_error err = getImageWidthAndHeight(tif, width, height);
  if (err.code != heif_error_Ok) {
    return err;
  }
  if (samplesPerPixel == 3) {
    err = heif_image_create((int)width, (int)height, heif_colorspace_RGB, heif_chroma_interleaved_RGB, image);
  } else {
    err = heif_image_create((int)width, (int)height, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, image);
  }
  if (err.code != heif_error_Ok) {
    return err;
  }
  heif_channel channel = heif_channel_interleaved;
  heif_image_add_plane(*image, channel, (int)width, (int)height, samplesPerPixel * 8);

  size_t y_stride;
  uint8_t *py = heif_image_get_plane2(*image, channel, &y_stride);

  uint8_t *buf = static_cast<uint8_t *>(_TIFFmalloc(TIFFScanlineSize(tif)));
  for (uint16_t i = 0; i < samplesPerPixel; i++)
  {
    uint8_t *dest = py + i;
    for (uint32_t row = 0; row < height; row++)
    {
      TIFFReadScanline(tif, buf, row, i);
      for (uint32_t x = 0; x < width; x++, dest += samplesPerPixel)
      {
        *dest = buf[x];
      }
      dest += (y_stride - width * samplesPerPixel);
    }
  }
  _TIFFfree(buf);
  return heif_error_ok;
}


heif_error readBandInterleave(TIFF *tif, uint16_t samplesPerPixel, heif_image **image)
{
  if (samplesPerPixel == 1) {
    return readMono(tif, image);
  } else if (samplesPerPixel == 3) {
    return readBandInterleaveRGB(tif, samplesPerPixel, image);
  } else if (samplesPerPixel == 4) {
    return readBandInterleaveRGB(tif,  samplesPerPixel, image);
  } else {
    struct heif_error err = {
      .code = heif_error_Unsupported_feature,
      .subcode = heif_suberror_Unspecified,
      .message = "Only 1, 3 and 4 bands are supported"};
    return err;
  }
}


static void suppress_warnings(const char* module, const char* fmt, va_list ap) {
  // Do nothing
}


static heif_error validateTiffFormat(TIFF* tif, uint16_t& samplesPerPixel, uint16_t& bps, uint16_t& config, bool& hasAlpha)
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
      // No EXTRASAMPLES tag with 4 spp — assume RGBA for backward compatibility
      hasAlpha = true;
    }
  }

  TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bps);
  if (bps != 8) {
    return {heif_error_Invalid_input, heif_suberror_Unspecified,
            "Only 8 bits per sample are supported."};
  }

  uint16_t format;
  if (TIFFGetField(tif, TIFFTAG_SAMPLEFORMAT, &format) && format != SAMPLEFORMAT_UINT) {
    return {heif_error_Invalid_input, heif_suberror_Unspecified,
            "Only UINT sample format is supported."};
  }

  return heif_error_ok;
}


static heif_error readTiledContiguous(TIFF* tif, uint32_t width, uint32_t height,
                                  uint32_t tile_width, uint32_t tile_height,
                                  uint16_t samplesPerPixel, bool hasAlpha, heif_image** out_image)
{
  uint16_t outSpp = (samplesPerPixel == 4 && !hasAlpha) ? 3 : samplesPerPixel;
  heif_chroma chroma = (outSpp == 1) ? heif_chroma_monochrome
                       : (outSpp == 4) ? heif_chroma_interleaved_RGBA
                       : heif_chroma_interleaved_RGB;
  heif_colorspace colorspace = (outSpp == 1) ? heif_colorspace_monochrome : heif_colorspace_RGB;
  heif_channel channel = (outSpp == 1) ? heif_channel_Y : heif_channel_interleaved;

  heif_error err = heif_image_create((int)width, (int)height, colorspace, chroma, out_image);
  if (err.code != heif_error_Ok) return err;

  heif_image_add_plane(*out_image, channel, (int)width, (int)height, outSpp * 8);

  size_t out_stride;
  uint8_t* out_plane = heif_image_get_plane2(*out_image, channel, &out_stride);

  tmsize_t tile_buf_size = TIFFTileSize(tif);
  std::vector<uint8_t> tile_buf(tile_buf_size);

  uint32_t n_cols = (width + tile_width - 1) / tile_width;
  uint32_t n_rows = (height + tile_height - 1) / tile_height;

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
        uint8_t* dst = out_plane + (ty * tile_height + row) * out_stride + tx * tile_width * outSpp;
        uint8_t* src = tile_buf.data() + row * tile_width * samplesPerPixel;
        if (outSpp == samplesPerPixel) {
          memcpy(dst, src, actual_w * outSpp);
        }
        else {
          // Strip extra sample (RGBX -> RGB)
          for (uint32_t x = 0; x < actual_w; x++) {
            memcpy(dst + x * outSpp, src + x * samplesPerPixel, outSpp);
          }
        }
      }
    }
  }

  return heif_error_ok;
}


static heif_error readTiledSeparate(TIFF* tif, uint32_t width, uint32_t height,
                                    uint32_t tile_width, uint32_t tile_height,
                                    uint16_t samplesPerPixel, bool hasAlpha, heif_image** out_image)
{
  uint16_t outSpp = (samplesPerPixel == 4 && !hasAlpha) ? 3 : samplesPerPixel;
  heif_chroma chroma = (outSpp == 1) ? heif_chroma_monochrome
                       : (outSpp == 4) ? heif_chroma_interleaved_RGBA
                       : heif_chroma_interleaved_RGB;
  heif_colorspace colorspace = (outSpp == 1) ? heif_colorspace_monochrome : heif_colorspace_RGB;
  heif_channel channel = (outSpp == 1) ? heif_channel_Y : heif_channel_interleaved;

  heif_error err = heif_image_create((int)width, (int)height, colorspace, chroma, out_image);
  if (err.code != heif_error_Ok) return err;

  heif_image_add_plane(*out_image, channel, (int)width, (int)height, outSpp * 8);

  size_t out_stride;
  uint8_t* out_plane = heif_image_get_plane2(*out_image, channel, &out_stride);

  tmsize_t tile_buf_size = TIFFTileSize(tif);
  std::vector<uint8_t> tile_buf(tile_buf_size);

  uint32_t n_cols = (width + tile_width - 1) / tile_width;
  uint32_t n_rows = (height + tile_height - 1) / tile_height;

  // Only interleave the first outSpp planes (skip the extra sample plane if !hasAlpha)
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

        for (uint32_t row = 0; row < actual_h; row++) {
          uint8_t* dst = out_plane + (ty * tile_height + row) * out_stride + tx * tile_width * outSpp + s;
          uint8_t* src = tile_buf.data() + row * tile_width;
          for (uint32_t x = 0; x < actual_w; x++) {
            dst[x * outSpp] = src[x];
          }
        }
      }
    }
  }

  return heif_error_ok;
}


heif_error loadTIFF(const char* filename, InputImage *input_image) {
  TIFFSetWarningHandler(suppress_warnings);

  std::unique_ptr<TIFF, void(*)(TIFF*)> tifPtr(TIFFOpen(filename, "r"), [](TIFF* tif) { TIFFClose(tif); });
  if (!tifPtr) {
    return {heif_error_Invalid_input, heif_suberror_Unspecified, "Cannot open TIFF file"};
  }

  TIFF* tif = tifPtr.get();

  uint16_t samplesPerPixel, bps, config;
  bool hasAlpha;
  heif_error err = validateTiffFormat(tif, samplesPerPixel, bps, config, hasAlpha);
  if (err.code != heif_error_Ok) return err;

  struct heif_image* image = nullptr;

  if (TIFFIsTiled(tif)) {
    uint32_t width, height, tile_width, tile_height;
    err = getImageWidthAndHeight(tif, width, height);
    if (err.code != heif_error_Ok) return err;

    if (!TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tile_width) ||
        !TIFFGetField(tif, TIFFTAG_TILELENGTH, &tile_height)) {
      return {heif_error_Invalid_input, heif_suberror_Unspecified, "Cannot read TIFF tile dimensions"};
    }

    switch (config) {
      case PLANARCONFIG_CONTIG:
        err = readTiledContiguous(tif, width, height, tile_width, tile_height, samplesPerPixel, hasAlpha, &image);
        break;
      case PLANARCONFIG_SEPARATE:
        err = readTiledSeparate(tif, width, height, tile_width, tile_height, samplesPerPixel, hasAlpha, &image);
        break;
      default:
        return {heif_error_Invalid_input, heif_suberror_Unspecified, "Unsupported planar configuration"};
    }
  }
  else {
    switch (config) {
      case PLANARCONFIG_CONTIG:
        err = readPixelInterleave(tif, samplesPerPixel, &image);
        break;
      case PLANARCONFIG_SEPARATE:
        err = readBandInterleave(tif, samplesPerPixel, &image);
        break;
      default:
        return {heif_error_Invalid_input, heif_suberror_Unspecified, "Unsupported planar configuration"};
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
  heif_error err = validateTiffFormat(tif, reader->m_samples_per_pixel, bps, reader->m_planar_config, reader->m_has_alpha);
  if (err.code != heif_error_Ok) {
    *out_err = err;
    return nullptr;
  }
  reader->m_bits_per_sample = bps;

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

  reader->m_n_columns = (reader->m_image_width + reader->m_tile_width - 1) / reader->m_tile_width;
  reader->m_n_rows = (reader->m_image_height + reader->m_tile_height - 1) / reader->m_tile_height;

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

    uint16_t spp, bps_ov, config_ov;
    bool hasAlpha_ov;
    heif_error valErr = validateTiffFormat(tif, spp, bps_ov, config_ov, hasAlpha_ov);
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

  uint16_t bps;
  heif_error err = validateTiffFormat(tif, m_samples_per_pixel, bps, m_planar_config, m_has_alpha);
  if (err.code != heif_error_Ok) {
    return false;
  }
  m_bits_per_sample = bps;

  m_n_columns = (m_image_width + m_tile_width - 1) / m_tile_width;
  m_n_rows = (m_image_height + m_tile_height - 1) / m_tile_height;

  return true;
}


heif_error TiledTiffReader::readTile(uint32_t tx, uint32_t ty, heif_image** out_image)
{
  TIFF* tif = static_cast<TIFF*>(m_tif.get());

  uint32_t actual_w = std::min(m_tile_width, m_image_width - tx * m_tile_width);
  uint32_t actual_h = std::min(m_tile_height, m_image_height - ty * m_tile_height);

  uint16_t outSpp = (m_samples_per_pixel == 4 && !m_has_alpha) ? 3 : m_samples_per_pixel;
  heif_chroma chroma = (outSpp == 1) ? heif_chroma_monochrome
                       : (outSpp == 4) ? heif_chroma_interleaved_RGBA
                       : heif_chroma_interleaved_RGB;
  heif_colorspace colorspace = (outSpp == 1) ? heif_colorspace_monochrome : heif_colorspace_RGB;
  heif_channel channel = (outSpp == 1) ? heif_channel_Y : heif_channel_interleaved;

  heif_error err = heif_image_create((int)actual_w, (int)actual_h, colorspace, chroma, out_image);
  if (err.code != heif_error_Ok) return err;

  heif_image_add_plane(*out_image, channel, (int)actual_w, (int)actual_h, outSpp * 8);

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
  else {
    // PLANARCONFIG_SEPARATE: only read the first outSpp planes
    for (uint16_t s = 0; s < outSpp; s++) {
      tmsize_t read = TIFFReadEncodedTile(tif, TIFFComputeTile(tif, tx * m_tile_width, ty * m_tile_height, 0, s),
                                          tile_buf.data(), tile_buf_size);
      if (read < 0) {
        heif_image_release(*out_image);
        *out_image = nullptr;
        return {heif_error_Invalid_input, heif_suberror_Unspecified, "Failed to read TIFF tile"};
      }

      for (uint32_t row = 0; row < actual_h; row++) {
        uint8_t* dst = out_plane + row * out_stride + s;
        uint8_t* src = tile_buf.data() + row * m_tile_width;
        for (uint32_t x = 0; x < actual_w; x++) {
          dst[x * outSpp] = src[x];
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

