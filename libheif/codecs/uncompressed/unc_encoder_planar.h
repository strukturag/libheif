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

#ifndef LIBHEIF_UNC_ENCODER_PLANAR_H
#define LIBHEIF_UNC_ENCODER_PLANAR_H

#include "unc_encoder.h"


class unc_encoder_planar : public unc_encoder
{
public:
  [[nodiscard]] bool can_encode(const std::shared_ptr<const HeifPixelImage>& image,
                                const heif_encoding_options& options) const override;

  void fill_cmpd_and_uncC(std::shared_ptr<Box_cmpd>& out_cmpd,
                          std::shared_ptr<Box_uncC>& out_uncC,
                          const std::shared_ptr<const HeifPixelImage>& image,
                          const heif_encoding_options& options) const override;

  [[nodiscard]] std::vector<uint8_t> encode_tile(const std::shared_ptr<const HeifPixelImage>& image,
                                                 const heif_encoding_options& options) const override;
};


#endif //LIBHEIF_UNC_ENCODER_PLANAR_H