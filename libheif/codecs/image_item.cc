/*
 * HEIF image base codec.
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

#include "image_item.h"
#include <context.h>
#include <file.h>
#include <cassert>
#include <cstring>
#include <codecs/jpeg.h>
#include <codecs/jpeg2000.h>
#include <codecs/uncompressed_image.h>
#include <color-conversion/colorconversion.h>
#include <libheif/api_structs.h>
#include <context.h>



template<typename I> void writevec(uint8_t* data, size_t& idx, I value, int len)
{
  for (int i=0;i<len;i++) {
    data[idx + i] = static_cast<uint8_t>((value >> (len-1-i)*8) & 0xFF);
  }

  idx += len;
}


static int32_t readvec_signed(const std::vector<uint8_t>& data, int& ptr, int len)
{
  const uint32_t high_bit = 0x80 << ((len - 1) * 8);

  uint32_t val = 0;
  while (len--) {
    val <<= 8;
    val |= data[ptr++];
  }

  bool negative = (val & high_bit) != 0;
  val &= ~high_bit;

  if (negative) {
    return -(high_bit - val);
  }
  else {
    return val;
  }

  return val;
}


static uint32_t readvec(const std::vector<uint8_t>& data, int& ptr, int len)
{
  uint32_t val = 0;
  while (len--) {
    val <<= 8;
    val |= data[ptr++];
  }

  return val;
}


ImageItem::ImageItem(HeifContext* context)
    : m_heif_context(context)
{
  memset(&m_depth_representation_info, 0, sizeof(m_depth_representation_info));
}


ImageItem::ImageItem(HeifContext* context, heif_item_id id)
    : ImageItem(context)
{
  m_id = id;
}


bool HeifContext::is_image(heif_item_id ID) const
{
  for (const auto& img : m_all_images) {
    if (img.first == ID)
      return true;
  }

  return false;
}


Error ImageItem::check_resolution(uint32_t w, uint32_t h) const
{
  return m_heif_context->check_resolution(w, h);
}


Error ImageGrid::parse(const std::vector<uint8_t>& data)
{
  if (data.size() < 8) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Invalid_grid_data,
                 "Less than 8 bytes of data");
  }

  uint8_t version = data[0];
  if (version != 0) {
    std::stringstream sstr;
    sstr << "Grid image version " << ((int)version) << " is not supported";
    return {heif_error_Unsupported_feature,
            heif_suberror_Unsupported_data_version,
            sstr.str()};
  }

  uint8_t flags = data[1];
  int field_size = ((flags & 1) ? 32 : 16);

  m_rows = static_cast<uint16_t>(data[2] + 1);
  m_columns = static_cast<uint16_t>(data[3] + 1);

  if (field_size == 32) {
    if (data.size() < 12) {
      return Error(heif_error_Invalid_input,
                   heif_suberror_Invalid_grid_data,
                   "Grid image data incomplete");
    }

    m_output_width = ((data[4] << 24) |
                      (data[5] << 16) |
                      (data[6] << 8) |
                      (data[7]));

    m_output_height = ((data[8] << 24) |
                       (data[9] << 16) |
                       (data[10] << 8) |
                       (data[11]));
  }
  else {
    m_output_width = ((data[4] << 8) |
                      (data[5]));

    m_output_height = ((data[6] << 8) |
                       (data[7]));
  }

  return Error::Ok;
}


std::vector<uint8_t> ImageGrid::write() const
{
  int field_size;

  if (m_output_width > 0xFFFF ||
      m_output_height > 0xFFFF) {
    field_size = 32;
  }
  else {
    field_size = 16;
  }

  std::vector<uint8_t> data(field_size == 16 ? 8 : 12);

  data[0] = 0; // version

  uint8_t flags = 0;
  if (field_size == 32) {
    flags |= 1;
  }

  data[1] = flags;
  data[2] = (uint8_t) (m_rows - 1);
  data[3] = (uint8_t) (m_columns - 1);

  if (field_size == 32) {
    data[4] = (uint8_t) ((m_output_width >> 24) & 0xFF);
    data[5] = (uint8_t) ((m_output_width >> 16) & 0xFF);
    data[6] = (uint8_t) ((m_output_width >> 8) & 0xFF);
    data[7] = (uint8_t) ((m_output_width) & 0xFF);

    data[8] = (uint8_t) ((m_output_height >> 24) & 0xFF);
    data[9] = (uint8_t) ((m_output_height >> 16) & 0xFF);
    data[10] = (uint8_t) ((m_output_height >> 8) & 0xFF);
    data[11] = (uint8_t) ((m_output_height) & 0xFF);
  }
  else {
    data[4] = (uint8_t) ((m_output_width >> 8) & 0xFF);
    data[5] = (uint8_t) ((m_output_width) & 0xFF);

    data[6] = (uint8_t) ((m_output_height >> 8) & 0xFF);
    data[7] = (uint8_t) ((m_output_height) & 0xFF);
  }

  return data;
}


std::string ImageGrid::dump() const
{
  std::ostringstream sstr;

  sstr << "rows: " << m_rows << "\n"
       << "columns: " << m_columns << "\n"
       << "output width: " << m_output_width << "\n"
       << "output height: " << m_output_height << "\n";

  return sstr.str();
}


Error ImageOverlay::parse(size_t num_images, const std::vector<uint8_t>& data)
{
  Error eofError(heif_error_Invalid_input,
                 heif_suberror_Invalid_overlay_data,
                 "Overlay image data incomplete");

  if (data.size() < 2 + 4 * 2) {
    return eofError;
  }

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

  return Error::Ok;
}


std::vector<uint8_t> ImageOverlay::write() const
{
  assert(m_version==0);

  bool longFields = (m_width > 0xFFFF) || (m_height > 0xFFFF);
  for (const auto& img : m_offsets) {
    if (img.x > 0x7FFF || img.y > 0x7FFF || img.x < -32768 || img.y < -32768) {
      longFields = true;
      break;
    }
  }

  std::vector<uint8_t> data;

  data.resize(2 + 4 * 2 + (longFields ? 4 : 2) * (2 + m_offsets.size() * 2));

  size_t idx=0;
  data[idx++] = m_version;
  data[idx++] = (longFields ? 1 : 0); // flags

  for (uint16_t color : m_background_color) {
    writevec(data.data(), idx, color, 2);
  }

  writevec(data.data(), idx, m_width, longFields ? 4 : 2);
  writevec(data.data(), idx, m_height, longFields ? 4 : 2);

  for (const auto& img : m_offsets) {
    writevec(data.data(), idx, img.x, longFields ? 4 : 2);
    writevec(data.data(), idx, img.y, longFields ? 4 : 2);
  }

  assert(idx == data.size());

  return data;
}


std::string ImageOverlay::dump() const
{
  std::stringstream sstr;

  sstr << "version: " << ((int) m_version) << "\n"
       << "flags: " << ((int) m_flags) << "\n"
       << "background color: " << m_background_color[0]
       << ";" << m_background_color[1]
       << ";" << m_background_color[2]
       << ";" << m_background_color[3] << "\n"
       << "canvas size: " << m_width << "x" << m_height << "\n"
       << "offsets: ";

  for (const ImageWithOffset& offset : m_offsets) {
    sstr << offset.x << ";" << offset.y << " ";
  }
  sstr << "\n";

  return sstr.str();
}


void ImageOverlay::get_background_color(uint16_t col[4]) const
{
  for (int i = 0; i < 4; i++) {
    col[i] = m_background_color[i];
  }
}


void ImageOverlay::get_offset(size_t image_index, int32_t* x, int32_t* y) const
{
  assert(image_index < m_offsets.size());
  assert(x && y);

  *x = m_offsets[image_index].x;
  *y = m_offsets[image_index].y;
}



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


std::vector<uint8_t> TildHeader::write()
{
  assert(m_parameters.version == 1);

  uint8_t flags = 0;
  bool dimensions_are_64bit = false;

  if (m_parameters.image_width > 0xFFFF || m_parameters.image_height > 0xFFFF) {
    flags |= 0x01;
    dimensions_are_64bit = true;
  }

  switch (m_parameters.offset_field_length) {
    case 32:
      flags |= 0;
      break;
    case 40:
      flags |= 0x02;
      break;
    case 48:
      flags |= 0x04;
      break;
    case 64:
      flags |= 0x06;
      break;
    default:
      assert(false);
  }

  if (m_parameters.with_tile_sizes) {
    flags |= 0x08;

    if (m_parameters.size_field_length == 64) {
      flags |= 0x10;
    }
  }

  if (m_parameters.tiles_are_sequential) {
    flags |= 0x20;
  }

  if (m_parameters.number_of_dimensions > 2) {
    flags |= 0x40;
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
  size_t idx=0;
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



std::shared_ptr<ImageItem> ImageItem::alloc_for_infe_box(HeifContext* ctx, const std::shared_ptr<Box_infe>& infe)
{
  std::string item_type = infe->get_item_type();
  heif_item_id id = infe->get_item_ID();

  if (item_type == "jpeg" ||
      (item_type == "mime" && infe->get_content_type() == "image/jpeg")) {
    return std::make_shared<ImageItem_JPEG>(ctx, id);
  }
  else {
    return nullptr;
  }

#if 0
  return (item_type == "hvc1" ||
          item_type == "grid" ||
          item_type == "tild" ||
          item_type == "iden" ||
          item_type == "iovl" ||
          item_type == "av01" ||
          item_type == "unci" ||
          item_type == "vvc1" ||
          item_type == "jpeg" ||
          (item_type == "mime" && content_type == "image/jpeg") ||
          item_type == "j2k1" ||
          item_type == "mski");
#endif
}


Result<ImageItem::CodedImageData> ImageItem::encode_image(const std::shared_ptr<HeifPixelImage>& image,
                                                          struct heif_encoder* encoder,
                                                          const struct heif_encoding_options& options,
                                                          enum heif_image_input_class input_class)
{
  switch (encoder->plugin->compression_format) {
    case heif_compression_JPEG:
      return ImageItem_JPEG::encode_image_as_jpeg(image, encoder, options, input_class);
    default:
      assert(false); // TODO
      return {};
  }
}


Result<std::shared_ptr<ImageItem>> ImageItem::encode_to_item(HeifContext* ctx,
                                                             const std::shared_ptr<HeifPixelImage>& image,
                                                             struct heif_encoder* encoder,
                                                             const struct heif_encoding_options& options,
                                                             enum heif_image_input_class input_class)
{
  std::shared_ptr<ImageItem> item;

  // alloc ImageItem of the requested type

  switch (encoder->plugin->compression_format) {
    case heif_compression_JPEG:
      item = std::make_shared<ImageItem_JPEG>(ctx);
      break;
    default:
      assert(false);
      return {};
  }


  // compress image and assign data to item

  Result<CodedImageData> codingResult = item->encode(image, encoder, options, input_class);

  auto infe_box = ctx->get_heif_file()->add_new_infe_box(item->get_infe_type());
  heif_item_id image_id = infe_box->get_item_ID();
  item->set_id(image_id);

  ctx->get_heif_file()->append_iloc_data(image_id, codingResult.value.bitstream);


  // set item properties

  for (auto& propertyBox : codingResult.value.properties) {
    int index = ctx->get_heif_file()->get_ipco_box()->find_or_append_child_box(propertyBox);
    ctx->get_heif_file()->get_ipma_box()->add_property_for_item_ID(image_id, Box_ipma::PropertyAssociation{propertyBox->is_essential(),
                                                                                                           uint16_t(index + 1)});
  }


  // MIAF 7.3.6.7
  // This is according to MIAF without Amd2. With Amd2, the restriction has been lifted and the image is MIAF compatible.
  // We might remove this code at a later point in time when MIAF Amd2 is in wide use.

  printf("cod: %p\n", codingResult.value.encoded_image.get());
  if (!is_integer_multiple_of_chroma_size(image->get_width(),
                                          image->get_height(),
                                          codingResult.value.encoded_image->get_chroma_format())) {
    item->mark_not_miaf_compatible();
  }

  ctx->get_heif_file()->add_orientation_properties(image_id, options.image_orientation);

  ctx->write_image_metadata(image, image_id);

  return item;
}


uint32_t ImageItem::get_ispe_width() const
{
  auto ispe = m_heif_context->get_heif_file()->get_property<Box_ispe>(m_id);
  if (!ispe) {
    return 0;
  }
  else {
    return ispe->get_width();
  }
}


uint32_t ImageItem::get_ispe_height() const
{
  auto ispe = m_heif_context->get_heif_file()->get_property<Box_ispe>(m_id);
  if (!ispe) {
    return 0;
  }
  else {
    return ispe->get_height();
  }
}


Error ImageItem::get_preferred_decoding_colorspace(heif_colorspace* out_colorspace, heif_chroma* out_chroma) const
{
  heif_item_id id;
  Error err = m_heif_context->get_id_of_non_virtual_child_image(m_id, id);
  if (err) {
    return err;
  }

  auto pixi = m_heif_context->get_heif_file()->get_property<Box_pixi>(id);
  if (pixi && pixi->get_num_channels() == 1) {
    *out_colorspace = heif_colorspace_monochrome;
    *out_chroma = heif_chroma_monochrome;
    return err;
  }

  auto nclx = get_color_profile_nclx();
  if (nclx && nclx->get_matrix_coefficients() == 0) {
    *out_colorspace = heif_colorspace_RGB;
    *out_chroma = heif_chroma_444;
    return err;
  }

  // TODO: this should be codec specific. JPEG 2000, for example, can use RGB internally.

  *out_colorspace = heif_colorspace_YCbCr;
  *out_chroma = heif_chroma_undefined;

  if (auto hvcC = m_heif_context->get_heif_file()->get_property<Box_hvcC>(id)) {
    *out_chroma = (heif_chroma)(hvcC->get_configuration().chroma_format);
  }
  else if (auto vvcC = m_heif_context->get_heif_file()->get_property<Box_vvcC>(id)) {
    *out_chroma = (heif_chroma)(vvcC->get_configuration().chroma_format_idc);
  }
  else if (auto av1C = m_heif_context->get_heif_file()->get_property<Box_av1C>(id)) {
    *out_chroma = (heif_chroma)(av1C->get_configuration().get_heif_chroma());
  }
  else if (auto j2kH = m_heif_context->get_heif_file()->get_property<Box_j2kH>(id)) {
    JPEG2000MainHeader jpeg2000Header;
    err = jpeg2000Header.parseHeader(*m_heif_context->get_heif_file(), id);
    if (err) {
      return err;
    }
    *out_chroma = jpeg2000Header.get_chroma_format();
  }
#if WITH_UNCOMPRESSED_CODEC
  else if (auto uncC = m_heif_context->get_heif_file()->get_property<Box_uncC>(id)) {
    if (uncC->get_version() == 1) {
      // This is the shortform case, no cmpd box, and always some kind of RGB
      *out_colorspace = heif_colorspace_RGB;
      if (uncC->get_profile() == fourcc("rgb3")) {
        *out_chroma = heif_chroma_interleaved_RGB;
      } else if ((uncC->get_profile() == fourcc("rgba")) || (uncC->get_profile() == fourcc("abgr"))) {
        *out_chroma = heif_chroma_interleaved_RGBA;
      }
    }
    if (auto cmpd = m_heif_context->get_heif_file()->get_property<Box_cmpd>(id)) {
      UncompressedImageCodec::get_heif_chroma_uncompressed(uncC, cmpd, out_chroma, out_colorspace);
    }
  }
#endif

  return err;
}


int ImageItem::get_luma_bits_per_pixel() const
{
  heif_item_id id;
  Error err = m_heif_context->get_id_of_non_virtual_child_image(m_id, id);
  if (err) {
    return -1;
  }

  // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
  return m_heif_context->get_heif_file()->get_luma_bits_per_pixel_from_configuration(id);
}


int ImageItem::get_chroma_bits_per_pixel() const
{
  heif_item_id id;
  Error err = m_heif_context->get_id_of_non_virtual_child_image(m_id, id);
  if (err) {
    return -1;
  }

  // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
  return m_heif_context->get_heif_file()->get_chroma_bits_per_pixel_from_configuration(id);
}


void ImageItem::process_before_write()
{
  if (m_is_tild) {
    // overwrite offsets

    const int construction_method = 0; // 0=mdat 1=idat

    std::vector<uint8_t> header_data = m_tild_header.write();
    m_heif_context->get_heif_file()->replace_iloc_data(m_id, 0, header_data, construction_method);
  }
}


Error ImageItem::read_grid_spec()
{
  m_is_grid = true;

  auto heif_file = m_heif_context->get_heif_file();

  std::vector<uint8_t> grid_data;
  Error err= heif_file->get_compressed_image_data(m_id, &grid_data);
  if (err) {
    return err;
  }

  err = m_grid_spec.parse(grid_data);
  if (err) {
    return err;
  }

  //std::cout << grid.dump();


  auto iref_box = heif_file->get_iref_box();

  if (!iref_box) {
    return {heif_error_Invalid_input,
            heif_suberror_No_iref_box,
            "No iref box available, but needed for grid image"};
  }

  m_grid_tile_ids = iref_box->get_references(m_id, fourcc("dimg"));

  if ((int) m_grid_tile_ids.size() != m_grid_spec.get_rows() * m_grid_spec.get_columns()) {
    std::stringstream sstr;
    sstr << "Tiled image with " << m_grid_spec.get_rows() << "x" << m_grid_spec.get_columns() << "="
         << (m_grid_spec.get_rows() * m_grid_spec.get_columns()) << " tiles, but only "
         << m_grid_tile_ids.size() << " tile images in file";

    return {heif_error_Invalid_input,
            heif_suberror_Missing_grid_images,
            sstr.str()};
  }

  return Error::Ok;
}


void ImageItem::set_preencoded_hevc_image(const std::vector<uint8_t>& data)
{
  auto hvcC = std::make_shared<Box_hvcC>();


  // --- parse the h265 stream and set hvcC headers and compressed image data

  int state = 0;

  bool first = true;
  bool eof = false;

  int prev_start_code_start = -1; // init to an invalid value, will always be overwritten before use
  int start_code_start;
  int ptr = 0;

  for (;;) {
    bool dump_nal = false;

    uint8_t c = data[ptr++];

    if (state == 3) {
      state = 0;
    }

    if (c == 0 && state <= 1) {
      state++;
    }
    else if (c == 0) {
      // NOP
    }
    else if (c == 1 && state == 2) {
      start_code_start = ptr - 3;
      dump_nal = true;
      state = 3;
    }
    else {
      state = 0;
    }

    if (ptr == (int) data.size()) {
      start_code_start = (int) data.size();
      dump_nal = true;
      eof = true;
    }

    if (dump_nal) {
      if (first) {
        first = false;
      }
      else {
        std::vector<uint8_t> nal_data;
        size_t length = start_code_start - (prev_start_code_start + 3);

        nal_data.resize(length);

        assert(prev_start_code_start >= 0);
        memcpy(nal_data.data(), data.data() + prev_start_code_start + 3, length);

        int nal_type = (nal_data[0] >> 1);

        switch (nal_type) {
          case 0x20:
          case 0x21:
          case 0x22:
            hvcC->append_nal_data(nal_data);
            break;

          default: {
            std::vector<uint8_t> nal_data_with_size;
            nal_data_with_size.resize(nal_data.size() + 4);

            memcpy(nal_data_with_size.data() + 4, nal_data.data(), nal_data.size());
            nal_data_with_size[0] = ((nal_data.size() >> 24) & 0xFF);
            nal_data_with_size[1] = ((nal_data.size() >> 16) & 0xFF);
            nal_data_with_size[2] = ((nal_data.size() >> 8) & 0xFF);
            nal_data_with_size[3] = ((nal_data.size() >> 0) & 0xFF);

            m_heif_context->get_heif_file()->append_iloc_data(m_id, nal_data_with_size);
          }
            break;
        }
      }

      prev_start_code_start = start_code_start;
    }

    if (eof) {
      break;
    }
  }

  m_heif_context->get_heif_file()->add_property(m_id, hvcC, true);
}


Result<std::shared_ptr<HeifPixelImage>> ImageItem::convert_colorspace_for_encoding(const std::shared_ptr<HeifPixelImage>& image,
                                                                                   struct heif_encoder* encoder,
                                                                                   const struct heif_encoding_options& options,
                                                                                   const heif_color_profile_nclx* target_heif_nclx)
{
  heif_colorspace colorspace = image->get_colorspace();
  heif_chroma chroma = image->get_chroma_format();

  if (encoder->plugin->plugin_api_version >= 2) {
    encoder->plugin->query_input_colorspace2(encoder->encoder, &colorspace, &chroma);
  }
  else {
    encoder->plugin->query_input_colorspace(&colorspace, &chroma);
  }


  std::shared_ptr<HeifPixelImage> output_image;

  if (colorspace != image->get_colorspace() ||
      chroma != image->get_chroma_format() ||
      !nclx_profile_matches_spec(colorspace, image->get_color_profile_nclx(), target_heif_nclx)) {
    // @TODO: use color profile when converting
    int output_bpp = 0; // same as input

    auto target_nclx = std::make_shared<color_profile_nclx>();
    target_nclx->set_from_heif_color_profile_nclx(target_heif_nclx);

    output_image = convert_colorspace(image, colorspace, chroma, target_nclx,
                                   output_bpp, options.color_conversion_options);
    if (!output_image) {
      return Error(heif_error_Unsupported_feature, heif_suberror_Unsupported_color_conversion);
    }
  }
  else {
    output_image = image;
  }

  return output_image;
}


void ImageItem::add_color_profile(const std::shared_ptr<HeifPixelImage>& image,
                                  const struct heif_encoding_options& options,
                                  enum heif_image_input_class input_class,
                                  const heif_color_profile_nclx* target_heif_nclx,
                                  ImageItem::CodedImageData& inout_codedImage)
{
  if (input_class == heif_image_input_class_normal || input_class == heif_image_input_class_thumbnail) {
    auto icc_profile = image->get_color_profile_icc();
    if (icc_profile) {
      auto colr = std::make_shared<Box_colr>();
      colr->set_color_profile(icc_profile);
      inout_codedImage.properties.push_back(colr);
    }

    if (// target_nclx_profile &&
        (!icc_profile || (options.version >= 3 &&
                          options.save_two_colr_boxes_when_ICC_and_nclx_available))) {

      auto target_nclx_profile = std::make_shared<color_profile_nclx>();
      target_nclx_profile->set_from_heif_color_profile_nclx(target_heif_nclx);

      auto colr = std::make_shared<Box_colr>();
      colr->set_color_profile(target_nclx_profile);
      inout_codedImage.properties.push_back(colr);
    }
  }
}