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


#ifndef LIBHEIF_UNCOMPRESSED_IMAGE_H
#define LIBHEIF_UNCOMPRESSED_IMAGE_H

#include "pixelimage.h"
#include "file.h"
#include "context.h"

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

class HeifContext;

class UncompressedImageCodec
{
public:
  static int get_luma_bits_per_pixel_from_configuration_unci(const HeifFile& heif_file, heif_item_id imageID);
  static int get_chroma_bits_per_pixel_from_configuration_unci(const HeifFile& heif_file, heif_item_id imageID);

  static Error decode_uncompressed_image(const HeifContext* context,
                                         heif_item_id ID,
                                         std::shared_ptr<HeifPixelImage>& img,
                                         const std::vector<uint8_t>& uncompressed_data);

  static Error get_heif_chroma_uncompressed(std::shared_ptr<Box_uncC>& uncC,
                                            std::shared_ptr<Box_cmpd>& cmpd,
                                            heif_chroma* out_chroma,
                                            heif_colorspace* out_colourspace);

};


class ImageItem_uncompressed : public ImageItem
{
public:
  ImageItem_uncompressed(HeifContext* ctx, heif_item_id id) : ImageItem(ctx, id) {}

  ImageItem_uncompressed(HeifContext* ctx) : ImageItem(ctx) {}

  const char* get_infe_type() const override { return "unci"; }

  heif_compression_format get_compression_format() const override { return heif_compression_uncompressed; }

  // Instead of storing alpha in a separate unci, this is put into the main image item
  const char* get_auxC_alpha_channel_type() const override { return nullptr; }

  const heif_color_profile_nclx* get_forced_output_nclx() const override { return nullptr; }

  bool is_ispe_essential() const override { return true; }

  int get_luma_bits_per_pixel() const override;

  int get_chroma_bits_per_pixel() const override;

  // Code from encode_uncompressed_image() has been moved to here.

  Result<std::shared_ptr<HeifPixelImage>> decode_compressed_image(const struct heif_decoding_options& options,
                                                                  bool decode_tile_only, uint32_t tile_x0, uint32_t tile_y0) const override;

  Result<CodedImageData> encode(const std::shared_ptr<HeifPixelImage>& image,
                                struct heif_encoder* encoder,
                                const struct heif_encoding_options& options,
                                enum heif_image_input_class input_class) override;
};

#endif //LIBHEIF_UNCOMPRESSED_IMAGE_H
