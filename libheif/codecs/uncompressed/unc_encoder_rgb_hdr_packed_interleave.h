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

#ifndef LIBHEIF_UNC_ENCODER_RGB10_12_H
#define LIBHEIF_UNC_ENCODER_RGB10_12_H
#include "unc_encoder.h"


class unc_encoder_rgb_hdr_packed_interleave : public unc_encoder
{
public:
  unc_encoder_rgb_hdr_packed_interleave(const std::shared_ptr<const HeifPixelImage>& image,
                                        const heif_encoding_options& options);

  [[nodiscard]] std::vector<uint8_t> encode_tile(const std::shared_ptr<const HeifPixelImage>& image) const override;
};


class unc_encoder_factory_rgb_hdr_packed_interleave : public unc_encoder_factory
{
public:
  [[nodiscard]] bool can_encode(const std::shared_ptr<const HeifPixelImage>& image,
                                const heif_encoding_options& options) const override;

  std::unique_ptr<const unc_encoder> create(const std::shared_ptr<const HeifPixelImage>& image,
                                            const heif_encoding_options& options) const override;
};

#endif //LIBHEIF_UNC_ENCODER_RGB10_12_H
