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

#include "evc_dec.h"
#include "evc_boxes.h"
#include "error.h"
#include "context.h"

#include <string>


Result<std::vector<uint8_t>> Decoder_EVC::read_bitstream_configuration_data() const
{
  std::vector<uint8_t> data;
  m_evcC->get_header_nals(data);
  return data;
}


int Decoder_EVC::get_luma_bits_per_pixel() const
{
  return m_evcC->get_configuration().bit_depth_luma;
}


int Decoder_EVC::get_chroma_bits_per_pixel() const
{
  return m_evcC->get_configuration().bit_depth_chroma;
}


Error Decoder_EVC::get_coded_image_colorspace(heif_colorspace* out_colorspace, heif_chroma* out_chroma) const
{
  switch (m_evcC->get_configuration().chroma_format_idc) {
    case Box_evcC::CHROMA_FORMAT_MONOCHROME:
      *out_chroma = heif_chroma_monochrome;
      *out_colorspace = heif_colorspace_monochrome;
      return Error::Ok;
    case Box_evcC::CHROMA_FORMAT_420:
      *out_chroma = heif_chroma_420;
      *out_colorspace = heif_colorspace_YCbCr;
      return Error::Ok;
    case Box_evcC::CHROMA_FORMAT_422:
      *out_chroma = heif_chroma_422;
      *out_colorspace = heif_colorspace_YCbCr;
      return Error::Ok;
    case Box_evcC::CHROMA_FORMAT_444:
      *out_chroma = heif_chroma_444;
      *out_colorspace = heif_colorspace_YCbCr;
      return Error::Ok;
    default:
      *out_chroma = heif_chroma_undefined;
      *out_colorspace = heif_colorspace_undefined;
      return Error{heif_error_Invalid_input, heif_suberror_Decompression_invalid_data, "unsupported (impossible?) EVC chroma value"};
    }
}

