/*
 * HEIF mask image codec.
 *
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
 * Copyright (c) 2023 Brad Hards <bradh@frogmouth.net>
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


#ifndef LIBHEIF_MASK_IMAGE_H
#define LIBHEIF_MASK_IMAGE_H

#include "box.h"
#include "bitstream.h"
#include "pixelimage.h"
#include "file.h"
#include "context.h"

#include <memory>
#include <string>
#include <vector>

/**
  * Mask Configuration Property (mskC).
  *
  * Each mask image item (mski) shall have an associated MaskConfigurationProperty
  * that provides information required to generate the mask of the associated mask
  * item.
  */
class Box_mskC : public FullBox
{
public:

  Box_mskC()
  {
    set_short_type(fourcc("mskC"));
  }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

  uint8_t get_bits_per_pixel() const
  { return m_bits_per_pixel; }

  void set_bits_per_pixel(uint8_t bits_per_pixel)
  { m_bits_per_pixel = bits_per_pixel; }

protected:
  Error parse(BitstreamRange& range) override;

private:
  uint8_t m_bits_per_pixel;
};

class MaskImageCodec
{
public:
  static Error decode_mask_image(const std::shared_ptr<const HeifFile>& heif_file,
                                  heif_item_id ID,
                                  std::shared_ptr<HeifPixelImage>& img,
                                  uint32_t maximum_image_width_limit,
                                  uint32_t maximum_image_height_limit,
                                  const std::vector<uint8_t>& data);
  static Error encode_mask_image(const std::shared_ptr<HeifFile>& heif_file,
                                 const std::shared_ptr<HeifPixelImage>& src_image,
                                 void* encoder_struct,
                                 const struct heif_encoding_options& options,
                                 std::shared_ptr<HeifContext::Image>& out_image);
};

#endif //LIBHEIF_MASK_IMAGE_H

