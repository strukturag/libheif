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
#include "libheif/heif.h"
#include <cstddef>
#include <cstdint>
#include "box.h"
#include "security_limits.h"
#include "nclx.h"
#include "jpeg.h"
#include "jpeg2000.h"
#include "hevc.h"
#include "mask_image.h"
#include "vvc.h"
#include "avc.h"

#include <iomanip>
#include <utility>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <set>
#include <cassert>

#if WITH_UNCOMPRESSED_CODEC
#include "uncompressed_box.h"
#endif


Fraction::Fraction(int32_t num, int32_t den)
{
  // Reduce resolution of fraction until we are in a safe range.
  // We need this as adding fractions may lead to very large denominators
  // (e.g. 0x10000 * 0x10000 > 0x100000000 -> overflow, leading to integer 0)

  numerator = num;
  denominator = den;

  while (denominator > MAX_FRACTION_VALUE || denominator < -MAX_FRACTION_VALUE) {
    numerator /= 2;
    denominator /= 2;
  }

  while (denominator > 1 && (numerator > MAX_FRACTION_VALUE || numerator < -MAX_FRACTION_VALUE)) {
    numerator /= 2;
    denominator /= 2;
  }
}

Fraction::Fraction(uint32_t num, uint32_t den)
{
  assert(num <= (uint32_t) std::numeric_limits<int32_t>::max());
  assert(den <= (uint32_t) std::numeric_limits<int32_t>::max());

  *this = Fraction(int32_t(num), int32_t(den));
}

Fraction::Fraction(int64_t num, int64_t den)
{
  while (num < std::numeric_limits<int32_t>::min() || num > std::numeric_limits<int32_t>::max() ||
         den < std::numeric_limits<int32_t>::min() || den > std::numeric_limits<int32_t>::max()) {
    num = (num + (num>=0 ? 1 : -1)) / 2;
    den = (den + (den>=0 ? 1 : -1)) / 2;
  }

  numerator = static_cast<int32_t>(num);
  denominator = static_cast<int32_t>(den);
}

Fraction Fraction::operator+(const Fraction& b) const
{
  if (denominator == b.denominator) {
    int64_t n = int64_t{numerator} + b.numerator;
    int64_t d = denominator;
    return Fraction{n,d};
  }
  else {
    int64_t n = int64_t{numerator} * b.denominator + int64_t{b.numerator} * denominator;
    int64_t d = int64_t{denominator} * b.denominator;
    return Fraction{n,d};
  }
}

Fraction Fraction::operator-(const Fraction& b) const
{
  if (denominator == b.denominator) {
    int64_t n = int64_t{numerator} - b.numerator;
    int64_t d = denominator;
    return Fraction{n,d};
  }
  else {
    int64_t n = int64_t{numerator} * b.denominator - int64_t{b.numerator} * denominator;
    int64_t d = int64_t{denominator} * b.denominator;
    return Fraction{n,d};
  }
}

Fraction Fraction::operator+(int v) const
{
  return Fraction{numerator + v * int64_t(denominator), int64_t(denominator)};
}

Fraction Fraction::operator-(int v) const
{
  return Fraction{numerator - v * int64_t(denominator), int64_t(denominator)};
}

Fraction Fraction::operator/(int v) const
{
  return Fraction{int64_t(numerator), int64_t(denominator) * v};
}

int32_t Fraction::round_down() const
{
  return numerator / denominator;
}

int32_t Fraction::round_up() const
{
  return int32_t((numerator + int64_t(denominator) - 1) / denominator);
}

int32_t Fraction::round() const
{
  return int32_t((numerator + int64_t(denominator) / 2) / denominator);
}

bool Fraction::is_valid() const
{
  return denominator != 0;
}

uint32_t from_fourcc(const char* string)
{
  return ((string[0] << 24) |
          (string[1] << 16) |
          (string[2] << 8) |
          (string[3]));
}

std::string to_fourcc(uint32_t code)
{
  std::string str("    ");
  str[0] = static_cast<char>((code >> 24) & 0xFF);
  str[1] = static_cast<char>((code >> 16) & 0xFF);
  str[2] = static_cast<char>((code >> 8) & 0xFF);
  str[3] = static_cast<char>((code >> 0) & 0xFF);

  return str;
}


BoxHeader::BoxHeader() = default;


std::vector<uint8_t> BoxHeader::get_type() const
{
  if (m_type == fourcc("uuid")) {
    return m_uuid_type;
  }
  else {
    std::vector<uint8_t> type(4);
    type[0] = static_cast<uint8_t>((m_type >> 24) & 0xFF);
    type[1] = static_cast<uint8_t>((m_type >> 16) & 0xFF);
    type[2] = static_cast<uint8_t>((m_type >> 8) & 0xFF);
    type[3] = static_cast<uint8_t>((m_type >> 0) & 0xFF);
    return type;
  }
}


std::string BoxHeader::get_type_string() const
{
  if (m_type == fourcc("uuid")) {
    // 8-4-4-4-12

    std::ostringstream sstr;
    sstr << std::hex;
    sstr << std::setfill('0');

    for (int i = 0; i < 16; i++) {
      if (i == 4 || i == 6 || i == 8 || i == 10) {
        sstr << '-';
      }

      sstr << std::setw(2);
      sstr << ((int) m_uuid_type[i]);
    }

    return sstr.str();
  }
  else {
    return to_fourcc(m_type);
  }
}


std::vector<uint8_t> BoxHeader::get_uuid_type() const
{
  if (m_type != fourcc("uuid")) {
    return {};
  }

  return m_uuid_type;
}


void BoxHeader::set_uuid_type(const std::vector<uint8_t>& type)
{
  m_type = fourcc("uuid");
  m_uuid_type = type;
}


Error BoxHeader::parse_header(BitstreamRange& range)
{
  StreamReader::grow_status status;
  status = range.wait_for_available_bytes(8);
  if (status != StreamReader::size_reached) {
    // TODO: return recoverable error at timeout
    return Error(heif_error_Invalid_input,
                 heif_suberror_End_of_data);
  }

  m_size = range.read32();
  m_type = range.read32();

  m_header_size = 8;

  if (m_size == 1) {
    status = range.wait_for_available_bytes(8);
    if (status != StreamReader::size_reached) {
      // TODO: return recoverable error at timeout
      return Error(heif_error_Invalid_input,
                   heif_suberror_End_of_data);
    }

    uint64_t high = range.read32();
    uint64_t low = range.read32();

    m_size = (high << 32) | low;
    m_header_size += 8;

    std::stringstream sstr;
    sstr << "Box size " << m_size << " exceeds security limit.";

    if (m_size > MAX_LARGE_BOX_SIZE) {
      return Error(heif_error_Memory_allocation_error,
                   heif_suberror_Security_limit_exceeded,
                   sstr.str());
    }
  }

  if (m_type == fourcc("uuid")) {
    status = range.wait_for_available_bytes(16);
    if (status != StreamReader::size_reached) {
      // TODO: return recoverable error at timeout
      return Error(heif_error_Invalid_input,
                   heif_suberror_End_of_data);
    }

    if (range.prepare_read(16)) {
      m_uuid_type.resize(16);
      bool success = range.get_istream()->read((char*) m_uuid_type.data(), 16);
      assert(success);
      (void) success;
    }

    m_header_size += 16;
  }

  return range.get_error();
}


int Box::calculate_header_size(bool data64bit) const
{
  int header_size = 8;  // does not include "FullBox" fields.

  if (get_short_type() == fourcc("uuid")) {
    header_size += 16;
  }

  if (data64bit) {
    header_size += 8;
  }

  return header_size;
}


size_t Box::reserve_box_header_space(StreamWriter& writer, bool data64bit) const
{
  size_t start_pos = writer.get_position();

  int header_size = calculate_header_size(data64bit);

  writer.skip(header_size);

  return start_pos;
}


size_t FullBox::reserve_box_header_space(StreamWriter& writer, bool data64bit) const
{
  size_t start_pos = Box::reserve_box_header_space(writer, data64bit);

  writer.skip(4);

  return start_pos;
}


Error FullBox::write_header(StreamWriter& writer, size_t total_size, bool data64bit) const
{
  auto err = Box::write_header(writer, total_size, data64bit);
  if (err) {
    return err;
  }

  assert((get_flags() & ~0x00FFFFFFU) == 0);

  writer.write32((get_version() << 24) | get_flags());

  return Error::Ok;
}


Error Box::prepend_header(StreamWriter& writer, size_t box_start, bool data64bit) const
{
  size_t total_size = writer.data_size() - box_start;

  writer.set_position(box_start);

  auto err = write_header(writer, total_size, data64bit);

  writer.set_position_to_end();  // Note: should we move to the end of the box after writing the header?

  return err;
}


Error Box::write_header(StreamWriter& writer, size_t total_size, bool data64bit) const
{
  bool large_size = (total_size > 0xFFFFFFFF);

  // --- write header

  if (large_size && !data64bit) {
    // Note: as an alternative, we could return an error here. If it fails, the user has to try again with 64 bit.
    writer.insert(8);
  }

  if (large_size) {
    writer.write32(1);
  }
  else {
    assert(total_size <= 0xFFFFFFFF);
    writer.write32((uint32_t) total_size);
  }

  writer.write32(get_short_type());

  if (large_size) {
    writer.write64(total_size);
  }

  if (get_short_type() == fourcc("uuid")) {
    assert(get_type().size() == 16);
    writer.write(get_type());
  }

  return Error::Ok;
}


std::string BoxHeader::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << indent << "Box: " << get_type_string() << " -----\n";
  sstr << indent << "size: " << get_box_size() << "   (header size: " << get_header_size() << ")\n";

  return sstr.str();
}


Error Box::parse(BitstreamRange& range)
{
  // skip box

  if (get_box_size() == size_until_end_of_file) {
    range.skip_to_end_of_file();
  }
  else {
    uint64_t content_size = get_box_size() - get_header_size();
    if (range.prepare_read(content_size)) {
      if (content_size > MAX_BOX_SIZE) {
        return Error(heif_error_Invalid_input,
                     heif_suberror_Invalid_box_size);
      }

      range.get_istream()->seek_cur(get_box_size() - get_header_size());
    }
  }

  // Note: seekg() clears the eof flag and it will not be set again afterwards,
  // hence we have to test for the fail flag.

  return range.get_error();
}


Error FullBox::parse_full_box_header(BitstreamRange& range)
{
  uint32_t data = range.read32();
  m_version = static_cast<uint8_t>(data >> 24);
  m_flags = data & 0x00FFFFFF;
  //m_is_full_box = true;

  m_header_size += 4;

  return range.get_error();
}


Error Box::read(BitstreamRange& range, std::shared_ptr<Box>* result)
{
  BoxHeader hdr;
  Error err = hdr.parse_header(range);
  if (err) {
    return err;
  }

  if (range.error()) {
    return range.get_error();
  }

  std::shared_ptr<Box> box;

  switch (hdr.get_short_type()) {
    case fourcc("ftyp"):
      box = std::make_shared<Box_ftyp>();
      break;

    case fourcc("meta"):
      box = std::make_shared<Box_meta>();
      break;

    case fourcc("hdlr"):
      box = std::make_shared<Box_hdlr>();
      break;

    case fourcc("pitm"):
      box = std::make_shared<Box_pitm>();
      break;

    case fourcc("iloc"):
      box = std::make_shared<Box_iloc>();
      break;

    case fourcc("iinf"):
      box = std::make_shared<Box_iinf>();
      break;

    case fourcc("infe"):
      box = std::make_shared<Box_infe>();
      break;

    case fourcc("iprp"):
      box = std::make_shared<Box_iprp>();
      break;

    case fourcc("ipco"):
      box = std::make_shared<Box_ipco>();
      break;

    case fourcc("ipma"):
      box = std::make_shared<Box_ipma>();
      break;

    case fourcc("ispe"):
      box = std::make_shared<Box_ispe>();
      break;

    case fourcc("auxC"):
      box = std::make_shared<Box_auxC>();
      break;

    case fourcc("irot"):
      box = std::make_shared<Box_irot>();
      break;

    case fourcc("imir"):
      box = std::make_shared<Box_imir>();
      break;

    case fourcc("clap"):
      box = std::make_shared<Box_clap>();
      break;

    case fourcc("iref"):
      box = std::make_shared<Box_iref>();
      break;

    case fourcc("hvcC"):
      box = std::make_shared<Box_hvcC>();
      break;

    case fourcc("av1C"):
      box = std::make_shared<Box_av1C>();
      break;

    case fourcc("vvcC"):
      box = std::make_shared<Box_vvcC>();
      break;

    case fourcc("idat"):
      box = std::make_shared<Box_idat>();
      break;

    case fourcc("grpl"):
      box = std::make_shared<Box_grpl>();
      break;

    case fourcc("dinf"):
      box = std::make_shared<Box_dinf>();
      break;

    case fourcc("dref"):
      box = std::make_shared<Box_dref>();
      break;

    case fourcc("url "):
      box = std::make_shared<Box_url>();
      break;

    case fourcc("colr"):
      box = std::make_shared<Box_colr>();
      break;

    case fourcc("pixi"):
      box = std::make_shared<Box_pixi>();
      break;

    case fourcc("pasp"):
      box = std::make_shared<Box_pasp>();
      break;

    case fourcc("lsel"):
      box = std::make_shared<Box_lsel>();
      break;

    case fourcc("a1op"):
      box = std::make_shared<Box_a1op>();
      break;

    case fourcc("a1lx"):
      box = std::make_shared<Box_a1lx>();
      break;

    case fourcc("clli"):
      box = std::make_shared<Box_clli>();
      break;

    case fourcc("mdcv"):
      box = std::make_shared<Box_mdcv>();
      break;

    case fourcc("cmin"):
      box = std::make_shared<Box_cmin>();
      break;

    case fourcc("udes"):
      box = std::make_shared<Box_udes>();
      break;

    case fourcc("jpgC"):
      box = std::make_shared<Box_jpgC>();
      break;

#if WITH_UNCOMPRESSED_CODEC
    case fourcc("cmpd"):
      box = std::make_shared<Box_cmpd>();
      break;

    case fourcc("uncC"):
      box = std::make_shared<Box_uncC>();
      break;
#endif

    // --- JPEG 2000
      
    case fourcc("j2kH"):
      box = std::make_shared<Box_j2kH>();
      break;

    case fourcc("cdef"):
      box = std::make_shared<Box_cdef>();
      break;

    case fourcc("cmap"):
      box = std::make_shared<Box_cmap>();
      break;

    case fourcc("pclr"):
      box = std::make_shared<Box_pclr>();
      break;

    case fourcc("j2kL"):
      box = std::make_shared<Box_j2kL>();
      break;

    // --- mski
      
    case fourcc("mskC"):
      box = std::make_shared<Box_mskC>();
      break;

    // --- AVC (H.264)

    case fourcc("avcC"):
      box = std::make_shared<Box_avcC>();
      break;

    case fourcc("mdat"):
      // avoid generating a 'Box_other'
      box = std::make_shared<Box>();
      break;

    case fourcc("uuid"):
      if (hdr.get_uuid_type() == std::vector<uint8_t>{0x22, 0xcc, 0x04, 0xc7, 0xd6, 0xd9, 0x4e, 0x07, 0x9d, 0x90, 0x4e, 0xb6, 0xec, 0xba, 0xf3, 0xa3}) {
        box = std::make_shared<Box_cmin>();
      }
      else {
        box = std::make_shared<Box_other>(hdr.get_short_type());
      }
      break;

    default:
      box = std::make_shared<Box_other>(hdr.get_short_type());
      break;
  }

  box->set_short_header(hdr);

  if (hdr.has_fixed_box_size() && hdr.get_box_size() < hdr.get_header_size()) {
    std::stringstream sstr;
    sstr << "Box size (" << hdr.get_box_size() << " bytes) smaller than header size ("
         << hdr.get_header_size() << " bytes)";

    // Sanity check.
    return Error(heif_error_Invalid_input,
                 heif_suberror_Invalid_box_size,
                 sstr.str());
  }


  if (range.get_nesting_level() > MAX_BOX_NESTING_LEVEL) {
    return Error(heif_error_Memory_allocation_error,
                 heif_suberror_Security_limit_exceeded,
                 "Security limit for maximum nesting of boxes has been exceeded");
  }


  if (hdr.has_fixed_box_size()) {
    auto status = range.wait_for_available_bytes(hdr.get_box_size() - hdr.get_header_size());
    if (status != StreamReader::size_reached) {
      // TODO: return recoverable error at timeout
      return Error(heif_error_Invalid_input,
                   heif_suberror_End_of_data);
    }
  }

  // Security check: make sure that box size does not exceed int64 size.

  if (hdr.get_box_size() > (uint64_t) std::numeric_limits<int64_t>::max()) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Invalid_box_size);
  }

  int64_t box_size = static_cast<int64_t>(hdr.get_box_size());
  int64_t box_size_without_header = hdr.has_fixed_box_size() ? (box_size - hdr.get_header_size()) : (int64_t)range.get_remaining_bytes();

  // Box size may not be larger than remaining bytes in parent box.

  if ((int64_t)range.get_remaining_bytes() < box_size_without_header) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Invalid_box_size);
  }


  // Create child bitstream range and read box from that range.

  BitstreamRange boxrange(range.get_istream(),
                          box_size_without_header,
                          &range);

  err = box->parse(boxrange);
  if (err == Error::Ok) {
    *result = std::move(box);
  }

  boxrange.skip_to_end_of_box();

  return err;
}


std::string Box::dump(Indent& indent) const
{
  std::ostringstream sstr;

  sstr << BoxHeader::dump(indent);

  return sstr.str();
}


std::string FullBox::dump(Indent& indent) const
{
  std::ostringstream sstr;

  sstr << Box::dump(indent);

  sstr << indent << "version: " << ((int) m_version) << "\n"
       << indent << "flags: " << std::hex << m_flags << "\n";

  return sstr.str();
}


Error Box::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  Error err = write_children(writer);

  prepend_header(writer, box_start);

  return err;
}


std::shared_ptr<Box> Box::get_child_box(uint32_t short_type) const
{
  for (auto& box : m_children) {
    if (box->get_short_type() == short_type) {
      return box;
    }
  }

  return nullptr;
}


std::vector<std::shared_ptr<Box>> Box::get_child_boxes(uint32_t short_type) const
{
  std::vector<std::shared_ptr<Box>> result;
  for (auto& box : m_children) {
    if (box->get_short_type() == short_type) {
      result.push_back(box);
    }
  }

  return result;
}


bool Box::operator==(const Box& other) const
{
  if (this->get_short_type() != other.get_short_type()) {
    return false;
  }

  StreamWriter writer1;
  StreamWriter writer2;

  this->write(writer1);
  other.write(writer2);

  return writer1.get_data() == writer2.get_data();
}


bool Box::equal(const std::shared_ptr<Box>& box1, const std::shared_ptr<Box>& box2)
{
    if (!box1 || !box2) {
        return false;
    }
    return *box1 == *box2;
}


Error Box::read_children(BitstreamRange& range, int max_number)
{
  int count = 0;

  while (!range.eof() && !range.error()) {
    std::shared_ptr<Box> box;
    Error error = Box::read(range, &box);
    if (error != Error::Ok) {
      return error;
    }

    if (m_children.size() > MAX_CHILDREN_PER_BOX) {
      std::stringstream sstr;
      sstr << "Maximum number of child boxes " << MAX_CHILDREN_PER_BOX << " exceeded.";

      // Sanity check.
      return Error(heif_error_Memory_allocation_error,
                   heif_suberror_Security_limit_exceeded,
                   sstr.str());
    }

    m_children.push_back(std::move(box));


    // count the new child and end reading new children when we reached the expected number

    count++;

    if (max_number != READ_CHILDREN_ALL &&
        count == max_number) {
      break;
    }
  }

  return range.get_error();
}


Error Box::write_children(StreamWriter& writer) const
{
  for (const auto& child : m_children) {
    Error err = child->write(writer);
    if (err) {
      return err;
    }
  }

  return Error::Ok;
}


std::string Box::dump_children(Indent& indent) const
{
  std::ostringstream sstr;

  bool first = true;

  indent++;
  for (const auto& childBox : m_children) {
    if (first) {
      first = false;
    }
    else {
      sstr << indent << "\n";
    }

    sstr << childBox->dump(indent);
  }
  indent--;

  return sstr.str();
}


void Box::derive_box_version_recursive()
{
  derive_box_version();

  for (auto& child : m_children) {
    child->derive_box_version_recursive();
  }
}


Error Box_other::parse(BitstreamRange& range)
{
  if (has_fixed_box_size()) {
    size_t len = get_box_size() - get_header_size();
    m_data.resize(len);
    range.read(m_data.data(), len);
  }
  else {
    // TODO: boxes until end of file (we will probably never need this)
  }

  return range.get_error();
}


Error Box_other::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write(m_data);

  prepend_header(writer, box_start);

  return Error::Ok;
}


std::string Box_other::dump(Indent& indent) const
{
  std::ostringstream sstr;

  sstr << BoxHeader::dump(indent);

  // --- show raw box content

  sstr << std::hex << std::setfill('0');

  for (size_t i = 0; i < m_data.size(); i++) {
    if (i % 16 == 0) {
      // start of line

      if (i == 0) {
        sstr << indent << "data: ";
      }
      else {
        sstr << indent << "      ";
      }
      sstr << std::setw(4) << i << ": "; // address
    }
    else if (i % 16 == 8) {
      // space in middle
      sstr << "  ";
    }
    else {
      // space between bytes
      sstr << " ";
    }

    sstr << std::setw(2) << ((int) m_data[i]);

    if (i % 16 == 15 || i == m_data.size() - 1) {
      sstr << "\n";
    }
  }

  return sstr.str();
}


Error Box_ftyp::parse(BitstreamRange& range)
{
  m_major_brand = range.read32();
  m_minor_version = range.read32();

  if (get_box_size() <= get_header_size() + 8) {
    // Sanity check.
    return Error(heif_error_Invalid_input,
                 heif_suberror_Invalid_box_size,
                 "ftyp box too small (less than 8 bytes)");
  }

  uint64_t n_minor_brands = (get_box_size() - get_header_size() - 8) / 4;

  for (uint64_t i = 0; i < n_minor_brands && !range.error(); i++) {
    m_compatible_brands.push_back(range.read32());
  }

  return range.get_error();
}


bool Box_ftyp::has_compatible_brand(heif_brand2 brand) const
{
  return std::find(m_compatible_brands.begin(),
                   m_compatible_brands.end(),
                   brand) !=
         m_compatible_brands.end();
}


std::string Box_ftyp::dump(Indent& indent) const
{
  std::ostringstream sstr;

  sstr << BoxHeader::dump(indent);

  sstr << indent << "major brand: " << to_fourcc(m_major_brand) << "\n"
       << indent << "minor version: " << m_minor_version << "\n"
       << indent << "compatible brands: ";

  bool first = true;
  for (uint32_t brand : m_compatible_brands) {
    if (first) { first = false; }
    else { sstr << ','; }

    sstr << to_fourcc(brand);
  }
  sstr << "\n";

  return sstr.str();
}


void Box_ftyp::add_compatible_brand(heif_brand2 brand)
{
  if (!has_compatible_brand(brand)) {
    m_compatible_brands.push_back(brand);
  }
}


Error Box_ftyp::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write32(m_major_brand);
  writer.write32(m_minor_version);

  for (uint32_t b : m_compatible_brands) {
    writer.write32(b);
  }

  prepend_header(writer, box_start);

  return Error::Ok;
}


Error Box_meta::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  /*
  uint64_t boxSizeLimit;
  if (get_box_size() == BoxHeader::size_until_end_of_file) {
    boxSizeLimit = sizeLimit;
  }
  else {
    boxSizeLimit = get_box_size() - get_header_size();
  }
  */

  return read_children(range);
}


std::string Box_meta::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << dump_children(indent);

  return sstr.str();
}


Error Box_hdlr::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  m_pre_defined = range.read32();
  m_handler_type = range.read32();

  for (int i = 0; i < 3; i++) {
    m_reserved[i] = range.read32();
  }

  m_name = range.read_string();

  return range.get_error();
}


std::string Box_hdlr::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << indent << "pre_defined: " << m_pre_defined << "\n"
       << indent << "handler_type: " << to_fourcc(m_handler_type) << "\n"
       << indent << "name: " << m_name << "\n";

  return sstr.str();
}


Error Box_hdlr::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write32(m_pre_defined);
  writer.write32(m_handler_type);

  for (int i = 0; i < 3; i++) {
    writer.write32(m_reserved[i]);
  }

  writer.write(m_name);

  prepend_header(writer, box_start);

  return Error::Ok;
}


Error Box_pitm::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  if (get_version() == 0) {
    m_item_ID = range.read16();
  }
  else {
    m_item_ID = range.read32();
  }

  return range.get_error();
}


std::string Box_pitm::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << indent << "item_ID: " << m_item_ID << "\n";

  return sstr.str();
}


void Box_pitm::derive_box_version()
{
  if (m_item_ID <= 0xFFFF) {
    set_version(0);
  }
  else {
    set_version(1);
  }
}


Error Box_pitm::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  if (get_version() == 0) {
    assert(m_item_ID <= 0xFFFF);
    writer.write16((uint16_t) m_item_ID);
  }
  else {
    writer.write32(m_item_ID);
  }

  prepend_header(writer, box_start);

  return Error::Ok;
}


Error Box_iloc::parse(BitstreamRange& range)
{
  /*
  printf("box size: %d\n",get_box_size());
  printf("header size: %d\n",get_header_size());
  printf("start limit: %d\n",sizeLimit);
  */

  parse_full_box_header(range);

  uint16_t values4 = range.read16();

  int offset_size = (values4 >> 12) & 0xF;
  int length_size = (values4 >> 8) & 0xF;
  int base_offset_size = (values4 >> 4) & 0xF;
  int index_size = 0;

  if (get_version() >= 1) {
    index_size = (values4 & 0xF);
  }

  uint32_t item_count;
  if (get_version() < 2) {
    item_count = range.read16();
  }
  else {
    item_count = range.read32();
  }

  // Sanity check.
  if (item_count > MAX_ILOC_ITEMS) {
    std::stringstream sstr;
    sstr << "iloc box contains " << item_count << " items, which exceeds the security limit of "
         << MAX_ILOC_ITEMS << " items.";

    return Error(heif_error_Memory_allocation_error,
                 heif_suberror_Security_limit_exceeded,
                 sstr.str());
  }

  for (uint32_t i = 0; i < item_count; i++) {
    Item item;

    if (get_version() < 2) {
      item.item_ID = range.read16();
    }
    else {
      item.item_ID = range.read32();
    }

    if (get_version() >= 1) {
      values4 = range.read16();
      item.construction_method = (values4 & 0xF);
    }

    item.data_reference_index = range.read16();

    item.base_offset = 0;
    if (base_offset_size == 4) {
      item.base_offset = range.read32();
    }
    else if (base_offset_size == 8) {
      item.base_offset = ((uint64_t) range.read32()) << 32;
      item.base_offset |= range.read32();
    }

    int extent_count = range.read16();
    // Sanity check.
    if (extent_count > MAX_ILOC_EXTENTS_PER_ITEM) {
      std::stringstream sstr;
      sstr << "Number of extents in iloc box (" << extent_count << ") exceeds security limit ("
           << MAX_ILOC_EXTENTS_PER_ITEM << ")\n";

      return Error(heif_error_Memory_allocation_error,
                   heif_suberror_Security_limit_exceeded,
                   sstr.str());
    }

    for (int e = 0; e < extent_count; e++) {
      Extent extent;

      if (index_size == 4) {
        extent.index = range.read32();
      }
      else if (index_size == 8) {
        extent.index = ((uint64_t) range.read32()) << 32;
        extent.index |= range.read32();
      }

      extent.offset = 0;
      if (offset_size == 4) {
        extent.offset = range.read32();
      }
      else if (offset_size == 8) {
        extent.offset = ((uint64_t) range.read32()) << 32;
        extent.offset |= range.read32();
      }


      extent.length = 0;
      if (length_size == 4) {
        extent.length = range.read32();
      }
      else if (length_size == 8) {
        extent.length = ((uint64_t) range.read32()) << 32;
        extent.length |= range.read32();
      }

      item.extents.push_back(extent);
    }

    if (!range.error()) {
      m_items.push_back(item);
    }
  }

  //printf("end limit: %d\n",sizeLimit);

  return range.get_error();
}


std::string Box_iloc::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  for (const Item& item : m_items) {
    sstr << indent << "item ID: " << item.item_ID << "\n"
         << indent << "  construction method: " << ((int) item.construction_method) << "\n"
         << indent << "  data_reference_index: " << std::hex
         << item.data_reference_index << std::dec << "\n"
         << indent << "  base_offset: " << item.base_offset << "\n";

    sstr << indent << "  extents: ";
    for (const Extent& extent : item.extents) {
      sstr << extent.offset << "," << extent.length;
      if (extent.index != 0) {
        sstr << ";index=" << extent.index;
      }
      sstr << " ";
    }
    sstr << "\n";
  }

  return sstr.str();
}


Error Box_iloc::read_data(const Item& item,
                          const std::shared_ptr<StreamReader>& istr,
                          const std::shared_ptr<Box_idat>& idat,
                          std::vector<uint8_t>* dest) const
{
  // TODO: this function should always append the data to the output vector as this is used when
  //       the image data is concatenated with data in a configuration box. However, it seems that
  //       this function clears the array in some cases. This should be corrected.

  for (const auto& extent : item.extents) {
    if (item.construction_method == 0) {

      // --- security check that we do not allocate too much memory

      size_t old_size = dest->size();
      if (MAX_MEMORY_BLOCK_SIZE - old_size < extent.length) {
        std::stringstream sstr;
        sstr << "iloc box contained " << extent.length << " bytes, total memory size would be "
             << (old_size + extent.length) << " bytes, exceeding the security limit of "
             << MAX_MEMORY_BLOCK_SIZE << " bytes";

        return Error(heif_error_Memory_allocation_error,
                     heif_suberror_Security_limit_exceeded,
                     sstr.str());
      }


      // --- make sure that all data is available

      if (extent.offset > MAX_FILE_POS ||
          item.base_offset > MAX_FILE_POS ||
          extent.length > MAX_FILE_POS) {
        return Error(heif_error_Invalid_input,
                     heif_suberror_Security_limit_exceeded,
                     "iloc data pointers out of allowed range");
      }

      StreamReader::grow_status status = istr->wait_for_file_size(extent.offset + item.base_offset + extent.length);
      if (status == StreamReader::size_beyond_eof) {
        // Out-of-bounds
        // TODO: I think we should not clear this. Maybe we want to try reading again later and
        // hence should not lose the data already read.
        dest->clear();

        std::stringstream sstr;
        sstr << "Extent in iloc box references data outside of file bounds "
             << "(points to file position " << extent.offset + item.base_offset << ")\n";

        return Error(heif_error_Invalid_input,
                     heif_suberror_End_of_data,
                     sstr.str());
      }
      else if (status == StreamReader::timeout) {
        // TODO: maybe we should introduce some 'Recoverable error' instead of 'Invalid input'
        return Error(heif_error_Invalid_input,
                     heif_suberror_End_of_data);
      }

      // --- move file pointer to start of data

      bool success = istr->seek(extent.offset + item.base_offset);
      assert(success);
      (void) success;


      // --- read data

      dest->resize(static_cast<size_t>(old_size + extent.length));
      success = istr->read((char*) dest->data() + old_size, static_cast<size_t>(extent.length));
      assert(success);
      (void) success;
    }
    else if (item.construction_method == 1) {
      if (!idat) {
        return Error(heif_error_Invalid_input,
                     heif_suberror_No_idat_box,
                     "idat box referenced in iref box is not present in file");
      }

      idat->read_data(istr,
                      extent.offset + item.base_offset,
                      extent.length,
                      *dest);
    }
    else {
      std::stringstream sstr;
      sstr << "Item construction method " << (int) item.construction_method << " not implemented";
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_item_construction_method,
                   sstr.str());
    }
  }

  return Error::Ok;
}


Error Box_iloc::append_data(heif_item_id item_ID,
                            const std::vector<uint8_t>& data,
                            uint8_t construction_method)
{
  // check whether this item ID already exists

  size_t idx;
  for (idx = 0; idx < m_items.size(); idx++) {
    if (m_items[idx].item_ID == item_ID) {
      break;
    }
  }

  // item does not exist -> add a new one to the end

  if (idx == m_items.size()) {
    Item item;
    item.item_ID = item_ID;
    item.construction_method = construction_method;

    m_items.push_back(item);
  }

  if (m_items[idx].construction_method != construction_method) {
    // TODO: return error: construction methods do not match
  }

  Extent extent;
  extent.data = data;

  if (construction_method == 1) {
    extent.offset = m_idat_offset;
    extent.length = data.size();

    m_idat_offset += (int) data.size();
  }

  m_items[idx].extents.push_back(std::move(extent));

  return Error::Ok;
}


void Box_iloc::derive_box_version()
{
  int min_version = m_user_defined_min_version;

  if (m_items.size() > 0xFFFF) {
    min_version = std::max(min_version, 2);
  }

  m_offset_size = 0;
  m_length_size = 0;
  m_base_offset_size = 0;
  m_index_size = 0;

  for (const auto& item : m_items) {
    // check item_ID size
    if (item.item_ID > 0xFFFF) {
      min_version = std::max(min_version, 2);
    }

    // check construction method
    if (item.construction_method != 0) {
      min_version = std::max(min_version, 1);
    }

    // base offset size
    /*
    if (item.base_offset > 0xFFFFFFFF) {
      m_base_offset_size = 8;
    }
    else if (item.base_offset > 0) {
      m_base_offset_size = 4;
    }
    */

    /*
    for (const auto& extent : item.extents) {
      // extent index size

      if (extent.index != 0) {
        min_version = std::max(min_version, 1);
        m_index_size = 4;
      }

      if (extent.index > 0xFFFFFFFF) {
        m_index_size = 8;
      }

      // extent offset size
      if (extent.offset > 0xFFFFFFFF) {
        m_offset_size = 8;
      }
      else {
        m_offset_size = 4;
      }

      // extent length size
      if (extent.length > 0xFFFFFFFF) {
        m_length_size = 8;
      }
      else {
        m_length_size = 4;
      }
    }
      */
  }

  m_offset_size = 4;
  m_length_size = 4;
  m_base_offset_size = 4; // TODO: or could be 8 if we write >4GB files
  m_index_size = 0;

  set_version((uint8_t) min_version);
}


Error Box_iloc::write(StreamWriter& writer) const
{
  // --- write idat

  size_t sum_idat_size = 0;

  for (const auto& item : m_items) {
    if (item.construction_method == 1) {
      for (const auto& extent : item.extents) {
        sum_idat_size += extent.data.size();
      }
    }
  }

  if (sum_idat_size > 0) {
    writer.write32((uint32_t) (sum_idat_size + 8));
    writer.write32(fourcc("idat"));

    for (auto& item : m_items) {
      if (item.construction_method == 1) {
        for (auto& extent : item.extents) {
          writer.write(extent.data);
        }
      }
    }
  }


  // --- write iloc box

  size_t box_start = reserve_box_header_space(writer);

  m_iloc_box_start = writer.get_position();

  int nSkip = 0;

  nSkip += 2;
  nSkip += (get_version() < 2) ? 2 : 4; // item_count

  for (const auto& item : m_items) {
    nSkip += (get_version() < 2) ? 2 : 4; // item_ID
    nSkip += (get_version() >= 1) ? 2 : 0; // construction method
    nSkip += 4 + m_base_offset_size;

    for (const auto& extent : item.extents) {
      (void) extent;

      if (get_version() >= 1) {
        nSkip += m_index_size;
      }

      nSkip += m_offset_size + m_length_size;
    }
  }

  writer.skip(nSkip);
  prepend_header(writer, box_start);

  return Error::Ok;
}


Error Box_iloc::write_mdat_after_iloc(StreamWriter& writer)
{
  // --- compute sum of all mdat data

  size_t sum_mdat_size = 0;

  for (const auto& item : m_items) {
    if (item.construction_method == 0) {
      for (const auto& extent : item.extents) {
        sum_mdat_size += extent.data.size();
      }
    }
  }

  if (sum_mdat_size > 0xFFFFFFFF) {
    // TODO: box size > 4 GB
  }


  // --- write mdat box

  writer.write32((uint32_t) (sum_mdat_size + 8));
  writer.write32(fourcc("mdat"));

  for (auto& item : m_items) {
    if (item.construction_method == 0) {
      item.base_offset = writer.get_position();

      for (auto& extent : item.extents) {
        extent.offset = writer.get_position() - item.base_offset;
        extent.length = extent.data.size();

        writer.write(extent.data);
      }
    }
  }


  // --- patch iloc box

  patch_iloc_header(writer);

  return Error::Ok;
}


void Box_iloc::patch_iloc_header(StreamWriter& writer) const
{
  size_t old_pos = writer.get_position();
  writer.set_position(m_iloc_box_start);

  writer.write8((uint8_t) ((m_offset_size << 4) | (m_length_size)));
  writer.write8((uint8_t) ((m_base_offset_size << 4) | (m_index_size)));

  if (get_version() < 2) {
    writer.write16((uint16_t) m_items.size());
  }
  else {
    writer.write32((uint32_t) m_items.size());
  }

  for (const auto& item : m_items) {
    if (get_version() < 2) {
      writer.write16((uint16_t) item.item_ID);
    }
    else {
      writer.write32((uint32_t) item.item_ID);
    }

    if (get_version() >= 1) {
      writer.write16(item.construction_method);
    }

    writer.write16(item.data_reference_index);
    writer.write(m_base_offset_size, item.base_offset);
    writer.write16((uint16_t) item.extents.size());

    for (const auto& extent : item.extents) {
      if (get_version() >= 1 && m_index_size > 0) {
        writer.write(m_index_size, extent.index);
      }

      writer.write(m_offset_size, extent.offset);
      writer.write(m_length_size, extent.length);
    }
  }

  writer.set_position(old_pos);
}


Error Box_infe::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  if (get_version() <= 1) {
    m_item_ID = range.read16();
    m_item_protection_index = range.read16();

    m_item_name = range.read_string();
    m_content_type = range.read_string();
    m_content_encoding = range.read_string();
  }

  if (get_version() >= 2) {
    m_hidden_item = (get_flags() & 1);

    if (get_version() == 2) {
      m_item_ID = range.read16();
    }
    else {
      m_item_ID = range.read32();
    }

    m_item_protection_index = range.read16();
    uint32_t item_type = range.read32();
    if (item_type != 0) {
      m_item_type = to_fourcc(item_type);
    }

    m_item_name = range.read_string();
    if (item_type == fourcc("mime")) {
      m_content_type = range.read_string();
      m_content_encoding = range.read_string();
    }
    else if (item_type == fourcc("uri ")) {
      m_item_uri_type = range.read_string();
    }
  }

  return range.get_error();
}


void Box_infe::derive_box_version()
{
  int min_version = 0;

  if (m_hidden_item) {
    min_version = std::max(min_version, 2);
  }

  if (m_item_ID > 0xFFFF) {
    min_version = std::max(min_version, 3);
  }


  if (m_item_type != "") {
    min_version = std::max(min_version, 2);
  }

  set_version((uint8_t) min_version);
}


void Box_infe::set_hidden_item(bool hidden)
{
  m_hidden_item = hidden;

  if (m_hidden_item) {
    set_flags(get_flags() | 1U);
  }
  else {
    set_flags(get_flags() & ~1U);
  }
}

Error Box_infe::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  if (get_version() <= 1) {
    writer.write16((uint16_t) m_item_ID);
    writer.write16(m_item_protection_index);

    writer.write(m_item_name);
    writer.write(m_content_type);
    writer.write(m_content_encoding);
  }

  if (get_version() >= 2) {
    if (get_version() == 2) {
      writer.write16((uint16_t) m_item_ID);
    }
    else if (get_version() == 3) {
      writer.write32(m_item_ID);
    }

    writer.write16(m_item_protection_index);

    if (m_item_type.empty()) {
      writer.write32(0);
    }
    else {
      writer.write32(from_fourcc(m_item_type.c_str()));
    }

    writer.write(m_item_name);
    if (m_item_type == "mime") {
      writer.write(m_content_type);
      writer.write(m_content_encoding);
    }
    else if (m_item_type == "uri ") {
      writer.write(m_item_uri_type);
    }
  }

  prepend_header(writer, box_start);

  return Error::Ok;
}


std::string Box_infe::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "item_ID: " << m_item_ID << "\n"
       << indent << "item_protection_index: " << m_item_protection_index << "\n"
       << indent << "item_type: " << m_item_type << "\n"
       << indent << "item_name: " << m_item_name << "\n"
       << indent << "content_type: " << m_content_type << "\n"
       << indent << "content_encoding: " << m_content_encoding << "\n"
       << indent << "item uri type: " << m_item_uri_type << "\n"
       << indent << "hidden item: " << std::boolalpha << m_hidden_item << "\n";

  return sstr.str();
}


Error Box_iinf::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  int nEntries_size = (get_version() > 0) ? 4 : 2;

  uint32_t item_count;
  if (nEntries_size == 2) {
    item_count = range.read16();
  }
  else {
    item_count = range.read32();
  }

  if (item_count == 0) {
    return Error::Ok;
  }

  // TODO: Only try to read "item_count" children.
  return read_children(range);
}


std::string Box_iinf::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << dump_children(indent);

  return sstr.str();
}


Error Box_iprp::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  return read_children(range);
}


void Box_iinf::derive_box_version()
{
  if (m_children.size() > 0xFFFF) {
    set_version(1);
  }
  else {
    set_version(0);
  }
}


Error Box_iinf::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  int nEntries_size = (get_version() > 0) ? 4 : 2;

  writer.write(nEntries_size, m_children.size());


  Error err = write_children(writer);

  prepend_header(writer, box_start);

  return err;
}


std::string Box_iprp::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << dump_children(indent);

  return sstr.str();
}


int Box_ipco::find_or_append_child_box(const std::shared_ptr<Box>& box)
{
  for (int i = 0; i < (int) m_children.size(); i++) {
    if (Box::equal(m_children[i], box)) {
      return i;
    }
  }
  return append_child_box(box);
}


Error Box_ipco::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  return read_children(range);
}


std::string Box_ipco::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << dump_children(indent);

  return sstr.str();
}


Error Box_pixi::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  StreamReader::grow_status status;
  uint8_t num_channels = range.read8();
  status = range.wait_for_available_bytes(num_channels);
  if (status != StreamReader::size_reached) {
    // TODO: return recoverable error at timeout
    return Error(heif_error_Invalid_input,
                 heif_suberror_End_of_data);
  }

  m_bits_per_channel.resize(num_channels);
  for (int i = 0; i < num_channels; i++) {
    m_bits_per_channel[i] = range.read8();
  }

  return range.get_error();
}


std::string Box_pixi::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "bits_per_channel: ";

  for (size_t i = 0; i < m_bits_per_channel.size(); i++) {
    if (i > 0) sstr << ",";
    sstr << ((int) m_bits_per_channel[i]);
  }

  sstr << "\n";

  return sstr.str();
}


Error Box_pixi::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  if (m_bits_per_channel.size() > 255 ||
      m_bits_per_channel.empty()) {
    // TODO: error
    assert(false);
  }

  writer.write8((uint8_t) (m_bits_per_channel.size()));
  for (size_t i = 0; i < m_bits_per_channel.size(); i++) {
    writer.write8(m_bits_per_channel[i]);
  }

  prepend_header(writer, box_start);

  return Error::Ok;
}


Error Box_pasp::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  hSpacing = range.read32();
  vSpacing = range.read32();

  return range.get_error();
}


std::string Box_pasp::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "hSpacing: " << hSpacing << "\n";
  sstr << indent << "vSpacing: " << vSpacing << "\n";

  return sstr.str();
}


Error Box_pasp::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write32(hSpacing);
  writer.write32(vSpacing);

  prepend_header(writer, box_start);

  return Error::Ok;
}


Error Box_lsel::parse(BitstreamRange& range)
{
  layer_id = range.read16();

  return range.get_error();
}


std::string Box_lsel::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "layer_id: " << layer_id << "\n";

  return sstr.str();
}


Error Box_lsel::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write16(layer_id);

  prepend_header(writer, box_start);

  return Error::Ok;
}


Error Box_clli::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  clli.max_content_light_level = range.read16();
  clli.max_pic_average_light_level = range.read16();

  return range.get_error();
}


std::string Box_clli::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "max_content_light_level: " << clli.max_content_light_level << "\n";
  sstr << indent << "max_pic_average_light_level: " << clli.max_pic_average_light_level << "\n";

  return sstr.str();
}


Error Box_clli::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write16(clli.max_content_light_level);
  writer.write16(clli.max_pic_average_light_level);

  prepend_header(writer, box_start);

  return Error::Ok;
}


Box_mdcv::Box_mdcv()
{
  set_short_type(fourcc("mdcv"));
  
  memset(&mdcv, 0, sizeof(heif_mastering_display_colour_volume));
}


Error Box_mdcv::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  for (int c = 0; c < 3; c++) {
    mdcv.display_primaries_x[c] = range.read16();
    mdcv.display_primaries_y[c] = range.read16();
  }

  mdcv.white_point_x = range.read16();
  mdcv.white_point_y = range.read16();
  mdcv.max_display_mastering_luminance = range.read32();
  mdcv.min_display_mastering_luminance = range.read32();

  return range.get_error();
}


std::string Box_mdcv::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "display_primaries (x,y): ";
  sstr << "(" << mdcv.display_primaries_x[0] << ";" << mdcv.display_primaries_y[0] << "), ";
  sstr << "(" << mdcv.display_primaries_x[1] << ";" << mdcv.display_primaries_y[1] << "), ";
  sstr << "(" << mdcv.display_primaries_x[2] << ";" << mdcv.display_primaries_y[2] << ")\n";

  sstr << indent << "white point (x,y): (" << mdcv.white_point_x << ";" << mdcv.white_point_y << ")\n";
  sstr << indent << "max display mastering luminance: " << mdcv.max_display_mastering_luminance << "\n";
  sstr << indent << "min display mastering luminance: " << mdcv.min_display_mastering_luminance << "\n";

  return sstr.str();
}


Error Box_mdcv::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  for (int c = 0; c < 3; c++) {
    writer.write16(mdcv.display_primaries_x[c]);
    writer.write16(mdcv.display_primaries_y[c]);
  }

  writer.write16(mdcv.white_point_x);
  writer.write16(mdcv.white_point_y);

  writer.write32(mdcv.max_display_mastering_luminance);
  writer.write32(mdcv.min_display_mastering_luminance);

  prepend_header(writer, box_start);

  return Error::Ok;
}


Error Box_ipco::get_properties_for_item_ID(uint32_t itemID,
                                           const std::shared_ptr<class Box_ipma>& ipma,
                                           std::vector<std::shared_ptr<Box>>& out_properties) const
{
  const std::vector<Box_ipma::PropertyAssociation>* property_assoc = ipma->get_properties_for_item_ID(itemID);
  if (property_assoc == nullptr) {
    std::stringstream sstr;
    sstr << "Item (ID=" << itemID << ") has no properties assigned to it in ipma box";

    return Error(heif_error_Invalid_input,
                 heif_suberror_No_properties_assigned_to_item,
                 sstr.str());
  }

  const auto& allProperties = get_all_child_boxes();
  for (const Box_ipma::PropertyAssociation& assoc : *property_assoc) {
    if (assoc.property_index > allProperties.size()) {
      std::stringstream sstr;
      sstr << "Nonexisting property (index=" << assoc.property_index << ") for item "
           << " ID=" << itemID << " referenced in ipma box";

      return Error(heif_error_Invalid_input,
                   heif_suberror_Ipma_box_references_nonexisting_property,
                   sstr.str());
    }

    if (assoc.property_index > 0) {
      out_properties.push_back(allProperties[assoc.property_index - 1]);
    }
  }

  return Error::Ok;
}


std::shared_ptr<Box> Box_ipco::get_property_for_item_ID(heif_item_id itemID,
                                                        const std::shared_ptr<class Box_ipma>& ipma,
                                                        uint32_t box_type) const
{
  const std::vector<Box_ipma::PropertyAssociation>* property_assoc = ipma->get_properties_for_item_ID(itemID);
  if (property_assoc == nullptr) {
    return nullptr;
  }

  const auto& allProperties = get_all_child_boxes();
  for (const Box_ipma::PropertyAssociation& assoc : *property_assoc) {
    if (assoc.property_index > allProperties.size() ||
        assoc.property_index == 0) {
      return nullptr;
    }

    const auto& property = allProperties[assoc.property_index - 1];
    if (property->get_short_type() == box_type) {
      return property;
    }
  }

  return nullptr;
}


bool Box_ipco::is_property_essential_for_item(heif_item_id itemId,
                                              const std::shared_ptr<const class Box>& property,
                                              const std::shared_ptr<class Box_ipma>& ipma) const
{
  // find property index

  for (int i=0;i<(int)m_children.size();i++) {
    if (m_children[i] == property) {
      return ipma->is_property_essential_for_item(itemId, i);
    }
  }

  assert(false); // non-existing property
  return false;
}


Error Box_ispe::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  m_image_width = range.read32();
  m_image_height = range.read32();

  return range.get_error();
}


std::string Box_ispe::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "image width: " << m_image_width << "\n"
       << indent << "image height: " << m_image_height << "\n";

  return sstr.str();
}


Error Box_ispe::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write32(m_image_width);
  writer.write32(m_image_height);

  prepend_header(writer, box_start);

  return Error::Ok;
}


bool Box_ispe::operator==(const Box& other) const
{
  const auto* other_ispe = dynamic_cast<const Box_ispe*>(&other);
  if (other_ispe == nullptr) {
    return false;
  }

  return (m_image_width == other_ispe->m_image_width &&
          m_image_height == other_ispe->m_image_height);
}


Error Box_ipma::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  uint32_t entry_cnt = range.read32();
  for (uint32_t i = 0; i < entry_cnt && !range.error() && !range.eof(); i++) {
    Entry entry;
    if (get_version() < 1) {
      entry.item_ID = range.read16();
    }
    else {
      entry.item_ID = range.read32();
    }

    int assoc_cnt = range.read8();
    for (int k = 0; k < assoc_cnt; k++) {
      PropertyAssociation association;

      uint16_t index;
      if (get_flags() & 1) {
        index = range.read16();
        association.essential = !!(index & 0x8000);
        association.property_index = (index & 0x7fff);
      }
      else {
        index = range.read8();
        association.essential = !!(index & 0x80);
        association.property_index = (index & 0x7f);
      }

      entry.associations.push_back(association);
    }

    m_entries.push_back(entry);
  }

  return range.get_error();
}


const std::vector<Box_ipma::PropertyAssociation>* Box_ipma::get_properties_for_item_ID(uint32_t itemID) const
{
  for (const auto& entry : m_entries) {
    if (entry.item_ID == itemID) {
      return &entry.associations;
    }
  }

  return nullptr;
}


bool Box_ipma::is_property_essential_for_item(heif_item_id itemId, int propertyIndex) const
{
  for (const auto& entry : m_entries) {
    if (entry.item_ID == itemId) {
      for (const auto& assoc : entry.associations) {
        if (assoc.property_index == propertyIndex) {
          return assoc.essential;
        }
      }
    }
  }

  assert(false);
  return false;
}


void Box_ipma::add_property_for_item_ID(heif_item_id itemID,
                                        PropertyAssociation assoc)
{
  size_t idx;
  for (idx = 0; idx < m_entries.size(); idx++) {
    if (m_entries[idx].item_ID == itemID) {
      break;
    }
  }

  // if itemID does not exist, add a new entry
  if (idx == m_entries.size()) {
    Entry entry;
    entry.item_ID = itemID;
    m_entries.push_back(entry);
  }

  // add the property association
  m_entries[idx].associations.push_back(assoc);
}


std::string Box_ipma::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  for (const Entry& entry : m_entries) {
    sstr << indent << "associations for item ID: " << entry.item_ID << "\n";
    indent++;
    for (const auto& assoc : entry.associations) {
      sstr << indent << "property index: " << assoc.property_index
           << " (essential: " << std::boolalpha << assoc.essential << ")\n";
    }
    indent--;
  }

  return sstr.str();
}


void Box_ipma::derive_box_version()
{
  int version = 0;
  bool large_property_indices = false;

  for (const Entry& entry : m_entries) {
    if (entry.item_ID > 0xFFFF) {
      version = 1;
    }

    for (const auto& assoc : entry.associations) {
      if (assoc.property_index > 0x7F) {
        large_property_indices = true;
      }
    }
  }

  set_version((uint8_t) version);
  set_flags(large_property_indices ? 1 : 0);
}


Error Box_ipma::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  size_t entry_cnt = m_entries.size();
  writer.write32((uint32_t) entry_cnt);

  for (const Entry& entry : m_entries) {

    if (get_version() < 1) {
      writer.write16((uint16_t) entry.item_ID);
    }
    else {
      writer.write32(entry.item_ID);
    }

    size_t assoc_cnt = entry.associations.size();
    if (assoc_cnt > 0xFF) {
      // TODO: error, too many associations
    }

    writer.write8((uint8_t) assoc_cnt);

    for (const PropertyAssociation& association : entry.associations) {

      if (get_flags() & 1) {
        writer.write16((uint16_t) ((association.essential ? 0x8000 : 0) |
                                   (association.property_index & 0x7FFF)));
      }
      else {
        writer.write8((uint8_t) ((association.essential ? 0x80 : 0) |
                                 (association.property_index & 0x7F)));
      }
    }
  }

  prepend_header(writer, box_start);

  return Error::Ok;
}


void Box_ipma::insert_entries_from_other_ipma_box(const Box_ipma& b)
{
  m_entries.insert(m_entries.end(),
                   b.m_entries.begin(),
                   b.m_entries.end());
}


Error Box_auxC::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  m_aux_type = range.read_string();

  while (!range.eof()) {
    m_aux_subtypes.push_back(range.read8());
  }

  return range.get_error();
}


Error Box_auxC::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write(m_aux_type);

  for (uint8_t subtype : m_aux_subtypes) {
    writer.write8(subtype);
  }

  prepend_header(writer, box_start);

  return Error::Ok;
}


std::string Box_auxC::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "aux type: " << m_aux_type << "\n"
       << indent << "aux subtypes: ";
  for (uint8_t subtype : m_aux_subtypes) {
    sstr << std::hex << std::setw(2) << std::setfill('0') << ((int) subtype) << " ";
  }

  sstr << "\n";

  return sstr.str();
}


Error Box_irot::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  uint16_t rotation = range.read8();
  rotation &= 0x03;

  m_rotation = rotation * 90;

  return range.get_error();
}


Error Box_irot::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write8((uint8_t) (m_rotation / 90));

  prepend_header(writer, box_start);

  return Error::Ok;
}


std::string Box_irot::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "rotation: " << m_rotation << " degrees (CCW)\n";

  return sstr.str();
}


Error Box_imir::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  uint8_t axis = range.read8();
  if (axis & 1) {
    m_axis = heif_transform_mirror_direction_horizontal;
  }
  else {
    m_axis = heif_transform_mirror_direction_vertical;
  }

  return range.get_error();
}


Error Box_imir::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write8(m_axis);

  prepend_header(writer, box_start);

  return Error::Ok;
}


std::string Box_imir::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "mirror direction: ";
  switch (m_axis) {
    case heif_transform_mirror_direction_vertical:
      sstr << "vertical\n";
      break;
    case heif_transform_mirror_direction_horizontal:
      sstr << "horizontal\n";
      break;
    case heif_transform_mirror_direction_invalid:
      sstr << "invalid\n";
      break;
  }

  return sstr.str();
}


Error Box_clap::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  uint32_t clean_aperture_width_num = range.read32();
  uint32_t clean_aperture_width_den = range.read32();
  uint32_t clean_aperture_height_num = range.read32();
  uint32_t clean_aperture_height_den = range.read32();

  // Note: in the standard document 14496-12(2015), it says that the offset values should also be unsigned integers,
  // but this is obviously an error. Even the accompanying standard text says that offsets may be negative.
  int32_t horizontal_offset_num = (int32_t) range.read32();
  uint32_t horizontal_offset_den = (uint32_t) range.read32();
  int32_t vertical_offset_num = (int32_t) range.read32();
  uint32_t vertical_offset_den = (uint32_t) range.read32();

  if (clean_aperture_width_num > (uint32_t) std::numeric_limits<int32_t>::max() ||
      clean_aperture_width_den > (uint32_t) std::numeric_limits<int32_t>::max() ||
      clean_aperture_height_num > (uint32_t) std::numeric_limits<int32_t>::max() ||
      clean_aperture_height_den > (uint32_t) std::numeric_limits<int32_t>::max() ||
      horizontal_offset_den > (uint32_t) std::numeric_limits<int32_t>::max() ||
      vertical_offset_den > (uint32_t) std::numeric_limits<int32_t>::max()) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Invalid_fractional_number,
                 "Exceeded supported value range.");
  }

  m_clean_aperture_width = Fraction(clean_aperture_width_num,
                                    clean_aperture_width_den);
  m_clean_aperture_height = Fraction(clean_aperture_height_num,
                                     clean_aperture_height_den);
  m_horizontal_offset = Fraction(horizontal_offset_num, (int32_t) horizontal_offset_den);
  m_vertical_offset = Fraction(vertical_offset_num, (int32_t) vertical_offset_den);
  if (!m_clean_aperture_width.is_valid() || !m_clean_aperture_height.is_valid() ||
      !m_horizontal_offset.is_valid() || !m_vertical_offset.is_valid()) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Invalid_fractional_number);
  }

  return range.get_error();
}


Error Box_clap::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write32(m_clean_aperture_width.numerator);
  writer.write32(m_clean_aperture_width.denominator);
  writer.write32(m_clean_aperture_height.numerator);
  writer.write32(m_clean_aperture_height.denominator);
  writer.write32(m_horizontal_offset.numerator);
  writer.write32(m_horizontal_offset.denominator);
  writer.write32(m_vertical_offset.numerator);
  writer.write32(m_vertical_offset.denominator);

  prepend_header(writer, box_start);

  return Error::Ok;
}


std::string Box_clap::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "clean_aperture: " << m_clean_aperture_width.numerator
       << "/" << m_clean_aperture_width.denominator << " x "
       << m_clean_aperture_height.numerator << "/"
       << m_clean_aperture_height.denominator << "\n";
  sstr << indent << "offset: " << m_horizontal_offset.numerator << "/"
       << m_horizontal_offset.denominator << " ; "
       << m_vertical_offset.numerator << "/"
       << m_vertical_offset.denominator << "\n";

  return sstr.str();
}


int Box_clap::left_rounded(int image_width) const
{
  // pcX = horizOff + (width  - 1)/2
  // pcX ± (cleanApertureWidth - 1)/2

  // left = horizOff + (width-1)/2 - (clapWidth-1)/2

  Fraction pcX = m_horizontal_offset + Fraction(image_width - 1, 2);
  Fraction left = pcX - (m_clean_aperture_width - 1) / 2;

  return left.round_down();
}

int Box_clap::right_rounded(int image_width) const
{
  Fraction right = m_clean_aperture_width - 1 + left_rounded(image_width);

  return right.round();
}

int Box_clap::top_rounded(int image_height) const
{
  Fraction pcY = m_vertical_offset + Fraction(image_height - 1, 2);
  Fraction top = pcY - (m_clean_aperture_height - 1) / 2;

  return top.round();
}

int Box_clap::bottom_rounded(int image_height) const
{
  Fraction bottom = m_clean_aperture_height - 1 + top_rounded(image_height);

  return bottom.round();
}

int Box_clap::get_width_rounded() const
{
  return m_clean_aperture_width.round();
}

int Box_clap::get_height_rounded() const
{
  return m_clean_aperture_height.round();
}

void Box_clap::set(uint32_t clap_width, uint32_t clap_height,
                   uint32_t image_width, uint32_t image_height)
{
  assert(image_width >= clap_width);
  assert(image_height >= clap_height);

  m_clean_aperture_width = Fraction(clap_width, 1U);
  m_clean_aperture_height = Fraction(clap_height, 1U);

  m_horizontal_offset = Fraction(-(int32_t) (image_width - clap_width), 2);
  m_vertical_offset = Fraction(-(int32_t) (image_height - clap_height), 2);
}


Error Box_iref::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  while (!range.eof()) {
    Reference ref;

    Error err = ref.header.parse_header(range);
    if (err != Error::Ok) {
      return err;
    }

    if (get_version() == 0) {
      ref.from_item_ID = range.read16();
      int nRefs = range.read16();
      for (int i = 0; i < nRefs; i++) {
        ref.to_item_ID.push_back(range.read16());
        if (range.eof()) {
          break;
        }
      }
    }
    else {
      ref.from_item_ID = range.read32();
      int nRefs = range.read16();
      for (int i = 0; i < nRefs; i++) {
        ref.to_item_ID.push_back(range.read32());
        if (range.eof()) {
          break;
        }
      }
    }

    m_references.push_back(ref);
  }


  // --- check number of total refs

  size_t nTotalRefs = 0;
  for (const auto& ref : m_references) {
    nTotalRefs += ref.to_item_ID.size();
  }

  if (nTotalRefs > MAX_IREF_REFERENCES) {
    return Error(heif_error_Memory_allocation_error, heif_suberror_Security_limit_exceeded,
                 "Number of iref references exceeds security limit.");
  }

  // --- check for duplicate references

  for (const auto& ref : m_references) {
    std::set<heif_item_id> to_ids;
    for (const auto to_id : ref.to_item_ID) {
      if (to_ids.find(to_id) == to_ids.end()) {
        to_ids.insert(to_id);
      }
      else {
        return Error(heif_error_Invalid_input,
                     heif_suberror_Unspecified,
                     "'iref' has double references");
      }
    }
  }


#if 0
  // Note: This input sanity check does not work as expected.
  // Its idea was to prevent infinite recursions while decoding when the input file
  // contains cyclic references. However, apparently there are cases where cyclic
  // references are actually allowed, like with images that have premultiplied alpha channels:
  // | Box: iref -----
  // | size: 40   (header size: 12)
  // | reference with type 'auxl' from ID: 2 to IDs: 1
  // | reference with type 'prem' from ID: 1 to IDs: 2
  //
  // TODO: implement the infinite recursion detection in a different way. E.g. by passing down
  //       the already processed item-ids while decoding an image and checking whether the current
  //       item has already been decoded before.

  // --- check for cyclic references

  for (const auto& ref : m_references) {
    std::set<heif_item_id> reached_ids; // IDs that we have already reached in the DAG
    std::set<heif_item_id> todo;    // IDs that still need to be followed

    todo.insert(ref.from_item_ID);  // start at base item

    while (!todo.empty()) {
      // transfer ID into set of reached IDs
      auto id = *todo.begin();
      todo.erase(id);
      reached_ids.insert(id);

      // if this ID refers to another 'iref', follow it

      for (const auto& succ_ref : m_references) {
        if (succ_ref.from_item_ID == id) {

          // Check whether any successor IDs has been visited yet, which would be an error.
          // Otherwise, put that ID into the 'todo' set.

          for (const auto& succ_ref_id : succ_ref.to_item_ID) {
            if (reached_ids.find(succ_ref_id) != reached_ids.end()) {
              return Error(heif_error_Invalid_input,
                           heif_suberror_Unspecified,
                           "'iref' has cyclic references");
            }

            todo.insert(succ_ref_id);
          }
        }
      }
    }
  }
#endif

  return range.get_error();
}


void Box_iref::derive_box_version()
{
  uint8_t version = 0;

  for (const auto& ref : m_references) {
    if (ref.from_item_ID > 0xFFFF) {
      version = 1;
      break;
    }

    for (uint32_t r : ref.to_item_ID) {
      if (r > 0xFFFF) {
        version = 1;
        break;
      }
    }
  }

  set_version(version);
}


Error Box_iref::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  int id_size = ((get_version() == 0) ? 2 : 4);

  for (const auto& ref : m_references) {
    uint32_t box_size = uint32_t(4 + 4 + 2 + id_size * (1 + ref.to_item_ID.size()));

    // we write the BoxHeader ourselves since it is very simple
    writer.write32(box_size);
    writer.write32(ref.header.get_short_type());

    writer.write(id_size, ref.from_item_ID);
    writer.write16((uint16_t) ref.to_item_ID.size());

    for (uint32_t r : ref.to_item_ID) {
      writer.write(id_size, r);
    }
  }


  prepend_header(writer, box_start);

  return Error::Ok;
}


std::string Box_iref::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  for (const auto& ref : m_references) {
    sstr << indent << "reference with type '" << ref.header.get_type_string() << "'"
         << " from ID: " << ref.from_item_ID
         << " to IDs: ";
    for (uint32_t id : ref.to_item_ID) {
      sstr << id << " ";
    }
    sstr << "\n";
  }

  return sstr.str();
}


bool Box_iref::has_references(uint32_t itemID) const
{
  for (const Reference& ref : m_references) {
    if (ref.from_item_ID == itemID) {
      return true;
    }
  }

  return false;
}


std::vector<Box_iref::Reference> Box_iref::get_references_from(heif_item_id itemID) const
{
  std::vector<Reference> references;

  for (const Reference& ref : m_references) {
    if (ref.from_item_ID == itemID) {
      references.push_back(ref);
    }
  }

  return references;
}


std::vector<uint32_t> Box_iref::get_references(uint32_t itemID, uint32_t ref_type) const
{
  for (const Reference& ref : m_references) {
    if (ref.from_item_ID == itemID &&
        ref.header.get_short_type() == ref_type) {
      return ref.to_item_ID;
    }
  }

  return std::vector<uint32_t>();
}


void Box_iref::add_references(heif_item_id from_id, uint32_t type, const std::vector<heif_item_id>& to_ids)
{
  Reference ref;
  ref.header.set_short_type(type);
  ref.from_item_ID = from_id;
  ref.to_item_ID = to_ids;

  m_references.push_back(ref);
}


Error Box_idat::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  m_data_start_pos = range.get_istream()->get_position();

  return range.get_error();
}


Error Box_idat::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write(m_data_for_writing);

  prepend_header(writer, box_start);

  return Error::Ok;
}


std::string Box_idat::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  if (get_box_size() >= get_header_size()) {
    sstr << indent << "number of data bytes: " << get_box_size() - get_header_size() << "\n";
  } else {
     sstr << indent << "number of data bytes is invalid\n";
  }

  return sstr.str();
}


Error Box_idat::read_data(const std::shared_ptr<StreamReader>& istr,
                          uint64_t start, uint64_t length,
                          std::vector<uint8_t>& out_data) const
{
  // --- security check that we do not allocate too much data

  auto curr_size = out_data.size();

  if (MAX_MEMORY_BLOCK_SIZE - curr_size < length) {
    std::stringstream sstr;
    sstr << "idat box contained " << length << " bytes, total memory size would be "
         << (curr_size + length) << " bytes, exceeding the security limit of "
         << MAX_MEMORY_BLOCK_SIZE << " bytes";

    return Error(heif_error_Memory_allocation_error,
                 heif_suberror_Security_limit_exceeded,
                 sstr.str());
  }


  // move to start of data
  if (start > (uint64_t) m_data_start_pos + get_box_size()) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_End_of_data);
  }
  else if (length > get_box_size() || start + length > get_box_size()) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_End_of_data);
  }

  StreamReader::grow_status status = istr->wait_for_file_size((int64_t) m_data_start_pos + start + length);
  if (status == StreamReader::size_beyond_eof ||
      status == StreamReader::timeout) {
    // TODO: maybe we should introduce some 'Recoverable error' instead of 'Invalid input'
    return Error(heif_error_Invalid_input,
                 heif_suberror_End_of_data);
  }

  bool success;
  success = istr->seek(m_data_start_pos + (std::streampos) start);
  assert(success);
  (void) success;

  if (length > 0) {
    // reserve space for the data in the output array
    out_data.resize(static_cast<size_t>(curr_size + length));
    uint8_t* data = &out_data[curr_size];

    success = istr->read((char*) data, static_cast<size_t>(length));
    assert(success);
    (void) success;
  }

  return Error::Ok;
}


Error Box_grpl::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  //return read_children(range);

  while (!range.eof()) {
    EntityGroup group;
    Error err = group.header.parse_header(range);
    if (err != Error::Ok) {
      return err;
    }

    err = group.header.parse_full_box_header(range);
    if (err != Error::Ok) {
      return err;
    }

    group.group_id = range.read32();
    uint32_t nEntities = range.read32();
    for (uint32_t i = 0; i < nEntities; i++) {
      if (range.eof()) {
        break;
      }

      group.entity_ids.push_back(range.read32());
    }

    m_entity_groups.push_back(group);
  }

  return range.get_error();
}


std::string Box_grpl::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  for (const auto& group : m_entity_groups) {
    sstr << indent << "group type: " << group.header.get_type_string() << "\n"
         << indent << "| group id: " << group.group_id << "\n"
         << indent << "| entity IDs: ";

    for (uint32_t id : group.entity_ids) {
      sstr << id << " ";
    }

    sstr << "\n";
  }

  return sstr.str();
}


Error Box_dinf::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  return read_children(range);
}


std::string Box_dinf::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << dump_children(indent);

  return sstr.str();
}


Error Box_dref::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  uint32_t nEntities = range.read32();

  /*
  for (int i=0;i<nEntities;i++) {
    if (range.eof()) {
      break;
    }
  }
  */

  if (nEntities > (uint32_t)std::numeric_limits<int>::max()) {
    return Error(heif_error_Memory_allocation_error,
                 heif_suberror_Security_limit_exceeded,
                 "Too many entities in dref box.");
  }

  Error err = read_children(range, (int)nEntities);
  if (err) {
    return err;
  }

  if (m_children.size() != nEntities) {
    // TODO return Error(
  }

  return err;
}


std::string Box_dref::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << dump_children(indent);

  return sstr.str();
}


Error Box_url::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  m_location = range.read_string();

  return range.get_error();
}


std::string Box_url::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  //sstr << dump_children(indent);

  sstr << indent << "location: " << m_location << "\n";

  return sstr.str();
}


Error Box_udes::parse(BitstreamRange& range)
{
  parse_full_box_header(range);
  m_lang = range.read_string();
  m_name = range.read_string();
  m_description = range.read_string();
  m_tags = range.read_string();
  return range.get_error();
}

std::string Box_udes::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << indent << "lang: " << m_lang << "\n";
  sstr << indent << "name: " << m_name << "\n";
  sstr << indent << "description: " << m_description << "\n";
  sstr << indent << "tags: " << m_lang << "\n";
  return sstr.str();
}

Error Box_udes::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);
  writer.write(m_lang);
  writer.write(m_name);
  writer.write(m_description);
  writer.write(m_tags);
  prepend_header(writer, box_start);
  return Error::Ok;
}


std::string Box_cmin::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << indent << "focal-length: " << m_matrix.focal_length_x << ", " << m_matrix.focal_length_y << "\n";
  sstr << indent << "principal-point: " << m_matrix.principal_point_x << ", " << m_matrix.principal_point_y << "\n";
  sstr << indent << "skew: " << m_matrix.skew << "\n";
  return sstr.str();
}


Error Box_cmin::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  uint32_t denominatorShift = (get_flags() & 0x1F00) >> 8;
  uint32_t denominator = (1 << denominatorShift);

  m_matrix.focal_length_x = range.read32s() / (double)denominator;
  m_matrix.principal_point_x = range.read32s() / (double)denominator;
  m_matrix.principal_point_y = range.read32s() / (double)denominator;

  if (get_flags() & 1) {
    uint32_t skewDenominatorShift = ((get_flags()) & 0x1F0000) >> 16;
    uint32_t skewDenominator = (1<<skewDenominatorShift);

    m_matrix.focal_length_y = range.read32s() / (double)denominator;
    m_matrix.skew = range.read32s() / (double)skewDenominator;
  }
  else {
    // TODO: this is wrong. We cannot simply set f_y = f_x because both use a different normalization.
    //       However, we also cannot compute this here without knowing the image size.
    m_matrix.focal_length_y = m_matrix.focal_length_x;

    m_matrix.skew = 0;
  }
  return range.get_error();
}


Error Box_cmin::write(StreamWriter& writer) const
{
  return Error::Ok;
}
