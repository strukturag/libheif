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
#include <utility>
#include <vector>

#include <string.h>

extern "C" {
#include <tiff.h>
#include <tiffio.h>
}

#include "decoder_tiff.h"

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

void readPixelInterleave(TIFF *tif, uint32_t width, uint32_t height, uint16_t samplesPerPixel, heif_image *image)
{
  uint32_t row;
  heif_channel channel = heif_channel_interleaved;
  heif_image_add_plane(image, channel, (int)width, (int)height, samplesPerPixel * 8);

  int y_stride;
  uint8_t *py = heif_image_get_plane(image, channel, &y_stride);

  tdata_t buf = _TIFFmalloc(TIFFScanlineSize(tif));
  for (row = 0; row < height; row++)
  {
    TIFFReadScanline(tif, buf, row, 0);
    memcpy(py, buf, width * samplesPerPixel);
    py += y_stride;
  }
  _TIFFfree(buf);
}


void readBandInterleave(TIFF *tif, uint32_t width, uint32_t height, uint16_t samplesPerPixel, heif_image *image)
{
  uint32_t row;
  heif_channel channel = heif_channel_interleaved;
  heif_image_add_plane(image, channel, (int)width, (int)height, samplesPerPixel * 8);

  int y_stride;
  uint8_t *py = heif_image_get_plane(image, channel, &y_stride);

  if (samplesPerPixel == 4)
  {
    TIFFRGBAImage img;
    char emsg[1024] = {0};
    if (!TIFFRGBAImageBegin(&img, tif, 1, emsg))
    {
      heif_image_release(image);
      std::cerr << "Could not get RGBA image: " << emsg << "\n";
      exit(1);
    }

    uint32_t *buf = static_cast<uint32_t *>(_TIFFmalloc(width * samplesPerPixel));
    for (row = 0; row < height; row++)
    {
      TIFFReadRGBAStrip(tif, row, buf);
      memcpy(py, buf, width * samplesPerPixel);
      py += y_stride;
    }
    _TIFFfree(buf);
    TIFFRGBAImageEnd(&img);
  }
  else
  {
    uint8_t *buf = static_cast<uint8_t *>(_TIFFmalloc(TIFFScanlineSize(tif)));
    for (uint16_t i = 0; i < samplesPerPixel; i++)
    {
      uint8_t *dest = py + i;
      for (row = 0; row < height; row++)
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
  }
}

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

  uint16_t shortv, samplesPerPixel, bps, config, format;
  uint32_t width, height;
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
  TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
  if (samplesPerPixel != 1 && samplesPerPixel != 3 && samplesPerPixel != 4) {
    std::cerr << "Unsupported TIFF samples per pixel: " << samplesPerPixel << "\n";
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
  heif_colorspace colorspace = samplesPerPixel == 1 ? heif_colorspace_monochrome : heif_colorspace_RGB;
  heif_chroma chroma = samplesPerPixel == 1 ? heif_chroma_monochrome : heif_chroma_interleaved_RGB;
  if (samplesPerPixel == 4) {
    chroma = heif_chroma_interleaved_RGBA;
  }

  err = heif_image_create((int) width, (int) height, colorspace, chroma, &image);
  (void) err;
  // TODO: handle error

  switch (config) {
    case PLANARCONFIG_CONTIG:
      readPixelInterleave(tif, width, height, samplesPerPixel, image);
      break;
    case PLANARCONFIG_SEPARATE:
      readBandInterleave(tif, width, height, samplesPerPixel, image);
      break;
    default:
      heif_image_release(image);
      std::cerr << "Unsupported planar config: " << config << "\n";
      exit(1);
  }

  input_image.image = std::shared_ptr<heif_image>(image,
                                          [](heif_image* img) { heif_image_release(img); });

  // Unfortunately libtiff doesn't provide a way to read a raw dictionary.
  // Therefore we manually parse the EXIF data, extract the tags and encode
  // them for use in the HEIF image.
  std::unique_ptr<ExifTags> tags = ExifTags::Parse(tif);
  if (tags) {
    tags->Encode(&input_image.exif);
  }
  return input_image;
}

