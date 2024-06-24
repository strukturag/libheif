/*
 * HEIF codec.
 * Copyright (c) 2017 Dirk Farin <dirk.farin@gmail.com>
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

#include "hevc.h"
#include "bitstream.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <string>
#include <utility>

Error Box_hvcC::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  uint8_t byte;

  auto& c = m_configuration; // abbreviation

  c.configuration_version = range.read8();
  byte = range.read8();
  c.general_profile_space = (byte >> 6) & 3;
  c.general_tier_flag = (byte >> 5) & 1;
  c.general_profile_idc = (byte & 0x1F);

  c.general_profile_compatibility_flags = range.read32();

  for (int i = 0; i < 6; i++) {
    byte = range.read8();

    for (int b = 0; b < 8; b++) {
      c.general_constraint_indicator_flags[i * 8 + b] = (byte >> (7 - b)) & 1;
    }
  }

  c.general_level_idc = range.read8();
  c.min_spatial_segmentation_idc = range.read16() & 0x0FFF;
  c.parallelism_type = range.read8() & 0x03;
  c.chroma_format = range.read8() & 0x03;
  c.bit_depth_luma = static_cast<uint8_t>((range.read8() & 0x07) + 8);
  c.bit_depth_chroma = static_cast<uint8_t>((range.read8() & 0x07) + 8);
  c.avg_frame_rate = range.read16();

  byte = range.read8();
  c.constant_frame_rate = (byte >> 6) & 0x03;
  c.num_temporal_layers = (byte >> 3) & 0x07;
  c.temporal_id_nested = (byte >> 2) & 1;

  m_length_size = static_cast<uint8_t>((byte & 0x03) + 1);

  int nArrays = range.read8();

  for (int i = 0; i < nArrays && !range.error(); i++) {
    byte = range.read8();

    NalArray array;

    array.m_array_completeness = (byte >> 6) & 1;
    array.m_NAL_unit_type = (byte & 0x3F);

    int nUnits = range.read16();
    for (int u = 0; u < nUnits && !range.error(); u++) {

      std::vector<uint8_t> nal_unit;
      int size = range.read16();
      if (!size) {
        // Ignore empty NAL units.
        continue;
      }

      if (range.prepare_read(size)) {
        nal_unit.resize(size);
        bool success = range.get_istream()->read((char*) nal_unit.data(), size);
        if (!success) {
          return Error{heif_error_Invalid_input, heif_suberror_End_of_data, "error while reading hvcC box"};
        }
      }

      array.m_nal_units.push_back(std::move(nal_unit));
    }

    m_nal_array.push_back(std::move(array));
  }

  range.skip_to_end_of_box();

  return range.get_error();
}


std::string Box_hvcC::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  const auto& c = m_configuration; // abbreviation

  sstr << indent << "configuration_version: " << ((int) c.configuration_version) << "\n"
       << indent << "general_profile_space: " << ((int) c.general_profile_space) << "\n"
       << indent << "general_tier_flag: " << c.general_tier_flag << "\n"
       << indent << "general_profile_idc: " << ((int) c.general_profile_idc) << "\n";

  sstr << indent << "general_profile_compatibility_flags: ";
  for (int i = 0; i < 32; i++) {
    sstr << ((c.general_profile_compatibility_flags >> (31 - i)) & 1);
    if ((i % 8) == 7) sstr << ' ';
    else if ((i % 4) == 3) sstr << '.';
  }
  sstr << "\n";

  sstr << indent << "general_constraint_indicator_flags: ";
  int cnt = 0;
  for (int i = 0; i < configuration::NUM_CONSTRAINT_INDICATOR_FLAGS; i++) {
    bool b = c.general_constraint_indicator_flags[i];

    sstr << (b ? 1 : 0);
    cnt++;
    if ((cnt % 8) == 0)
      sstr << ' ';
  }
  sstr << "\n";

  sstr << indent << "general_level_idc: " << ((int) c.general_level_idc) << "\n"
       << indent << "min_spatial_segmentation_idc: " << c.min_spatial_segmentation_idc << "\n"
       << indent << "parallelism_type: " << ((int) c.parallelism_type) << "\n"
       << indent << "chroma_format: ";

  switch (c.chroma_format) {
    case 1:
      sstr << "4:2:0";
      break;
    case 2:
      sstr << "4:2:2";
      break;
    case 3:
      sstr << "4:4:4";
      break;
    default:
      sstr << ((int) c.chroma_format);
      break;
  }

  sstr << "\n"
       << indent << "bit_depth_luma: " << ((int) c.bit_depth_luma) << "\n"
       << indent << "bit_depth_chroma: " << ((int) c.bit_depth_chroma) << "\n"
       << indent << "avg_frame_rate: " << c.avg_frame_rate << "\n"
       << indent << "constant_frame_rate: " << ((int) c.constant_frame_rate) << "\n"
       << indent << "num_temporal_layers: " << ((int) c.num_temporal_layers) << "\n"
       << indent << "temporal_id_nested: " << ((int) c.temporal_id_nested) << "\n"
       << indent << "length_size: " << ((int) m_length_size) << "\n";

  for (const auto& array : m_nal_array) {
    sstr << indent << "<array>\n";

    indent++;
    sstr << indent << "array_completeness: " << ((int) array.m_array_completeness) << "\n"
         << indent << "NAL_unit_type: " << ((int) array.m_NAL_unit_type) << "\n";

    for (const auto& unit : array.m_nal_units) {
      //sstr << "  unit with " << unit.size() << " bytes of data\n";
      sstr << indent;
      for (uint8_t b : unit) {
        sstr << std::setfill('0') << std::setw(2) << std::hex << ((int) b) << " ";
      }
      sstr << "\n";
      sstr << std::dec;
    }

    indent--;
  }

  return sstr.str();
}


bool Box_hvcC::get_headers(std::vector<uint8_t>* dest) const
{
  for (const auto& array : m_nal_array) {
    for (const auto& unit : array.m_nal_units) {

      dest->push_back((unit.size() >> 24) & 0xFF);
      dest->push_back((unit.size() >> 16) & 0xFF);
      dest->push_back((unit.size() >> 8) & 0xFF);
      dest->push_back((unit.size() >> 0) & 0xFF);

      /*
      dest->push_back(0);
      dest->push_back(0);
      dest->push_back(1);
      */

      dest->insert(dest->end(), unit.begin(), unit.end());
    }
  }

  return true;
}


void Box_hvcC::append_nal_data(const std::vector<uint8_t>& nal)
{
  NalArray array;
  array.m_array_completeness = 0;
  array.m_NAL_unit_type = uint8_t(nal[0] >> 1);
  array.m_nal_units.push_back(nal);

  m_nal_array.push_back(array);
}

void Box_hvcC::append_nal_data(const uint8_t* data, size_t size)
{
  std::vector<uint8_t> nal;
  nal.resize(size);
  memcpy(nal.data(), data, size);

  NalArray array;
  array.m_array_completeness = 0;
  array.m_NAL_unit_type = uint8_t(nal[0] >> 1);
  array.m_nal_units.push_back(std::move(nal));

  m_nal_array.push_back(array);
}


Error Box_hvcC::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  const auto& c = m_configuration; // abbreviation

  writer.write8(c.configuration_version);

  writer.write8((uint8_t) (((c.general_profile_space & 3) << 6) |
                           ((c.general_tier_flag & 1) << 5) |
                           (c.general_profile_idc & 0x1F)));

  writer.write32(c.general_profile_compatibility_flags);

  for (int i = 0; i < 6; i++) {
    uint8_t byte = 0;

    for (int b = 0; b < 8; b++) {
      if (c.general_constraint_indicator_flags[i * 8 + b]) {
        byte |= 1;
      }

      byte = (uint8_t) (byte << 1);
    }

    writer.write8(byte);
  }

  writer.write8(c.general_level_idc);
  writer.write16((c.min_spatial_segmentation_idc & 0x0FFF) | 0xF000);
  writer.write8(c.parallelism_type | 0xFC);
  writer.write8(c.chroma_format | 0xFC);
  writer.write8((uint8_t) ((c.bit_depth_luma - 8) | 0xF8));
  writer.write8((uint8_t) ((c.bit_depth_chroma - 8) | 0xF8));
  writer.write16(c.avg_frame_rate);

  writer.write8((uint8_t) (((c.constant_frame_rate & 0x03) << 6) |
                           ((c.num_temporal_layers & 0x07) << 3) |
                           ((c.temporal_id_nested & 1) << 2) |
                           ((m_length_size - 1) & 0x03)));

  size_t nArrays = m_nal_array.size();
  if (nArrays > 0xFF) {
    // TODO: error: too many NAL units
  }

  writer.write8((uint8_t) nArrays);

  for (const NalArray& array : m_nal_array) {

    writer.write8((uint8_t) (((array.m_array_completeness & 1) << 6) |
                             (array.m_NAL_unit_type & 0x3F)));

    size_t nUnits = array.m_nal_units.size();
    if (nUnits > 0xFFFF) {
      // TODO: error: too many NAL units
    }

    writer.write16((uint16_t) nUnits);

    for (const std::vector<uint8_t>& nal_unit : array.m_nal_units) {
      writer.write16((uint16_t) nal_unit.size());
      writer.write(nal_unit);
    }
  }

  prepend_header(writer, box_start);

  return Error::Ok;
}


static double read_depth_rep_info_element(BitReader& reader)
{
  int sign_flag = reader.get_bits(1);
  int exponent = reader.get_bits(7);
  int mantissa_len = reader.get_bits(5) + 1;
  if (mantissa_len < 1 || mantissa_len > 32) {
    // TODO err
  }

  if (exponent == 127) {
    // TODO value unspecified
  }

  int mantissa = reader.get_bits(mantissa_len);
  double value;

  //printf("sign:%d exponent:%d mantissa_len:%d mantissa:%d\n",sign_flag,exponent,mantissa_len,mantissa);

  if (exponent > 0) {
    value = pow(2.0, exponent - 31) * (1.0 + mantissa / pow(2.0, mantissa_len));
  }
  else {
    value = pow(2.0, -(30 + mantissa_len)) * mantissa;
  }

  if (sign_flag) {
    value = -value;
  }

  return value;
}


static std::shared_ptr<SEIMessage> read_depth_representation_info(BitReader& reader)
{
  auto msg = std::make_shared<SEIMessage_depth_representation_info>();


  // default values

  msg->version = 1;

  msg->disparity_reference_view = 0;
  msg->depth_nonlinear_representation_model_size = 0;
  msg->depth_nonlinear_representation_model = nullptr;


  // read header

  msg->has_z_near = (uint8_t) reader.get_bits(1);
  msg->has_z_far = (uint8_t) reader.get_bits(1);
  msg->has_d_min = (uint8_t) reader.get_bits(1);
  msg->has_d_max = (uint8_t) reader.get_bits(1);

  int rep_type;
  if (!reader.get_uvlc(&rep_type)) {
    // TODO error
  }
  // TODO: check rep_type range
  msg->depth_representation_type = (enum heif_depth_representation_type) rep_type;

  //printf("flags: %d %d %d %d\n",msg->has_z_near,msg->has_z_far,msg->has_d_min,msg->has_d_max);
  //printf("type: %d\n",rep_type);

  if (msg->has_d_min || msg->has_d_max) {
    int ref_view;
    if (!reader.get_uvlc(&ref_view)) {
      // TODO error
    }
    msg->disparity_reference_view = ref_view;

    //printf("ref_view: %d\n",msg->disparity_reference_view);
  }

  if (msg->has_z_near) msg->z_near = read_depth_rep_info_element(reader);
  if (msg->has_z_far) msg->z_far = read_depth_rep_info_element(reader);
  if (msg->has_d_min) msg->d_min = read_depth_rep_info_element(reader);
  if (msg->has_d_max) msg->d_max = read_depth_rep_info_element(reader);

  /*
  printf("z_near: %f\n",msg->z_near);
  printf("z_far: %f\n",msg->z_far);
  printf("dmin: %f\n",msg->d_min);
  printf("dmax: %f\n",msg->d_max);
  */

  if (msg->depth_representation_type == heif_depth_representation_type_nonuniform_disparity) {
    // TODO: load non-uniform response curve
  }

  return msg;
}


// aux subtypes: 00 00 00 11 / 00 00 00 0d / 4e 01 / b1 09 / 35 1e 78 c8 01 03 c5 d0 20

Error decode_hevc_aux_sei_messages(const std::vector<uint8_t>& data,
                                   std::vector<std::shared_ptr<SEIMessage>>& msgs)
{
  // TODO: we probably do not need a full BitReader just for the array size.
  // Read this and the NAL size directly on the array data.

  BitReader reader(data.data(), (int) data.size());
  uint32_t len = (uint32_t) reader.get_bits(32);

  if (len > data.size() - 4) {
    // ERROR: read past end of data
  }

  while (reader.get_current_byte_index() < (int) len) {
    int currPos = reader.get_current_byte_index();
    BitReader sei_reader(data.data() + currPos, (int) data.size() - currPos);

    uint32_t nal_size = (uint32_t) sei_reader.get_bits(32);
    (void) nal_size;

    uint8_t nal_type = (uint8_t) (sei_reader.get_bits(8) >> 1);
    sei_reader.skip_bits(8);

    // SEI

    if (nal_type == 39 ||
        nal_type == 40) {

      // TODO: loading of multi-byte sei headers
      uint8_t payload_id = (uint8_t) (sei_reader.get_bits(8));
      uint8_t payload_size = (uint8_t) (sei_reader.get_bits(8));
      (void) payload_size;

      switch (payload_id) {
        case 177: // depth_representation_info
          std::shared_ptr<SEIMessage> sei = read_depth_representation_info(sei_reader);
          msgs.push_back(sei);
          break;
      }
    }

    break; // TODO: read next SEI
  }


  return Error::Ok;
}


static std::vector<uint8_t> remove_start_code_emulation(const uint8_t* sps, size_t size)
{
  std::vector<uint8_t> out_data;

  for (size_t i = 0; i < size; i++) {
    if (i + 2 < size &&
        sps[i] == 0 &&
        sps[i + 1] == 0 &&
        sps[i + 2] == 3) {
      out_data.push_back(0);
      out_data.push_back(0);
      i += 2;
    }
    else {
      out_data.push_back(sps[i]);
    }
  }

  return out_data;
}


Error parse_sps_for_hvcC_configuration(const uint8_t* sps, size_t size,
                                       Box_hvcC::configuration* config,
                                       int* width, int* height)
{
  // remove start-code emulation bytes from SPS header stream

  std::vector<uint8_t> sps_no_emul = remove_start_code_emulation(sps, size);

  sps = sps_no_emul.data();
  size = sps_no_emul.size();


  BitReader reader(sps, (int) size);

  // skip NAL header
  reader.skip_bits(2 * 8);

  // skip VPS ID
  reader.skip_bits(4);

  int nMaxSubLayersMinus1 = reader.get_bits(3);

  config->temporal_id_nested = (uint8_t) reader.get_bits(1);

  // --- profile_tier_level ---

  config->general_profile_space = (uint8_t) reader.get_bits(2);
  config->general_tier_flag = (uint8_t) reader.get_bits(1);
  config->general_profile_idc = (uint8_t) reader.get_bits(5);
  config->general_profile_compatibility_flags = reader.get_bits(32);

  reader.skip_bits(16); // skip reserved bits
  reader.skip_bits(16); // skip reserved bits
  reader.skip_bits(16); // skip reserved bits

  config->general_level_idc = (uint8_t) reader.get_bits(8);

  std::vector<bool> layer_profile_present(nMaxSubLayersMinus1);
  std::vector<bool> layer_level_present(nMaxSubLayersMinus1);

  for (int i = 0; i < nMaxSubLayersMinus1; i++) {
    layer_profile_present[i] = reader.get_bits(1);
    layer_level_present[i] = reader.get_bits(1);
  }

  if (nMaxSubLayersMinus1 > 0) {
    for (int i = nMaxSubLayersMinus1; i < 8; i++) {
      reader.skip_bits(2);
    }
  }

  for (int i = 0; i < nMaxSubLayersMinus1; i++) {
    if (layer_profile_present[i]) {
      reader.skip_bits(2 + 1 + 5);
      reader.skip_bits(32);
      reader.skip_bits(16);
    }

    if (layer_level_present[i]) {
      reader.skip_bits(8);
    }
  }


  // --- SPS continued ---

  int dummy, value;
  reader.get_uvlc(&dummy); // skip seq_parameter_seq_id

  reader.get_uvlc(&value);
  config->chroma_format = (uint8_t) value;

  if (config->chroma_format == 3) {
    reader.skip_bits(1);
  }

  reader.get_uvlc(width);
  reader.get_uvlc(height);

  bool conformance_window = reader.get_bits(1);
  if (conformance_window) {
    int left, right, top, bottom;
    reader.get_uvlc(&left);
    reader.get_uvlc(&right);
    reader.get_uvlc(&top);
    reader.get_uvlc(&bottom);

    //printf("conformance borders: %d %d %d %d\n",left,right,top,bottom);

    int subH = 1, subV = 1;
    if (config->chroma_format == 1) {
      subV = 2;
      subH = 2;
    }
    if (config->chroma_format == 2) { subH = 2; }

    *width -= subH * (left + right);
    *height -= subV * (top + bottom);
  }

  reader.get_uvlc(&value);
  config->bit_depth_luma = (uint8_t) (value + 8);

  reader.get_uvlc(&value);
  config->bit_depth_chroma = (uint8_t) (value + 8);



  // --- init static configuration fields ---

  config->configuration_version = 1;
  config->min_spatial_segmentation_idc = 0; // TODO: get this value from the VUI, 0 should be safe
  config->parallelism_type = 0; // TODO, 0 should be safe
  config->avg_frame_rate = 0; // makes no sense for HEIF
  config->constant_frame_rate = 0; // makes no sense for HEIF
  config->num_temporal_layers = 1; // makes no sense for HEIF

  return Error::Ok;
}
