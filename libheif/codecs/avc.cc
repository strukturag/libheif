/*
 * HEIF AVC codec.
 * Copyright (c) 2023 Brad Hards <bradh@frogmouth.net>
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

#include "avc.h"
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>
#include "file.h"
#include "context.h"


Error Box_avcC::parse(BitstreamRange &range) {
  m_configuration.configuration_version = range.read8();
  m_configuration.AVCProfileIndication = range.read8();
  m_configuration.profile_compatibility = range.read8();
  m_configuration.AVCLevelIndication = range.read8();
  uint8_t lengthSizeMinusOneWithReserved = range.read8();
  m_configuration.lengthSize =
      (lengthSizeMinusOneWithReserved & 0b00000011) + 1;

  uint8_t numOfSequenceParameterSets = (range.read8() & 0b00011111);
  for (int i = 0; i < numOfSequenceParameterSets; i++) {
    uint16_t sequenceParameterSetLength = range.read16();
    std::vector<uint8_t> sps(sequenceParameterSetLength);
    range.read(sps.data(), sps.size());
    m_sps.push_back(sps);
  }

  uint8_t numOfPictureParameterSets = range.read8();
  for (int i = 0; i < numOfPictureParameterSets; i++) {
    uint16_t pictureParameterSetLength = range.read16();
    std::vector<uint8_t> pps(pictureParameterSetLength);
    range.read(pps.data(), pps.size());
    m_pps.push_back(pps);
  }

  if ((m_configuration.AVCProfileIndication != 66) &&
      (m_configuration.AVCProfileIndication != 77) &&
      (m_configuration.AVCProfileIndication != 88)) {
    m_configuration.chroma_format = range.read8() & 0b00000011;
    m_configuration.bit_depth_luma = 8 + (range.read8() & 0b00000111);
    m_configuration.bit_depth_chroma = 8 + (range.read8() & 0b00000111);
    uint8_t numOfSequenceParameterSetExt = range.read8();
    for (int i = 0; i < numOfSequenceParameterSetExt; i++) {
      uint16_t sequenceParameterSetExtLength = range.read16();
      std::vector<uint8_t> sps_ext(sequenceParameterSetExtLength);
      range.read(sps_ext.data(), sps_ext.size());
      m_sps_ext.push_back(sps_ext);
    }
  }

  return range.get_error();
}

Error Box_avcC::write(StreamWriter &writer) const {
  size_t box_start = reserve_box_header_space(writer);

  writer.write8(m_configuration.configuration_version);
  writer.write8(m_configuration.AVCProfileIndication);
  writer.write8(m_configuration.profile_compatibility);
  writer.write8(m_configuration.AVCLevelIndication);
  uint8_t lengthSizeMinusOneWithReserved = 0b11111100 | ((m_configuration.lengthSize - 1) & 0b11);
  writer.write8(lengthSizeMinusOneWithReserved);
  uint8_t numSpsWithReserved = 0b11100000 | (m_sps.size() & 0b00011111);
  writer.write8(numSpsWithReserved);
  for (const auto &sps: m_sps) {
    writer.write16((uint16_t) sps.size());
    writer.write(sps);
  }
  writer.write8(m_pps.size() & 0xFF);
  for (const auto &pps: m_pps) {
    writer.write16((uint16_t) pps.size());
    writer.write(pps);
  }
  prepend_header(writer, box_start);

  return Error::Ok;
}

std::string Box_avcC::dump(Indent &indent) const {
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << indent << "configuration_version: "
       << ((int)m_configuration.configuration_version) << "\n"
       << indent << "AVCProfileIndication: "
       << ((int)m_configuration.AVCProfileIndication) << " ("
       << profileIndicationAsText() << ")"
       << "\n"
       << indent << "profile_compatibility: "
       << ((int)m_configuration.profile_compatibility) << "\n"
       << indent
       << "AVCLevelIndication: " << ((int)m_configuration.AVCLevelIndication)
       << "\n";

  for (const auto &sps : m_sps) {
    sstr << indent << "SPS: ";
    for (uint8_t b : sps) {
      sstr << std::setfill('0') << std::setw(2) << std::hex << ((int)b) << " ";
    }
    sstr << "\n";
    sstr << std::dec;
  }

  for (const auto &pps : m_pps) {
    sstr << indent << "PPS: ";
    for (uint8_t b : pps) {
      sstr << std::setfill('0') << std::setw(2) << std::hex << ((int)b) << " ";
    }
    sstr << "\n";
    sstr << std::dec;
  }

  return sstr.str();
}

std::string Box_avcC::profileIndicationAsText() const {
  // See ISO/IEC 14496-10:2022 Annex A
  switch (m_configuration.AVCProfileIndication) {
  case 44:
    return "CALVC 4:4:4";
  case 66:
    return "Constrained Baseline";
  case 77:
    return "Main";
  case 88:
    return "Extended";
  case 100:
    return "High variant";
  case 110:
    return "High 10";
  case 122:
    return "High 4:2:2";
  case 244:
    return "High 4:4:4";
  default:
    return "Unknown";
  }
}


void Box_avcC::get_header_nals(std::vector<uint8_t>& data) const
{
  for (const auto& sps : m_sps) {
    data.push_back((sps.size() >> 24) & 0xFF);
    data.push_back((sps.size() >> 16) & 0xFF);
    data.push_back((sps.size() >> 8) & 0xFF);
    data.push_back((sps.size() >> 0) & 0xFF);

    data.insert(data.end(), sps.begin(), sps.end());
  }

  for (const auto& pps : m_pps) {
    data.push_back((pps.size() >> 24) & 0xFF);
    data.push_back((pps.size() >> 16) & 0xFF);
    data.push_back((pps.size() >> 8) & 0xFF);
    data.push_back((pps.size() >> 0) & 0xFF);

    data.insert(data.end(), pps.begin(), pps.end());
  }
}



Result<ImageItem::CodedImageData> ImageItem_AVC::encode(const std::shared_ptr<HeifPixelImage>& image,
                                                         struct heif_encoder* encoder,
                                                         const struct heif_encoding_options& options,
                                                         enum heif_image_input_class input_class)
{
#if 0
  CodedImageData codedImage;

  auto hvcC = std::make_shared<Box_hvcC>();

  heif_image c_api_image;
  c_api_image.image = image;

  struct heif_error err = encoder->plugin->encode_image(encoder->encoder, &c_api_image, input_class);
  if (err.code) {
    return Error(err.code,
                 err.subcode,
                 err.message);
  }

  int encoded_width = 0;
  int encoded_height = 0;

  for (;;) {
    uint8_t* data;
    int size;

    encoder->plugin->get_compressed_data(encoder->encoder, &data, &size, nullptr);

    if (data == nullptr) {
      break;
    }


    const uint8_t NAL_SPS = 33;

    if ((data[0] >> 1) == NAL_SPS) {
      Box_hvcC::configuration config;

      parse_sps_for_hvcC_configuration(data, size, &config, &encoded_width, &encoded_height);

      hvcC->set_configuration(config);

      codedImage.encoded_image_width = encoded_width;
      codedImage.encoded_image_height = encoded_height;
    }

    switch (data[0] >> 1) {
      case 0x20:
      case 0x21:
      case 0x22:
        hvcC->append_nal_data(data, size);
        break;

      default:
        codedImage.append_with_4bytes_size(data, size);
        // m_heif_file->append_iloc_data_with_4byte_size(image_id, data, size);
    }
  }

  if (!encoded_width || !encoded_height) {
    return Error(heif_error_Encoder_plugin_error,
                 heif_suberror_Invalid_image_size);
  }

  codedImage.properties.push_back(hvcC);


  // Make sure that the encoder plugin works correctly and the encoded image has the correct size.

  if (encoder->plugin->plugin_api_version >= 3 &&
      encoder->plugin->query_encoded_size != nullptr) {
    uint32_t check_encoded_width = image->get_width(), check_encoded_height = image->get_height();

    encoder->plugin->query_encoded_size(encoder->encoder,
                                        image->get_width(), image->get_height(),
                                        &check_encoded_width,
                                        &check_encoded_height);

    assert((int)check_encoded_width == encoded_width);
    assert((int)check_encoded_height == encoded_height);
  }

  return codedImage;
#endif
  assert(false); // TODO
  return {};
}

Result<std::vector<uint8_t>> ImageItem_AVC::read_bitstream_configuration_data(heif_item_id itemId) const
{
  std::vector<uint8_t> data;

  auto avcC_box = get_file()->get_property<Box_avcC>(itemId);
  if (!avcC_box) {
    return Error{heif_error_Invalid_input,
                 heif_suberror_No_av1C_box};
  }

  avcC_box->get_header_nals(data);

  return data;
}


int ImageItem_AVC::get_luma_bits_per_pixel() const
{
  auto avcC_box = get_file()->get_property<Box_avcC>(get_id());
  if (avcC_box) {
    return avcC_box->get_configuration().bit_depth_luma;
  }

  return -1;
}


int ImageItem_AVC::get_chroma_bits_per_pixel() const
{
  auto avcC_box = get_file()->get_property<Box_avcC>(get_id());
  if (avcC_box) {
    return avcC_box->get_configuration().bit_depth_chroma;
  }

  return -1;
}
