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

#ifndef HEIF_DECODER_H
#define HEIF_DECODER_H

#include "libheif/heif.h"
#include "box.h"
#include "error.h"
#include "file.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "image-items/hevc.h"


// Specifies the input data for decoding.
// For images, this points to the iloc extents.
// For sequences, this points to the track data.
struct DataExtent
{
  std::shared_ptr<HeifFile> m_file;
  enum class Source : uint8_t { Raw, Image, FileRange } m_source = Source::Raw;

  // --- raw data
  mutable std::vector<uint8_t> m_raw; // also for cached data

  // --- image
  heif_item_id m_item_id = 0;

  // --- file range
  uint64_t m_offset = 0;
  uint32_t m_size = 0;

  void set_from_image_item(std::shared_ptr<HeifFile> file, heif_item_id item);

  void set_file_range(std::shared_ptr<HeifFile> file, uint64_t offset, uint32_t size);

  Result<std::vector<uint8_t>*> read_data() const;

  Result<std::vector<uint8_t>> read_data(uint64_t offset, uint64_t size) const;
};


class Decoder
{
public:
  static std::shared_ptr<Decoder> alloc_for_infe_type(const ImageItem* item);

  static std::shared_ptr<Decoder> alloc_for_sequence_sample_description_box(std::shared_ptr<const class Box_VisualSampleEntry> sample_description_box);


  virtual ~Decoder() = default;

  virtual heif_compression_format get_compression_format() const = 0;

  void set_data_extent(DataExtent extent) { m_data_extent = std::move(extent); }

  const DataExtent& get_data_extent() const { return m_data_extent; }

  // --- information about the image format

  [[nodiscard]] virtual int get_luma_bits_per_pixel() const = 0;

  [[nodiscard]] virtual int get_chroma_bits_per_pixel() const = 0;

  [[nodiscard]] virtual Error get_coded_image_colorspace(heif_colorspace*, heif_chroma*) const = 0;

  // --- raw data access

  [[nodiscard]] virtual Result<std::vector<uint8_t>> read_bitstream_configuration_data() const = 0;

  Result<std::vector<uint8_t>> get_compressed_data() const;

  // --- decoding

  virtual Result<std::shared_ptr<HeifPixelImage>>
  decode_single_frame_from_compressed_data(const struct heif_decoding_options& options,
                                           const struct heif_security_limits* limits);

private:
  DataExtent m_data_extent;
};

#endif
