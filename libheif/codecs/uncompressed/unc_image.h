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


#ifndef LIBHEIF_UNC_IMAGE_H
#define LIBHEIF_UNC_IMAGE_H

#include "pixelimage.h"
#include "file.h"
#include "context.h"

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

class HeifContext;


bool isKnownUncompressedFrameConfigurationBoxProfile(const std::shared_ptr<const Box_uncC>& uncC);


class UncompressedImageCodec
{
public:
  static int get_luma_bits_per_pixel_from_configuration_unci(const HeifFile& heif_file, heif_item_id imageID);
  static int get_chroma_bits_per_pixel_from_configuration_unci(const HeifFile& heif_file, heif_item_id imageID);

  static Error decode_uncompressed_image(const HeifContext* context,
                                         heif_item_id ID,
                                         std::shared_ptr<HeifPixelImage>& img);

  static Error decode_uncompressed_image_tile(const HeifContext* context,
                                              heif_item_id ID,
                                              std::shared_ptr<HeifPixelImage>& img,
                                              uint32_t tile_x0, uint32_t tile_y0);

  static Error get_heif_chroma_uncompressed(const std::shared_ptr<const Box_uncC>& uncC,
                                            const std::shared_ptr<const Box_cmpd>& cmpd,
                                            heif_chroma* out_chroma,
                                            heif_colorspace* out_colourspace);

  static Result<std::shared_ptr<HeifPixelImage>> create_image(std::shared_ptr<const Box_cmpd>,
                                                              std::shared_ptr<const Box_uncC>,
                                                              uint32_t width,
                                                              uint32_t height);

  static Error check_header_validity(const std::shared_ptr<const Box_ispe>&,
                                     const std::shared_ptr<const Box_cmpd>&,
                                     const std::shared_ptr<const Box_uncC>&);
};


class ImageItem_uncompressed : public ImageItem
{
public:
  ImageItem_uncompressed(HeifContext* ctx, heif_item_id id) : ImageItem(ctx, id) {}

  ImageItem_uncompressed(HeifContext* ctx) : ImageItem(ctx) {}

  uint32_t get_infe_type() const override { return fourcc("unci"); }

  heif_compression_format get_compression_format() const override { return heif_compression_uncompressed; }

  // Instead of storing alpha in a separate unci, this is put into the main image item
  const char* get_auxC_alpha_channel_type() const override { return nullptr; }

  const heif_color_profile_nclx* get_forced_output_nclx() const override { return nullptr; }

  bool is_ispe_essential() const override { return true; }

  void get_tile_size(uint32_t& w, uint32_t& h) const override;

  // Code from encode_uncompressed_image() has been moved to here.

  Result<std::shared_ptr<HeifPixelImage>> decode_compressed_image(const struct heif_decoding_options& options,
                                                                  bool decode_tile_only, uint32_t tile_x0, uint32_t tile_y0) const override;

  heif_image_tiling get_heif_image_tiling() const;

  Error on_load_file() override;

public:

  // --- encoding

  Result<CodedImageData> encode(const std::shared_ptr<HeifPixelImage>& image,
                                struct heif_encoder* encoder,
                                const struct heif_encoding_options& options,
                                enum heif_image_input_class input_class) override;

  static Result<std::shared_ptr<ImageItem_uncompressed>> add_unci_item(HeifContext* ctx,
                                                                const heif_unci_image_parameters* parameters,
                                                                const struct heif_encoding_options* encoding_options,
                                                                const std::shared_ptr<const HeifPixelImage>& prototype);

  Error add_image_tile(uint32_t tile_x, uint32_t tile_y, const std::shared_ptr<const HeifPixelImage>& image);

protected:
  std::shared_ptr<struct Decoder> get_decoder() const override;

private:
  std::shared_ptr<class Decoder_uncompressed> m_decoder;
  /*
  Result<ImageItem::CodedImageData> generate_headers(const std::shared_ptr<const HeifPixelImage>& src_image,
                                                     const heif_unci_image_parameters* parameters,
                                                     const struct heif_encoding_options* options);
                                                     */

  uint64_t m_next_tile_write_pos = 0;
};

#endif //LIBHEIF_UNC_IMAGE_H
