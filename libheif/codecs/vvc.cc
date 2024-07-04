/*
 * HEIF VVC codec.
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
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

#include "vvc.h"
#include <cstring>
#include <string>
#include <cassert>
#include <iomanip>
#include <utility>

Error Box_vvcC::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  uint8_t byte;

  auto& c = m_configuration; // abbreviation

  c.configurationVersion = range.read8();
  c.avgFrameRate_times_256 = range.read16();

  //printf("version: %d\n", c.configurationVersion);

  byte = range.read8();
  c.constantFrameRate = (byte & 0xc0) >> 6;
  c.numTemporalLayers = (byte & 0x38) >> 3;
  c.lengthSize = uint8_t(((byte & 0x06) >> 1) + 1);
  c.ptl_present_flag = (byte & 0x01);
  // assert(c.ptl_present_flag == false); // TODO   (removed the assert since it will trigger the fuzzers)

  byte = range.read8();
  c.chroma_format_present_flag = (byte & 0x80);
  c.chroma_format_idc = (byte & 0x60) >> 5;

  c.bit_depth_present_flag = (byte & 0x10);
  c.bit_depth = uint8_t(((byte & 0x0e) >> 1) + 8);

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

#if 0
  const int64_t configOBUs_bytes = range.get_remaining_bytes();
  m_config_OBUs.resize(configOBUs_bytes);

  if (!range.read(m_config_OBUs.data(), configOBUs_bytes)) {
    // error
  }
#endif

  return range.get_error();
}


void Box_vvcC::append_nal_data(const std::vector<uint8_t>& nal)
{
  NalArray array;
  array.m_array_completeness = 0;
  array.m_NAL_unit_type = uint8_t(nal[0] >> 1);
  array.m_nal_units.push_back(nal);

  m_nal_array.push_back(array);
}


void Box_vvcC::append_nal_data(const uint8_t* data, size_t size)
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


Error Box_vvcC::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  const auto& c = m_configuration;

  writer.write8(c.configurationVersion);
  writer.write16(c.avgFrameRate_times_256);

  assert(c.lengthSize == 1 || c.lengthSize == 2 || c.lengthSize == 4);

  uint8_t v = (uint8_t) ((c.constantFrameRate << 6) |
                         (c.numTemporalLayers << 3) |
                         ((c.lengthSize - 1) << 1) |
                         (c.ptl_present_flag ? 1 : 0));
  writer.write8(v);

  if (c.ptl_present_flag) {
    assert(false); // TODO
    //VvcPTLRecord(numTemporalLayers) track_ptl;
    //unsigned int(16) output_layer_set_idx;
  }

  v = 0;
  if (c.chroma_format_present_flag) {
    v |= 0x80 | (c.chroma_format_idc << 5);
  }
  else {
    v |= 0x60;
  }

  if (c.bit_depth_present_flag) {
    v |= (uint8_t)(0x10 | ((c.bit_depth - 8) << 1));
  }
  else {
    v |= 0x0e;
  }

  v |= 0x01; // reserved
  writer.write8(v);

  if (m_nal_array.size() >= 256) {
    // TODO: error
  }

  if (m_nal_array.size() > 255) {
    return {heif_error_Encoding_error, heif_suberror_Unspecified, "Too many VVC NAL arrays."};
  }

  writer.write8((uint8_t)m_nal_array.size());
  for (const NalArray& nal_array : m_nal_array) {
    uint8_t v2 = (nal_array.m_array_completeness ? 0x80 : 0);
    v2 |= nal_array.m_NAL_unit_type;
    writer.write8(v2);

    if (nal_array.m_nal_units.size() > 0xFFFF) {
      return {heif_error_Encoding_error, heif_suberror_Unspecified, "Too many VVC NAL units."};
    }

    writer.write16((uint16_t)nal_array.m_nal_units.size());
    for (const auto& nal : nal_array.m_nal_units) {

      if (nal.size() > 0xFFFF) {
        return {heif_error_Encoding_error, heif_suberror_Unspecified, "VVC NAL too large."};
      }

      writer.write16((uint16_t)nal.size());
      writer.write(nal);
    }
  }

  prepend_header(writer, box_start);

  return Error::Ok;
}


static const char* vvc_chroma_names[4] = {"mono", "4:2:0", "4:2:2", "4:4:4"};

std::string Box_vvcC::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  const auto& c = m_configuration; // abbreviation

  sstr << indent << "version: " << ((int) c.configurationVersion) << "\n"
       << indent << "frame-rate: " << (c.avgFrameRate_times_256 / 256.0f) << "\n"
       << indent << "constant frame rate: " << (c.constantFrameRate == 1 ? "constant" : (c.constantFrameRate == 2 ? "multi-layer" : "unknown")) << "\n"
       << indent << "num temporal layers: " << ((int) c.numTemporalLayers) << "\n"
       << indent << "length size: " << ((int) c.lengthSize) << "\n"
       << indent << "chroma-format: ";
  if (c.chroma_format_present_flag) {
    sstr << vvc_chroma_names[c.chroma_format_idc] << "\n";
  }
  else {
    sstr << "---\n";
  }

  sstr << indent << "bit-depth: ";
  if (c.bit_depth_present_flag) {
    sstr << ((int) c.bit_depth) << "\n";
  }
  else {
    sstr << "---\n";
  }

  sstr << indent << "num of arrays: " << m_nal_array.size() << "\n";

  sstr << indent << "config NALs:";
  for (size_t i = 0; i < m_nal_array.size(); i++) {
    indent++;
    sstr << indent << "array completeness: " << ((int)m_nal_array[i].m_array_completeness) << "\n";
    sstr << std::hex << std::setw(2) << std::setfill('0') << m_nal_array[i].m_NAL_unit_type << "\n";

    for (const auto& nal : m_nal_array[i].m_nal_units) {
      std::string ind = indent.get_string();
      sstr << write_raw_data_as_hex(nal.data(), nal.size(), ind, ind);
    }
  }
  sstr << std::dec << std::setw(0) << "\n";

  return sstr.str();
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



Error parse_sps_for_vvcC_configuration(const uint8_t* sps, size_t size,
                                       Box_vvcC::configuration* config,
                                       int* width, int* height)
{
  // remove start-code emulation bytes from SPS header stream

  std::vector<uint8_t> sps_no_emul = remove_start_code_emulation(sps, size);

  sps = sps_no_emul.data();
  size = sps_no_emul.size();

  BitReader reader(sps, (int) size);

  // skip NAL header
  reader.skip_bits(2 * 8);

  // skip SPS ID
  reader.skip_bits(4);

  // skip VPS ID
  reader.skip_bits(4);

  config->numTemporalLayers = (uint8_t)(reader.get_bits(3) + 1);
  config->chroma_format_idc = (uint8_t)(reader.get_bits(2));
  config->chroma_format_present_flag = true;
  reader.skip_bits(2);

  bool sps_ptl_dpb_hrd_params_present_flag = reader.get_bits(1);
  if (sps_ptl_dpb_hrd_params_present_flag) {
    // profile_tier_level( 1, sps_max_sublayers_minus1 )

    if (true /*profileTierPresentFlag*/) {
      //general_profile_idc
          //general_tier_flag
          reader.skip_bits(8);
    }
    reader.skip_bits(8); // general_level_idc
    reader.skip_bits(1); //ptl_frame_only_constraint_flag
    reader.skip_bits(1); //ptl_multilayer_enabled_flag
    if (true /* profileTierPresentFlag*/ ) {
      // general_constraints_info()

      bool gci_present_flag = reader.get_bits(1);
      if (gci_present_flag) {
        assert(false);
      }

      reader.skip_to_byte_boundary();
    }

    std::vector<bool> ptl_sublayer_level_present_flag(config->numTemporalLayers);
    for (int i = config->numTemporalLayers-2; i >= 0; i--) {
      ptl_sublayer_level_present_flag[i] = reader.get_bits(1);
    }

    reader.skip_to_byte_boundary();

    for (int i = config->numTemporalLayers-2; i >= 0; i--) {
      if (ptl_sublayer_level_present_flag[i]) {
        reader.skip_bits(8); // sublayer_level_idc[i]
      }
    }

    if (true /*profileTierPresentFlag*/) {
      int ptl_num_sub_profiles = reader.get_bits(8);
      for (int i = 0; i < ptl_num_sub_profiles; i++) {
        uint32_t idc = reader.get_bits(32); //general_sub_profile_idc[i]
        (void) idc;
      }
    }
  }

  reader.skip_bits(1); // sps_gdr_enabled_flag
  bool sps_ref_pic_resampling_enabled_flag = reader.get_bits(1);
  if (sps_ref_pic_resampling_enabled_flag) {
    reader.skip_bits(1); // sps_res_change_in_clvs_allowed_flag
  }

  int sps_pic_width_max_in_luma_samples;
  int sps_pic_height_max_in_luma_samples;

  bool success;
  success = reader.get_uvlc(&sps_pic_width_max_in_luma_samples);
  (void)success;
  success = reader.get_uvlc(&sps_pic_height_max_in_luma_samples);
  (void)success;

  *width = sps_pic_width_max_in_luma_samples;
  *height = sps_pic_height_max_in_luma_samples;

  int sps_conformance_window_flag = reader.get_bits(1);
  if (sps_conformance_window_flag) {
    int left,right,top,bottom;
    reader.get_uvlc(&left);
    reader.get_uvlc(&right);
    reader.get_uvlc(&top);
    reader.get_uvlc(&bottom);
  }

  bool sps_subpic_info_present_flag = reader.get_bits(1);
  if (sps_subpic_info_present_flag) {
    assert(false); // TODO
  }

  int bitDepth_minus8;
  success = reader.get_uvlc(&bitDepth_minus8);
  (void)success;

  if (bitDepth_minus8 > 0xFF - 8) {
    return {heif_error_Encoding_error, heif_suberror_Unspecified, "VCC bit depth out of range."};
  }

  config->bit_depth = (uint8_t)(bitDepth_minus8 + 8);
  config->bit_depth_present_flag = true;

  return Error::Ok;
}
