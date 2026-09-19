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

#include "avc_dec.h"
#include "avc_boxes.h"
#include "error.h"
#include "context.h"
#include "plugins/nalu_utils.h"

#include <algorithm>
#include <string>


Result<std::vector<uint8_t>> Decoder_AVC::read_bitstream_configuration_data() const
{
  std::vector<uint8_t> data;
  m_avcC->get_header_nals(data);

  return data;
}


int Decoder_AVC::get_luma_bits_per_pixel() const
{
  return m_avcC->get_configuration().bit_depth_luma;
}


int Decoder_AVC::get_chroma_bits_per_pixel() const
{
  return m_avcC->get_configuration().bit_depth_chroma;
}


Result<std::optional<ImageSize>> Decoder_AVC::get_max_coded_image_size(const std::vector<uint8_t>& compressed_data) const
{
  // `compressed_data` is the combined configuration + bitstream buffer about to be
  // pushed to the decoder. Scan it for every SPS NAL unit and return the largest coded picture
  // size any of them declares. An SPS carried in the item data (not just in avcC)
  // drives the decoder's buffer allocation and can be far larger than the
  // container 'ispe', so the config record alone is not a sufficient gate.
  bool found = false;
  uint32_t max_width = 0;
  uint32_t max_height = 0;

  for (const auto& nal : split_nal_units_4byte_length_prefixed(compressed_data.data(), compressed_data.size())) {
    const uint8_t* nal_data = nal.first;
    size_t nal_size = nal.second;

    // AVC NAL unit header (1 byte): forbidden_zero_bit(1), nal_ref_idc(2), nal_unit_type(5)
    if (nal_size < 1) {
      continue;
    }
    int nal_type = nal_data[0] & 0x1F;
    if (nal_type != AVC_NAL_UNIT_SPS_NUT) {
      continue;
    }

    Box_avcC::configuration scratch = m_avcC->get_configuration();
    uint32_t cropped_w = 0, cropped_h = 0;
    ImageSize coded{};
    Error e = parse_sps_for_avcC_configuration(nal_data, nal_size, &scratch,
                                               &cropped_w, &cropped_h, &coded);
    if (e) {
      // A malformed SPS we cannot parse is skipped rather than failing the whole
      // decode; the decoder plugin applies its own limits when it reaches it.
      continue;
    }

    found = true;
    max_width = std::max(max_width, coded.width);
    max_height = std::max(max_height, coded.height);
  }

  if (!found) {
    return std::optional<ImageSize>{};
  }

  return std::optional<ImageSize>{ImageSize{max_width, max_height}};
}


Error Decoder_AVC::get_coded_image_colorspace(heif_colorspace* out_colorspace, heif_chroma* out_chroma) const
{
  *out_chroma = m_avcC->get_configuration().chroma_format;

  if (*out_chroma == heif_chroma_monochrome) {
    *out_colorspace = heif_colorspace_monochrome;
  }
  else {
    *out_colorspace = heif_colorspace_YCbCr;
  }

  return Error::Ok;
}
