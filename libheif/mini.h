/*
 * HEIF codec.
 * Copyright (c) 2024 Brad Hards <bradh@frogmouth.net>
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

#ifndef LIBHEIF_MINI_H
#define LIBHEIF_MINI_H

#include "libheif/heif.h"
#include "box.h"


class Box_mini : public Box
{
public:
  Box_mini()
  {
    set_short_type(fourcc("mini"));
  }

  bool get_exif_flag() const { return m_exif_flag; }
  bool get_xmp_flag() const { return m_xmp_flag; }

  uint32_t get_width() const { return m_width; }
  uint32_t get_height() const { return m_height; }

  std::vector<uint8_t> get_main_item_codec_config() const { return m_main_item_codec_config; }

  uint64_t get_main_item_data_offset() const { return m_main_item_data_offset; }
  uint32_t get_main_item_data_size() const { return m_main_item_data_size; }
  uint64_t get_alpha_item_data_offset() const { return m_alpha_item_data_offset; }
  uint32_t get_alpha_item_data_size() const { return m_alpha_item_data_size; }
  uint64_t get_exif_item_data_offset() const { return m_exif_item_data_offset; }
  uint32_t get_exif_item_data_size() const { return m_exif_item_data_size; }
  uint64_t get_xmp_item_data_offset() const { return m_xmp_item_data_offset; }
  uint32_t get_xmp_item_data_size() const { return m_xmp_item_data_size; }

  uint16_t get_colour_primaries() const { return m_colour_primaries; }
  uint16_t get_transfer_characteristics() const { return m_transfer_characteristics; }
  uint16_t get_matrix_coefficients() const { return m_matrix_coefficients; }
  bool get_full_range_flag() const { return m_full_range_flag; }

  std::string dump(Indent&) const override;

protected:
  Error parse(BitstreamRange& range) override;

private:
  uint8_t m_version;
  bool m_explicit_codec_types_flag;
  bool m_float_flag;
  bool m_full_range_flag;
  bool m_alpha_flag;
  bool m_explicit_cicp_flag;
  bool m_hdr_flag;
  bool m_icc_flag;
  bool m_exif_flag;
  bool m_xmp_flag;
  uint8_t m_chroma_subsampling;
  uint8_t m_orientation;

  uint32_t m_width;
  uint32_t m_height;
  uint8_t m_bit_depth = 8;
  bool m_chroma_is_horizontally_centred = false;
  bool m_chroma_is_vertically_centred = false;
  bool m_alpha_is_premultiplied = false;
  uint16_t m_colour_primaries;
  uint16_t m_transfer_characteristics;
  uint16_t m_matrix_coefficients;

  bool m_gainmap_flag = false;
  bool m_tmap_icc_flag = false;

  std::vector<uint8_t> m_alpha_item_codec_config;
  std::vector<uint8_t> m_gainmap_item_codec_config;
  std::vector<uint8_t> m_main_item_codec_config;
  std::vector<uint8_t> m_icc_data;
  std::vector<uint8_t> m_tmap_icc_data;
  std::vector<uint8_t> m_gainmap_metadata;
  std::vector<uint8_t> m_gainmap_item_data;

  uint64_t m_alpha_item_data_offset;
  uint32_t m_alpha_item_data_size;
  uint64_t m_main_item_data_offset;
  uint32_t m_main_item_data_size;
  uint64_t m_exif_item_data_offset;
  uint32_t m_exif_item_data_size;
  uint64_t m_xmp_item_data_offset;
  uint32_t m_xmp_item_data_size;

};

#endif
