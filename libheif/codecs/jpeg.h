/*
 * HEIF JPEG codec.
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

#ifndef LIBHEIF_JPEG_H
#define LIBHEIF_JPEG_H

#include "box.h"
#include <string>
#include <vector>
#include <codecs/image_item.h>


class Box_jpgC : public Box
{
public:
  Box_jpgC()
  {
    set_short_type(fourcc("jpgC"));
  }

  const std::vector<uint8_t>& get_data() { return m_data; }

  void set_data(const std::vector<uint8_t>& data) { m_data = data; }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;

private:
  std::vector<uint8_t> m_data;
};


class ImageItem_JPEG : public ImageItem
{
public:
  ImageItem_JPEG(HeifContext* ctx, heif_item_id id) : ImageItem(ctx, id) { }

  ImageItem_JPEG(HeifContext* ctx) : ImageItem(ctx) { }

  const char* get_infe_type() const override { return "jpeg"; }

  const heif_color_profile_nclx* get_forced_output_nclx() const override;


  Result<CodedImageData> encode(const std::shared_ptr<HeifPixelImage>& image,
                                        struct heif_encoder* encoder,
                                        const struct heif_encoding_options& options,
                                        enum heif_image_input_class input_class) override;
};

#endif // LIBHEIF_JPEG_H
