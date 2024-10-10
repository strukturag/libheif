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

#include "mini.h"

#include <cmath>
#include <string>
#include <vector>

Error Box_mini::parse(BitstreamRange& range, const heif_security_limits* limits)
{
  uint64_t start_offset = range.get_istream()->get_position();
  size_t length = range.get_remaining_bytes();
  std::vector<uint8_t> mini_data(length);
  range.read(mini_data.data(), mini_data.size());
  BitReader bits(mini_data.data(), (int)(mini_data.size()));
  bits.set_start_offset(start_offset);
  m_version = bits.get_bits8(2);
  m_explicit_codec_types_flag = bits.get_flag();
  m_float_flag = bits.get_flag();
  m_full_range_flag = bits.get_flag();
  m_alpha_flag = bits.get_flag();
  m_explicit_cicp_flag = bits.get_flag();
  m_hdr_flag = bits.get_flag();
  m_icc_flag = bits.get_flag();
  m_exif_flag = bits.get_flag();
  m_xmp_flag = bits.get_flag();
  m_chroma_subsampling = bits.get_bits8(2);
  m_orientation = bits.get_bits8(3) + 1;
  bool small_dimensions_flag = bits.get_flag();
  if (small_dimensions_flag) {
    m_width = 1 + bits.get_bits32(7);
    m_height = 1 + bits.get_bits32(7);
  } else {
    m_width = 1 + bits.get_bits32(15);
    m_height = 1 + bits.get_bits32(15);
  }
  if ((m_chroma_subsampling == 1) || (m_chroma_subsampling == 2)) {
    m_chroma_is_horizontally_centred = bits.get_flag();
  }
  if (m_chroma_subsampling == 1) {
    m_chroma_is_vertically_centred = bits.get_flag();
  }

  bool high_bit_depth_flag = false;
  if (m_float_flag) {
    uint8_t bit_depth_log2_minus4 = bits.get_bits8(2);
    m_bit_depth = (uint8_t)powl(2, (bit_depth_log2_minus4 + 4));
  } else {
    high_bit_depth_flag = bits.get_flag();
    if (high_bit_depth_flag) {
      m_bit_depth = 9 + bits.get_bits8(3);
    }
  }

  if (m_alpha_flag) {
    m_alpha_is_premultiplied = bits.get_flag();
  }

  if (m_explicit_cicp_flag) {
    m_colour_primaries = bits.get_bits8(8);
    m_transfer_characteristics = bits.get_bits8(8);
    if (m_chroma_subsampling != 0) {
      m_matrix_coefficients = bits.get_bits8(8);
    } else {
      m_matrix_coefficients = 2;
    }
  } else {
    m_colour_primaries = m_icc_flag ? 2 : 1;
    m_transfer_characteristics = m_icc_flag ? 2 : 13;
    m_matrix_coefficients = (m_chroma_subsampling == 0) ? 2 : 6;
  }

  if (m_explicit_codec_types_flag) {
    assert(false);
  }
  if (m_hdr_flag) {
    m_gainmap_flag = bits.get_flag();
    // TODO
    assert(false);
  }
  bool few_metadata_bytes_flag = false;
  if (m_icc_flag || m_exif_flag || m_xmp_flag || (m_hdr_flag && m_gainmap_flag)) {
    few_metadata_bytes_flag = bits.get_flag();
  }
  bool few_codec_config_bytes_flag = bits.get_flag();
  bool few_item_data_bytes_flag = bits.get_flag();

  uint32_t icc_data_size_minus1;
  if (m_icc_flag) {
    icc_data_size_minus1 = bits.get_bits32(few_metadata_bytes_flag ? 10 : 20);
  }
  uint32_t tmap_icc_data_size_minus1;
  if (m_hdr_flag && m_gainmap_flag && m_tmap_icc_flag) {
    tmap_icc_data_size_minus1 = bits.get_bits32(few_metadata_bytes_flag ? 10 : 20);
  }
  uint32_t gainmap_metadata_size = 0;
  if (m_hdr_flag && m_gainmap_flag) {
    gainmap_metadata_size = bits.get_bits32(few_metadata_bytes_flag ? 10 : 20);
  }
  uint32_t gainmap_item_data_size = 0;
  if (m_hdr_flag && m_gainmap_flag) {
    gainmap_item_data_size = bits.get_bits32(few_item_data_bytes_flag ? 15 : 28);
  }
  uint32_t gainmap_item_codec_config_size = 0;
  if (m_hdr_flag && m_gainmap_flag && (gainmap_item_data_size > 0)) {
    gainmap_item_codec_config_size = bits.get_bits32(few_codec_config_bytes_flag ? 3 : 12);
  }

  uint32_t main_item_codec_config_size = bits.get_bits32(few_codec_config_bytes_flag ? 3 : 12);
  uint32_t main_item_data_size_minus1 = bits.get_bits32(few_item_data_bytes_flag ? 15 : 28);

  m_alpha_item_data_size = 0;
  if (m_alpha_flag) {
    m_alpha_item_data_size = bits.get_bits32(few_item_data_bytes_flag ? 15 : 28);
  }
  uint32_t alpha_item_codec_config_size = 0;
  if (m_alpha_flag && (m_alpha_item_data_size > 0)) {
    alpha_item_codec_config_size = bits.get_bits32(few_codec_config_bytes_flag ? 3 : 12);
  }

  uint32_t exif_data_size_minus1 = 0;
  if (m_exif_flag) {
    exif_data_size_minus1 = bits.get_bits32(few_metadata_bytes_flag ? 10 : 20);
  }
  uint32_t xmp_data_size_minus1 = 0;
  if (m_xmp_flag) {
    xmp_data_size_minus1 = bits.get_bits32(few_metadata_bytes_flag ? 10 : 20);
  }

  bits.skip_to_byte_boundary();

  if (m_alpha_flag && (m_alpha_item_data_size > 0) && (alpha_item_codec_config_size > 0)) {
    m_alpha_item_codec_config = bits.read_bytes(alpha_item_codec_config_size);
  }
  if (m_hdr_flag && m_gainmap_flag && (gainmap_item_codec_config_size > 0)) {
    m_gainmap_item_codec_config = bits.read_bytes(gainmap_item_codec_config_size);
  }
  if (main_item_codec_config_size > 0) {
    m_main_item_codec_config = bits.read_bytes(main_item_codec_config_size);
  }

  if (m_icc_flag) {
    m_icc_data = bits.read_bytes(icc_data_size_minus1 + 1);
  }
  if (m_hdr_flag && m_gainmap_flag && m_tmap_icc_flag) {
    m_tmap_icc_data = bits.read_bytes(tmap_icc_data_size_minus1 + 1);
  }
  if (m_hdr_flag && m_gainmap_flag && (gainmap_metadata_size > 0)) {
    m_gainmap_metadata = bits.read_bytes(gainmap_metadata_size);
  }

  if (m_alpha_flag && (m_alpha_item_data_size > 0)) {
    m_alpha_item_data_offset = bits.get_file_offset();
  }
  if (m_alpha_flag && m_gainmap_flag && (gainmap_item_data_size > 0)) {
    m_gainmap_item_data = bits.read_bytes(gainmap_item_data_size);
  }

  m_main_item_data_offset = bits.get_file_offset();
  m_main_item_data_size = main_item_data_size_minus1 + 1;
  bits.skip_bytes(m_main_item_data_size);

  if (m_exif_flag) {
    m_exif_item_data_offset = bits.get_file_offset();
    m_exif_item_data_size = exif_data_size_minus1 + 1;
  }
  if (m_xmp_flag) {
    m_xmp_item_data_offset = bits.get_file_offset();
    m_xmp_item_data_size = xmp_data_size_minus1 + 1;
  }
  return range.get_error();
}

std::string Box_mini::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << indent << "version: " << (int) m_version << "\n";

  sstr << indent << "explicit_codec_types_flag: " << m_explicit_codec_types_flag << "\n";
  sstr << indent << "float_flag: " << m_float_flag << "\n";
  sstr << indent << "full_range_flag: " << m_full_range_flag << "\n";
  sstr << indent << "alpha_flag: " << m_alpha_flag << "\n";
  sstr << indent << "explicit_cicp_flag: " << m_explicit_cicp_flag << "\n";
  sstr << indent << "hdr_flag: " << m_hdr_flag << "\n";
  sstr << indent << "icc_flag: " << m_icc_flag << "\n";
  sstr << indent << "exif_flag: " << m_exif_flag << "\n";
  sstr << indent << "xmp_flag: " << m_xmp_flag << "\n";

  sstr << indent << "chroma_subsampling: " << (int)m_chroma_subsampling << "\n";
  sstr << indent << "orientation: " << (int)m_orientation << "\n";

  sstr << indent << "width: " << m_width << "\n";
  sstr << indent << "height: " << m_height << "\n";

  if ((m_chroma_subsampling == 1) || (m_chroma_subsampling == 2))
  {
    sstr << indent << "chroma_is_horizontally_centered: " << m_chroma_is_horizontally_centred << "\n";
  }
  if (m_chroma_subsampling == 1)
  {
    sstr << indent << "chroma_is_vertically_centered: " << m_chroma_is_vertically_centred << "\n";
  }

  sstr << "bit_depth: " << (int)m_bit_depth << "\n";

  if (m_alpha_flag)
  {
    sstr << "alpha_is_premultiplied: " << m_alpha_is_premultiplied << "\n";
  }

  sstr << "colour_primaries: " << (int) m_colour_primaries << "\n";
  sstr << "transfer_characteristics: " << (int) m_transfer_characteristics << "\n";
  sstr << "matrix_coefficients: " << (int) m_matrix_coefficients << "\n";

  // TODO: if explicit_codec_types_flag

  // TODO: hdr_flag + gainmap_flag

  // TODO: sizes

  return sstr.str();
}
