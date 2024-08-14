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

#include "tild.h"
#include "context.h"
#include "file.h"


void TildHeader::set_parameters(const heif_tild_image_parameters& params)
{
  m_parameters = params;

  m_offsets.resize(number_of_tiles());

  for (auto& tile : m_offsets) {
    tile.offset = TILD_OFFSET_NOT_AVAILABLE;
  }
}


Error TildHeader::parse(size_t num_images, const std::vector<uint8_t>& data)
{
  Error eofError(heif_error_Invalid_input,
                 heif_suberror_Invalid_overlay_data,
                 "Tild header data incomplete");

  if (data.size() < 2 + 4 * 2) {
    return eofError;
  }
#if 0
  m_version = data[0];
  if (m_version != 0) {
    std::stringstream sstr;
    sstr << "Overlay image data version " << ((int) m_version) << " is not implemented yet";

    return {heif_error_Unsupported_feature,
            heif_suberror_Unsupported_data_version,
            sstr.str()};
  }

  m_flags = data[1];

  int field_len = ((m_flags & 1) ? 4 : 2);
  int ptr = 2;

  if (ptr + 4 * 2 + 2 * field_len + num_images * 2 * field_len > data.size()) {
    return eofError;
  }

  for (int i = 0; i < 4; i++) {
    uint16_t color = static_cast<uint16_t>(readvec(data, ptr, 2));
    m_background_color[i] = color;
  }

  m_width = readvec(data, ptr, field_len);
  m_height = readvec(data, ptr, field_len);

  if (m_width==0 || m_height==0) {
    return {heif_error_Invalid_input,
            heif_suberror_Invalid_overlay_data,
            "Overlay image with zero width or height."};
  }

  m_offsets.resize(num_images);

  for (size_t i = 0; i < num_images; i++) {
    m_offsets[i].x = readvec_signed(data, ptr, field_len);
    m_offsets[i].y = readvec_signed(data, ptr, field_len);
  }
#endif

  return Error::Ok;
}


uint64_t TildHeader::number_of_tiles() const
{
  uint64_t nTiles_h = (m_parameters.image_width + m_parameters.tile_width - 1) / m_parameters.tile_width;
  uint64_t nTiles_v = (m_parameters.image_height + m_parameters.tile_height - 1) / m_parameters.tile_height;
  uint64_t nTiles = nTiles_h * nTiles_v;

  return nTiles;
}


uint64_t TildHeader::nTiles_h() const
{
  return (m_parameters.image_width + m_parameters.tile_width - 1) / m_parameters.tile_width;
}


size_t TildHeader::get_header_size() const
{
  assert(m_header_size);
  return m_header_size;
}


void TildHeader::set_tild_tile_range(uint32_t tile_x, uint32_t tile_y, uint64_t offset, uint32_t size)
{
  uint64_t idx = tile_y * nTiles_h() + tile_x;
  m_offsets[idx].offset = offset;
  m_offsets[idx].size = size;
}


template<typename I>
void writevec(uint8_t* data, size_t& idx, I value, int len)
{
  for (int i = 0; i < len; i++) {
    data[idx + i] = static_cast<uint8_t>((value >> (len - 1 - i) * 8) & 0xFF);
  }

  idx += len;
}


std::vector<uint8_t> TildHeader::write()
{
  assert(m_parameters.version == 1);

  uint8_t flags = 0;
  bool dimensions_are_64bit = false;

  if (m_parameters.image_width > 0xFFFF || m_parameters.image_height > 0xFFFF) {
    flags |= 0x40;
    dimensions_are_64bit = true;
  }

  switch (m_parameters.offset_field_length) {
    case 32:
      flags |= 0;
      break;
    case 40:
      flags |= 0x01;
      break;
    case 48:
      flags |= 0x02;
      break;
    case 64:
      flags |= 0x03;
      break;
    default:
      assert(false);
  }

  if (m_parameters.with_tile_sizes) {
    flags |= 0x04;

    if (m_parameters.size_field_length == 64) {
      flags |= 0x08;
    }
  }

  if (m_parameters.tiles_are_sequential) {
    flags |= 0x10;
  }

  if (m_parameters.number_of_dimensions > 2) {
    flags |= 0x20;
  }

  uint64_t nTiles = number_of_tiles();

  std::vector<uint8_t> data;
  uint64_t size = (2 +  // version, flags
                   (dimensions_are_64bit ? 8 : 4) * 2 + // image size
                   2 * 4 + // tile size
                   4 + // compression type
                   nTiles * (m_parameters.offset_field_length / 8)); // offsets

  if (m_parameters.with_tile_sizes) {
    size += nTiles * (m_parameters.size_field_length / 8);
  }

  data.resize(size);
  size_t idx = 0;
  data[idx++] = 1; // version
  data[idx++] = flags;

  writevec(data.data(), idx, m_parameters.image_width, dimensions_are_64bit ? 8 : 4);
  writevec(data.data(), idx, m_parameters.image_height, dimensions_are_64bit ? 8 : 4);

  writevec(data.data(), idx, m_parameters.tile_width, 4);
  writevec(data.data(), idx, m_parameters.tile_height, 4);

  writevec(data.data(), idx, m_parameters.compression_type_fourcc, 4);

  for (const auto& offset : m_offsets) {
    writevec(data.data(), idx, offset.offset, m_parameters.offset_field_length / 8);

    if (m_parameters.with_tile_sizes) {
      writevec(data.data(), idx, offset.size, m_parameters.size_field_length / 8);
    }
  }

  assert(idx == data.size());

  m_header_size = data.size();

  return data;
}


std::string TildHeader::dump() const
{
  std::stringstream sstr;

  sstr << "version: " << ((int) m_parameters.version) << "\n"
       << "image size: " << m_parameters.image_width << "x" << m_parameters.image_height << "\n"
       << "tile size: " << m_parameters.tile_width << "x" << m_parameters.tile_height << "\n"
       << "offsets: ";

  // TODO

  for (const auto& offset : m_offsets) {
    sstr << offset.offset << ", size: " << offset.size << "\n";
  }

  return sstr.str();
}


ImageItem_Tild::ImageItem_Tild(HeifContext* ctx)
    : ImageItem(ctx)
{
}


ImageItem_Tild::ImageItem_Tild(HeifContext* ctx, heif_item_id id)
    : ImageItem(ctx, id)
{
}


Error ImageItem_Tild::on_load_file()
{
#if 0
  Error err = read_grid_spec();
  if (err) {
    return err;
  }
#endif
  return Error::Ok;
}


Result<std::shared_ptr<ImageItem_Tild>> ImageItem_Tild::add_new_tild_item(HeifContext* ctx, const heif_tild_image_parameters* parameters)
{
  // Create header

  TildHeader tild_header;
  tild_header.set_parameters(*parameters);

  std::vector<uint8_t> header_data = tild_header.write();

  // Create 'tild' Item

  auto file = ctx->get_heif_file();

  heif_item_id tild_id = ctx->get_heif_file()->add_new_image("tild");
  auto tild_image = std::make_shared<ImageItem_Tild>(ctx, tild_id);
  ctx->insert_new_image(tild_id, tild_image);

  const int construction_method = 0; // 0=mdat 1=idat
  file->append_iloc_data(tild_id, header_data, construction_method);


  if (parameters->image_width > 0xFFFFFFFF || parameters->image_height > 0xFFFFFFFF) {
    return {Error(heif_error_Usage_error, heif_suberror_Invalid_image_size,
                  "'ispe' only supports image sized up to 4294967295 pixels per dimension")};
  }

  // Add ISPE property
  file->add_ispe_property(tild_id,
                          static_cast<uint32_t>(parameters->image_width),
                          static_cast<uint32_t>(parameters->image_height));

#if 0
  // TODO

  // Add PIXI property (copy from first tile)
  auto pixi = m_heif_file->get_property<Box_pixi>(tile_ids[0]);
  m_heif_file->add_property(grid_id, pixi, true);
#endif

  tild_image->set_tild_header(tild_header);
  tild_image->set_next_tild_position(header_data.size());

  // Set Brands
  //m_heif_file->set_brand(encoder->plugin->compression_format,
  //                       out_grid_image->is_miaf_compatible());

  return {tild_image};
}


void ImageItem_Tild::process_before_write()
{
  // overwrite offsets

  const int construction_method = 0; // 0=mdat 1=idat

  std::vector<uint8_t> header_data = m_tild_header.write();
  get_file()->replace_iloc_data(get_id(), 0, header_data, construction_method);
}


Result<std::shared_ptr<HeifPixelImage>> ImageItem_Tild::decode_compressed_image(const struct heif_decoding_options& options,
                                                                                bool decode_tile_only, uint32_t tile_x0, uint32_t tile_y0) const
{
  if (decode_tile_only) {
    // TODO
    return Error::Ok;
  }
  else {
    return Error{heif_error_Unsupported_feature, heif_suberror_Unspecified, "'tild' images can only be access per tile"};
  }
}


