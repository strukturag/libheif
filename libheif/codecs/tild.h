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

#ifndef LIBHEIF_TILD_H
#define LIBHEIF_TILD_H


#include <codecs/image_item.h>
#include "box.h"
#include <vector>
#include <string>
#include <memory>


uint64_t number_of_tiles(const heif_tild_image_parameters& params);

uint64_t nTiles_h(const heif_tild_image_parameters& params);

uint64_t nTiles_v(const heif_tild_image_parameters& params);


class Box_tilC : public FullBox
{
  /*
   * Flags:
   * bit 0-1 - number of bits for offsets   (0: 32, 1: 40, 2: 48, 3: 64)
   * bit 2-3 - number of bits for tile size (0:  0, 1: 24; 2: 32, 3: 64)
   * bit 4   - sequential ordering hint
   * bit 5   - use 64 bit dimensions (currently unused because ispe is limited to 32 bit)
   */
public:
  Box_tilC()
  {
    set_short_type(fourcc("tilC"));
  }

  bool is_essential() const override { return true; }

  void derive_box_version() override;

  void set_parameters(const heif_tild_image_parameters& params) { m_parameters = params; }

  const heif_tild_image_parameters& get_parameters() const { return m_parameters; }

  Error write(StreamWriter& writer) const override;

  std::string dump(Indent&) const override;

protected:
  Error parse(BitstreamRange& range) override;

private:
  heif_tild_image_parameters m_parameters;
};


#define TILD_OFFSET_NOT_AVAILABLE 0
#define TILD_OFFSET_SEE_LOWER_RESOLUTION_LAYER 1
#define TILD_OFFSET_NOT_LOADED 10

class TildHeader
{
public:
  void set_parameters(const heif_tild_image_parameters& params);

  const heif_tild_image_parameters& get_parameters() const { return m_parameters; }

  Error read_full_offset_table(const std::shared_ptr<HeifFile>& file, heif_item_id tild_id);

  std::vector<uint8_t> write_offset_table();

  std::string dump() const;

  void set_tild_tile_range(uint32_t tile_x, uint32_t tile_y, uint64_t offset, uint32_t size);

  size_t get_header_size() const;

  uint64_t get_tile_offset(uint32_t idx) const { return m_offsets[idx].offset; }

  uint32_t get_tile_size(uint32_t idx) const { return m_offsets[idx].size; }

private:
  heif_tild_image_parameters m_parameters;

  struct TileOffset {
    uint64_t offset = TILD_OFFSET_NOT_LOADED;
    uint32_t size = 0;
  };

  // TODO uint64_t m_start_of_offset_table_in_file = 0;
  std::vector<TileOffset> m_offsets;

  // TODO size_t m_offset_table_start = 0; // start of offset table (= number of bytes in header)
  size_t m_header_size = 0; // including offset table
};


class ImageItem_Tild : public ImageItem
{
public:
  ImageItem_Tild(HeifContext* ctx, heif_item_id id);

  ImageItem_Tild(HeifContext* ctx);

  const char* get_infe_type() const override { return "tili"; }

  // const heif_color_profile_nclx* get_forced_output_nclx() const override { return nullptr; }

  heif_compression_format get_compression_format() const override;

  static Result<std::shared_ptr<ImageItem_Tild>> add_new_tild_item(HeifContext* ctx, const heif_tild_image_parameters* parameters);

  Error on_load_file() override;

  void process_before_write() override;

  int get_luma_bits_per_pixel() const override { return -1; } // TODO (create dummy ImageItem, then call this function)

  int get_chroma_bits_per_pixel() const override { return -1; } // TODO

  Result<CodedImageData> encode(const std::shared_ptr<HeifPixelImage>& image,
                                struct heif_encoder* encoder,
                                const struct heif_encoding_options& options,
                                enum heif_image_input_class input_class) override {
    return Error{heif_error_Unsupported_feature,
                 heif_suberror_Unspecified, "Cannot encode image to 'tild'"};
  }

  Result<std::shared_ptr<HeifPixelImage>> decode_compressed_image(const struct heif_decoding_options& options,
                                                                  bool decode_tile_only, uint32_t tile_x0, uint32_t tile_y0) const override;


  // --- tild

  void set_tild_header(const TildHeader& header) { m_tild_header = header; }

  TildHeader& get_tild_header() { return m_tild_header; }

  uint64_t get_next_tild_position() const { return m_next_tild_position; }

  void set_next_tild_position(uint64_t pos) { m_next_tild_position = pos; }

  heif_image_tiling get_heif_image_tiling() const;

  void get_tile_size(uint32_t& w, uint32_t& h) const;

private:
  TildHeader m_tild_header;
  uint64_t m_next_tild_position = 0;

  Result<std::shared_ptr<HeifPixelImage>> decode_grid_tile(const heif_decoding_options& options, uint32_t tx, uint32_t ty) const;
};


#endif //LIBHEIF_TILD_H
