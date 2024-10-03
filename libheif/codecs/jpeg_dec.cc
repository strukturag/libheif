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

#include "jpeg_dec.h"
#include "jpeg.h"
#include "error.h"
#include "context.h"

#include <string>


Result<std::vector<uint8_t>> Decoder_JPEG::read_bitstream_configuration_data() const
{
  if (m_jpgC) {
    return m_jpgC->get_data();
  }
  else {
    return std::vector<uint8_t>{};
  }
}


// This checks whether a start code FFCx with nibble 'x' is a SOF marker.
// E.g. FFC0-FFC3 are, while FFC4 is not.
static bool isSOF[16] = {true, true, true, true, false, true, true, true,
                         false, true, true, true, false, true, true, true};

int Decoder_JPEG::get_luma_bits_per_pixel() const
{
  // image data, usually from 'mdat'

  auto dataResult = get_compressed_data();
  if (dataResult.error) {
    return dataResult.error;
  }

  const std::vector<uint8_t>& data = dataResult.value;

  for (size_t i = 0; i + 1 < data.size(); i++) {
    if (data[i] == 0xFF && (data[i + 1] & 0xF0) == 0xC0 && isSOF[data[i + 1] & 0x0F]) {
      i += 4;
      if (i < data.size()) {
        return data[i];
      }
      else {
        return -1;
      }
    }
  }

  return -1;
}


int Decoder_JPEG::get_chroma_bits_per_pixel() const
{
  return get_luma_bits_per_pixel();
}


Error Decoder_JPEG::get_coded_image_colorspace(heif_colorspace* out_colorspace, heif_chroma* out_chroma) const
{
  //*out_chroma = (heif_chroma) (m_jpgC->);

  *out_chroma = heif_chroma_420; // TODO

  if (*out_chroma == heif_chroma_monochrome) {
    *out_colorspace = heif_colorspace_monochrome;
  }
  else {
    *out_colorspace = heif_colorspace_YCbCr;
  }

  return Error::Ok;
}
