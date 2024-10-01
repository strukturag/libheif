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

#include "codecs/decoder.h"
#include "error.h"
#include "context.h"

#include "codecs/hevc_dec.h"
#include "codecs/jpeg2000_dec.h"


void DataExtent::set_from_image_item(class HeifFile* file, heif_item_id item)
{
  m_iloc = file->get_property<Box_iloc>(item);
  m_item_id = item;
}


Result<std::vector<uint8_t>*> DataExtent::read_data() const
{
  if (!m_raw.empty()) {
    return &m_raw;
  }
  else if (m_iloc) {
    // image
    m_iloc->read_data(m_item_id, m_file->get_reader(), nullptr, &m_raw, 0, std::numeric_limits<uint64_t>::max());
  }
  else {
    // sequence
    assert(false); // TODO
  }

  return &m_raw;
}


Result<std::vector<uint8_t>> DataExtent::read_data(uint64_t offset, uint64_t size) const
{
  std::vector<uint8_t> data;

  if (!m_raw.empty()) {
    data.insert(data.begin(), m_raw.begin() + offset, m_raw.begin() + offset + size);
    return data;
  }
  else if (m_iloc) {
    // TODO: cache data

    // image
    Error err = m_iloc->read_data(m_item_id, m_file->get_reader(), nullptr, &m_raw, 0, size);
    if (err) {
      return err;
    }
    return data;
  }
  else {
    // sequence
    assert(false); // TODO
    return Error::Ok;
  }
}


std::shared_ptr<Decoder> Decoder::alloc_for_compression_format(const HeifContext* ctx, heif_item_id id, uint32_t format_4cc)
{
  switch (format_4cc) {
    case fourcc("hvc1"): {
      auto hvcC = ctx->get_heif_file()->get_property<Box_hvcC>(id);
      return std::make_shared<Decoder_HEVC>(hvcC);
    }
    case fourcc("j2k1"): {
      auto j2kH = ctx->get_heif_file()->get_property<Box_j2kH>(id);
      return std::make_shared<Decoder_JPEG2000>(j2kH);
    }
#if WITH_UNCOMPRESSED_CODEC
#endif
    default:
      assert(false);
      return nullptr;
  }
}


Result<std::vector<uint8_t>> Decoder::get_compressed_data() const
{
  // --- get the compressed image data

  // data from configuration blocks

  Result<std::vector<uint8_t>> confData = read_bitstream_configuration_data();
  if (confData.error) {
    return confData.error;
  }

  std::vector<uint8_t> data = confData.value;

  // image data, usually from 'mdat'

  Result dataResult = m_data_extent.read_data();
  if (dataResult.error) {
    return dataResult.error;
  }

  data.insert(data.begin(), dataResult.value->begin(), dataResult.value->end());

  return data;
}