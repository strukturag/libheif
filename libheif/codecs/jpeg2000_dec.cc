/*
 * HEIF codec.
 * Copyright (c) 2024 Dirk Farin <dirk.farin@gmail.com>
 *
 * This file is part of libheif.
 *
 * libheif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libheif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libheif.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "jpeg2000_dec.h"
#include "jpeg2000_boxes.h"
#include "error.h"
#include "context.h"

#include <string>


Result<std::vector<uint8_t>> Decoder_JPEG2000::read_bitstream_configuration_data() const
{
  return std::vector<uint8_t>{};
}


Result<std::optional<ImageSize>> Decoder_JPEG2000::get_max_coded_image_size(const std::vector<uint8_t>& compressed_data) const
{
  // The JPEG 2000 coded size is the reference grid (Xsiz, Ysiz) declared in the
  // SIZ marker of the codestream, which lives in the item data. A decoder performs
  // its tile and coefficient arithmetic over the full reference grid, not just the
  // visible window (Xsiz-XOsiz, Ysiz-YOsiz), so the reference grid is the size the
  // decoder effectively allocates over and can be far larger than the container
  // 'ispe'. Parse it here so the shared decode path can reject over-limit inputs
  // before ANY J2K plugin (openjpeg, ffmpeg, ...) runs -- the openjpeg plugin has
  // its own equivalent gate (GHSA-q492-cfcm-895h), this makes the check
  // backend-independent.
  JPEG2000MainHeader header;
  Error err = header.parseHeader(compressed_data);
  if (err) {
    // Not a parseable codestream header; let the decoder plugin deal with it.
    return std::optional<ImageSize>{};
  }

  uint32_t w = header.getXSize();
  uint32_t h = header.getYSize();
  if (w == 0 || h == 0) {
    return std::optional<ImageSize>{};
  }

  return std::optional<ImageSize>{ImageSize{w, h}};
}


int Decoder_JPEG2000::get_luma_bits_per_pixel() const
{
  Result<std::vector<uint8_t>> imageDataResult = get_compressed_data(true);
  if (!imageDataResult) {
    return -1;
  }

  JPEG2000MainHeader header;
  Error err = header.parseHeader(*imageDataResult);
  if (err) {
    return -1;
  }
  return header.get_precision(0);
}


int Decoder_JPEG2000::get_chroma_bits_per_pixel() const
{
  Result<std::vector<uint8_t>> imageDataResult = get_compressed_data(true);
  if (!imageDataResult) {
    return -1;
  }

  JPEG2000MainHeader header;
  Error err = header.parseHeader(*imageDataResult);
  if (err) {
    return -1;
  }
  return header.get_precision(1);
}


Error Decoder_JPEG2000::get_coded_image_colorspace(heif_colorspace* out_colorspace, heif_chroma* out_chroma) const
{
#if 0
  *out_chroma = (heif_chroma) (m_hvcC->get_configuration().chroma_format);

  if (*out_chroma == heif_chroma_monochrome) {
    *out_colorspace = heif_colorspace_monochrome;
  }
  else {
    *out_colorspace = heif_colorspace_YCbCr;
  }
#endif

  *out_colorspace = heif_colorspace_YCbCr;
  *out_chroma = heif_chroma_444;

  return Error::Ok;
}
