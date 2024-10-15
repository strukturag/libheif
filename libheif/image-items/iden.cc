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

#include "iden.h"
#include "context.h"
#include "file.h"


ImageItem_iden::ImageItem_iden(HeifContext* ctx)
    : ImageItem(ctx)
{
}


ImageItem_iden::ImageItem_iden(HeifContext* ctx, heif_item_id id)
    : ImageItem(ctx, id)
{
}


Result<std::shared_ptr<HeifPixelImage>> ImageItem_iden::decode_compressed_image(const struct heif_decoding_options& options,
                                                                                bool decode_tile_only, uint32_t tile_x0, uint32_t tile_y0) const
{
  std::shared_ptr<HeifPixelImage> img;

  // find the ID of the image this image is derived from

  auto iref_box = get_file()->get_iref_box();

  if (!iref_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_iref_box,
                 "No iref box available, but needed for iden image");
  }

  std::vector<heif_item_id> image_references = iref_box->get_references(get_id(), fourcc("dimg"));

  if ((int) image_references.size() != 1) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Unspecified,
                 "'iden' image with more than one reference image");
  }


  heif_item_id reference_image_id = image_references[0];

  if (reference_image_id == get_id()) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Unspecified,
                 "'iden' image referring to itself");
  }

  std::shared_ptr<const ImageItem> imgitem = get_context()->get_image(reference_image_id, true);
  if (!imgitem) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Unspecified,
                 "'iden' image references unavailable image");
  }
  if (auto error = imgitem->get_item_error()) {
    return error;
  }

  return imgitem->decode_compressed_image(options, decode_tile_only, tile_x0, tile_y0);
}


int ImageItem_iden::get_luma_bits_per_pixel() const
{
  heif_item_id child;
  Error err = get_context()->get_id_of_non_virtual_child_image(get_id(), child);
  if (err) {
    return -1;
  }

  auto image = get_context()->get_image(child, true);
  return image->get_luma_bits_per_pixel();
}


int ImageItem_iden::get_chroma_bits_per_pixel() const
{
  heif_item_id child;
  Error err = get_context()->get_id_of_non_virtual_child_image(get_id(), child);
  if (err) {
    return -1;
  }

  auto image = get_context()->get_image(child, true);
  return image->get_chroma_bits_per_pixel();
}
