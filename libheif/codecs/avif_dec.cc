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

#include "avif_dec.h"
#include "avif_boxes.h"
#include "error.h"
#include "context.h"

#include <string>


Result<std::vector<uint8_t>> Decoder_AVIF::read_bitstream_configuration_data() const
{
  std::vector<uint8_t> data;
  if (!m_av1C->get_headers(&data)) {
    return Error{heif_error_Invalid_input,
                 heif_suberror_No_item_data};
  }

  return data;
}


Result<std::optional<ImageSize>> Decoder_AVIF::get_max_coded_image_size(const std::vector<uint8_t>& compressed_data) const
{
  // The AV1 sequence header carries the coded frame size
  // (max_frame_width_minus_1 / max_frame_height_minus_1). Per the AVIF
  // specification it is present in the image item data, and it may additionally
  // be duplicated in the av1C configOBUs. A decoder allocates buffers for that
  // coded size, which can be far larger than the (possibly deliberately small)
  // 'ispe' dimensions on which the container-level size checks are based.
  //
  // `compressed_data` is the combined configOBUs + item data buffer that is about
  // to be pushed to the decoder plugin. Scan it for the largest sequence-header
  // frame size so the shared decode path can reject over-limit inputs before ANY
  // AV1 plugin (aom, dav1d, ffmpeg, ...) allocates a frame buffer.
  uint32_t max_width = 0;
  uint32_t max_height = 0;
  if (!find_max_av1_frame_size_in_stream(compressed_data.data(), compressed_data.size(),
                                         &max_width, &max_height)) {
    // No parseable sequence header (e.g. a non-sync frame in a sequence).
    return std::optional<ImageSize>{};
  }

  return std::optional<ImageSize>{ImageSize{max_width, max_height}};
}


int Decoder_AVIF::get_luma_bits_per_pixel() const
{
  Box_av1C::configuration config = m_av1C->get_configuration();
  if (!config.high_bitdepth) {
    return 8;
  }
  else if (config.twelve_bit) {
    return 12;
  }
  else {
    return 10;
  }
}


int Decoder_AVIF::get_chroma_bits_per_pixel() const
{
  return get_luma_bits_per_pixel();
}


Error Decoder_AVIF::get_coded_image_colorspace(heif_colorspace* out_colorspace, heif_chroma* out_chroma) const
{
  *out_chroma = m_av1C->get_configuration().get_heif_chroma();

  if (*out_chroma == heif_chroma_monochrome) {
    *out_colorspace = heif_colorspace_monochrome;
  }
  else {
    *out_colorspace = heif_colorspace_YCbCr;
  }

  return Error::Ok;
}
