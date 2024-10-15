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

#include "codecs/uncompressed/unc_dec.h"
#include "codecs/uncompressed/unc_codec.h"
#include "error.h"
#include "context.h"

#include <string>
#include <algorithm>


Result<std::vector<uint8_t>> Decoder_uncompressed::read_bitstream_configuration_data() const
{
  return std::vector<uint8_t>{};
}


int Decoder_uncompressed::get_luma_bits_per_pixel() const
{
  assert(m_uncC);

  if (!m_cmpd) {
    if (isKnownUncompressedFrameConfigurationBoxProfile(m_uncC)) {
      return 8;
    }
    else {
      return -1;
    }
  }

  int luma_bits = 0;
  int alternate_channel_bits = 0;
  for (Box_uncC::Component component : m_uncC->get_components()) {
    uint16_t component_index = component.component_index;
    if (component_index >= m_cmpd->get_components().size()) {
      return -1;
    }
    auto component_type = m_cmpd->get_components()[component_index].component_type;
    switch (component_type) {
      case component_type_monochrome:
      case component_type_red:
      case component_type_green:
      case component_type_blue:
      case component_type_filter_array:
        alternate_channel_bits = std::max(alternate_channel_bits, (int) component.component_bit_depth);
        break;
      case component_type_Y:
        luma_bits = std::max(luma_bits, (int) component.component_bit_depth);
        break;
        // TODO: there are other things we'll need to handle eventually, like palette.
    }
  }
  if (luma_bits > 0) {
    return luma_bits;
  }
  else if (alternate_channel_bits > 0) {
    return alternate_channel_bits;
  }
  else {
    return 8;
  }
}

int Decoder_uncompressed::get_chroma_bits_per_pixel() const
{
  if (m_uncC && m_uncC->get_version() == 1) {
    // All of the version 1 cases are 8 bit
    return 8;
  }

  if (!m_uncC || !m_cmpd) {
    return -1;
  }

  int chroma_bits = 0;
  int alternate_channel_bits = 0;
  for (Box_uncC::Component component : m_uncC->get_components()) {
    uint16_t component_index = component.component_index;
    if (component_index >= m_cmpd->get_components().size()) {
      return -1;
    }
    auto component_type = m_cmpd->get_components()[component_index].component_type;
    switch (component_type) {
      case component_type_monochrome:
      case component_type_red:
      case component_type_green:
      case component_type_blue:
      case component_type_filter_array:
        alternate_channel_bits = std::max(alternate_channel_bits, (int) component.component_bit_depth);
        break;
      case component_type_Cb:
      case component_type_Cr:
        chroma_bits = std::max(chroma_bits, (int) component.component_bit_depth);
        break;
        // TODO: there are other things we'll need to handle eventually, like palette.
    }
  }
  if (chroma_bits > 0) {
    return chroma_bits;
  }
  else if (alternate_channel_bits > 0) {
    return alternate_channel_bits;
  }
  else {
    return 8;
  }
}


Error Decoder_uncompressed::get_coded_image_colorspace(heif_colorspace* out_colorspace, heif_chroma* out_chroma) const
{
  if (m_uncC->get_version() == 1) {
    // This is the shortform case, no cmpd box, and always some kind of RGB
    *out_colorspace = heif_colorspace_RGB;
    if (m_uncC->get_profile() == fourcc("rgb3")) {
      *out_chroma = heif_chroma_interleaved_RGB;
    }
    else if ((m_uncC->get_profile() == fourcc("rgba")) || (m_uncC->get_profile() == fourcc("abgr"))) {
      *out_chroma = heif_chroma_interleaved_RGBA;
    }

    return Error::Ok;
  }
  else if (m_cmpd) {
    UncompressedImageCodec::get_heif_chroma_uncompressed(m_uncC, m_cmpd, out_chroma, out_colorspace);
    return Error::Ok;
  }
  else {
    return {heif_error_Invalid_input,
            heif_suberror_Unspecified,
            "Missing 'cmpd' box."};
  }
}
