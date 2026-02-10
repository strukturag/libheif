/*
 * ImageMeter confidential
 *
 * Copyright (C) 2026 by Dirk Farin, Kronenstr. 49b, 70174 Stuttgart, Germany
 * All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains the property
 * of Dirk Farin.  The intellectual and technical concepts contained
 * herein are proprietary to Dirk Farin and are protected by trade secret
 * and copyright law.
 * Dissemination of this information or reproduction of this material
 * is strictly forbidden unless prior written permission is obtained
 * from Dirk Farin.
 */

#ifndef LIBHEIF_UNC_ENCODER_BYTEALIGN_COMPONENT_INTERLEAVE_H
#define LIBHEIF_UNC_ENCODER_BYTEALIGN_COMPONENT_INTERLEAVE_H

#include "unc_encoder.h"
#include "unc_types.h"

class unc_encoder_bytealign_component_interleave : public unc_encoder
{
public:
  unc_encoder_bytealign_component_interleave(const std::shared_ptr<const HeifPixelImage>& image,
                     const heif_encoding_options& options);

  uint64_t compute_tile_data_size_bytes(uint32_t tile_width, uint32_t tile_height) const override;

  [[nodiscard]] std::vector<uint8_t> encode_tile(const std::shared_ptr<const HeifPixelImage>& image) const override;

private:
  struct channel_component
  {
    heif_channel channel;
    heif_uncompressed_component_type component_type;
  };

  std::vector<channel_component> m_components;
  uint32_t m_bytes_per_pixel_x4;

  void add_channel_if_exists(const std::shared_ptr<const HeifPixelImage>& image, heif_channel channel);
};


class unc_encoder_factory_bytealign_component_interleave : public unc_encoder_factory
{
public:

private:
  [[nodiscard]] bool can_encode(const std::shared_ptr<const HeifPixelImage>& image,
                                const heif_encoding_options& options) const override;

  std::unique_ptr<const unc_encoder> create(const std::shared_ptr<const HeifPixelImage>& image,
                                            const heif_encoding_options& options) const override;
};

#endif //LIBHEIF_UNC_ENCODER_BYTEALIGN_COMPONENT_INTERLEAVE_H
