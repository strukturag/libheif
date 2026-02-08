/*
 * HEIF codec.
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

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <map>
#include <iostream>
#include <cassert>
#include <utility>

#include "common_utils.h"
#include "context.h"
#include "compression.h"
#include "error.h"
#include "libheif/heif.h"
#include "codecs/uncompressed/unc_types.h"
#include "codecs/uncompressed/unc_boxes.h"
#include "unc_image.h"
#include "codecs/uncompressed/unc_dec.h"
#include "codecs/uncompressed/unc_enc.h"
#include "codecs/uncompressed/unc_codec.h"
#include "image_item.h"
#include "codecs/uncompressed/unc_encoder.h"


struct unciHeaders;

ImageItem_uncompressed::ImageItem_uncompressed(HeifContext* ctx, heif_item_id id)
    : ImageItem(ctx, id)
{
  m_encoder = std::make_shared<Encoder_uncompressed>();
}

ImageItem_uncompressed::ImageItem_uncompressed(HeifContext* ctx)
    : ImageItem(ctx)
{
  m_encoder = std::make_shared<Encoder_uncompressed>();
}


Result<std::shared_ptr<HeifPixelImage>> ImageItem_uncompressed::decode_compressed_image(const heif_decoding_options& options,
                                                                                bool decode_tile_only, uint32_t tile_x0, uint32_t tile_y0,
                                                                                std::set<heif_item_id> processed_ids) const
{
  std::shared_ptr<HeifPixelImage> img;

  std::vector<uint8_t> data;

  Error err;

  if (decode_tile_only) {
    err = UncompressedImageCodec::decode_uncompressed_image_tile(get_context(),
                                                                 get_id(),
                                                                 img,
                                                                 tile_x0, tile_y0);
  }
  else {
    err = UncompressedImageCodec::decode_uncompressed_image(get_context(),
                                                            get_id(),
                                                            img);
  }

  if (err) {
    return err;
  }
  else {
    return img;
  }
}


Result<Encoder::CodedImageData> ImageItem_uncompressed::encode(const std::shared_ptr<HeifPixelImage>& src_image,
                                                                 heif_encoder* encoder,
                                                                 const heif_encoding_options& options,
                                                                 heif_image_input_class input_class)
{
  Result<std::unique_ptr<const unc_encoder>> uncEncoder = unc_encoder_factory::get_unc_encoder(src_image, options);
  if (!uncEncoder) {
    return {uncEncoder.error()};
  }

  return (*uncEncoder)->encode_static(src_image, options);
}


Result<std::shared_ptr<ImageItem_uncompressed>> ImageItem_uncompressed::add_unci_item(HeifContext* ctx,
                                                                                      const heif_unci_image_parameters* parameters,
                                                                                      const heif_encoding_options* encoding_options,
                                                                                      const std::shared_ptr<const HeifPixelImage>& prototype)
{
  assert(encoding_options != nullptr);

  // Check input parameters

  if (parameters->image_width % parameters->tile_width != 0 ||
      parameters->image_height % parameters->tile_height != 0) {
    return Error{heif_error_Usage_error,
                 heif_suberror_Invalid_parameter_value,
                 "ISO 23001-17 image size must be an integer multiple of the tile size."};
  }


  Result<std::unique_ptr<const unc_encoder>> uncEncoder = unc_encoder_factory::get_unc_encoder(prototype, *encoding_options);
  if (!uncEncoder) {
    return {uncEncoder.error()};
  }


  // Create 'unci' Item

  auto file = ctx->get_heif_file();

  heif_item_id unci_id = ctx->get_heif_file()->add_new_image(fourcc("unci"));
  auto unci_image = std::make_shared<ImageItem_uncompressed>(ctx, unci_id);
  unci_image->set_resolution(parameters->image_width, parameters->image_height);
  unci_image->m_unc_encoder = std::move(*uncEncoder);
  unci_image->m_encoding_options = *encoding_options;

  ctx->insert_image_item(unci_id, unci_image);



  // Generate headers

  // --- generate configuration property boxes

  auto uncC = unci_image->m_unc_encoder->get_uncC();
  unci_image->add_property(uncC, true);
  if (!uncC->is_minimized()) {
    unci_image->add_property(unci_image->m_unc_encoder->get_cmpd(), true);
  }


  // Add `ispe` property

  auto ispe = std::make_shared<Box_ispe>();
  ispe->set_size(static_cast<uint32_t>(parameters->image_width),
                 static_cast<uint32_t>(parameters->image_height));
  unci_image->add_property(ispe, true);

  if (parameters->compression != heif_unci_compression_off) {
    auto icef = std::make_shared<Box_icef>();
    auto cmpC = std::make_shared<Box_cmpC>();
    cmpC->set_compressed_unit_type(heif_cmpC_compressed_unit_type_image_tile);

    if (false) {
    }
#if HAVE_ZLIB
    else if (parameters->compression == heif_unci_compression_deflate) {
      cmpC->set_compression_type(fourcc("defl"));
    }
    else if (parameters->compression == heif_unci_compression_zlib) {
      cmpC->set_compression_type(fourcc("zlib"));
    }
#endif
#if HAVE_BROTLI
    else if (parameters->compression == heif_unci_compression_brotli) {
      cmpC->set_compression_type(fourcc("brot"));
    }
#endif
    else {
      assert(false);
    }

    unci_image->add_property(cmpC, true);
    unci_image->add_property_without_deduplication(icef, true); // icef is empty. A normal add_property() would lead to a wrong deduplication.
  }

  // Create empty image. If we use compression, we append the data piece by piece.

  if (parameters->compression == heif_unci_compression_off) {
    assert(false); // TODO compute_tile_data_size_bytes() is too simplistic
    uint64_t tile_size = uncC->compute_tile_data_size_bytes(parameters->image_width / uncC->get_number_of_tile_columns(),
                                                            parameters->image_height / uncC->get_number_of_tile_rows());

    std::vector<uint8_t> dummydata;
    dummydata.resize(tile_size);

    uint32_t nTiles = (parameters->image_width / parameters->tile_width) * (parameters->image_height / parameters->tile_height);

    for (uint64_t i = 0; i < nTiles; i++) {
      const int construction_method = 0; // 0=mdat 1=idat
      file->append_iloc_data(unci_id, dummydata, construction_method);
    }
  }

  // Set Brands
  //ctx->get_heif_file()->set_brand(heif_compression_uncompressed, unci_image->is_miaf_compatible());

  return {unci_image};
}


Error ImageItem_uncompressed::add_image_tile(uint32_t tile_x, uint32_t tile_y, const std::shared_ptr<const HeifPixelImage>& image, bool save_alpha)
{
  std::shared_ptr<Box_uncC> uncC = get_property<Box_uncC>();
  assert(uncC);

  uint32_t tile_width = image->get_width();
  uint32_t tile_height = image->get_height();

  uint32_t tile_idx = tile_y * uncC->get_number_of_tile_columns() + tile_x;

  if (tile_y >= uncC->get_number_of_tile_rows() ||
      tile_x >= uncC->get_number_of_tile_columns()) {
    return Error{heif_error_Usage_error,
                 heif_suberror_Invalid_parameter_value,
                 "tile_x and/or tile_y are out of range."};
  }


  if (image->has_alpha() && !save_alpha) {
    // TODO: drop alpha
  }

  Result<std::vector<uint8_t>> codedBitstreamResult = m_unc_encoder->encode_tile(image);
  if (!codedBitstreamResult) {
    return codedBitstreamResult.error();
  }

  std::shared_ptr<Box_cmpC> cmpC = get_property<Box_cmpC>();
  std::shared_ptr<Box_icef> icef = get_property<Box_icef>();

  if (!icef || !cmpC) {
    assert(!icef);
    assert(!cmpC);

    // uncompressed

    uint64_t tile_data_size = uncC->compute_tile_data_size_bytes(tile_width, tile_height);

    get_file()->replace_iloc_data(get_id(), tile_idx * tile_data_size, *codedBitstreamResult, 0);
  }
  else {
    std::vector<uint8_t> compressed_data;
    const std::vector<uint8_t>& raw_data = std::move(*codedBitstreamResult);
    (void)raw_data;

    uint32_t compr = cmpC->get_compression_type();
    switch (compr) {
#if HAVE_ZLIB
      case fourcc("defl"):
        compressed_data = compress_deflate(raw_data.data(), raw_data.size());
        break;
      case fourcc("zlib"):
        compressed_data = compress_zlib(raw_data.data(), raw_data.size());
        break;
#endif
#if HAVE_BROTLI
      case fourcc("brot"):
        compressed_data = compress_brotli(raw_data.data(), raw_data.size());
        break;
#endif
      default:
        assert(false);
        break;
    }

    get_file()->append_iloc_data(get_id(), compressed_data, 0);

    Box_icef::CompressedUnitInfo unit_info;
    unit_info.unit_offset = m_next_tile_write_pos;
    unit_info.unit_size = compressed_data.size();
    icef->set_component(tile_idx, unit_info);

    m_next_tile_write_pos += compressed_data.size();
  }

  return Error::Ok;
}


void ImageItem_uncompressed::get_tile_size(uint32_t& w, uint32_t& h) const
{
  auto ispe = get_property<Box_ispe>();
  auto uncC = get_property<Box_uncC>();

  if (!ispe || !uncC) {
    w = h = 0;
  }
  else {
    w = ispe->get_width() / uncC->get_number_of_tile_columns();
    h = ispe->get_height() / uncC->get_number_of_tile_rows();
  }
}


heif_image_tiling ImageItem_uncompressed::get_heif_image_tiling() const
{
  heif_image_tiling tiling{};

  auto ispe = get_property<Box_ispe>();
  auto uncC = get_property<Box_uncC>();
  assert(ispe && uncC);

  tiling.num_columns = uncC->get_number_of_tile_columns();
  tiling.num_rows = uncC->get_number_of_tile_rows();

  tiling.tile_width = ispe->get_width() / tiling.num_columns;
  tiling.tile_height = ispe->get_height() / tiling.num_rows;

  tiling.image_width = ispe->get_width();
  tiling.image_height = ispe->get_height();
  tiling.number_of_extra_dimensions = 0;

  return tiling;
}

Result<std::shared_ptr<Decoder>> ImageItem_uncompressed::get_decoder() const
{
  return {m_decoder};
}

std::shared_ptr<Encoder> ImageItem_uncompressed::get_encoder() const
{
  return m_encoder;
}

Error ImageItem_uncompressed::initialize_decoder()
{
  std::shared_ptr<Box_cmpd> cmpd = get_property<Box_cmpd>();
  std::shared_ptr<Box_uncC> uncC = get_property<Box_uncC>();
  std::shared_ptr<Box_ispe> ispe = get_property<Box_ispe>();

  if (!uncC) {
    return Error{heif_error_Invalid_input,
                 heif_suberror_Unspecified,
                 "No 'uncC' box found."};
  }

  m_decoder = std::make_shared<Decoder_uncompressed>(uncC, cmpd, ispe);

  return Error::Ok;
}

void ImageItem_uncompressed::set_decoder_input_data()
{
  DataExtent extent;
  extent.set_from_image_item(get_context()->get_heif_file(), get_id());

  m_decoder->set_data_extent(std::move(extent));
}


bool ImageItem_uncompressed::has_coded_alpha_channel() const
{
  return m_decoder->has_alpha_component();
}

heif_brand2 ImageItem_uncompressed::get_compatible_brand() const
{
  return 0; // TODO: not clear to me what to use
}
