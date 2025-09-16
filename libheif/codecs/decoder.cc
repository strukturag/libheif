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

#include <utility>
#include "error.h"
#include "context.h"
#include "plugin_registry.h"
#include "api_structs.h"

#include "codecs/hevc_dec.h"
#include "codecs/avif_dec.h"
#include "codecs/avc_dec.h"
#include "codecs/vvc_dec.h"
#include "codecs/jpeg_dec.h"
#include "codecs/jpeg2000_dec.h"
#include "codecs/evc_dec.h"
#include "avc_boxes.h"
#include "avif_boxes.h"
#include "hevc_boxes.h"
#include "vvc_boxes.h"
#include "jpeg_boxes.h"
#include "jpeg2000_boxes.h"
#include "evc_boxes.h"

#if WITH_UNCOMPRESSED_CODEC
#include "codecs/uncompressed/unc_dec.h"
#include "codecs/uncompressed/unc_boxes.h"
#endif

void DataExtent::set_from_image_item(std::shared_ptr<HeifFile> file, heif_item_id item)
{
  m_file = std::move(file);
  m_item_id = item;
  m_source = Source::Image;
}


void DataExtent::set_file_range(std::shared_ptr<HeifFile> file, uint64_t offset, uint32_t size)
{
  m_file = std::move(file);
  m_source = Source::FileRange;
  m_offset = offset;
  m_size = size;
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
    // file range
    Error err = m_file->append_data_from_file_range(m_raw, m_offset, m_size);
    if (err) {
      return err;
    }
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
    Error err = m_file->append_data_from_iloc(m_item_id, data, offset, size);
    if (err) {
      return err;
    }
    return data;
  }
  else {
    // file range
    Error err = m_file->append_data_from_file_range(data, m_offset, m_size);
    if (err) {
      return err;
    }

    return data;
  }
}


std::shared_ptr<Decoder> Decoder::alloc_for_infe_type(const ImageItem* item)
{
  uint32_t format_4cc = item->get_infe_type();

  switch (format_4cc) {
    case fourcc("hvc1"): {
      auto hvcC = item->get_property<Box_hvcC>();
      return std::make_shared<Decoder_HEVC>(hvcC);
    }
    case fourcc("av01"): {
      auto av1C = item->get_property<Box_av1C>();
      return std::make_shared<Decoder_AVIF>(av1C);
    }
    case fourcc("avc1"): {
      auto avcC = item->get_property<Box_avcC>();
      return std::make_shared<Decoder_AVC>(avcC);
    }
    case fourcc("j2k1"): {
      auto j2kH = item->get_property<Box_j2kH>();
      return std::make_shared<Decoder_JPEG2000>(j2kH);
    }
    case fourcc("vvc1"): {
      auto vvcC = item->get_property<Box_vvcC>();
      return std::make_shared<Decoder_VVC>(vvcC);
    }
    case fourcc("jpeg"): {
      auto jpgC = item->get_property<Box_jpgC>();
      return std::make_shared<Decoder_JPEG>(jpgC);
    }
#if WITH_UNCOMPRESSED_CODEC
    case fourcc("unci"): {
      auto uncC = item->get_property<Box_uncC>();
      auto cmpd = item->get_property<Box_cmpd>();
      auto ispe = item->get_property<Box_ispe>();
      return std::make_shared<Decoder_uncompressed>(uncC,cmpd,ispe);
    }
#endif
    case fourcc("evc1"): {
      auto evcC = item->get_property<Box_evcC>();
      return std::make_shared<Decoder_EVC>(evcC);
    }
    case fourcc("mski"): {
      return nullptr; // do we need a decoder for this?
    }
    default:
      return nullptr;
  }
}


std::shared_ptr<Decoder> Decoder::alloc_for_sequence_sample_description_box(std::shared_ptr<const Box_VisualSampleEntry> sample_description_box)
{
  std::string compressor = sample_description_box->get_VisualSampleEntry_const().compressorname;
  uint32_t sampleType = sample_description_box->get_short_type();

  switch (sampleType) {
    case fourcc("hvc1"): {
      auto hvcC = sample_description_box->get_child_box<Box_hvcC>();
      return std::make_shared<Decoder_HEVC>(hvcC);
    }

    case fourcc("av01"): {
      auto av1C = sample_description_box->get_child_box<Box_av1C>();
      return std::make_shared<Decoder_AVIF>(av1C);
    }

    case fourcc("vvc1"): {
      auto vvcC = sample_description_box->get_child_box<Box_vvcC>();
      return std::make_shared<Decoder_VVC>(vvcC);
    }

    case fourcc("avc1"): {
      auto avcC = sample_description_box->get_child_box<Box_avcC>();
      return std::make_shared<Decoder_AVC>(avcC);
    }

#if WITH_UNCOMPRESSED_CODEC
    case fourcc("uncv"): {
      auto uncC = sample_description_box->get_child_box<Box_uncC>();
      auto cmpd = sample_description_box->get_child_box<Box_cmpd>();
      auto ispe = std::make_shared<Box_ispe>();
      ispe->set_size(sample_description_box->get_VisualSampleEntry_const().width,
                     sample_description_box->get_VisualSampleEntry_const().height);
      return std::make_shared<Decoder_uncompressed>(uncC, cmpd, ispe);
    }
#endif

    case fourcc("j2ki"): {
      auto j2kH = sample_description_box->get_child_box<Box_j2kH>();
      return std::make_shared<Decoder_JPEG2000>(j2kH);
    }

    case fourcc("mjpg"): {
      auto jpgC = sample_description_box->get_child_box<Box_jpgC>();
      return std::make_shared<Decoder_JPEG>(jpgC);
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
  if (!confData) {
    return confData.error();
  }

  std::vector<uint8_t> data = *confData;

  // append image data

  auto dataResult = m_data_extent.read_data();
  if (!dataResult) {
    return dataResult.error();
  }

  data.insert(data.end(), (*dataResult)->begin(), (*dataResult)->end());

  return data;
}


Decoder::~Decoder()
{
  if (m_decoder) {
    assert(m_decoder_plugin);
    m_decoder_plugin->free_decoder(m_decoder);
  }

  //std::unique_ptr<void, void (*)(void*)> decoderSmartPtr(m_decoder, m_decoder_plugin->free_decoder);
}


Result<std::shared_ptr<HeifPixelImage>>
Decoder::decode_single_frame_from_compressed_data(const heif_decoding_options& options,
                                                  const heif_security_limits* limits)
{
  if (!m_decoder_plugin) {
    m_decoder_plugin = get_decoder(get_compression_format(), options.decoder_id);
    if (!m_decoder_plugin) {
      return Error(heif_error_Plugin_loading_error, heif_suberror_No_matching_decoder_installed);
    }
  }

  // --- decode image with the plugin

  heif_error err;

  if (!m_decoder) {
    if (m_decoder_plugin->new_decoder == nullptr) {
      return Error(heif_error_Plugin_loading_error, heif_suberror_No_matching_decoder_installed,
                   "Cannot decode with a dummy decoder plugin.");
    }

    err = m_decoder_plugin->new_decoder(&m_decoder);
    if (err.code != heif_error_Ok) {
      return Error(err.code, err.subcode, err.message);
    }

    // automatically delete decoder plugin when we leave the scope
    //std::unique_ptr<void, void (*)(void*)> decoderSmartPtr(m_decoder, m_decoder_plugin->free_decoder);

    if (m_decoder_plugin->plugin_api_version >= 2) {
      if (m_decoder_plugin->set_strict_decoding) {
        m_decoder_plugin->set_strict_decoding(m_decoder, options.strict_decoding);
      }
    }
  }

  auto dataResult = get_compressed_data();
  if (!dataResult) {
    return dataResult.error();
  }

  err = m_decoder_plugin->push_data(m_decoder, dataResult->data(), dataResult->size());
  if (err.code != heif_error_Ok) {
    return Error(err.code, err.subcode, err.message);
  }

  heif_image* decoded_img = nullptr;

  if (m_decoder_plugin->plugin_api_version >= 4 &&
      m_decoder_plugin->decode_next_image != nullptr) {

    err = m_decoder_plugin->decode_next_image(m_decoder, &decoded_img, limits);
    if (err.code != heif_error_Ok) {
      return Error::from_heif_error(err);
    }
  }
  else {
    err = m_decoder_plugin->decode_image(m_decoder, &decoded_img);
    if (err.code != heif_error_Ok) {
      return Error::from_heif_error(err);
    }
  }

  if (!decoded_img) {
    // TODO(farindk): The plugin should return an error in this case.
    return Error(heif_error_Decoder_plugin_error, heif_suberror_Unspecified);
  }

  // -- cleanup

  std::shared_ptr<HeifPixelImage> img = std::move(decoded_img->image);
  heif_image_release(decoded_img);

  return img;
}
