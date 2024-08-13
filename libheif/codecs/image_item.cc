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
#include "mask_image.h"
#include "grid.h"
#include <context.h>
#include <file.h>
#include <cassert>
#include <cstring>
#include <codecs/jpeg.h>
#include <codecs/jpeg2000.h>
#include <codecs/avif.h>
#include <codecs/hevc.h>
#include <color-conversion/colorconversion.h>
#include <libheif/api_structs.h>
#include <plugin_registry.h>

#if WITH_UNCOMPRESSED_CODEC
#include <codecs/uncompressed_image.h>
#endif


template<typename I>
void writevec(uint8_t* data, size_t& idx, I value, int len)
{
  for (int i = 0; i < len; i++) {
    data[idx + i] = static_cast<uint8_t>((value >> (len - 1 - i) * 8) & 0xFF);
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


std::shared_ptr<HeifFile> ImageItem::get_file()
{
  return m_heif_context->get_heif_file();
}


std::shared_ptr<const HeifFile> ImageItem::get_file() const
{
  return m_heif_context->get_heif_file();
}


Error ImageItem::check_resolution(uint32_t w, uint32_t h) const
{
  return m_heif_context->check_resolution(w, h);
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

  if (m_width == 0 || m_height == 0) {
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
  assert(m_version == 0);

  bool longFields = (m_width > 0xFFFF) || (m_height > 0xFFFF);
  for (const auto& img : m_offsets) {
    if (img.x > 0x7FFF || img.y > 0x7FFF || img.x < -32768 || img.y < -32768) {
      longFields = true;
      break;
    }
  }

  std::vector<uint8_t> data;

  data.resize(2 + 4 * 2 + (longFields ? 4 : 2) * (2 + m_offsets.size() * 2));

  size_t idx = 0;
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


std::shared_ptr<ImageItem> ImageItem::alloc_for_infe_box(HeifContext* ctx, const std::shared_ptr<Box_infe>& infe)
{
  std::string item_type = infe->get_item_type();
  heif_item_id id = infe->get_item_ID();

  if (item_type == "jpeg" ||
      (item_type == "mime" && infe->get_content_type() == "image/jpeg")) {
    return std::make_shared<ImageItem_JPEG>(ctx, id);
  }
  else if (item_type == "hvc1") {
    return std::make_shared<ImageItem_HEVC>(ctx, id);
  }
  else if (item_type == "av01") {
    return std::make_shared<ImageItem_AVIF>(ctx, id);
  }
  else if (item_type == "vvc1") {
    return std::make_shared<ImageItem_VVC>(ctx, id);
  }
#if WITH_UNCOMPRESSED_CODEC
  else if (item_type == "unci") {
    return std::make_shared<ImageItem_uncompressed>(ctx, id);
  }
#endif
  else if (item_type == "j2k1") {
    return std::make_shared<ImageItem_JPEG2000>(ctx, id);
  }
  else if (item_type == "mski") {
    return std::make_shared<ImageItem_mask>(ctx, id);
  }
  else if (item_type == "grid") {
    return std::make_shared<ImageItem_Grid>(ctx, id);
  }
  else {
    return nullptr;
  }

#if 0
          item_type == "tild" ||
          item_type == "iden" ||
          item_type == "iovl" ||
          item_type == "mski");
#endif

  return nullptr;
}


std::shared_ptr<ImageItem> ImageItem::alloc_for_encoder(HeifContext* ctx, struct heif_encoder* encoder)
{
  switch (encoder->plugin->compression_format) {
    case heif_compression_JPEG:
      return std::make_shared<ImageItem_JPEG>(ctx);
    case heif_compression_HEVC:
      return std::make_shared<ImageItem_HEVC>(ctx);
    case heif_compression_AV1:
      return std::make_shared<ImageItem_AVIF>(ctx);
    case heif_compression_VVC:
      return std::make_shared<ImageItem_VVC>(ctx);
#if WITH_UNCOMPRESSED_CODEC
    case heif_compression_uncompressed:
      return std::make_shared<ImageItem_uncompressed>(ctx);
#endif
    case heif_compression_JPEG2000:
    case heif_compression_HTJ2K:
      return std::make_shared<ImageItem_JPEG2000>(ctx);
    case heif_compression_mask:
      return std::make_shared<ImageItem_mask>(ctx);
    default:
      assert(false);
      return nullptr;
  }
}


Result<ImageItem::CodedImageData> ImageItem::encode_to_bistream_and_boxes(const std::shared_ptr<HeifPixelImage>& image,
                                                                          struct heif_encoder* encoder,
                                                                          const struct heif_encoding_options& options,
                                                                          enum heif_image_input_class input_class)
{
  // === generate compressed image bitstream

  Result<ImageItem::CodedImageData> encodeResult = encode(image, encoder, options, input_class);
  if (encodeResult.error) {
    return encodeResult;
  }

  CodedImageData& codedImage = encodeResult.value;

  // === generate properties

  // --- choose which color profile to put into 'colr' box

  add_color_profile(image, options, input_class, options.output_nclx_profile, codedImage);


  // --- ispe
  // Note: 'ispe' must come before the transformation properties

  uint32_t input_width, input_height;
  input_width = image->get_width();
  input_height = image->get_height();

  // --- get the real size of the encoded image

  // highest priority: codedImageData
  uint32_t encoded_width = codedImage.encoded_image_width;
  uint32_t encoded_height = codedImage.encoded_image_height;

  // second priority: query plugin API
  if (encoded_width == 0 &&
      encoder->plugin->plugin_api_version >= 3 &&
      encoder->plugin->query_encoded_size != nullptr) {

    encoder->plugin->query_encoded_size(encoder->encoder,
                                        input_width, input_height,
                                        &encoded_width,
                                        &encoded_height);
  }
  else if (encoded_width == 0) {
    // fallback priority: use input size
    encoded_width = input_width;
    encoded_height = input_height;
  }

  auto ispe = std::make_shared<Box_ispe>();
  ispe->set_size(encoded_width, encoded_height);
  ispe->set_is_essential(is_ispe_essential());
  codedImage.properties.push_back(ispe);


  // --- clap (if needed)

  if (input_width != encoded_width ||
      input_height != encoded_height) {

    auto clap = std::make_shared<Box_clap>();
    clap->set(input_width, input_height, encoded_width, encoded_height);
    codedImage.properties.push_back(clap);
  }



  // --- add common metadata properties (pixi, ...)

  auto colorspace = image->get_colorspace();
  auto chroma = image->get_chroma_format();


  // --- write PIXI property

  std::shared_ptr<Box_pixi> pixi = std::make_shared<Box_pixi>();
  if (colorspace == heif_colorspace_monochrome) {
    pixi->add_channel_bits(image->get_bits_per_pixel(heif_channel_Y));
  }
  else if (colorspace == heif_colorspace_YCbCr) {
    pixi->add_channel_bits(image->get_bits_per_pixel(heif_channel_Y));
    pixi->add_channel_bits(image->get_bits_per_pixel(heif_channel_Cb));
    pixi->add_channel_bits(image->get_bits_per_pixel(heif_channel_Cr));
  }
  else if (colorspace == heif_colorspace_RGB) {
    if (chroma == heif_chroma_444) {
      pixi->add_channel_bits(image->get_bits_per_pixel(heif_channel_R));
      pixi->add_channel_bits(image->get_bits_per_pixel(heif_channel_G));
      pixi->add_channel_bits(image->get_bits_per_pixel(heif_channel_B));
    }
    else if (chroma == heif_chroma_interleaved_RGB ||
             chroma == heif_chroma_interleaved_RGBA) {
      pixi->add_channel_bits(8);
      pixi->add_channel_bits(8);
      pixi->add_channel_bits(8);
    }
  }
  codedImage.properties.push_back(pixi);


  // --- write PASP property

  if (image->has_nonsquare_pixel_ratio()) {
    auto pasp = std::make_shared<Box_pasp>();
    image->get_pixel_ratio(&pasp->hSpacing, &pasp->vSpacing);

    codedImage.properties.push_back(pasp);
  }


  // --- write CLLI property

  if (image->has_clli()) {
    auto clli = std::make_shared<Box_clli>();
    clli->clli = image->get_clli();

    codedImage.properties.push_back(clli);
  }


  // --- write MDCV property

  if (image->has_mdcv()) {
    auto mdcv = std::make_shared<Box_mdcv>();
    mdcv->mdcv = image->get_mdcv();

    codedImage.properties.push_back(mdcv);
  }

  return encodeResult;
}


Error ImageItem::encode_to_item(HeifContext* ctx,
                                const std::shared_ptr<HeifPixelImage>& image,
                                struct heif_encoder* encoder,
                                const struct heif_encoding_options& options,
                                enum heif_image_input_class input_class)
{
  uint32_t input_width = image->get_width(heif_channel_Y);
  uint32_t input_height = image->get_height(heif_channel_Y);

  set_size(input_width, input_height);


  // compress image and assign data to item

  Result<CodedImageData> codingResult = encode_to_bistream_and_boxes(image, encoder, options, input_class);
  if (codingResult.error) {
    return codingResult.error;
  }

  CodedImageData& codedImage = codingResult.value;

  auto infe_box = ctx->get_heif_file()->add_new_infe_box(get_infe_type());
  heif_item_id image_id = infe_box->get_item_ID();
  set_id(image_id);

  ctx->get_heif_file()->append_iloc_data(image_id, codedImage.bitstream, 0);


  // set item properties

  for (auto& propertyBox : codingResult.value.properties) {
    int index = ctx->get_heif_file()->get_ipco_box()->find_or_append_child_box(propertyBox);
    ctx->get_heif_file()->get_ipma_box()->add_property_for_item_ID(image_id, Box_ipma::PropertyAssociation{propertyBox->is_essential(),
                                                                                                           uint16_t(index + 1)});
  }


  // MIAF 7.3.6.7
  // This is according to MIAF without Amd2. With Amd2, the restriction has been lifted and the image is MIAF compatible.
  // However, since AVIF is based on MIAF, the whole image would be invalid in that case.

  // We might remove this code at a later point in time when MIAF Amd2 is in wide use.

  if (encoder->plugin->compression_format != heif_compression_AV1 &&
      image->get_colorspace() == heif_colorspace_YCbCr) {
    if (!is_integer_multiple_of_chroma_size(image->get_width(),
                                            image->get_height(),
                                            image->get_chroma_format())) {
      mark_not_miaf_compatible();
    }
  }

  // TODO: move this into encode_to_bistream_and_boxes()
  ctx->get_heif_file()->add_orientation_properties(image_id, options.image_orientation);

  return Error::Ok;
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


Error ImageItem::get_coded_image_colorspace(heif_colorspace* out_colorspace, heif_chroma* out_chroma) const
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
    *out_chroma = (heif_chroma) (hvcC->get_configuration().chroma_format);
  }
  else if (auto vvcC = m_heif_context->get_heif_file()->get_property<Box_vvcC>(id)) {
    *out_chroma = (heif_chroma) (vvcC->get_configuration().chroma_format_idc);
  }
  else if (auto av1C = m_heif_context->get_heif_file()->get_property<Box_av1C>(id)) {
    *out_chroma = (heif_chroma) (av1C->get_configuration().get_heif_chroma());
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
      }
      else if ((uncC->get_profile() == fourcc("rgba")) || (uncC->get_profile() == fourcc("abgr"))) {
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

            m_heif_context->get_heif_file()->append_iloc_data(m_id, nal_data_with_size, 0);
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


static std::shared_ptr<color_profile_nclx> compute_target_nclx_profile(const std::shared_ptr<HeifPixelImage>& image, const heif_color_profile_nclx* output_nclx_profile)
{
  auto target_nclx_profile = std::make_shared<color_profile_nclx>();

  // If there is an output NCLX specified, use that.
  if (output_nclx_profile) {
    target_nclx_profile->set_from_heif_color_profile_nclx(output_nclx_profile);
  }
    // Otherwise, if there is an input NCLX, keep that.
  else if (auto input_nclx = image->get_color_profile_nclx()) {
    *target_nclx_profile = *input_nclx;
  }
    // Otherwise, just use the defaults (set below)
  else {
    target_nclx_profile->set_undefined();
  }

  target_nclx_profile->replace_undefined_values_with_sRGB_defaults();

  return target_nclx_profile;
}


static bool nclx_profile_matches_spec(heif_colorspace colorspace,
                                      std::shared_ptr<const color_profile_nclx> image_nclx,
                                      const struct heif_color_profile_nclx* spec_nclx)
{
  if (colorspace != heif_colorspace_YCbCr) {
    return true;
  }

  // No target specification -> always matches
  if (!spec_nclx) {
    return true;
  }

  if (!image_nclx) {
    // if no input nclx is specified, compare against default one
    image_nclx = std::make_shared<color_profile_nclx>();
  }

  if (image_nclx->get_full_range_flag() != (spec_nclx->full_range_flag == 0 ? false : true)) {
    return false;
  }

  if (image_nclx->get_matrix_coefficients() != spec_nclx->matrix_coefficients) {
    return false;
  }

  // TODO: are the colour primaries relevant for matrix-coefficients != 12,13 ?
  //       If not, we should skip this test for anything else than matrix-coefficients != 12,13.
  if (image_nclx->get_colour_primaries() != spec_nclx->color_primaries) {
    return false;
  }

  return true;
}


Result<std::shared_ptr<HeifPixelImage>> ImageItem::convert_colorspace_for_encoding(const std::shared_ptr<HeifPixelImage>& image,
                                                                                   struct heif_encoder* encoder,
                                                                                   const struct heif_encoding_options& options)
//const heif_color_profile_nclx* target_heif_nclx)
{
  heif_colorspace colorspace = image->get_colorspace();
  heif_chroma chroma = image->get_chroma_format();

  if (encoder->plugin->plugin_api_version >= 2) {
    encoder->plugin->query_input_colorspace2(encoder->encoder, &colorspace, &chroma);
  }
  else {
    encoder->plugin->query_input_colorspace(&colorspace, &chroma);
  }


  // If output format forces an NCLX, use that. Otherwise use user selected NCLX.

  //const heif_color_profile_nclx* target_heif_nclx = options.output_nclx_profile;

  std::shared_ptr<color_profile_nclx> target_nclx_profile = compute_target_nclx_profile(image, options.output_nclx_profile);

  // --- convert colorspace

  std::shared_ptr<HeifPixelImage> output_image;

  if (colorspace != image->get_colorspace() ||
      chroma != image->get_chroma_format() ||
      !nclx_profile_matches_spec(colorspace, image->get_color_profile_nclx(), options.output_nclx_profile)) {
    // @TODO: use color profile when converting
    int output_bpp = 0; // same as input

    //auto target_nclx = std::make_shared<color_profile_nclx>();
    //target_nclx->set_from_heif_color_profile_nclx(target_heif_nclx);

    output_image = convert_colorspace(image, colorspace, chroma, target_nclx_profile,
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


    // save nclx profile

    bool save_nclx_profile = (options.output_nclx_profile != nullptr);

    // if there is an ICC profile, only save NCLX when we chose to save both profiles
    if (icc_profile && !(options.version >= 3 &&
                         options.save_two_colr_boxes_when_ICC_and_nclx_available)) {
      save_nclx_profile = false;
    }

    // we might have turned off nclx completely because macOS/iOS cannot read it
    if (options.version >= 4 && options.macOS_compatibility_workaround_no_nclx_profile) {
      save_nclx_profile = false;
    }

    if (save_nclx_profile) {
      auto target_nclx_profile = std::make_shared<color_profile_nclx>();
      target_nclx_profile->set_from_heif_color_profile_nclx(target_heif_nclx);

      auto colr = std::make_shared<Box_colr>();
      colr->set_color_profile(target_nclx_profile);
      inout_codedImage.properties.push_back(colr);
    }
  }
}


void ImageItem::CodedImageData::append(const uint8_t* data, size_t size)
{
  bitstream.insert(bitstream.end(), data, data + size);
}


void ImageItem::CodedImageData::append_with_4bytes_size(const uint8_t* data, size_t size)
{
  assert(size <= 0xFFFFFFFF);

  uint8_t size_field[4];
  size_field[0] = (uint8_t) ((size >> 24) & 0xFF);
  size_field[1] = (uint8_t) ((size >> 16) & 0xFF);
  size_field[2] = (uint8_t) ((size >> 8) & 0xFF);
  size_field[3] = (uint8_t) ((size >> 0) & 0xFF);

  bitstream.insert(bitstream.end(), size_field, size_field + 4);
  bitstream.insert(bitstream.end(), data, data + size);
}


Error ImageItem::check_for_valid_image_size(uint32_t width, uint32_t height) const
{
  uint64_t maximum_image_size_limit = m_heif_context->get_maximum_image_size_limit();

  // --- check whether the image size is "too large"

  auto max_width_height = static_cast<uint32_t>(std::numeric_limits<int>::max());
  if ((width > max_width_height || height > max_width_height) ||
      (height != 0 && width > maximum_image_size_limit / height)) {
    std::stringstream sstr;
    sstr << "Image size " << width << "x" << height << " exceeds the maximum image size "
         << maximum_image_size_limit << "\n";

    return {heif_error_Memory_allocation_error,
            heif_suberror_Security_limit_exceeded,
            sstr.str()};
  }

  if (width == 0 || height == 0) {
    return {heif_error_Memory_allocation_error,
            heif_suberror_Invalid_image_size,
            "zero width or height"};
  }

  return Error::Ok;

}


Result<std::shared_ptr<HeifPixelImage>> ImageItem::decode_image(heif_colorspace out_colorspace,
                                                                const struct heif_decoding_options& options,
                                                                bool decode_tile_only, uint32_t tile_x0, uint32_t tile_y0) const
{
  // --- check whether image size (according to 'ispe') exceeds maximum

  auto ispe = m_heif_context->get_heif_file()->get_property<Box_ispe>(m_id);
  if (ispe) {
    Error err = check_for_valid_image_size(ispe->get_width(), ispe->get_height());
    if (err) {
      return err;
    }
  }


  // --- decode image

  Result<std::shared_ptr<HeifPixelImage>> decodingResult = decode_compressed_image(options, decode_tile_only, tile_x0, tile_y0);
  if (decodingResult.error) {
    return decodingResult.error;
  }

  auto img = decodingResult.value;


  // --- convert color-space

  heif_colorspace target_colorspace = (out_colorspace == heif_colorspace_undefined ?
                                       img->get_colorspace() :
                                       out_colorspace);

#if 1  // TODO: disabling this will likely improve performance, but we have to implement "grid" and "iovl" for more input variants

  if (/*!alphaImage &&*/ target_colorspace == heif_colorspace_YCbCr) {
    target_colorspace = heif_colorspace_RGB;
  }
#endif

  heif_chroma target_chroma = (target_colorspace == heif_colorspace_monochrome ?
                               heif_chroma_monochrome : heif_chroma_444);

  bool different_chroma = (target_chroma != img->get_chroma_format());
  bool different_colorspace = (target_colorspace != img->get_colorspace());

  if (different_chroma || different_colorspace) {
    img = convert_colorspace(img, target_colorspace, target_chroma, nullptr, 0, options.color_conversion_options);
    if (!img) {
      return Error(heif_error_Unsupported_feature, heif_suberror_Unsupported_color_conversion);
    }
  }


  std::shared_ptr<HeifFile> file = m_heif_context->get_heif_file();


  // --- apply image transformations

  Error error;

  // TODO: for tile decoding, we should require that all transformations are ignored or processed

  if (options.ignore_transformations == false) {
    std::vector<std::shared_ptr<Box>> properties;
    auto ipco_box = file->get_ipco_box();
    auto ipma_box = file->get_ipma_box();
    error = ipco_box->get_properties_for_item_ID(m_id, ipma_box, properties);

    for (const auto& property : properties) {
      if (property->get_short_type() == fourcc("irot")) {
        auto rot = std::dynamic_pointer_cast<Box_irot>(property);
        std::shared_ptr<HeifPixelImage> rotated_img;
        error = img->rotate_ccw(rot->get_rotation(), rotated_img);
        if (error) {
          return error;
        }

        img = rotated_img;
      }


      if (property->get_short_type() == fourcc("imir")) {
        auto mirror = std::dynamic_pointer_cast<Box_imir>(property);
        error = img->mirror_inplace(mirror->get_mirror_direction());
        if (error) {
          return error;
        }
      }


      if (property->get_short_type() == fourcc("clap")) {
        auto clap = std::dynamic_pointer_cast<Box_clap>(property);
        std::shared_ptr<HeifPixelImage> clap_img;

        int img_width = img->get_width();
        int img_height = img->get_height();
        assert(img_width >= 0);
        assert(img_height >= 0);

        int left = clap->left_rounded(img_width);
        int right = clap->right_rounded(img_width);
        int top = clap->top_rounded(img_height);
        int bottom = clap->bottom_rounded(img_height);

        if (left < 0) { left = 0; }
        if (top < 0) { top = 0; }

        if (right >= img_width) { right = img_width - 1; }
        if (bottom >= img_height) { bottom = img_height - 1; }

        if (left > right ||
            top > bottom) {
          return Error(heif_error_Invalid_input,
                       heif_suberror_Invalid_clean_aperture);
        }

        std::shared_ptr<HeifPixelImage> cropped_img;
        error = img->crop(left, right, top, bottom, cropped_img);
        if (error) {
          return error;
        }

        img = cropped_img;
      }
    }
  }


  // --- add alpha channel, if available

  // TODO: this if statement is probably wrong. When we have a tiled image with alpha
  // channel, then the alpha images should be associated with their respective tiles.
  // However, the tile images are not part of the m_all_images list.
  // Fix this, when we have a test image available.

  std::shared_ptr<ImageItem> alpha_image = get_alpha_channel();
  if (alpha_image) {
    auto alphaDecodingResult = alpha_image->decode_image(heif_colorspace_monochrome, options, decode_tile_only, tile_x0, tile_y0);
    if (alphaDecodingResult.error) {
      return alphaDecodingResult.error;
    }

    std::shared_ptr<HeifPixelImage> alpha = alphaDecodingResult.value;

    // TODO: check that sizes are the same and that we have an Y channel
    // BUT: is there any indication in the standard that the alpha channel should have the same size?

    heif_channel channel;
    switch (alpha->get_colorspace()) {
      case heif_colorspace_YCbCr:
      case heif_colorspace_monochrome:
        channel = heif_channel_Y;
        break;
      case heif_colorspace_RGB:
        channel = heif_channel_R;
        break;
      case heif_colorspace_undefined:
      default:
        return Error(heif_error_Invalid_input,
                     heif_suberror_Unsupported_color_conversion);
    }


    // TODO: we should include a decoding option to control whether libheif should automatically scale the alpha channel, and if so, which scaling filter (enum: Off, NN, Bilinear, ...).
    //       It might also be that a specific output format implies that alpha is scaled (RGBA32). That would favor an enum for the scaling filter option + a bool to switch auto-filtering on.
    //       But we can only do this when libheif itself doesn't assume anymore that the alpha channel has the same resolution.

    if ((alpha_image->get_width() != img->get_width()) || (alpha_image->get_height() != img->get_height())) {
      std::shared_ptr<HeifPixelImage> scaled_alpha;
      Error err = alpha->scale_nearest_neighbor(scaled_alpha, img->get_width(), img->get_height());
      if (err) {
        return err;
      }
      alpha = std::move(scaled_alpha);
    }
    img->transfer_plane_from_image_as(alpha, channel, heif_channel_Alpha);

    if (is_premultiplied_alpha()) {
      img->set_premultiplied_alpha(true);
    }
  }


  // --- set color profile

  // If there is an NCLX profile in the HEIF/AVIF metadata, use this for the color conversion.
  // Otherwise, use the profile that is stored in the image stream itself and then set the
  // (non-NCLX) profile later.
  auto nclx = get_color_profile_nclx();
  if (nclx) {
    img->set_color_profile_nclx(nclx);
  }

  auto icc = get_color_profile_icc();
  if (icc) {
    img->set_color_profile_icc(icc);
  }


  // --- attach metadata to image

  {
    auto ipco_box = file->get_ipco_box();
    auto ipma_box = file->get_ipma_box();

    // CLLI

    auto clli_box = ipco_box->get_property_for_item_ID(m_id, ipma_box, fourcc("clli"));
    auto clli = std::dynamic_pointer_cast<Box_clli>(clli_box);

    if (clli) {
      img->set_clli(clli->clli);
    }

    // MDCV

    auto mdcv_box = ipco_box->get_property_for_item_ID(m_id, ipma_box, fourcc("mdcv"));
    auto mdcv = std::dynamic_pointer_cast<Box_mdcv>(mdcv_box);

    if (mdcv) {
      img->set_mdcv(mdcv->mdcv);
    }

    // PASP

    auto pasp_box = ipco_box->get_property_for_item_ID(m_id, ipma_box, fourcc("pasp"));
    auto pasp = std::dynamic_pointer_cast<Box_pasp>(pasp_box);

    if (pasp) {
      img->set_pixel_ratio(pasp->hSpacing, pasp->vSpacing);
    }
  }

  return img;
}


Result<std::shared_ptr<HeifPixelImage>> ImageItem::decode_compressed_image(const struct heif_decoding_options& options,
                                                                           bool decode_tile_only, uint32_t tile_x0, uint32_t tile_y0) const
{
  // --- find the decoder plugin with the correct compression format

  heif_compression_format compression_format = get_compression_format();
  if (compression_format == heif_compression_undefined) {
    return Error{heif_error_Decoder_plugin_error, heif_suberror_Unsupported_codec, "Decoding not supported"};
  }

  const struct heif_decoder_plugin* decoder_plugin = get_decoder(compression_format, options.decoder_id);
  if (!decoder_plugin) {
    return Error(heif_error_Plugin_loading_error, heif_suberror_No_matching_decoder_installed);
  }


  // --- get the compressed image data

  std::vector<uint8_t> data;
  Error error = m_heif_context->get_heif_file()->get_compressed_image_data(m_id, &data);
  if (error) {
    return error;
  }


  // --- decode image with the plugin

  void* decoder;
  struct heif_error err = decoder_plugin->new_decoder(&decoder);
  if (err.code != heif_error_Ok) {
    return Error(err.code, err.subcode, err.message);
  }

  if (decoder_plugin->plugin_api_version >= 2) {
    if (decoder_plugin->set_strict_decoding) {
      decoder_plugin->set_strict_decoding(decoder, options.strict_decoding);
    }
  }

  err = decoder_plugin->push_data(decoder, data.data(), data.size());
  if (err.code != heif_error_Ok) {
    decoder_plugin->free_decoder(decoder);
    return Error(err.code, err.subcode, err.message);
  }

  heif_image* decoded_img = nullptr;

  err = decoder_plugin->decode_image(decoder, &decoded_img);
  if (err.code != heif_error_Ok) {
    decoder_plugin->free_decoder(decoder);
    return Error(err.code, err.subcode, err.message);
  }

  if (!decoded_img) {
    // TODO(farindk): The plugin should return an error in this case.
    decoder_plugin->free_decoder(decoder);
    return Error(heif_error_Decoder_plugin_error, heif_suberror_Unspecified);
  }

  // -- cleanup

  std::shared_ptr<HeifPixelImage> img = std::move(decoded_img->image);
  heif_image_release(decoded_img);

  decoder_plugin->free_decoder(decoder);


  return img;
}
