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
#include "plugin_registry.h"
#include "libheif/api_structs.h"

#include "codecs/hevc_dec.h"
#include "codecs/avif_dec.h"
#include "codecs/avc_dec.h"
#include "codecs/vvc_dec.h"
#include "codecs/jpeg_dec.h"
#include "codecs/jpeg2000_dec.h"
#include "avc_boxes.h"
#include "avif_boxes.h"
#include "hevc_boxes.h"
#include "vvc_boxes.h"
#include "jpeg_boxes.h"
#include "jpeg2000_boxes.h"



void DataExtent::set_from_image_item(std::shared_ptr<HeifFile> file, heif_item_id item)
{
  m_file = file;
  m_item_id = item;
  m_source = Source::Image;
}


Result<std::vector<uint8_t>*> DataExtent::read_data() const
{
  if (!m_raw.empty()) {
    return &m_raw;
  }
  else if (m_source == Source::Image) {
    assert(m_file);

    // image
    Error err = m_file->append_data_from_iloc(m_item_id, m_raw);
    if (err) {
      return err;
    }
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
  else if (m_source == Source::Image) {
    // TODO: cache data

    // image
    Error err = m_file->append_data_from_iloc(m_item_id, m_raw, 0, size);
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


std::shared_ptr<Decoder> Decoder::alloc_for_infe_type(const HeifContext* ctx, heif_item_id id, uint32_t format_4cc)
{
  switch (format_4cc) {
    case fourcc("hvc1"): {
      auto hvcC = ctx->get_heif_file()->get_property<Box_hvcC>(id);
      return std::make_shared<Decoder_HEVC>(hvcC);
    }
    case fourcc("av01"): {
      auto av1C = ctx->get_heif_file()->get_property<Box_av1C>(id);
      return std::make_shared<Decoder_AVIF>(av1C);
    }
    case fourcc("avc1"): {
      auto avcC = ctx->get_heif_file()->get_property<Box_avcC>(id);
      return std::make_shared<Decoder_AVC>(avcC);
    }
    case fourcc("j2k1"): {
      auto j2kH = ctx->get_heif_file()->get_property<Box_j2kH>(id);
      return std::make_shared<Decoder_JPEG2000>(j2kH);
    }
    case fourcc("vvc1"): {
      auto vvcC = ctx->get_heif_file()->get_property<Box_vvcC>(id);
      return std::make_shared<Decoder_VVC>(vvcC);
    }
    case fourcc("jpeg"): {
      auto jpgC = ctx->get_heif_file()->get_property<Box_jpgC>(id);
      return std::make_shared<Decoder_JPEG>(jpgC);
    }
#if WITH_UNCOMPRESSED_CODEC
    case fourcc("unci"): {
      auto jpgC = ctx->get_heif_file()->get_property<Box_jpgC>(id);
      return std::make_shared<Decoder_JPEG>(jpgC);
    }
#endif
    case fourcc("mski"): {
      return nullptr; // do we need a decoder for this?
    }
    default:
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

  // append image data

  Result dataResult = m_data_extent.read_data();
  if (dataResult.error) {
    return dataResult.error;
  }

  data.insert(data.end(), dataResult.value->begin(), dataResult.value->end());

  return data;
}


Result<std::shared_ptr<HeifPixelImage>>
Decoder::decode_single_frame_from_compressed_data(const struct heif_decoding_options& options)
{
  const struct heif_decoder_plugin* decoder_plugin = get_decoder(get_compression_format(), options.decoder_id);
  if (!decoder_plugin) {
    return Error(heif_error_Plugin_loading_error, heif_suberror_No_matching_decoder_installed);
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

  auto dataResult = get_compressed_data();
  if (dataResult.error) {
    return dataResult.error;
  }

  err = decoder_plugin->push_data(decoder, dataResult.value.data(), dataResult.value.size());
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
