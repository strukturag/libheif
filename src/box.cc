/*
 * HEIF codec.
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
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
#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include "box.h"
#include "heif_context.h"
#include "heif_limits.h"

#include <iomanip>
#include <utility>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <string.h>
#include <assert.h>


using namespace heif;


heif::Error heif::Error::Ok(heif_error_Ok);



Fraction Fraction::operator+(const Fraction& b) const
{
  if (denominator == b.denominator) {
    return Fraction { numerator + b.numerator, denominator };
  }
  else {
    return Fraction { numerator * b.denominator + b.numerator * denominator,
        denominator * b.denominator };
  }
}

Fraction Fraction::operator-(const Fraction& b) const
{
  if (denominator == b.denominator) {
    return Fraction { numerator - b.numerator, denominator };
  }
  else {
    return Fraction { numerator * b.denominator - b.numerator * denominator,
        denominator * b.denominator };
  }
}

Fraction Fraction::operator-(int v) const
{
  return Fraction { numerator - v * denominator, denominator };
}

Fraction Fraction::operator/(int v) const
{
  return Fraction { numerator, denominator*v };
}

int Fraction::round_down() const
{
  return numerator / denominator;
}

int Fraction::round_up() const
{
  return (numerator + denominator - 1)/denominator;
}

int Fraction::round() const
{
  return (numerator + denominator/2)/denominator;
}


uint32_t from_fourcc(const char* string)
{
  return ((string[0]<<24) |
          (string[1]<<16) |
          (string[2]<< 8) |
          (string[3]));
}

static std::string to_fourcc(uint32_t code)
{
  std::string str("    ");
  str[0] = static_cast<char>((code>>24) & 0xFF);
  str[1] = static_cast<char>((code>>16) & 0xFF);
  str[2] = static_cast<char>((code>> 8) & 0xFF);
  str[3] = static_cast<char>((code>> 0) & 0xFF);

  return str;
}


heif::BoxHeader::BoxHeader()
{
}


std::vector<uint8_t> heif::BoxHeader::get_type() const
{
  if (m_type == fourcc("uuid")) {
    return m_uuid_type;
  }
  else {
    std::vector<uint8_t> type(4);
    type[0] = static_cast<uint8_t>((m_type>>24) & 0xFF);
    type[1] = static_cast<uint8_t>((m_type>>16) & 0xFF);
    type[2] = static_cast<uint8_t>((m_type>> 8) & 0xFF);
    type[3] = static_cast<uint8_t>((m_type>> 0) & 0xFF);
    return type;
  }
}


std::string heif::BoxHeader::get_type_string() const
{
  if (m_type == fourcc("uuid")) {
    // 8-4-4-4-12

    std::ostringstream sstr;
    sstr << std::hex;
    sstr << std::setfill('0');
    sstr << std::setw(2);

    for (int i=0;i<16;i++) {
      if (i==8 || i==12 || i==16 || i==20) {
        sstr << '-';
      }

      sstr << ((int)m_uuid_type[i]);
    }

    return sstr.str();
  }
  else {
    return to_fourcc(m_type);
  }
}


heif::Error heif::BoxHeader::parse(BitstreamRange& range)
{
  m_size = range.read32();
  m_type = range.read32();

  m_header_size = 8;

  if (m_size==1) {
    uint64_t high = range.read32();
    uint64_t low  = range.read32();

    m_size = (high<<32) | low;
    m_header_size += 8;
  }

  if (m_type==fourcc("uuid")) {
    static const size_t kUuidSize = 16;
    uint8_t uuid[kUuidSize];
    if (!range.read_data(&uuid, sizeof(uuid))) {
      m_uuid_type.resize(sizeof(uuid));
      memcpy(m_uuid_type.data(), uuid, sizeof(uuid));
    }

    m_header_size += sizeof(uuid);
  }

  return range.get_error();
}


size_t heif::BoxHeader::reserve_box_header_space(StreamWriter& writer) const
{
  size_t start_pos = writer.get_position();

  int header_size = is_full_box_header() ? (8+4) : 8;

  writer.skip(header_size);

  return start_pos;
}


heif::Error heif::BoxHeader::prepend_header(StreamWriter& writer, size_t box_start) const
{
  const int reserved_header_size = is_full_box_header() ? (8+4) : 8;


  // determine header size

  int header_size = 0;

  header_size += 8; // normal header size

  if (is_full_box_header()) {
    header_size += 4;
  }

  if (m_type==fourcc("uuid")) {
    header_size += 16;
  }

  bool large_size = false;

  size_t data_size = writer.data_size() - box_start - reserved_header_size;

  if (data_size + header_size > 0xFFFFFFFF) {
    header_size += 8;
    large_size = true;
  }

  size_t box_size = data_size + header_size;


  // --- write header

  writer.set_position(box_start);
  assert(header_size >= reserved_header_size);
  writer.insert(header_size - reserved_header_size);

  if (large_size) {
    writer.write32(1);
  }
  else {
    assert(box_size <= 0xFFFFFFFF);
    writer.write32( (uint32_t)box_size );
  }

  writer.write32( m_type );

  if (large_size) {
    writer.write64( box_size );
  }

  if (m_type==fourcc("uuid")) {
    assert(m_uuid_type.size()==16);
    writer.write(m_uuid_type);
  }

  if (is_full_box_header()) {
    assert((m_flags & ~0x00FFFFFF) == 0);

    writer.write32( (m_version << 24) | m_flags );
  }

  writer.set_position_to_end();  // Note: should we move to the end of the box after writing the header?

  return Error::Ok;
}


std::string BoxHeader::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << indent << "Box: " << get_type_string() << " -----\n";
  sstr << indent << "size: " << get_box_size() << "   (header size: " << get_header_size() << ")\n";

  if (m_is_full_box) {
    sstr << indent << "version: " << ((int)m_version) << "\n"
         << indent << "flags: " << std::hex << m_flags << "\n";
  }

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
    if (content_size > MAX_BOX_SIZE) {
      return Error(heif_error_Invalid_input,
                   heif_suberror_Invalid_box_size);
    }

    range.skip(content_size);
  }

  return range.get_error();
}


Error BoxHeader::parse_full_box_header(BitstreamRange& range)
{
  uint32_t data = range.read32();
  m_version = static_cast<uint8_t>(data >> 24);
  m_flags = data & 0x00FFFFFF;
  m_is_full_box = true;

  m_header_size += 4;

  return range.get_error();
}


Error Box::read(BitstreamRange& range, std::shared_ptr<heif::Box>* result)
{
  BoxHeader hdr;
  hdr.parse(range);
  if (range.error()) {
    return range.get_error();
  }

  std::shared_ptr<Box> box;

  switch (hdr.get_short_type()) {
  case fourcc("ftyp"):
    box = std::make_shared<Box_ftyp>(hdr);
    break;

  case fourcc("meta"):
    box = std::make_shared<Box_meta>(hdr);
    break;

  case fourcc("hdlr"):
    box = std::make_shared<Box_hdlr>(hdr);
    break;

  case fourcc("pitm"):
    box = std::make_shared<Box_pitm>(hdr);
    break;

  case fourcc("iloc"):
    box = std::make_shared<Box_iloc>(hdr);
    break;

  case fourcc("iinf"):
    box = std::make_shared<Box_iinf>(hdr);
    break;

  case fourcc("infe"):
    box = std::make_shared<Box_infe>(hdr);
    break;

  case fourcc("iprp"):
    box = std::make_shared<Box_iprp>(hdr);
    break;

  case fourcc("ipco"):
    box = std::make_shared<Box_ipco>(hdr);
    break;

  case fourcc("ipma"):
    box = std::make_shared<Box_ipma>(hdr);
    break;

  case fourcc("ispe"):
    box = std::make_shared<Box_ispe>(hdr);
    break;

  case fourcc("auxC"):
    box = std::make_shared<Box_auxC>(hdr);
    break;

  case fourcc("irot"):
    box = std::make_shared<Box_irot>(hdr);
    break;

  case fourcc("imir"):
    box = std::make_shared<Box_imir>(hdr);
    break;

  case fourcc("clap"):
    box = std::make_shared<Box_clap>(hdr);
    break;

  case fourcc("iref"):
    box = std::make_shared<Box_iref>(hdr);
    break;

  case fourcc("hvcC"):
    box = std::make_shared<Box_hvcC>(hdr);
    break;

  case fourcc("idat"):
    box = std::make_shared<Box_idat>(hdr);
    break;

  case fourcc("grpl"):
    box = std::make_shared<Box_grpl>(hdr);
    break;

  case fourcc("dinf"):
    box = std::make_shared<Box_dinf>(hdr);
    break;

  case fourcc("dref"):
    box = std::make_shared<Box_dref>(hdr);
    break;

  case fourcc("url "):
    box = std::make_shared<Box_url>(hdr);
    break;

  default:
    box = std::make_shared<Box>(hdr);
    break;
  }

  if (hdr.get_box_size() < hdr.get_header_size()) {
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


  BitstreamRange boxrange(range.reader(),
                          hdr.get_box_size() - hdr.get_header_size(),
                          &range);

  Error err = box->parse(boxrange);
  if (err == Error::Ok) {
    *result = std::move(box);
  }

  boxrange.skip_to_end_of_box();

  return err;
}


std::string Box::dump(Indent& indent ) const
{
  std::ostringstream sstr;

  sstr << BoxHeader::dump(indent);

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
    if (box->get_short_type()==short_type) {
      return box;
    }
  }

  return nullptr;
}


std::vector<std::shared_ptr<Box>> Box::get_child_boxes(uint32_t short_type) const
{
  std::vector<std::shared_ptr<Box>> result;
  for (auto& box : m_children) {
    if (box->get_short_type()==short_type) {
      result.push_back(box);
    }
  }

  return result;
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
      first=false;
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

  for (uint64_t i=0;i<n_minor_brands && !range.error();i++) {
    m_compatible_brands.push_back( range.read32() );
  }

  return range.get_error();
}


bool Box_ftyp::has_compatible_brand(uint32_t brand) const
{
  for (uint32_t b : m_compatible_brands) {
    if (b==brand) {
      return true;
    }
  }

  return false;
}


std::string Box_ftyp::dump(Indent& indent) const
{
  std::ostringstream sstr;

  sstr << BoxHeader::dump(indent);

  sstr << indent << "major brand: " << to_fourcc(m_major_brand) << "\n"
       << indent << "minor version: " << m_minor_version << "\n"
       << indent << "compatible brands: ";

  bool first=true;
  for (uint32_t brand : m_compatible_brands) {
    if (first) { first=false; }
    else { sstr << ','; }

    sstr << to_fourcc(brand);
  }
  sstr << "\n";

  return sstr.str();
}


void Box_ftyp::add_compatible_brand(uint32_t brand)
{
  // TODO: check whether brand already exists in the list

  m_compatible_brands.push_back(brand);
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

  for (int i=0;i<3;i++) {
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

  for (int i=0;i<3;i++) {
    writer.write32(m_reserved[i]);
  }

  writer.write(m_name);

  prepend_header(writer, box_start);

  return Error::Ok;
}



Error Box_pitm::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  if (get_version()==0) {
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

  if (get_version()==0) {
    assert(m_item_ID <= 0xFFFF);
    writer.write16((uint16_t)m_item_ID);
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
  int length_size = (values4 >>  8) & 0xF;
  int base_offset_size = (values4 >> 4) & 0xF;
  int index_size = 0;

  if (get_version()>1) {
    index_size = (values4 & 0xF);
  }

  int item_count;
  if (get_version()<2) {
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

  for (int i=0;i<item_count;i++) {
    Item item;

    if (get_version()<2) {
      item.item_ID = range.read16();
    }
    else {
      item.item_ID = range.read32();
    }

    if (get_version()>=1) {
      values4 = range.read16();
      item.construction_method = (values4 & 0xF);
    }

    item.data_reference_index = range.read16();

    item.base_offset = 0;
    if (base_offset_size==4) {
      item.base_offset = range.read32();
    }
    else if (base_offset_size==8) {
      item.base_offset = ((uint64_t)range.read32()) << 32;
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

    for (int e=0;e<extent_count;e++) {
      Extent extent;

      if (get_version()>1 && index_size>0) {
        if (index_size==4) {
          extent.index = range.read32();
        }
        else if (index_size==8) {
          extent.index = ((uint64_t)range.read32()) << 32;
          extent.index |= range.read32();
        }
      }

      extent.offset = 0;
      if (offset_size==4) {
        extent.offset = range.read32();
      }
      else if (offset_size==8) {
        extent.offset = ((uint64_t)range.read32()) << 32;
        extent.offset |= range.read32();
      }


      extent.length = 0;
      if (length_size==4) {
        extent.length = range.read32();
      }
      else if (length_size==8) {
        extent.length = ((uint64_t)range.read32()) << 32;
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
         << indent << "  construction method: " << ((int)item.construction_method) << "\n"
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


Error Box_iloc::read_data(const Item& item, HeifReader* reader,
                          const std::shared_ptr<Box_idat>& idat,
                          std::vector<uint8_t>* dest) const
{
  for (const auto& extent : item.extents) {
    if (item.construction_method == 0) {
      if (!reader->seek(extent.offset + item.base_offset, heif_seek_start)) {
        // Out-of-bounds
        std::stringstream sstr;
        sstr << "Extent in iloc box references data outside of file bounds "
             << "(points to file position " << extent.offset + item.base_offset << ")\n";

        return Error(heif_error_Invalid_input,
                     heif_suberror_End_of_data,
                     sstr.str());
      }

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

      dest->resize(static_cast<size_t>(old_size + extent.length));
      if (!reader->read((char*)dest->data() + old_size, static_cast<size_t>(extent.length))) {
        dest->resize(old_size);
        return Error(heif_error_Invalid_input,
                     heif_suberror_End_of_data);
      }
    }
    else if (item.construction_method==1) {
      if (!idat) {
        return Error(heif_error_Invalid_input,
                     heif_suberror_No_idat_box,
                     "idat box referenced in iref box is not present in file");
      }

      idat->read_data(reader,
                      extent.offset + item.base_offset,
                      extent.length,
                      *dest);
    }
    else {
      std::stringstream sstr;
      sstr << "Item construction method " << item.construction_method << " not implemented";
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_No_idat_box,
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
  for (idx=0;idx<m_items.size();idx++) {
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
  m_items[idx].extents.push_back( std::move(extent) );

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

  set_version((uint8_t)min_version);
}


Error Box_iloc::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  m_iloc_box_start = writer.get_position();

  int nSkip = 0;

  nSkip += 2;
  nSkip += (get_version()<2) ? 2 : 4; // item_count

  for (const auto& item : m_items) {
    nSkip += (get_version()<2) ? 2 : 4; // item_ID
    nSkip += (get_version()>=1) ? 2 : 0; // construction method
    nSkip += 4 + m_base_offset_size;

    for (const auto& extent : item.extents) {
      (void)extent;

      if (get_version()>=1) {
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

  writer.write32((uint32_t)(sum_mdat_size + 8));
  writer.write32(fourcc("mdat"));

  for (auto& item : m_items) {
    item.base_offset = writer.get_position();

    for (auto& extent : item.extents) {
      extent.offset = writer.get_position() - item.base_offset;
      extent.length = extent.data.size();

      writer.write(extent.data);
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

  writer.write8((uint8_t)((m_offset_size<<4) | (m_length_size)));
  writer.write8((uint8_t)((m_base_offset_size<<4) | (m_index_size)));

  if (get_version() < 2) {
    writer.write16((uint16_t)m_items.size());
  } else {
    writer.write32((uint32_t)m_items.size());
  }

  for (const auto& item : m_items) {
    if (get_version() < 2) {
      writer.write16((uint16_t)item.item_ID);
    } else {
      writer.write32((uint32_t)item.item_ID);
    }

    if (get_version() >= 1) {
      writer.write16(item.construction_method);
    }

    writer.write16(item.data_reference_index);
    writer.write(m_base_offset_size, item.base_offset);
    writer.write16((uint16_t)item.extents.size());

    for (const auto& extent : item.extents) {
      if (get_version()>=1 && m_index_size > 0) {
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
    uint32_t item_type =range.read32();
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

  set_version((uint8_t)min_version);
}


void Box_infe::set_hidden_item(bool hidden)
{
  m_hidden_item = hidden;

  if (m_hidden_item) {
    set_flags( get_flags() | 1);
  }
  else {
    set_flags( get_flags() & ~1);
  }
}

Error Box_infe::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  if (get_version() <= 1) {
    writer.write16((uint16_t)m_item_ID);
    writer.write16(m_item_protection_index);

    writer.write(m_item_name);
    writer.write(m_content_type);
    writer.write(m_content_encoding);
  }

  if (get_version() >= 2) {
    if (get_version() == 2) {
      writer.write16((uint16_t)m_item_ID);
    }
    else if (get_version()==3) {
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

  int item_count;
  if (nEntries_size==2) {
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


std::string Box_iinf::dump(Indent& indent ) const
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


Error Box_ipco::get_properties_for_item_ID(uint32_t itemID,
                                           const std::shared_ptr<class Box_ipma>& ipma,
                                           std::vector<Property>& out_properties) const
{
  const std::vector<Box_ipma::PropertyAssociation>* property_assoc = ipma->get_properties_for_item_ID(itemID);
  if (property_assoc == nullptr) {
    std::stringstream sstr;
    sstr << "Item (ID=" << itemID << ") has no properties assigned to it in ipma box";

    return Error(heif_error_Invalid_input,
                 heif_suberror_No_properties_assigned_to_item,
                 sstr.str());
  }

  auto allProperties = get_all_child_boxes();
  for (const  Box_ipma::PropertyAssociation& assoc : *property_assoc) {
    if (assoc.property_index > allProperties.size()) {
      std::stringstream sstr;
      sstr << "Nonexisting property (index=" << assoc.property_index << ") for item "
           << " ID=" << itemID << " referenced in ipma box";

      return Error(heif_error_Invalid_input,
                   heif_suberror_Ipma_box_references_nonexisting_property,
                   sstr.str());
    }

    Property prop;
    prop.essential = assoc.essential;

    if (assoc.property_index > 0) {
      prop.property = allProperties[assoc.property_index - 1];
      out_properties.push_back(prop);
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

  auto allProperties = get_all_child_boxes();
  for (const  Box_ipma::PropertyAssociation& assoc : *property_assoc) {
    if (assoc.property_index > allProperties.size() ||
        assoc.property_index == 0) {
      return nullptr;
    }

    auto property = allProperties[assoc.property_index - 1];
    if (property->get_short_type() == box_type) {
      return property;
    }
  }

  return nullptr;
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


Error Box_ipma::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  int entry_cnt = range.read32();
  for (int i = 0; i < entry_cnt && !range.error() && !range.eof(); i++) {
    Entry entry;
    if (get_version()<1) {
      entry.item_ID = range.read16();
    }
    else {
      entry.item_ID = range.read32();
    }

    int assoc_cnt = range.read8();
    for (int k=0;k<assoc_cnt;k++) {
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


void Box_ipma::add_property_for_item_ID(heif_item_id itemID,
                                        PropertyAssociation assoc)
{
  size_t idx;
  for (idx=0; idx<m_entries.size(); idx++) {
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
        large_property_indices=true;
      }
    }
  }

  set_version((uint8_t)version);
  set_flags(large_property_indices ? 1 : 0);
}


Error Box_ipma::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  size_t entry_cnt = m_entries.size();
  writer.write32((uint32_t)entry_cnt);

  for (const Entry& entry : m_entries) {

    if (get_version()<1) {
      writer.write16((uint16_t)entry.item_ID);
    }
    else {
      writer.write32(entry.item_ID);
    }

    size_t assoc_cnt = entry.associations.size();
    if (assoc_cnt > 0xFF) {
      // TODO: error, too many associations
    }

    writer.write8((uint8_t)assoc_cnt);

    for (const PropertyAssociation& association : entry.associations) {

      if (get_flags() & 1) {
        writer.write16( (uint16_t)((association.essential ? 0x8000 : 0) |
                                   (association.property_index & 0x7FFF)) );
      }
      else {
        writer.write8( (uint8_t)((association.essential ? 0x80 : 0) |
                                 (association.property_index & 0x7F)) );
      }
    }
  }

  prepend_header(writer, box_start);

  return Error::Ok;
}


Error Box_auxC::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  m_aux_type = range.read_string();

  while (!range.eof() && !range.error()) {
    m_aux_subtypes.push_back( range.read8() );
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
    sstr << std::hex << std::setw(2) << std::setfill('0') << ((int)subtype) << " ";
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

  uint16_t axis = range.read8();
  if (axis & 1) {
    m_axis = MirrorAxis::Horizontal;
  }
  else {
    m_axis = MirrorAxis::Vertical;
  }

  return range.get_error();
}


std::string Box_imir::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "mirror axis: ";
  switch (m_axis) {
  case MirrorAxis::Vertical:   sstr << "vertical\n"; break;
  case MirrorAxis::Horizontal: sstr << "horizontal\n"; break;
  }

  return sstr.str();
}


Error Box_clap::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  m_clean_aperture_width.numerator   = range.read32();
  m_clean_aperture_width.denominator = range.read32();
  m_clean_aperture_height.numerator   = range.read32();
  m_clean_aperture_height.denominator = range.read32();
  m_horizontal_offset.numerator   = range.read32();
  m_horizontal_offset.denominator = range.read32();
  m_vertical_offset.numerator   = range.read32();
  m_vertical_offset.denominator = range.read32();

  return range.get_error();
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

  Fraction pcX  = m_horizontal_offset + Fraction(image_width-1, 2);
  Fraction left = pcX - (m_clean_aperture_width-1)/2;

  return left.round();
}

int Box_clap::right_rounded(int image_width) const
{
  Fraction pcX  = m_horizontal_offset + Fraction(image_width-1, 2);
  Fraction right = pcX + (m_clean_aperture_width-1)/2;

  return right.round();
}

int Box_clap::top_rounded(int image_height) const
{
  Fraction pcY  = m_vertical_offset + Fraction(image_height-1, 2);
  Fraction top = pcY - (m_clean_aperture_height-1)/2;

  return top.round();
}

int Box_clap::bottom_rounded(int image_height) const
{
  Fraction pcY  = m_vertical_offset + Fraction(image_height-1, 2);
  Fraction bottom = pcY + (m_clean_aperture_height-1)/2;

  return bottom.round();
}

int Box_clap::get_width_rounded() const
{
  int left  = (Fraction(0,1)-(m_clean_aperture_width-1)/2).round();
  int right = (  (m_clean_aperture_width-1)/2).round();

  return right+1-left;
}

int Box_clap::get_height_rounded() const
{
  int top    = (Fraction(0,1)-(m_clean_aperture_height-1)/2).round();
  int bottom = ( (m_clean_aperture_height-1)/2).round();

  return bottom+1-top;
}



Error Box_iref::parse(BitstreamRange& range)
{
  parse_full_box_header(range);

  while (!range.eof()) {
    Reference ref;

    Error err = ref.header.parse(range);
    if (err != Error::Ok) {
      return err;
    }

    if (get_version()==0) {
      ref.from_item_ID = range.read16();
      int nRefs = range.read16();
      for (int i=0;i<nRefs;i++) {
        ref.to_item_ID.push_back( range.read16() );
        if (range.eof()) {
          break;
        }
      }
    }
    else {
      ref.from_item_ID = range.read32();
      int nRefs = range.read16();
      for (int i=0;i<nRefs;i++) {
        ref.to_item_ID.push_back( range.read32() );
        if (range.eof()) {
          break;
        }
      }
    }

    m_references.push_back(ref);
  }

  return range.get_error();
}


void Box_iref::derive_box_version()
{
  uint8_t version = 0;

  for (const auto& ref : m_references) {
    if (ref.from_item_ID > 0xFFFF) {
      version=1;
      break;
    }

    for (uint32_t r : ref.to_item_ID) {
      if (r > 0xFFFF) {
        version=1;
        break;
      }
    }
  }

  set_version(version);
}


Error Box_iref::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  int id_size = ((get_version()==0) ? 2 : 4);

  for (const auto& ref : m_references) {
    uint32_t box_size = uint32_t(4+4 + 2 + id_size * (1+ref.to_item_ID.size()));

    // we write the BoxHeader ourselves since it is very simple
    writer.write32(box_size);
    writer.write32(ref.header.get_short_type());

    writer.write(id_size, ref.from_item_ID);
    writer.write16((uint16_t)ref.to_item_ID.size());

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


uint32_t Box_iref::get_reference_type(uint32_t itemID) const
{
  for (const Reference& ref : m_references) {
    if (ref.from_item_ID == itemID) {
      return ref.header.get_short_type();
    }
  }

  return 0;
}


std::vector<uint32_t> Box_iref::get_references(uint32_t itemID) const
{
  for (const Reference& ref : m_references) {
    if (ref.from_item_ID == itemID) {
      return ref.to_item_ID;
    }
  }

  return std::vector<uint32_t>();
}


void Box_iref::add_reference(heif_item_id from_id, uint32_t type, std::vector<heif_item_id> to_ids)
{
  Reference ref;
  ref.header.set_short_type(type);
  ref.from_item_ID = from_id;
  ref.to_item_ID = to_ids;

  m_references.push_back(ref);
}


Error Box_hvcC::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  uint8_t byte;

  auto& c = m_configuration; // abbreviation

  c.configuration_version = range.read8();
  byte = range.read8();
  c.general_profile_space = (byte>>6) & 3;
  c.general_tier_flag = (byte>>5) & 1;
  c.general_profile_idc = (byte & 0x1F);

  c.general_profile_compatibility_flags = range.read32();

  for (int i=0; i<6; i++)
    {
      byte = range.read8();

      for (int b=0;b<8;b++) {
        c.general_constraint_indicator_flags[i*8+b] = (byte >> (7-b)) & 1;
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

  for (int i=0; i<nArrays && !range.error(); i++)
    {
      byte = range.read8();

      NalArray array;

      array.m_array_completeness = (byte >> 6) & 1;
      array.m_NAL_unit_type = (byte & 0x3F);

      int nUnits = range.read16();
      for (int u=0; u<nUnits && !range.error(); u++) {

        std::vector<uint8_t> nal_unit;
        int size = range.read16();
        if (!size) {
          // Ignore empty NAL units.
          continue;
        }

        if (size > MAX_MEMORY_BLOCK_SIZE) {
          std::stringstream sstr;
          sstr << "hvcC box contained " << size << " bytes, total memory size would be "
               << size << " bytes, exceeding the security limit of "
               << MAX_MEMORY_BLOCK_SIZE << " bytes";

          return Error(heif_error_Memory_allocation_error,
                       heif_suberror_Security_limit_exceeded,
                       sstr.str());
        }
        if (!range.read_vector(&nal_unit, size)) {
          return range.get_error();
        }

        array.m_nal_units.push_back( std::move(nal_unit) );
      }

      m_nal_array.push_back( std::move(array) );
    }

  range.skip_to_end_of_box();

  return range.get_error();
}


std::string Box_hvcC::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  const auto& c = m_configuration; // abbreviation

  sstr << indent << "configuration_version: " << ((int)c.configuration_version) << "\n"
       << indent << "general_profile_space: " << ((int)c.general_profile_space) << "\n"
       << indent << "general_tier_flag: " << c.general_tier_flag << "\n"
       << indent << "general_profile_idc: " << ((int)c.general_profile_idc) << "\n";

  sstr << indent << "general_profile_compatibility_flags: ";
  for (int i=0;i<32;i++) {
    sstr << ((c.general_profile_compatibility_flags>>(31-i))&1);
    if ((i%8)==7) sstr << ' ';
    else if ((i%4)==3) sstr << '.';
  }
  sstr << "\n";

  sstr << indent << "general_constraint_indicator_flags: ";
  int cnt=0;
  for (int i=0; i<configuration::NUM_CONSTRAINT_INDICATOR_FLAGS; i++) {
    bool b = c.general_constraint_indicator_flags[i];

    sstr << (b ? 1:0);
    cnt++;
    if ((cnt%8)==0)
      sstr << ' ';
  }
  sstr << "\n";

  sstr << indent << "general_level_idc: " << ((int)c.general_level_idc) << "\n"
       << indent << "min_spatial_segmentation_idc: " << c.min_spatial_segmentation_idc << "\n"
       << indent << "parallelism_type: " << ((int)c.parallelism_type) << "\n"
       << indent << "chroma_format: " << ((int)c.chroma_format) << "\n"
       << indent << "bit_depth_luma: " << ((int)c.bit_depth_luma) << "\n"
       << indent << "bit_depth_chroma: " << ((int)c.bit_depth_chroma) << "\n"
       << indent << "avg_frame_rate: " << c.avg_frame_rate << "\n"
       << indent << "constant_frame_rate: " << ((int)c.constant_frame_rate) << "\n"
       << indent << "num_temporal_layers: " << ((int)c.num_temporal_layers) << "\n"
       << indent << "temporal_id_nested: " << ((int)c.temporal_id_nested) << "\n"
       << indent << "length_size: " << ((int)m_length_size) << "\n";

  for (const auto& array : m_nal_array) {
    sstr << indent << "<array>\n";

    indent++;
    sstr << indent << "array_completeness: " << ((int)array.m_array_completeness) << "\n"
         << indent << "NAL_unit_type: " << ((int)array.m_NAL_unit_type) << "\n";

    for (const auto& unit : array.m_nal_units) {
      //sstr << "  unit with " << unit.size() << " bytes of data\n";
      sstr << indent;
      for (uint8_t b : unit) {
        sstr << std::setfill('0') << std::setw(2) << std::hex << ((int)b) << " ";
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

      dest->push_back( (unit.size()>>24) & 0xFF );
      dest->push_back( (unit.size()>>16) & 0xFF );
      dest->push_back( (unit.size()>> 8) & 0xFF );
      dest->push_back( (unit.size()>> 0) & 0xFF );

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
  array.m_NAL_unit_type = uint8_t(nal[0]>>1);
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
  array.m_NAL_unit_type = uint8_t(nal[0]>>1);
  array.m_nal_units.push_back( std::move(nal) );

  m_nal_array.push_back(array);
}


Error Box_hvcC::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  const auto& c = m_configuration; // abbreviation

  writer.write8(c.configuration_version);

  writer.write8((uint8_t)(((c.general_profile_space & 3) << 6) |
                          ((c.general_tier_flag & 1) << 5) |
                          (c.general_profile_idc & 0x1F)));

  writer.write32(c.general_profile_compatibility_flags);

  for (int i=0; i<6; i++)
    {
      uint8_t byte = 0;

      for (int b=0;b<8;b++) {
        if (c.general_constraint_indicator_flags[i*8+b]) {
          byte |= 1;
        }

        byte = (uint8_t)(byte<<1);
      }

      writer.write8(byte);
    }

  writer.write8(c.general_level_idc);
  writer.write16(c.min_spatial_segmentation_idc & 0x0FFF);
  writer.write8(c.parallelism_type | 0xFC);
  writer.write8(c.chroma_format | 0xFC);
  writer.write8((uint8_t)((c.bit_depth_luma - 8) | 0xF8));
  writer.write8((uint8_t)((c.bit_depth_chroma - 8) | 0xF8));
  writer.write16(c.avg_frame_rate);

  writer.write8((uint8_t)(((c.constant_frame_rate & 0x03) << 6) |
                          ((c.num_temporal_layers & 0x07) << 3) |
                          ((c.temporal_id_nested & 1) << 2) |
                          ((m_length_size-1) & 0x03)));

  size_t nArrays = m_nal_array.size();
  if (nArrays>0xFF) {
    // TODO: error: too many NAL units
  }

  writer.write8((uint8_t)nArrays);

  for (const NalArray& array : m_nal_array) {

    writer.write8((uint8_t)(((array.m_array_completeness & 1) << 6) |
                            (array.m_NAL_unit_type & 0x3F)));

    size_t nUnits = array.m_nal_units.size();
    if (nUnits > 0xFFFF) {
      // TODO: error: too many NAL units
    }

    writer.write16((uint16_t)nUnits);

    for (const std::vector<uint8_t>& nal_unit : array.m_nal_units) {
      writer.write16((uint16_t)nal_unit.size());
      writer.write(nal_unit);
    }
  }

  prepend_header(writer, box_start);

  return Error::Ok;
}


Error Box_idat::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  m_data_start_pos = range.reader()->position();

  return range.get_error();
}


std::string Box_idat::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "number of data bytes: " << get_box_size() - get_header_size() << "\n";

  return sstr.str();
}


Error Box_idat::read_data(HeifReader* reader, uint64_t start, uint64_t length,
                          std::vector<uint8_t>& out_data) const
{
  // move to start of data
  if (!reader->seek(m_data_start_pos + start, heif_seek_start)) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_End_of_data);
  }

  // reserve space for the data in the output array
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

  out_data.resize(static_cast<size_t>(curr_size + length));
  uint8_t* data = &out_data[curr_size];

  if (!reader->read(data, length)) {
    out_data.resize(curr_size);
    return Error(heif_error_Invalid_input,
                 heif_suberror_End_of_data);
  }

  return Error::Ok;
}


Error Box_grpl::parse(BitstreamRange& range)
{
  //parse_full_box_header(range);

  //return read_children(range);

  while (!range.eof()) {
    EntityGroup group;
    Error err = group.header.parse(range);
    if (err != Error::Ok) {
      return err;
    }

    err = group.header.parse_full_box_header(range);
    if (err != Error::Ok) {
      return err;
    }

    group.group_id = range.read32();
    int nEntities = range.read32();
    for (int i=0;i<nEntities;i++) {
      if (range.eof()) {
        break;
      }

      group.entity_ids.push_back( range.read32() );
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

  int nEntities = range.read32();

  /*
  for (int i=0;i<nEntities;i++) {
    if (range.eof()) {
      break;
    }
  }
  */

  Error err = read_children(range, nEntities);
  if (err) {
    return err;
  }

  if ((int)m_children.size() != nEntities) {
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
