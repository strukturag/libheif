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

#include <algorithm>
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


Error Decoder_AVIF::prepare_compressed_data_for_decoding(std::vector<uint8_t>& compressed_data,
                                                         size_t configuration_data_size) const
{
  return normalize_av1_sample_data(compressed_data, configuration_data_size);
}


Result<std::optional<ImageSize>> Decoder_AVIF::get_max_coded_image_size(const std::vector<uint8_t>& compressed_data,
                                                                       size_t configuration_data_size) const
{
  // The AV1 sequence header carries the coded frame size
  // (max_frame_width_minus_1 / max_frame_height_minus_1). Per the AVIF
  // specification it is present in the image item data, and it may additionally
  // be duplicated in the av1C configOBUs. A decoder allocates buffers for that
  // coded size, which can be far larger than the (possibly deliberately small)
  // 'ispe' dimensions on which the container-level size checks are based.
  //
  // `compressed_data` is the combined configOBUs + item/sample data buffer that
  // is about to be pushed to the decoder plugin. Scan both regions for the
  // largest sequence-header frame size so the shared decode path can reject
  // over-limit inputs before ANY AV1 plugin allocates a frame buffer. They are
  // scanned separately because the sample boundary must not depend on the
  // contents of configOBUs. prepare_compressed_data_for_decoding() has already
  // made any terminal unsized sample OBU explicit, so every OBU is now sized.
  if (configuration_data_size > compressed_data.size()) {
    return Error{heif_error_Invalid_input,
                 heif_suberror_Unspecified,
                 "AV1 stream was rejected because its configuration-data size is invalid."};
  }

  bool found = false;
  uint32_t max_width = 0;
  uint32_t max_height = 0;

  auto scan_region = [&](size_t offset, size_t size) -> Error {
    if (size == 0) {
      return Error::Ok;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    auto result = find_max_av1_frame_size_in_stream(compressed_data.data() + offset, size,
                                                    &width, &height);
    if (!result) {
      return result.error();
    }
    if (*result) {
      found = true;
      max_width = std::max(max_width, width);
      max_height = std::max(max_height, height);
    }
    return Error::Ok;
  };

  Error err = scan_region(0, configuration_data_size);
  if (err) {
    return err;
  }

  err = scan_region(configuration_data_size,
                    compressed_data.size() - configuration_data_size);
  if (err) {
    return err;
  }

  if (!found) {
    // No parseable sequence header (e.g. a non-sync frame in a sequence).
    return std::optional<ImageSize>{};
  }

  return std::optional<ImageSize>{ImageSize{max_width, max_height}};
}


Result<uint32_t> Decoder_AVIF::get_number_of_coded_pictures(const std::vector<uint8_t>& compressed_data,
                                                             size_t configuration_data_size) const
{
  if (configuration_data_size > compressed_data.size()) {
    return Error{heif_error_Invalid_input, heif_suberror_Unspecified,
                 "AV1 stream was rejected because its configuration-data size is invalid."};
  }
  uint32_t total = 0;
  auto scan_region = [&](size_t offset, size_t size) -> Error {
    uint32_t width = 0, height = 0, count = 0;
    auto result = find_max_av1_frame_size_in_stream(compressed_data.data() + offset, size,
                                                    &width, &height, &count);
    if (!result) { return result.error(); }
    total += count;
    return Error::Ok;
  };
  Error err = scan_region(0, configuration_data_size);
  if (err) { return err; }
  err = scan_region(configuration_data_size,
                    compressed_data.size() - configuration_data_size);
  if (err) { return err; }
  return total;
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
