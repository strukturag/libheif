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

#include "file.h"
#include "box.h"
#include "libheif/heif.h"
#include "libheif/heif_properties.h"
#include "compression.h"
#include "codecs/jpeg2000.h"
#include "codecs/jpeg.h"
#include "codecs/vvc.h"
#include "codecs/uncompressed_box.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <utility>
#include <cstring>
#include <cassert>
#include <algorithm>

#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_MSC_VER)

#ifndef NOMINMAX
#define NOMINMAX 1
#endif

#include <windows.h>
#endif


#if WITH_UNCOMPRESSED_CODEC
#include "codecs/uncompressed_image.h"
#endif
#include <iomanip>

// TODO: make this a decoder option
#define STRICT_PARSING false


HeifFile::HeifFile() = default;

HeifFile::~HeifFile() = default;

std::vector<heif_item_id> HeifFile::get_item_IDs() const
{
  std::vector<heif_item_id> IDs;

  for (const auto& infe : m_infe_boxes) {
    IDs.push_back(infe.second->get_item_ID());
  }

  return IDs;
}


std::shared_ptr<const Box_infe> HeifFile::get_infe_box(heif_item_id ID) const
{
  auto iter = m_infe_boxes.find(ID);
  if (iter == m_infe_boxes.end()) {
    return nullptr;
  }

  return iter->second;
}


std::shared_ptr<Box_infe> HeifFile::get_infe_box(heif_item_id ID)
{
  auto iter = m_infe_boxes.find(ID);
  if (iter == m_infe_boxes.end()) {
    return nullptr;
  }

  return iter->second;
}


Error HeifFile::read_from_file(const char* input_filename)
{
#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_MSC_VER)
  auto input_stream_istr = std::unique_ptr<std::istream>(new std::ifstream(convert_utf8_path_to_utf16(input_filename).c_str(), std::ios_base::binary));
#else
  auto input_stream_istr = std::unique_ptr<std::istream>(new std::ifstream(input_filename, std::ios_base::binary));
#endif
  if (!input_stream_istr->good()) {
    std::stringstream sstr;
    sstr << "Error opening file: " << strerror(errno) << " (" << errno << ")\n";
    return Error(heif_error_Input_does_not_exist, heif_suberror_Unspecified, sstr.str());
  }

  auto input_stream = std::make_shared<StreamReader_istream>(std::move(input_stream_istr));
  return read(input_stream);
}


Error HeifFile::read_from_memory(const void* data, size_t size, bool copy)
{
  auto input_stream = std::make_shared<StreamReader_memory>((const uint8_t*) data, size, copy);

  return read(input_stream);
}


Error HeifFile::read(const std::shared_ptr<StreamReader>& reader)
{
  m_input_stream = reader;

  uint64_t maxSize = std::numeric_limits<int64_t>::max();
  BitstreamRange range(m_input_stream, maxSize);

  Error error = parse_heif_file(range);
  return error;
}


void HeifFile::new_empty_file()
{
  m_input_stream.reset();
  m_top_level_boxes.clear();

  m_ftyp_box = std::make_shared<Box_ftyp>();
  m_hdlr_box = std::make_shared<Box_hdlr>();
  m_meta_box = std::make_shared<Box_meta>();
  m_ipco_box = std::make_shared<Box_ipco>();
  m_ipma_box = std::make_shared<Box_ipma>();
  m_iloc_box = std::make_shared<Box_iloc>();
  m_iinf_box = std::make_shared<Box_iinf>();
  m_iprp_box = std::make_shared<Box_iprp>();
  m_pitm_box = std::make_shared<Box_pitm>();

  m_meta_box->append_child_box(m_hdlr_box);
  m_meta_box->append_child_box(m_pitm_box);
  m_meta_box->append_child_box(m_iloc_box);
  m_meta_box->append_child_box(m_iinf_box);
  m_meta_box->append_child_box(m_iprp_box);

  m_iprp_box->append_child_box(m_ipco_box);
  m_iprp_box->append_child_box(m_ipma_box);

  m_infe_boxes.clear();

  m_top_level_boxes.push_back(m_ftyp_box);
  m_top_level_boxes.push_back(m_meta_box);
}


void HeifFile::set_brand(heif_compression_format format, bool miaf_compatible)
{
  // Note: major brand should be repeated in the compatible brands, according to this:
  //   ISOBMFF (ISO/IEC 14496-12:2020) § K.4:
  //   NOTE This document requires that the major brand be repeated in the compatible-brands,
  //   but this requirement is relaxed in the 'profiles' parameter for compactness.
  // See https://github.com/strukturag/libheif/issues/478

  switch (format) {
    case heif_compression_HEVC:
      m_ftyp_box->set_major_brand(heif_brand2_heic);
      m_ftyp_box->set_minor_version(0);
      m_ftyp_box->add_compatible_brand(heif_brand2_mif1);
      m_ftyp_box->add_compatible_brand(heif_brand2_heic);
      break;

    case heif_compression_AV1:
      m_ftyp_box->set_major_brand(heif_brand2_avif);
      m_ftyp_box->set_minor_version(0);
      m_ftyp_box->add_compatible_brand(heif_brand2_avif);
      m_ftyp_box->add_compatible_brand(heif_brand2_mif1);
      break;

    case heif_compression_VVC:
      m_ftyp_box->set_major_brand(heif_brand2_vvic);
      m_ftyp_box->set_minor_version(0);
      m_ftyp_box->add_compatible_brand(heif_brand2_mif1);
      m_ftyp_box->add_compatible_brand(heif_brand2_vvic);
      break;

    case heif_compression_JPEG:
      m_ftyp_box->set_major_brand(heif_brand2_jpeg);
      m_ftyp_box->set_minor_version(0);
      m_ftyp_box->add_compatible_brand(heif_brand2_jpeg);
      m_ftyp_box->add_compatible_brand(heif_brand2_mif1);
      break;

    case heif_compression_uncompressed:
      // Not clear what the correct major brand should be
      m_ftyp_box->set_major_brand(heif_brand2_mif2);
      m_ftyp_box->set_minor_version(0);
      m_ftyp_box->add_compatible_brand(heif_brand2_mif1);
      break;

    case heif_compression_JPEG2000:
    case heif_compression_HTJ2K:
      m_ftyp_box->set_major_brand(fourcc("j2ki"));
      m_ftyp_box->set_minor_version(0);
      m_ftyp_box->add_compatible_brand(fourcc("mif1"));
      m_ftyp_box->add_compatible_brand(fourcc("j2ki"));
      break;

    default:
      break;
  }

  if (miaf_compatible) {
    m_ftyp_box->add_compatible_brand(heif_brand2_miaf);
  }

#if 0
  // Temporarily disabled, pending resolution of
  // https://github.com/strukturag/libheif/issues/888
  if (get_num_images() == 1) {
    // This could be overly conservative, but is safe
    m_ftyp_box->add_compatible_brand(heif_brand2_1pic);
  }
#endif
}


void HeifFile::write(StreamWriter& writer)
{
  for (auto& box : m_top_level_boxes) {
    box->derive_box_version_recursive();
    box->write(writer);
  }

  m_iloc_box->write_mdat_after_iloc(writer);
}


std::string HeifFile::debug_dump_boxes() const
{
  std::stringstream sstr;

  bool first = true;

  for (const auto& box : m_top_level_boxes) {
    // dump box content for debugging

    if (first) {
      first = false;
    }
    else {
      sstr << "\n";
    }

    Indent indent;
    sstr << box->dump(indent);
  }

  return sstr.str();
}


Error HeifFile::parse_heif_file(BitstreamRange& range)
{
  // --- read all top-level boxes

  for (;;) {
    std::shared_ptr<Box> box;
    Error error = Box::read(range, &box);

    if (range.error() || range.eof()) {
      break;
    }

    // When an EOF error is returned, this is not really a fatal exception,
    // but simply the indication that we reached the end of the file.
    // TODO: this design should be cleaned up
    if (error.error_code == heif_error_Invalid_input && error.sub_error_code == heif_suberror_End_of_data) {
      break;
    }

    if (error != Error::Ok) {
      return error;
    }

    m_top_level_boxes.push_back(box);


    // extract relevant boxes (ftyp, meta)

    if (box->get_short_type() == fourcc("meta")) {
      m_meta_box = std::dynamic_pointer_cast<Box_meta>(box);
    }

    if (box->get_short_type() == fourcc("ftyp")) {
      m_ftyp_box = std::dynamic_pointer_cast<Box_ftyp>(box);
    }
  }



  // --- check whether this is a HEIF file and its structural format

  if (!m_ftyp_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_ftyp_box);
  }

  if (!m_ftyp_box->has_compatible_brand(heif_brand2_heic) &&
      !m_ftyp_box->has_compatible_brand(heif_brand2_heix) &&
      !m_ftyp_box->has_compatible_brand(heif_brand2_mif1) &&
      !m_ftyp_box->has_compatible_brand(heif_brand2_avif) &&
      !m_ftyp_box->has_compatible_brand(heif_brand2_1pic) &&
      !m_ftyp_box->has_compatible_brand(heif_brand2_jpeg)) {
    std::stringstream sstr;
    sstr << "File does not include any supported brands.\n";

    return Error(heif_error_Unsupported_filetype,
                 heif_suberror_Unspecified,
                 sstr.str());
  }

  if (!m_meta_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_meta_box);
  }


  m_hdlr_box = std::dynamic_pointer_cast<Box_hdlr>(m_meta_box->get_child_box(fourcc("hdlr")));
  if (STRICT_PARSING && !m_hdlr_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_hdlr_box);
  }

  if (m_hdlr_box &&
      m_hdlr_box->get_handler_type() != fourcc("pict")) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_pict_handler);
  }


  // --- find mandatory boxes needed for image decoding

  m_pitm_box = std::dynamic_pointer_cast<Box_pitm>(m_meta_box->get_child_box(fourcc("pitm")));
  if (!m_pitm_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_pitm_box);
  }

  m_iprp_box = std::dynamic_pointer_cast<Box_iprp>(m_meta_box->get_child_box(fourcc("iprp")));
  if (!m_iprp_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_iprp_box);
  }

  m_ipco_box = std::dynamic_pointer_cast<Box_ipco>(m_iprp_box->get_child_box(fourcc("ipco")));
  if (!m_ipco_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_ipco_box);
  }

  auto ipma_boxes = m_iprp_box->get_typed_child_boxes<Box_ipma>(fourcc("ipma"));
  if (ipma_boxes.empty()) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_ipma_box);
  }
  for (size_t i=1;i<ipma_boxes.size();i++) {
    ipma_boxes[0]->insert_entries_from_other_ipma_box(*ipma_boxes[i]);
  }
  m_ipma_box = ipma_boxes[0];

  m_iloc_box = std::dynamic_pointer_cast<Box_iloc>(m_meta_box->get_child_box(fourcc("iloc")));
  if (!m_iloc_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_iloc_box);
  }

  m_idat_box = std::dynamic_pointer_cast<Box_idat>(m_meta_box->get_child_box(fourcc("idat")));

  m_iref_box = std::dynamic_pointer_cast<Box_iref>(m_meta_box->get_child_box(fourcc("iref")));
  if (m_iref_box) {
    Error error = check_for_ref_cycle(get_primary_image_ID(), m_iref_box);
    if (error) {
      return error;
    }
  }

  m_iinf_box = std::dynamic_pointer_cast<Box_iinf>(m_meta_box->get_child_box(fourcc("iinf")));
  if (!m_iinf_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_iinf_box);
  }



  // --- build list of images

  std::vector<std::shared_ptr<Box>> infe_boxes = m_iinf_box->get_child_boxes(fourcc("infe"));

  for (auto& box : infe_boxes) {
    std::shared_ptr<Box_infe> infe_box = std::dynamic_pointer_cast<Box_infe>(box);
    if (!infe_box) {
      return Error(heif_error_Invalid_input,
                   heif_suberror_No_infe_box);
    }

    m_infe_boxes.insert(std::make_pair(infe_box->get_item_ID(), infe_box));
  }

  return Error::Ok;
}


Error HeifFile::check_for_ref_cycle(heif_item_id ID,
                                    const std::shared_ptr<Box_iref>& iref_box) const
{
  std::unordered_set<heif_item_id> parent_items;
  return check_for_ref_cycle_recursion(ID, iref_box, parent_items);
}


Error HeifFile::check_for_ref_cycle_recursion(heif_item_id ID,
                                    const std::shared_ptr<Box_iref>& iref_box,
                                    std::unordered_set<heif_item_id>& parent_items) const {
  if (parent_items.find(ID) != parent_items.end()) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Item_reference_cycle,
                 "Image reference cycle");
  }
  parent_items.insert(ID);

  std::vector<heif_item_id> image_references = iref_box->get_references(ID, fourcc("dimg"));
  for (heif_item_id reference_idx : image_references) {
    Error error = check_for_ref_cycle_recursion(reference_idx, iref_box, parent_items);
    if (error) {
      return error;
    }
  }

  parent_items.erase(ID);
  return Error::Ok;
}


bool HeifFile::image_exists(heif_item_id ID) const
{
  auto image_iter = m_infe_boxes.find(ID);
  return image_iter != m_infe_boxes.end();
}


bool HeifFile::has_item_with_id(heif_item_id ID) const
{
  auto infe_box = get_infe_box(ID);
  return infe_box != nullptr;
}


std::string HeifFile::get_item_type(heif_item_id ID) const
{
  auto infe_box = get_infe_box(ID);
  if (!infe_box) {
    return "";
  }

  return infe_box->get_item_type();
}


std::string HeifFile::get_content_type(heif_item_id ID) const
{
  auto infe_box = get_infe_box(ID);
  if (!infe_box) {
    return "";
  }

  return infe_box->get_content_type();
}

std::string HeifFile::get_item_uri_type(heif_item_id ID) const
{
  auto infe_box = get_infe_box(ID);
  if (!infe_box) {
    return "";
  }

  return infe_box->get_item_uri_type();
}


Error HeifFile::get_properties(heif_item_id imageID,
                               std::vector<std::shared_ptr<Box>>& properties) const
{
  if (!m_ipco_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_ipco_box);
  }
  else if (!m_ipma_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_ipma_box);
  }

  return m_ipco_box->get_properties_for_item_ID(imageID, m_ipma_box, properties);
}


heif_chroma HeifFile::get_image_chroma_from_configuration(heif_item_id imageID) const
{
  // HEVC

  auto box = m_ipco_box->get_property_for_item_ID(imageID, m_ipma_box, fourcc("hvcC"));
  std::shared_ptr<Box_hvcC> hvcC_box = std::dynamic_pointer_cast<Box_hvcC>(box);
  if (hvcC_box) {
    return (heif_chroma) (hvcC_box->get_configuration().chroma_format);
  }


  // VVC

  box = m_ipco_box->get_property_for_item_ID(imageID, m_ipma_box, fourcc("vvcC"));
  std::shared_ptr<Box_vvcC> vvcC_box = std::dynamic_pointer_cast<Box_vvcC>(box);
  if (vvcC_box) {
    return (heif_chroma) (vvcC_box->get_configuration().chroma_format_idc);
  }


  // AV1

  box = m_ipco_box->get_property_for_item_ID(imageID, m_ipma_box, fourcc("av1C"));
  std::shared_ptr<Box_av1C> av1C_box = std::dynamic_pointer_cast<Box_av1C>(box);
  if (av1C_box) {
    Box_av1C::configuration config = av1C_box->get_configuration();
    if (config.chroma_subsampling_x == 1 &&
        config.chroma_subsampling_y == 1) {
      return heif_chroma_420;
    }
    else if (config.chroma_subsampling_x == 1 &&
             config.chroma_subsampling_y == 0) {
      return heif_chroma_422;
    }
    else if (config.chroma_subsampling_x == 0 &&
             config.chroma_subsampling_y == 0) {
      return heif_chroma_444;
    }
    else {
      return heif_chroma_undefined;
    }
  }


  assert(false);
  return heif_chroma_undefined;
}


int HeifFile::get_luma_bits_per_pixel_from_configuration(heif_item_id imageID) const
{
  std::string image_type = get_item_type(imageID);

  // HEVC

  if (image_type == "hvc1") {
    auto box = m_ipco_box->get_property_for_item_ID(imageID, m_ipma_box, fourcc("hvcC"));
    std::shared_ptr<Box_hvcC> hvcC_box = std::dynamic_pointer_cast<Box_hvcC>(box);
    if (hvcC_box) {
      return hvcC_box->get_configuration().bit_depth_luma;
    }
  }


  // VVC

  if (image_type == "vvc1") {
    auto box = m_ipco_box->get_property_for_item_ID(imageID, m_ipma_box, fourcc("vvcC"));
    std::shared_ptr<Box_vvcC> vvcC_box = std::dynamic_pointer_cast<Box_vvcC>(box);
    if (vvcC_box) {
      Box_vvcC::configuration config = vvcC_box->get_configuration();
      if (config.bit_depth_present_flag) {
        return config.bit_depth;
      }
      else {
        return 8; // TODO: what shall we do if the bit-depth is unknown? Use PIXI?
      }
    }
  }

  
  // AV1

  if (image_type == "av01") {
    auto box = m_ipco_box->get_property_for_item_ID(imageID, m_ipma_box, fourcc("av1C"));
    std::shared_ptr<Box_av1C> av1C_box = std::dynamic_pointer_cast<Box_av1C>(box);
    if (av1C_box) {
      Box_av1C::configuration config = av1C_box->get_configuration();
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
  }


  // JPEG

  if (image_type == "jpeg" || (image_type=="mime" && get_content_type(imageID)=="image/jpeg")) {
    return jpeg_get_bits_per_pixel(imageID);
  }

  // JPEG 2000

  if (image_type == "j2k1") {
    JPEG2000MainHeader header;
    Error err = header.parseHeader(*this, imageID);
    if (err) {
      return -1;
    }
    return header.get_precision(0);
  }

#if WITH_UNCOMPRESSED_CODEC
  // Uncompressed

  if (image_type == "unci") {
    int bpp = UncompressedImageCodec::get_luma_bits_per_pixel_from_configuration_unci(*this, imageID);
    return bpp;
  }
#endif

  return -1;
}


int HeifFile::get_chroma_bits_per_pixel_from_configuration(heif_item_id imageID) const
{
  std::string image_type = get_item_type(imageID);

  // HEVC

  if (image_type == "hvc1") {
    auto box = m_ipco_box->get_property_for_item_ID(imageID, m_ipma_box, fourcc("hvcC"));
    std::shared_ptr<Box_hvcC> hvcC_box = std::dynamic_pointer_cast<Box_hvcC>(box);
    if (hvcC_box) {
      return hvcC_box->get_configuration().bit_depth_chroma;
    }
  }

  // VVC

  if (image_type == "vvc1") {
    auto box = m_ipco_box->get_property_for_item_ID(imageID, m_ipma_box, fourcc("vvcC"));
    std::shared_ptr<Box_vvcC> vvcC_box = std::dynamic_pointer_cast<Box_vvcC>(box);
    if (vvcC_box) {
      Box_vvcC::configuration config = vvcC_box->get_configuration();
      if (config.bit_depth_present_flag) {
        return config.bit_depth;
      }
      else {
        return 8; // TODO: what shall we do if the bit-depth is unknown? Use PIXI?
      }
    }
  }

  // AV1

  if (image_type == "av01") {
    auto box = m_ipco_box->get_property_for_item_ID(imageID, m_ipma_box, fourcc("av1C"));
    std::shared_ptr<Box_av1C> av1C_box = std::dynamic_pointer_cast<Box_av1C>(box);
    if (av1C_box) {
      Box_av1C::configuration config = av1C_box->get_configuration();
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
  }

  // JPEG

  if (image_type == "jpeg" || (image_type=="mime" && get_content_type(imageID)=="image/jpeg")) {
    return jpeg_get_bits_per_pixel(imageID);
  }

  // JPEG 2000

  if (image_type == "j2k1") {
    JPEG2000MainHeader header;
    Error err = header.parseHeader(*this, imageID);
    if (err) {
      return -1;
    }
    return header.get_precision(1);
  }

  return -1;
}


// This checks whether a start code FFCx with nibble 'x' is a SOF marker.
// E.g. FFC0-FFC3 are, while FFC4 is not.
static bool isSOF[16] = { 1,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1 };

int HeifFile::jpeg_get_bits_per_pixel(heif_item_id imageID) const
{
  std::vector<uint8_t> data;
  Error err = get_compressed_image_data(imageID, &data);
  if (err) {
    return -1;
  }

  for (size_t i = 0; i + 1 < data.size(); i++) {
    if (data[i] == 0xFF && (data[i+1] & 0xF0) == 0xC0 && isSOF[data[i+1] & 0x0F]) {
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


Error HeifFile::get_compressed_image_data(heif_item_id ID, std::vector<uint8_t>* data) const
{
#if ENABLE_PARALLEL_TILE_DECODING
  std::lock_guard<std::mutex> guard(m_read_mutex);
#endif

  if (!image_exists(ID)) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Nonexisting_item_referenced);
  }

  auto infe_box = get_infe_box(ID);
  if (!infe_box) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Nonexisting_item_referenced);
  }


  std::string item_type = infe_box->get_item_type();
  std::string content_type = infe_box->get_content_type();

  // --- get coded image data pointers

  auto items = m_iloc_box->get_items();
  const Box_iloc::Item* item = nullptr;
  for (const auto& i : items) {
    if (i.item_ID == ID) {
      item = &i;
      break;
    }
  }
  if (!item) {
    std::stringstream sstr;
    sstr << "Item with ID " << ID << " has no compressed data";

    return Error(heif_error_Invalid_input,
                 heif_suberror_No_item_data,
                 sstr.str());
  }

  if (item_type == "hvc1") {
    // --- --- --- HEVC
    return get_compressed_image_data_hvc1(ID, data, item);
  }
  else if (item_type == "vvc1") {
    // --- --- --- VVC
    return get_compressed_image_data_vvc(ID, data, item);
  }
  else if (item_type == "av01") {
    return get_compressed_image_data_av1(ID, data, item);
  }
  else if (item_type == "jpeg" ||
           (item_type == "mime" && get_content_type(ID) == "image/jpeg")) {
    return get_compressed_image_data_jpeg(ID, data, item);
  }
  else if (item_type == "j2k1") {
      return get_compressed_image_data_jpeg2000(ID, item, data);
  }
#if WITH_UNCOMPRESSED_CODEC
  else if (item_type == "unci") {
    return get_compressed_image_data_uncompressed(ID, data, item);
  }
#endif
  else if (true ||  // fallback case for all kinds of generic metadata (e.g. 'iptc')
           item_type == "grid" ||
           item_type == "iovl" ||
           item_type == "Exif" ||
           (item_type == "mime" && content_type == "application/rdf+xml")) {
    Error error;
    bool read_uncompressed = true;
    if (item_type == "mime") {
      std::string encoding = infe_box->get_content_encoding();
      if (encoding == "deflate") {
#if WITH_DEFLATE_HEADER_COMPRESSION
        read_uncompressed = false;
        std::vector<uint8_t> compressed_data;
        error = m_iloc_box->read_data(*item, m_input_stream, m_idat_box, &compressed_data);
        if (error) {
          return error;
        }
        error = inflate_zlib(compressed_data, data);
        if (error) {
          return error;
        }
#else
        return Error(heif_error_Unsupported_feature,
                     heif_suberror_Unsupported_header_compression_method,
                     encoding);
#endif
      }
    }

    if (read_uncompressed) {
      return m_iloc_box->read_data(*item, m_input_stream, m_idat_box, data);
    }
  }
  return Error(heif_error_Unsupported_feature, heif_suberror_Unsupported_codec);
}

// generic compression and uncompressed, per 23001-17
const Error HeifFile::get_compressed_image_data_uncompressed(heif_item_id ID, std::vector<uint8_t> *data, const Box_iloc::Item *item) const
{
  std::vector<std::shared_ptr<Box>> properties;
  Error err = m_ipco_box->get_properties_for_item_ID(ID, m_ipma_box, properties);
  if (err) {
    return err;
  }

  // --- get codec configuration

  std::shared_ptr<Box_cmpC> cmpC_box;
  std::shared_ptr<Box_icbr> icbr_box;
  for (auto& prop : properties) {
    if (prop->get_short_type() == fourcc("cmpC")) {
      cmpC_box = std::dynamic_pointer_cast<Box_cmpC>(prop);
    }
    if (prop->get_short_type() == fourcc("icbr")) {
      icbr_box = std::dynamic_pointer_cast<Box_icbr>(prop);
    }
    if (cmpC_box && icbr_box) {
      break;
    }
  }
  if (!cmpC_box) {
    // assume no generic compression
    return m_iloc_box->read_data(*item, m_input_stream, m_idat_box, data);
  }
  if (!cmpC_box->get_must_decompress_individual_entities()) {
    std::vector<uint8_t> compressed_data;
    m_iloc_box->read_data(*item, m_input_stream, m_idat_box, &compressed_data);
    return do_decompress_data(cmpC_box, compressed_data, data);
  } else {
    if (!icbr_box) {
      std::stringstream sstr;
      sstr << "cannot decode unci item requiring entity decompression without icbr box" << std::endl;
      return Error(heif_error_Invalid_input,
                  heif_suberror_No_icbr_box,
                  sstr.str());
    }
    if (item->construction_method == 0) {
      for (Box_icbr::ByteRange range: icbr_box->get_ranges()) {
        // TODO: check errors
        bool success = m_input_stream->seek(range.range_offset);
        if (!success) {
          return Error{heif_error_Invalid_input, heif_suberror_End_of_data, "error while seeking to generically compressed data"};
        }
        std::vector<uint8_t> compressed_range_bytes(range.range_size);
        success = m_input_stream->read((char*) compressed_range_bytes.data(), static_cast<size_t>(compressed_range_bytes.size()));
        if (!success) {
          return Error{heif_error_Invalid_input, heif_suberror_End_of_data, "error while reading generically compressed data"};
        }
        std::vector<uint8_t> uncompressed_range_data;
        Error err = do_decompress_data(cmpC_box, compressed_range_bytes, &uncompressed_range_data);
        if (err) {
          return err;
        }
        data->insert(data->end(), uncompressed_range_data.data(), uncompressed_range_data.data() + uncompressed_range_data.size());
      }
      return Error::Ok;
    } else {
      // TODO: implement...
      std::stringstream sstr;
      sstr << "cannot decode unci item from idat yet" << std::endl;
      return Error(heif_error_Unsupported_feature,
                  heif_suberror_Unsupported_data_version,
                  sstr.str());
    }
  }
  return Error(heif_error_Unsupported_feature, heif_suberror_Unsupported_codec);
}

const Error HeifFile::do_decompress_data(std::shared_ptr<Box_cmpC> &cmpC_box, std::vector<uint8_t> compressed_data, std::vector<uint8_t> *data) const
{
  if (cmpC_box->get_compression_type() == fourcc("brot")) {
#if HAVE_BROTLI
    return inflate_brotli(compressed_data, data);
#else
    std::stringstream sstr;
    sstr << "cannot decode unci item with brotli compression - not enabled" << std::endl;
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_generic_compression_method,
                 sstr.str());
#endif
  } else if (cmpC_box->get_compression_type() == fourcc("zlib")) {
#if WITH_ZLIB_COMPRESSION
    return inflate_zlib(compressed_data, data);
#else
    std::stringstream sstr;
    sstr << "cannot decode unci item with zlib compression - not enabled" << std::endl;
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_generic_compression_method,
                 sstr.str());
#endif
  } else if (cmpC_box->get_compression_type() == fourcc("defl")) {
#if WITH_ZLIB_COMPRESSION
    return inflate_deflate(compressed_data, data);
#else
    std::stringstream sstr;
    sstr << "cannot decode unci item with deflate compression - not enabled" << std::endl;
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_generic_compression_method,
                 sstr.str());
#endif
  } else {
    std::stringstream sstr;
    sstr << "cannot decode unci item with unsupported compression type: " << cmpC_box->get_compression_type() << std::endl;
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_generic_compression_method,
                 sstr.str());
  }
}

const Error HeifFile::get_compressed_image_data_hvc1(heif_item_id ID, std::vector<uint8_t> *data, const Box_iloc::Item *item) const
{
  // --- get properties for this image
  std::vector<std::shared_ptr<Box>> properties;
  Error err = m_ipco_box->get_properties_for_item_ID(ID, m_ipma_box, properties);
  if (err)
  {
    return err;
  }

  // --- get codec configuration

  std::shared_ptr<Box_hvcC> hvcC_box;
  for (auto &prop : properties)
  {
    if (prop->get_short_type() == fourcc("hvcC"))
    {
      hvcC_box = std::dynamic_pointer_cast<Box_hvcC>(prop);
      if (hvcC_box)
      {
        break;
      }
    }
  }

  if (!hvcC_box)
  {
    // Should always have an hvcC box, because we are checking this in
    // heif_context::interpret_heif_file()
    assert(false);
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_hvcC_box);
  }
  else if (!hvcC_box->get_headers(data))
  {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_item_data);
  }

  return m_iloc_box->read_data(*item, m_input_stream, m_idat_box, data);
}

const Error HeifFile::get_compressed_image_data_vvc(heif_item_id ID, std::vector<uint8_t> *data, const Box_iloc::Item *item) const
{

  // --- get properties for this image

  std::vector<std::shared_ptr<Box>> properties;
  Error err = m_ipco_box->get_properties_for_item_ID(ID, m_ipma_box, properties);
  if (err)
  {
    return err;
  }

  // --- get codec configuration

  std::shared_ptr<Box_vvcC> vvcC_box;
  for (auto &prop : properties)
  {
    if (prop->get_short_type() == fourcc("vvcC"))
    {
      vvcC_box = std::dynamic_pointer_cast<Box_vvcC>(prop);
      if (vvcC_box)
      {
        break;
      }
    }
  }

  if (!vvcC_box)
  {
    assert(false);
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_vvcC_box);
  }
  else if (!vvcC_box->get_headers(data))
  {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_item_data);
  }

  return m_iloc_box->read_data(*item, m_input_stream, m_idat_box, data);
}

const Error HeifFile::get_compressed_image_data_av1(heif_item_id ID, std::vector<uint8_t> *data, const Box_iloc::Item *item) const
{
  // --- --- --- AV1

  // --- get properties for this image

  std::vector<std::shared_ptr<Box>> properties;
  Error err = m_ipco_box->get_properties_for_item_ID(ID, m_ipma_box, properties);
  if (err)
  {
    return err;
  }

  // --- get codec configuration

  std::shared_ptr<Box_av1C> av1C_box;
  for (auto &prop : properties)
  {
    if (prop->get_short_type() == fourcc("av1C"))
    {
      av1C_box = std::dynamic_pointer_cast<Box_av1C>(prop);
      if (av1C_box)
      {
        break;
      }
    }
  }

  if (!av1C_box)
  {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_av1C_box);
  }
  else if (!av1C_box->get_headers(data))
  {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_item_data);
  }

  return m_iloc_box->read_data(*item, m_input_stream, m_idat_box, data);
}

const Error HeifFile::get_compressed_image_data_jpeg2000(heif_item_id ID, const Box_iloc::Item *item, std::vector<uint8_t> *data) const
{
  std::vector<std::shared_ptr<Box>> properties;
  Error err = m_ipco_box->get_properties_for_item_ID(ID, m_ipma_box, properties);
  if (err)
  {
    return err;
  }

  // --- get codec configuration

  std::shared_ptr<Box_j2kH> j2kH_box;
  for (auto &prop : properties)
  {
    if (prop->get_short_type() == fourcc("j2kH"))
    {
      j2kH_box = std::dynamic_pointer_cast<Box_j2kH>(prop);
      if (j2kH_box)
      {
        break;
      }
    }
  }

  if (!j2kH_box)
  {
    // TODO - Correctly Find the j2kH box
    //  return Error(heif_error_Invalid_input,
    //               heif_suberror_Unspecified);
  }
  // else if (!j2kH_box->get_headers(data)) {
  //   return Error(heif_error_Invalid_input,
  //                heif_suberror_No_item_data);
  // }

  return m_iloc_box->read_data(*item, m_input_stream, m_idat_box, data);
}

const Error HeifFile::get_compressed_image_data_jpeg(heif_item_id ID, std::vector<uint8_t> * data, const Box_iloc::Item *item) const
{
  // --- check if 'jpgC' is present
  std::vector<std::shared_ptr<Box>> properties;
  Error err = m_ipco_box->get_properties_for_item_ID(ID, m_ipma_box, properties);
  if (err)
  {
    return err;
  }

  // --- get codec configuration

  std::shared_ptr<Box_jpgC> jpgC_box;
  for (auto &prop : properties)
  {
    if (prop->get_short_type() == fourcc("jpgC"))
    {
      jpgC_box = std::dynamic_pointer_cast<Box_jpgC>(prop);
      if (jpgC_box)
      {
        *data = jpgC_box->get_data();
        break;
      }
    }
  }

  return m_iloc_box->read_data(*item, m_input_stream, m_idat_box, data);
}


Error HeifFile::get_item_data(heif_item_id ID, std::vector<uint8_t>* out_data, heif_metadata_compression* out_compression) const
{
  Error error;

  auto infe_box = get_infe_box(ID);
  if (!infe_box) {
    return {heif_error_Usage_error,
            heif_suberror_Nonexisting_item_referenced};
  }

  std::string item_type = infe_box->get_item_type();
  std::string content_type = infe_box->get_content_type();

  // --- get item

  auto items = m_iloc_box->get_items();
  const Box_iloc::Item* item = nullptr;
  for (const auto& i : items) {
    if (i.item_ID == ID) {
      item = &i;
      break;
    }
  }
  if (!item) {
    std::stringstream sstr;
    sstr << "Item with ID " << ID << " has no data";

    return {heif_error_Invalid_input,
            heif_suberror_No_item_data,
            sstr.str()};
  }

  // --- non 'mime' data (uncompressed)

  if (item_type != "mime") {
    if (out_compression) {
      *out_compression = heif_metadata_compression_off;
    }

    return m_iloc_box->read_data(*item, m_input_stream, m_idat_box, out_data);
  }


  // --- mime data

  std::string encoding = infe_box->get_content_encoding();

  heif_metadata_compression compression;

  if (encoding.empty()) {
    // shortcut for case of uncompressed mime data

    if (out_compression) {
      *out_compression = heif_metadata_compression_off;
    }

    return m_iloc_box->read_data(*item, m_input_stream, m_idat_box, out_data);
  }
  else if (encoding == "deflate") {
    compression = heif_metadata_compression_deflate;
  }
  else {
    compression = heif_metadata_compression_unknown;
  }

  // read compressed data

  std::vector<uint8_t> compressed_data;
  error = m_iloc_box->read_data(*item, m_input_stream, m_idat_box, &compressed_data);
  if (error) {
    return error;
  }

  // return compressed data, if we do not want to have it uncompressed

  const bool do_decode = (out_compression == nullptr);
  if (!do_decode) {
    *out_compression = compression;
    *out_data = std::move(compressed_data);
    return Error::Ok;
  }

  // decompress the data

  switch (compression) {
#if WITH_DEFLATE_HEADER_COMPRESSION
    case heif_metadata_compression_deflate:
      return inflate_zlib(compressed_data, out_data);
#endif
    default:
      return {heif_error_Unsupported_filetype, heif_suberror_Unsupported_header_compression_method};
  }
}


heif_item_id HeifFile::get_unused_item_id() const
{
  for (heif_item_id id = 1;;
       id++) {

    bool id_exists = false;

    for (const auto& infe : m_infe_boxes) {
      if (infe.second->get_item_ID() == id) {
        id_exists = true;
        break;
      }
    }

    if (!id_exists) {
      return id;
    }
  }

  assert(false); // should never be reached
  return 0;
}


heif_item_id HeifFile::add_new_image(const char* item_type)
{
  auto box = add_new_infe_box(item_type);
  return box->get_item_ID();
}


heif_item_id HeifFile::add_new_hidden_image(const char* item_type)
{
  auto box = add_new_infe_box(item_type);
  box->set_hidden_item(true);
  return box->get_item_ID();
}


std::shared_ptr<Box_infe> HeifFile::add_new_infe_box(const char* item_type)
{
  heif_item_id id = get_unused_item_id();

  auto infe = std::make_shared<Box_infe>();
  infe->set_item_ID(id);
  infe->set_hidden_item(false);
  infe->set_item_type(item_type);

  m_infe_boxes[id] = infe;
  m_iinf_box->append_child_box(infe);

  return infe;
}


void HeifFile::add_ispe_property(heif_item_id id, uint32_t width, uint32_t height)
{
  auto ispe = std::make_shared<Box_ispe>();
  ispe->set_size(width, height);

  int index = m_ipco_box->find_or_append_child_box(ispe);

  m_ipma_box->add_property_for_item_ID(id, Box_ipma::PropertyAssociation{false, uint16_t(index + 1)});
}

void HeifFile::add_clap_property(heif_item_id id, uint32_t clap_width, uint32_t clap_height,
                                 uint32_t image_width, uint32_t image_height)
{
  auto clap = std::make_shared<Box_clap>();
  clap->set(clap_width, clap_height, image_width, image_height);

  int index = m_ipco_box->find_or_append_child_box(clap);

  m_ipma_box->add_property_for_item_ID(id, Box_ipma::PropertyAssociation{true, uint16_t(index + 1)});
}


heif_property_id HeifFile::add_property(heif_item_id id, const std::shared_ptr<Box>& property, bool essential)
{
  int index = m_ipco_box->find_or_append_child_box(property);

  m_ipma_box->add_property_for_item_ID(id, Box_ipma::PropertyAssociation{essential, uint16_t(index + 1)});

  return index + 1;
}


void HeifFile::add_orientation_properties(heif_item_id id, heif_orientation orientation)
{
  // Note: ISO/IEC 23000-22:2019(E) (MIAF) 7.3.6.7 requires the following order:
  // clean aperture first, then rotation, then mirror

  int rotation_ccw = 0;
  heif_transform_mirror_direction mirror;
  bool has_mirror = false;

  switch (orientation) {
    case heif_orientation_normal:
      break;
    case heif_orientation_flip_horizontally:
      mirror = heif_transform_mirror_direction_horizontal;
      has_mirror = true;
      break;
    case heif_orientation_rotate_180:
      rotation_ccw = 180;
      break;
    case heif_orientation_flip_vertically:
      mirror = heif_transform_mirror_direction_vertical;
      has_mirror = true;
      break;
    case heif_orientation_rotate_90_cw_then_flip_horizontally:
      rotation_ccw = 270;
      mirror = heif_transform_mirror_direction_horizontal;
      has_mirror = true;
      break;
    case heif_orientation_rotate_90_cw:
      rotation_ccw = 270;
      break;
    case heif_orientation_rotate_90_cw_then_flip_vertically:
      rotation_ccw = 270;
      mirror = heif_transform_mirror_direction_vertical;
      has_mirror = true;
      break;
    case heif_orientation_rotate_270_cw:
      rotation_ccw = 90;
      break;
  }

  // omit rotation when angle is 0
  if (rotation_ccw!=0) {
    auto irot = std::make_shared<Box_irot>();
    irot->set_rotation_ccw(rotation_ccw);

    int index = m_ipco_box->find_or_append_child_box(irot);

    m_ipma_box->add_property_for_item_ID(id, Box_ipma::PropertyAssociation{false, uint16_t(index + 1)});
  }

  if (has_mirror) {
    auto imir = std::make_shared<Box_imir>();
    imir->set_mirror_direction(mirror);

    int index = m_ipco_box->find_or_append_child_box(imir);

    m_ipma_box->add_property_for_item_ID(id, Box_ipma::PropertyAssociation{false, uint16_t(index + 1)});
  }
}


void HeifFile::add_pixi_property(heif_item_id id, uint8_t c1, uint8_t c2, uint8_t c3)
{
  auto pixi = std::make_shared<Box_pixi>();
  pixi->add_channel_bits(c1);
  if (c2 || c3) {
    pixi->add_channel_bits(c2);
    pixi->add_channel_bits(c3);
  }

  int index = m_ipco_box->find_or_append_child_box(pixi);

  m_ipma_box->add_property_for_item_ID(id, Box_ipma::PropertyAssociation{false, uint16_t(index + 1)});
}


void HeifFile::add_vvcC_property(heif_item_id id)
{
  auto vvcC = std::make_shared<Box_vvcC>();
  int index = m_ipco_box->append_child_box(vvcC);

  m_ipma_box->add_property_for_item_ID(id, Box_ipma::PropertyAssociation{true, uint16_t(index + 1)});
}


Error HeifFile::append_vvcC_nal_data(heif_item_id id, const std::vector<uint8_t>& nal_data)
{
  auto vvcC = std::dynamic_pointer_cast<Box_vvcC>(m_ipco_box->get_property_for_item_ID(id,
                                                                                       m_ipma_box,
                                                                                       fourcc("vvcC")));

  if (vvcC) {
    vvcC->append_nal_data(nal_data);
    return Error::Ok;
  }
  else {
    // Should always have an vvcC box, because we are checking this in
    // heif_context::interpret_heif_file()
    assert(false);
    return Error(heif_error_Usage_error,
                 heif_suberror_No_vvcC_box);
  }
}


Error HeifFile::set_vvcC_configuration(heif_item_id id, const Box_vvcC::configuration& config)
{
  auto vvcC = std::dynamic_pointer_cast<Box_vvcC>(m_ipco_box->get_property_for_item_ID(id,
                                                                                       m_ipma_box,
                                                                                       fourcc("vvcC")));

  if (vvcC) {
    vvcC->set_configuration(config);
    return Error::Ok;
  }
  else {
    return Error(heif_error_Usage_error,
                 heif_suberror_No_vvcC_box);
  }
}


Error HeifFile::append_vvcC_nal_data(heif_item_id id, const uint8_t* data, size_t size)
{
  std::vector<std::shared_ptr<Box>> properties;

  auto vvcC = std::dynamic_pointer_cast<Box_vvcC>(m_ipco_box->get_property_for_item_ID(id,
                                                                                       m_ipma_box,
                                                                                       fourcc("vvcC")));

  if (vvcC) {
    vvcC->append_nal_data(data, size);
    return Error::Ok;
  }
  else {
    return Error(heif_error_Usage_error,
                 heif_suberror_No_vvcC_box);
  }
}


void HeifFile::add_hvcC_property(heif_item_id id)
{
  auto hvcC = std::make_shared<Box_hvcC>();
  int index = m_ipco_box->append_child_box(hvcC);

  m_ipma_box->add_property_for_item_ID(id, Box_ipma::PropertyAssociation{true, uint16_t(index + 1)});
}


Error HeifFile::append_hvcC_nal_data(heif_item_id id, const std::vector<uint8_t>& nal_data)
{
  auto hvcC = std::dynamic_pointer_cast<Box_hvcC>(m_ipco_box->get_property_for_item_ID(id,
                                                                                       m_ipma_box,
                                                                                       fourcc("hvcC")));

  if (hvcC) {
    hvcC->append_nal_data(nal_data);
    return Error::Ok;
  }
  else {
    // Should always have an hvcC box, because we are checking this in
    // heif_context::interpret_heif_file()
    assert(false);
    return Error(heif_error_Usage_error,
                 heif_suberror_No_hvcC_box);
  }
}


Error HeifFile::set_hvcC_configuration(heif_item_id id, const Box_hvcC::configuration& config)
{
  auto hvcC = std::dynamic_pointer_cast<Box_hvcC>(m_ipco_box->get_property_for_item_ID(id,
                                                                                       m_ipma_box,
                                                                                       fourcc("hvcC")));

  if (hvcC) {
    hvcC->set_configuration(config);
    return Error::Ok;
  }
  else {
    return Error(heif_error_Usage_error,
                 heif_suberror_No_hvcC_box);
  }
}


Error HeifFile::append_hvcC_nal_data(heif_item_id id, const uint8_t* data, size_t size)
{
  std::vector<std::shared_ptr<Box>> properties;

  auto hvcC = std::dynamic_pointer_cast<Box_hvcC>(m_ipco_box->get_property_for_item_ID(id,
                                                                                       m_ipma_box,
                                                                                       fourcc("hvcC")));

  if (hvcC) {
    hvcC->append_nal_data(data, size);
    return Error::Ok;
  }
  else {
    return Error(heif_error_Usage_error,
                 heif_suberror_No_hvcC_box);
  }
}


void HeifFile::add_av1C_property(heif_item_id id, const Box_av1C::configuration& config)
{
  auto av1C = std::make_shared<Box_av1C>();
  av1C->set_configuration(config);

  add_property(id, av1C, true);
}


std::shared_ptr<Box_j2kH> HeifFile::add_j2kH_property(heif_item_id id)
{
  auto j2kH = std::make_shared<Box_j2kH>();
  int index = m_ipco_box->append_child_box(j2kH); // do not deduplicate because this can have a child box

  m_ipma_box->add_property_for_item_ID(id, Box_ipma::PropertyAssociation{true, uint16_t(index + 1)});

  return j2kH;
}


Result<heif_item_id> HeifFile::add_infe(const char* item_type, const uint8_t* data, size_t size)
{
  Result<heif_item_id> result;

  // create an infe box describing what kind of data we are storing (this also creates a new ID)

  auto infe_box = add_new_infe_box(item_type);
  infe_box->set_hidden_item(true);

  heif_item_id metadata_id = infe_box->get_item_ID();
  result.value = metadata_id;

  set_item_data(infe_box, data, size, heif_metadata_compression_off);

  return result;
}


Result<heif_item_id> HeifFile::add_infe_mime(const char* content_type, heif_metadata_compression content_encoding, const uint8_t* data, size_t size)
{
  Result<heif_item_id> result;

  // create an infe box describing what kind of data we are storing (this also creates a new ID)

  auto infe_box = add_new_infe_box("mime");
  infe_box->set_hidden_item(true);
  infe_box->set_content_type(content_type);

  heif_item_id metadata_id = infe_box->get_item_ID();
  result.value = metadata_id;

  set_item_data(infe_box, data, size, content_encoding);

  return result;
}


Result<heif_item_id> HeifFile::add_precompressed_infe_mime(const char* content_type, std::string content_encoding, const uint8_t* data, size_t size)
{
  Result<heif_item_id> result;

  // create an infe box describing what kind of data we are storing (this also creates a new ID)

  auto infe_box = add_new_infe_box("mime");
  infe_box->set_hidden_item(true);
  infe_box->set_content_type(content_type);

  heif_item_id metadata_id = infe_box->get_item_ID();
  result.value = metadata_id;

  set_precompressed_item_data(infe_box, data, size, content_encoding);

  return result;
}


Result<heif_item_id> HeifFile::add_infe_uri(const char* item_uri_type, const uint8_t* data, size_t size)
{
  Result<heif_item_id> result;

  // create an infe box describing what kind of data we are storing (this also creates a new ID)

  auto infe_box = add_new_infe_box("uri ");
  infe_box->set_hidden_item(true);
  infe_box->set_item_uri_type(item_uri_type);

  heif_item_id metadata_id = infe_box->get_item_ID();
  result.value = metadata_id;

  set_item_data(infe_box, data, size, heif_metadata_compression_off);

  return result;
}


Error HeifFile::set_item_data(const std::shared_ptr<Box_infe>& item, const uint8_t* data, size_t size, heif_metadata_compression compression)
{
  // --- metadata compression

  if (compression == heif_metadata_compression_auto) {
    compression = heif_metadata_compression_off; // currently, we don't use header compression by default
  }

  // only set metadata compression for MIME type data which has 'content_encoding' field
  if (compression != heif_metadata_compression_off &&
      item->get_item_type() != "mime") {
    // TODO: error, compression not supported
  }


  std::vector<uint8_t> data_array;
  if (compression == heif_metadata_compression_deflate) {
#if WITH_DEFLATE_HEADER_COMPRESSION
    data_array = deflate((const uint8_t*) data, size);
    item->set_content_encoding("deflate");
#else
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_header_compression_method);
#endif
  }
  else {
    // uncompressed data, plain copy

    data_array.resize(size);
    memcpy(data_array.data(), data, size);
  }

  // copy the data into the file, store the pointer to it in an iloc box entry

  append_iloc_data(item->get_item_ID(), data_array);

  return Error::Ok;
}


Error HeifFile::set_precompressed_item_data(const std::shared_ptr<Box_infe>& item, const uint8_t* data, size_t size, std::string content_encoding)
{
  // only set metadata compression for MIME type data which has 'content_encoding' field
  if (!content_encoding.empty() &&
      item->get_item_type() != "mime") {
    // TODO: error, compression not supported
  }


  std::vector<uint8_t> data_array;
  data_array.resize(size);
  memcpy(data_array.data(), data, size);

  item->set_content_encoding(content_encoding);

  // copy the data into the file, store the pointer to it in an iloc box entry

  append_iloc_data(item->get_item_ID(), data_array);

  return Error::Ok;
}


void HeifFile::append_iloc_data(heif_item_id id, const std::vector<uint8_t>& nal_packets, uint8_t construction_method)
{
  m_iloc_box->append_data(id, nal_packets, construction_method);
}


void HeifFile::append_iloc_data_with_4byte_size(heif_item_id id, const uint8_t* data, size_t size)
{
  std::vector<uint8_t> nal;
  nal.resize(size + 4);

  nal[0] = (uint8_t) ((size >> 24) & 0xFF);
  nal[1] = (uint8_t) ((size >> 16) & 0xFF);
  nal[2] = (uint8_t) ((size >> 8) & 0xFF);
  nal[3] = (uint8_t) ((size >> 0) & 0xFF);

  memcpy(nal.data() + 4, data, size);

  append_iloc_data(id, nal);
}

void HeifFile::set_primary_item_id(heif_item_id id)
{
  m_pitm_box->set_item_ID(id);
}

void HeifFile::add_iref_reference(heif_item_id from, uint32_t type,
                                  const std::vector<heif_item_id>& to)
{
  if (!m_iref_box) {
    m_iref_box = std::make_shared<Box_iref>();
    m_meta_box->append_child_box(m_iref_box);
  }

  m_iref_box->add_references(from, type, to);
}

void HeifFile::set_auxC_property(heif_item_id id, const std::string& type)
{
  auto auxC = std::make_shared<Box_auxC>();
  auxC->set_aux_type(type);

  int index = m_ipco_box->find_or_append_child_box(auxC);

  m_ipma_box->add_property_for_item_ID(id, Box_ipma::PropertyAssociation{true, uint16_t(index + 1)});
}

void HeifFile::set_color_profile(heif_item_id id, const std::shared_ptr<const color_profile>& profile)
{
  auto colr = std::make_shared<Box_colr>();
  colr->set_color_profile(profile);

  int index = m_ipco_box->find_or_append_child_box(colr);
  m_ipma_box->add_property_for_item_ID(id, Box_ipma::PropertyAssociation{false, uint16_t(index + 1)});
}


// TODO: the hdlr box is probably not the right place for this. Into which box should we write comments?
void HeifFile::set_hdlr_library_info(const std::string& encoder_plugin_version)
{
  std::stringstream sstr;
  sstr << "libheif (" << LIBHEIF_VERSION << ") / " << encoder_plugin_version;
  m_hdlr_box->set_name(sstr.str());
}

#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_MSC_VER)
std::wstring HeifFile::convert_utf8_path_to_utf16(std::string str)
{
  std::wstring ret;
  int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
  if (len > 0)
  {
    ret.resize(len);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &ret[0], len);
  }
  return ret;
}
#endif
