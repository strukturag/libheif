/*
 * HEIF JPEG codec.
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

#include "jpeg.h"
#include <string>
#include "security_limits.h"
#include <pixelimage.h>
#include <libheif/api_structs.h>
#include <cstring>
#include "file.h"


static uint8_t JPEG_SOS = 0xDA;

// returns 0 if the marker_type was not found
size_t find_jpeg_marker_start(const std::vector<uint8_t>& data, uint8_t marker_type)
{
  for (size_t i = 0; i < data.size() - 1; i++) {
    if (data[i] == 0xFF && data[i + 1] == marker_type) {
      return i;
    }
  }

  return 0;
}


std::string Box_jpgC::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "num bytes: " << m_data.size() << "\n";

  return sstr.str();
}


Error Box_jpgC::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write(m_data);

  prepend_header(writer, box_start);

  return Error::Ok;
}


Error Box_jpgC::parse(BitstreamRange& range)
{
  if (!has_fixed_box_size()) {
    return Error{heif_error_Unsupported_feature, heif_suberror_Unspecified, "jpgC with unspecified size are not supported"};
  }

  size_t nBytes = range.get_remaining_bytes();
  if (nBytes > MAX_MEMORY_BLOCK_SIZE) {
    return Error{heif_error_Invalid_input, heif_suberror_Unspecified, "jpgC block exceeds maximum size"};
  }

  m_data.resize(nBytes);
  range.read(m_data.data(), nBytes);
  return range.get_error();
}


const heif_color_profile_nclx* ImageItem_JPEG::get_forced_output_nclx() const
{
  // JPEG always uses CCIR-601

  static heif_color_profile_nclx target_heif_nclx;
  target_heif_nclx.version = 1;
  target_heif_nclx.matrix_coefficients = heif_matrix_coefficients_ITU_R_BT_601_6;
  target_heif_nclx.color_primaries = heif_color_primaries_ITU_R_BT_601_6;
  target_heif_nclx.transfer_characteristics = heif_transfer_characteristic_ITU_R_BT_601_6;
  target_heif_nclx.full_range_flag = true;

  return &target_heif_nclx;
}


Result<ImageItem::CodedImageData> ImageItem_JPEG::encode(const std::shared_ptr<HeifPixelImage>& image,
                                                         struct heif_encoder* encoder,
                                                         const struct heif_encoding_options& options,
                                                         enum heif_image_input_class input_class)
{
  CodedImageData codedImage;


  heif_image c_api_image;
  c_api_image.image = image;

  struct heif_error err = encoder->plugin->encode_image(encoder->encoder, &c_api_image, input_class);
  if (err.code) {
    return Error(err.code,
                 err.subcode,
                 err.message);
  }

  std::vector<uint8_t> vec;

  for (;;) {
    uint8_t* data;
    int size;

    encoder->plugin->get_compressed_data(encoder->encoder, &data, &size, nullptr);

    if (data == nullptr) {
      break;
    }

    size_t oldsize = vec.size();
    vec.resize(oldsize + size);
    memcpy(vec.data() + oldsize, data, size);
  }

#if 0
  // Optional: split the JPEG data into a jpgC box and the actual image data.
  // Currently disabled because not supported yet in other decoders.
  if (false) {
    size_t pos = find_jpeg_marker_start(vec, JPEG_SOS);
    if (pos > 0) {
      std::vector<uint8_t> jpgC_data(vec.begin(), vec.begin() + pos);
      auto jpgC = std::make_shared<Box_jpgC>();
      jpgC->set_data(jpgC_data);

      auto ipma_box = m_heif_file->get_ipma_box();
      int index = m_heif_file->get_ipco_box()->find_or_append_child_box(jpgC);
      ipma_box->add_property_for_item_ID(image_id, Box_ipma::PropertyAssociation{true, uint16_t(index + 1)});

      std::vector<uint8_t> image_data(vec.begin() + pos, vec.end());
      vec = std::mo ve(image_data);
    }
  }
#endif
  (void) JPEG_SOS;

  codedImage.bitstream = vec;

#if 0
  // TODO: extract 'jpgC' header data
#endif

  return {codedImage};
}


Result<std::vector<uint8_t>> ImageItem_JPEG::read_bitstream_configuration_data(heif_item_id itemId) const
{
  // --- get codec configuration

  std::shared_ptr<Box_jpgC> jpgC_box = get_file()->get_property<Box_jpgC>(itemId);
  if (jpgC_box) {
    return jpgC_box->get_data();
  }

  return std::vector<uint8_t>{};
}


// This checks whether a start code FFCx with nibble 'x' is a SOF marker.
// E.g. FFC0-FFC3 are, while FFC4 is not.
static bool isSOF[16] = {true, true, true, true, false, true, true, true,
                         false, true, true, true, false, true, true, true};

int ImageItem_JPEG::get_luma_bits_per_pixel() const
{
  std::vector<uint8_t> data;

  // image data, usually from 'mdat'

  Error error = get_file()->append_data_from_iloc(get_id(), data);
  if (error) {
    return error;
  }

  for (size_t i = 0; i + 1 < data.size(); i++) {
    if (data[i] == 0xFF && (data[i + 1] & 0xF0) == 0xC0 && isSOF[data[i + 1] & 0x0F]) {
      i += 4;
      if (i < data.size()) {
        return data[i];
      }
      else {
        return -1;
      }
    }
  }

  return -1;
}


int ImageItem_JPEG::get_chroma_bits_per_pixel() const
{
  return get_luma_bits_per_pixel();
}
