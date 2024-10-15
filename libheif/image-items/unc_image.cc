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
#include "codecs/uncompressed/unc_codec.h"
#include "image_item.h"



static void maybe_make_minimised_uncC(std::shared_ptr<Box_uncC>& uncC, const std::shared_ptr<const HeifPixelImage>& image)
{
  uncC->set_version(0);
  if (image->get_colorspace() != heif_colorspace_RGB) {
    return;
  }
  if (!((image->get_chroma_format() == heif_chroma_interleaved_RGB) || (image->get_chroma_format() == heif_chroma_interleaved_RGBA))) {
    return;
  }
  if (image->get_bits_per_pixel(heif_channel_interleaved) != 8) {
    return;
  }
  if (image->get_chroma_format() == heif_chroma_interleaved_RGBA) {
    uncC->set_profile(fourcc("rgba"));
  } else {
    uncC->set_profile(fourcc("rgb3"));
  }
  uncC->set_version(1);
}


Result<std::shared_ptr<HeifPixelImage>> ImageItem_uncompressed::decode_compressed_image(const struct heif_decoding_options& options,
                                                                                bool decode_tile_only, uint32_t tile_x0, uint32_t tile_y0) const
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


struct unciHeaders
{
  std::shared_ptr<Box_uncC> uncC;
  std::shared_ptr<Box_cmpd> cmpd;
};


static Result<unciHeaders> generate_headers(const std::shared_ptr<const HeifPixelImage>& src_image,
                                            const heif_unci_image_parameters* parameters,
                                            const struct heif_encoding_options* options)
{
  unciHeaders headers;

  bool uses_tiles = (parameters->tile_width != parameters->image_width ||
                     parameters->tile_height != parameters->image_height);

  std::shared_ptr<Box_uncC> uncC = std::make_shared<Box_uncC>();
  if (options && options->prefer_uncC_short_form && !uses_tiles) {
    maybe_make_minimised_uncC(uncC, src_image);
  }

  if (uncC->get_version() == 1) {
    headers.uncC = uncC;
  } else {
    std::shared_ptr<Box_cmpd> cmpd = std::make_shared<Box_cmpd>();

    Error error = fill_cmpd_and_uncC(cmpd, uncC, src_image, parameters);
    if (error) {
      return error;
    }

    headers.cmpd = cmpd;
    headers.uncC = uncC;
  }

  return headers;
}


Result<std::vector<uint8_t>> encode_image_tile(const std::shared_ptr<const HeifPixelImage>& src_image)
{
  std::vector<uint8_t> data;

  if (src_image->get_colorspace() == heif_colorspace_YCbCr)
  {
    uint64_t offset = 0;
    for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr})
    {
      uint32_t src_stride;
      uint32_t src_width = src_image->get_width(channel);
      uint32_t src_height = src_image->get_height(channel);
      const uint8_t* src_data = src_image->get_plane(channel, &src_stride);
      uint64_t out_size = src_width * src_height;
      data.resize(data.size() + out_size);
      for (uint32_t y = 0; y < src_height; y++) {
        memcpy(data.data() + offset + y * src_width, src_data + src_stride * y, src_width);
      }
      offset += out_size;
    }

    return data;
  }
  else if (src_image->get_colorspace() == heif_colorspace_RGB)
  {
    if (src_image->get_chroma_format() == heif_chroma_444)
    {
      uint64_t offset = 0;
      std::vector<heif_channel> channels = {heif_channel_R, heif_channel_G, heif_channel_B};
      if (src_image->has_channel(heif_channel_Alpha))
      {
        channels.push_back(heif_channel_Alpha);
      }
      for (heif_channel channel : channels)
      {
        uint32_t src_stride;
        const uint8_t* src_data = src_image->get_plane(channel, &src_stride);
        uint64_t out_size = src_image->get_height() * src_image->get_width();

        data.resize(data.size() + out_size);
        for (uint32_t y = 0; y < src_image->get_height(); y++) {
          memcpy(data.data() + offset + y * src_image->get_width(), src_data + y * src_stride, src_image->get_width());
        }

        offset += out_size;
      }

      return data;
    }
    else if ((src_image->get_chroma_format() == heif_chroma_interleaved_RGB) ||
             (src_image->get_chroma_format() == heif_chroma_interleaved_RGBA) ||
             (src_image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_BE) ||
             (src_image->get_chroma_format() == heif_chroma_interleaved_RRGGBB_LE) ||
             (src_image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_BE) ||
             (src_image->get_chroma_format() == heif_chroma_interleaved_RRGGBBAA_LE))
    {
      int bytes_per_pixel = 0;
      switch (src_image->get_chroma_format()) {
        case heif_chroma_interleaved_RGB:
          bytes_per_pixel=3;
          break;
        case heif_chroma_interleaved_RGBA:
          bytes_per_pixel=4;
          break;
        case heif_chroma_interleaved_RRGGBB_BE:
        case heif_chroma_interleaved_RRGGBB_LE:
          bytes_per_pixel=6;
          break;
        case heif_chroma_interleaved_RRGGBBAA_BE:
        case heif_chroma_interleaved_RRGGBBAA_LE:
          bytes_per_pixel=8;
          break;
        default:
          assert(false);
      }

      uint32_t src_stride;
      const uint8_t* src_data = src_image->get_plane(heif_channel_interleaved, &src_stride);
      uint64_t out_size = src_image->get_height() * src_image->get_width() * bytes_per_pixel;
      data.resize(out_size);
      for (uint32_t y = 0; y < src_image->get_height(); y++) {
        memcpy(data.data() + y * src_image->get_width() * bytes_per_pixel, src_data + src_stride * y, src_image->get_width() * bytes_per_pixel);
      }

      return data;
    }
    else
    {
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_data_version,
                   "Unsupported RGB chroma");
    }
  }
  else if (src_image->get_colorspace() == heif_colorspace_monochrome)
  {
    uint64_t offset = 0;
    std::vector<heif_channel> channels;
    if (src_image->has_channel(heif_channel_Alpha))
    {
      channels = {heif_channel_Y, heif_channel_Alpha};
    }
    else
    {
      channels = {heif_channel_Y};
    }
    for (heif_channel channel : channels)
    {
      uint32_t src_stride;
      const uint8_t* src_data = src_image->get_plane(channel, &src_stride);
      uint64_t out_size = src_image->get_height() * src_stride;
      data.resize(data.size() + out_size);
      memcpy(data.data() + offset, src_data, out_size);
      offset += out_size;
    }

    return data;
  }
  else
  {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 "Unsupported colourspace");
  }

}


Result<ImageItem::CodedImageData> ImageItem_uncompressed::encode(const std::shared_ptr<HeifPixelImage>& src_image,
                                                                 struct heif_encoder* encoder,
                                                                 const struct heif_encoding_options& options,
                                                                 enum heif_image_input_class input_class)
{
  heif_unci_image_parameters parameters{};
  parameters.image_width = src_image->get_width();
  parameters.image_height = src_image->get_height();
  parameters.tile_width = parameters.image_width;
  parameters.tile_height = parameters.image_height;


  // --- generate configuration property boxes

  Result<unciHeaders> genHeadersResult = generate_headers(src_image, &parameters, &options);
  if (genHeadersResult.error) {
    return genHeadersResult.error;
  }

  const unciHeaders& headers = *genHeadersResult;

  CodedImageData codedImageData;
  if (headers.uncC) {
    codedImageData.properties.push_back(headers.uncC);
  }
  if (headers.cmpd) {
    codedImageData.properties.push_back(headers.cmpd);
  }


  // --- encode image

  Result<std::vector<uint8_t>> codedBitstreamResult = encode_image_tile(src_image);
  if (codedBitstreamResult.error) {
    return codedBitstreamResult.error;
  }

  codedImageData.bitstream = *codedBitstreamResult;

  return codedImageData;
}


Result<std::shared_ptr<ImageItem_uncompressed>> ImageItem_uncompressed::add_unci_item(HeifContext* ctx,
                                                                                      const heif_unci_image_parameters* parameters,
                                                                                      const struct heif_encoding_options* encoding_options,
                                                                                      const std::shared_ptr<const HeifPixelImage>& prototype)
{
  // Check input parameters

  if (parameters->image_width % parameters->tile_width != 0 ||
      parameters->image_height % parameters->tile_height != 0) {
    return Error{heif_error_Invalid_input,
                 heif_suberror_Invalid_parameter_value,
                 "ISO 23001-17 image size must be an integer multiple of the tile size."};
  }

  // Create 'unci' Item

  auto file = ctx->get_heif_file();

  heif_item_id unci_id = ctx->get_heif_file()->add_new_image(fourcc("unci"));
  auto unci_image = std::make_shared<ImageItem_uncompressed>(ctx, unci_id);
  unci_image->set_resolution(parameters->image_width, parameters->image_height);
  ctx->insert_image_item(unci_id, unci_image);


  // Generate headers

  Result<unciHeaders> genHeadersResult = generate_headers(prototype, parameters, encoding_options);
  if (genHeadersResult.error) {
    return genHeadersResult.error;
  }

  const unciHeaders& headers = *genHeadersResult;

  if (headers.uncC) {
    file->add_property(unci_id, headers.uncC, true);
  }

  if (headers.cmpd) {
    file->add_property(unci_id, headers.cmpd, true);
  }

  // Add `ispe` property

  file->add_ispe_property(unci_id,
                          static_cast<uint32_t>(parameters->image_width),
                          static_cast<uint32_t>(parameters->image_height),
                          true);

  if (parameters->compression != heif_metadata_compression_off) {
    auto icef = std::make_shared<Box_icef>();
    auto cmpC = std::make_shared<Box_cmpC>();
    cmpC->set_compressed_unit_type(heif_cmpC_compressed_unit_type_image_tile);

    if (parameters->compression == heif_metadata_compression_deflate) {
      cmpC->set_compression_type(fourcc("defl"));
    }
    else if (parameters->compression == heif_metadata_compression_zlib) {
      cmpC->set_compression_type(fourcc("zlib"));
    }
#if HAVE_BROTLI
    else if (parameters->compression == heif_metadata_compression_brotli) {
      cmpC->set_compression_type(fourcc("brot"));
    }
#endif
    else {
      assert(false);
    }

    file->add_property(unci_id, cmpC, true);
    file->add_property_without_deduplication(unci_id, icef, true); // icef is empty. A normal add_property() would lead to a wrong deduplication.
  }

  // Create empty image. If we use compression, we append the data piece by piece.

  if (parameters->compression == heif_metadata_compression_off) {
    uint64_t tile_size = headers.uncC->compute_tile_data_size_bytes(parameters->image_width / headers.uncC->get_number_of_tile_columns(),
                                                                    parameters->image_height / headers.uncC->get_number_of_tile_rows());

    std::vector<uint8_t> dummydata;
    dummydata.resize(tile_size);

    uint32_t nTiles = (parameters->image_width / parameters->tile_width) * (parameters->image_height / parameters->tile_height);

    for (uint64_t i = 0; i < nTiles; i++) {
      const int construction_method = 0; // 0=mdat 1=idat
      file->append_iloc_data(unci_id, dummydata, construction_method);
    }
  }

  // Set Brands
  ctx->get_heif_file()->set_brand(heif_compression_uncompressed, unci_image->is_miaf_compatible());

  return {unci_image};
}


Error ImageItem_uncompressed::add_image_tile(uint32_t tile_x, uint32_t tile_y, const std::shared_ptr<const HeifPixelImage>& image)
{
  std::shared_ptr<Box_uncC> uncC = get_file()->get_property<Box_uncC>(get_id());
  assert(uncC);

  uint32_t tile_width = image->get_width();
  uint32_t tile_height = image->get_height();

  uint32_t tile_idx = tile_y * uncC->get_number_of_tile_columns() + tile_x;

  Result<std::vector<uint8_t>> codedBitstreamResult = encode_image_tile(image);
  if (codedBitstreamResult.error) {
    return codedBitstreamResult.error;
  }

  std::shared_ptr<Box_cmpC> cmpC = get_file()->get_property<Box_cmpC>(get_id());
  std::shared_ptr<Box_icef> icef = get_file()->get_property<Box_icef>(get_id());

  if (!icef || !cmpC) {
    assert(!icef);
    assert(!cmpC);

    // uncompressed

    uint64_t tile_data_size = uncC->compute_tile_data_size_bytes(tile_width, tile_height);

    get_file()->replace_iloc_data(get_id(), tile_idx * tile_data_size, *codedBitstreamResult, 0);
  }
  else {
    std::vector<uint8_t> compressed_data;
    const std::vector<uint8_t>& raw_data = codedBitstreamResult.value;

    uint32_t compr = cmpC->get_compression_type();
    switch (compr) {
      case fourcc("defl"):
        compressed_data = compress_deflate(raw_data.data(), raw_data.size());
        break;
      case fourcc("zlib"):
        compressed_data = compress_zlib(raw_data.data(), raw_data.size());
        break;
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
  auto ispe = get_file()->get_property<Box_ispe>(get_id());
  auto uncC = get_file()->get_property<Box_uncC>(get_id());

  if (!ispe || !uncC) {
    w=h=0;
  }

  w = ispe->get_width() / uncC->get_number_of_tile_columns();
  h = ispe->get_height() / uncC->get_number_of_tile_rows();
}


heif_image_tiling ImageItem_uncompressed::get_heif_image_tiling() const
{
  heif_image_tiling tiling{};

  auto ispe = get_file()->get_property<Box_ispe>(get_id());
  auto uncC = get_file()->get_property<Box_uncC>(get_id());
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

std::shared_ptr<Decoder> ImageItem_uncompressed::get_decoder() const
{
  return m_decoder;
}

Error ImageItem_uncompressed::on_load_file()
{
  std::shared_ptr<Box_cmpd> cmpd = get_file()->get_property<Box_cmpd>(get_id());
  std::shared_ptr<Box_uncC> uncC = get_file()->get_property<Box_uncC>(get_id());

  if (!uncC) {
    return Error{heif_error_Invalid_input,
                 heif_suberror_Unspecified,
                 "No 'uncC' box found."};
  }

  m_decoder = std::make_shared<Decoder_uncompressed>(uncC, cmpd);

  DataExtent extent;
  extent.set_from_image_item(get_context()->get_heif_file(), get_id());

  m_decoder->set_data_extent(extent);

  return Error::Ok;
}
