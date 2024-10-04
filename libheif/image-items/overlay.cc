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

#include "overlay.h"
#include "context.h"
#include "file.h"
#include "color-conversion/colorconversion.h"


template<typename I>
void writevec(uint8_t* data, size_t& idx, I value, int len)
{
  for (int i = 0; i < len; i++) {
    data[idx + i] = static_cast<uint8_t>((value >> (len - 1 - i) * 8) & 0xFF);
  }

  idx += len;
}


static int32_t readvec_signed(const std::vector<uint8_t>& data, int& ptr, int len)
{
  const uint32_t high_bit = 0x80 << ((len - 1) * 8);

  uint32_t val = 0;
  while (len--) {
    val <<= 8;
    val |= data[ptr++];
  }

  bool negative = (val & high_bit) != 0;
  val &= ~high_bit;

  if (negative) {
    return -(high_bit - val);
  }
  else {
    return val;
  }

  return val;
}


static uint32_t readvec(const std::vector<uint8_t>& data, int& ptr, int len)
{
  uint32_t val = 0;
  while (len--) {
    val <<= 8;
    val |= data[ptr++];
  }

  return val;
}


Error ImageOverlay::parse(size_t num_images, const std::vector<uint8_t>& data)
{
  Error eofError(heif_error_Invalid_input,
                 heif_suberror_Invalid_overlay_data,
                 "Overlay image data incomplete");

  if (data.size() < 2 + 4 * 2) {
    return eofError;
  }

  m_version = data[0];
  if (m_version != 0) {
    std::stringstream sstr;
    sstr << "Overlay image data version " << ((int) m_version) << " is not implemented yet";

    return {heif_error_Unsupported_feature,
            heif_suberror_Unsupported_data_version,
            sstr.str()};
  }

  m_flags = data[1];

  int field_len = ((m_flags & 1) ? 4 : 2);
  int ptr = 2;

  if (ptr + 4 * 2 + 2 * field_len + num_images * 2 * field_len > data.size()) {
    return eofError;
  }

  for (int i = 0; i < 4; i++) {
    uint16_t color = static_cast<uint16_t>(readvec(data, ptr, 2));
    m_background_color[i] = color;
  }

  m_width = readvec(data, ptr, field_len);
  m_height = readvec(data, ptr, field_len);

  if (m_width == 0 || m_height == 0) {
    return {heif_error_Invalid_input,
            heif_suberror_Invalid_overlay_data,
            "Overlay image with zero width or height."};
  }

  m_offsets.resize(num_images);

  for (size_t i = 0; i < num_images; i++) {
    m_offsets[i].x = readvec_signed(data, ptr, field_len);
    m_offsets[i].y = readvec_signed(data, ptr, field_len);
  }

  return Error::Ok;
}


std::vector<uint8_t> ImageOverlay::write() const
{
  assert(m_version == 0);

  bool longFields = (m_width > 0xFFFF) || (m_height > 0xFFFF);
  for (const auto& img : m_offsets) {
    if (img.x > 0x7FFF || img.y > 0x7FFF || img.x < -32768 || img.y < -32768) {
      longFields = true;
      break;
    }
  }

  std::vector<uint8_t> data;

  data.resize(2 + 4 * 2 + (longFields ? 4 : 2) * (2 + m_offsets.size() * 2));

  size_t idx = 0;
  data[idx++] = m_version;
  data[idx++] = (longFields ? 1 : 0); // flags

  for (uint16_t color : m_background_color) {
    writevec(data.data(), idx, color, 2);
  }

  writevec(data.data(), idx, m_width, longFields ? 4 : 2);
  writevec(data.data(), idx, m_height, longFields ? 4 : 2);

  for (const auto& img : m_offsets) {
    writevec(data.data(), idx, img.x, longFields ? 4 : 2);
    writevec(data.data(), idx, img.y, longFields ? 4 : 2);
  }

  assert(idx == data.size());

  return data;
}


std::string ImageOverlay::dump() const
{
  std::stringstream sstr;

  sstr << "version: " << ((int) m_version) << "\n"
       << "flags: " << ((int) m_flags) << "\n"
       << "background color: " << m_background_color[0]
       << ";" << m_background_color[1]
       << ";" << m_background_color[2]
       << ";" << m_background_color[3] << "\n"
       << "canvas size: " << m_width << "x" << m_height << "\n"
       << "offsets: ";

  for (const ImageWithOffset& offset : m_offsets) {
    sstr << offset.x << ";" << offset.y << " ";
  }
  sstr << "\n";

  return sstr.str();
}


void ImageOverlay::get_background_color(uint16_t col[4]) const
{
  for (int i = 0; i < 4; i++) {
    col[i] = m_background_color[i];
  }
}


void ImageOverlay::get_offset(size_t image_index, int32_t* x, int32_t* y) const
{
  assert(image_index < m_offsets.size());
  assert(x && y);

  *x = m_offsets[image_index].x;
  *y = m_offsets[image_index].y;
}



ImageItem_Overlay::ImageItem_Overlay(HeifContext* ctx)
    : ImageItem(ctx)
{
}


ImageItem_Overlay::ImageItem_Overlay(HeifContext* ctx, heif_item_id id)
    : ImageItem(ctx, id)
{
}


Error ImageItem_Overlay::on_load_file()
{
  Error err = read_overlay_spec();
  if (err) {
    return err;
  }

  return Error::Ok;
}


Error ImageItem_Overlay::read_overlay_spec()
{
  auto heif_file = get_context()->get_heif_file();

  auto iref_box = heif_file->get_iref_box();

  if (!iref_box) {
    return {heif_error_Invalid_input,
            heif_suberror_No_iref_box,
            "No iref box available, but needed for iovl image"};
  }


  m_overlay_image_ids = iref_box->get_references(get_id(), fourcc("dimg"));

  /* TODO: probably, it is valid that an iovl image has no references ?

  if (image_references.empty()) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Missing_grid_images,
                 "'iovl' image with more than one reference image");
  }
  */


  std::vector<uint8_t> overlay_data;
  Error err = heif_file->get_uncompressed_item_data(get_id(), &overlay_data);
  if (err) {
    return err;
  }

  err = m_overlay_spec.parse(m_overlay_image_ids.size(), overlay_data);
  if (err) {
    return err;
  }

  if (m_overlay_image_ids.size() != m_overlay_spec.get_num_offsets()) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Invalid_overlay_data,
                 "Number of image offsets does not match the number of image references");
  }

  return Error::Ok;
}


Result<std::shared_ptr<HeifPixelImage>> ImageItem_Overlay::decode_compressed_image(const struct heif_decoding_options& options,
                                                                                   bool decode_tile_only, uint32_t tile_x0, uint32_t tile_y0) const
{
  return decode_overlay_image(options);
}


Result<std::shared_ptr<HeifPixelImage>> ImageItem_Overlay::decode_overlay_image(const heif_decoding_options& options) const
{
  std::shared_ptr<HeifPixelImage> img;

  uint32_t w = m_overlay_spec.get_canvas_width();
  uint32_t h = m_overlay_spec.get_canvas_height();

  Error err = check_resolution(w, h);
  if (err) {
    return err;
  }

  // TODO: seems we always have to compose this in RGB since the background color is an RGB value
  img = std::make_shared<HeifPixelImage>();
  img->create(w, h,
              heif_colorspace_RGB,
              heif_chroma_444);
  img->add_plane(heif_channel_R, w, h, 8); // TODO: other bit depths
  img->add_plane(heif_channel_G, w, h, 8); // TODO: other bit depths
  img->add_plane(heif_channel_B, w, h, 8); // TODO: other bit depths

  uint16_t bkg_color[4];
  m_overlay_spec.get_background_color(bkg_color);

  err = img->fill_RGB_16bit(bkg_color[0], bkg_color[1], bkg_color[2], bkg_color[3]);
  if (err) {
    return err;
  }

  for (size_t i = 0; i < m_overlay_image_ids.size(); i++) {

    // detect if 'iovl' is referencing itself

    if (m_overlay_image_ids[i] == get_id()) {
      return Error{heif_error_Invalid_input,
                   heif_suberror_Unspecified,
                   "Self-reference in 'iovl' image item."};
    }

    auto imgItem = get_context()->get_image(m_overlay_image_ids[i]);
    if (!imgItem) {
      return Error(heif_error_Invalid_input, heif_suberror_Nonexisting_item_referenced, "'iovl' image references a non-existing item.");
    }

    auto decodeResult = imgItem->decode_image(options, false, 0,0);
    if (decodeResult.error) {
      return decodeResult.error;
    }

    std::shared_ptr<HeifPixelImage> overlay_img = decodeResult.value;


    // process overlay in RGB space

    if (overlay_img->get_colorspace() != heif_colorspace_RGB ||
        overlay_img->get_chroma_format() != heif_chroma_444) {
      overlay_img = convert_colorspace(overlay_img, heif_colorspace_RGB, heif_chroma_444, nullptr, 0, options.color_conversion_options);
      if (!overlay_img) {
        return Error(heif_error_Unsupported_feature, heif_suberror_Unsupported_color_conversion);
      }
    }

    int32_t dx, dy;
    m_overlay_spec.get_offset(i, &dx, &dy);

    err = img->overlay(overlay_img, dx, dy);
    if (err) {
      if (err.error_code == heif_error_Invalid_input &&
          err.sub_error_code == heif_suberror_Overlay_image_outside_of_canvas) {
        // NOP, ignore this error
      }
      else {
        return err;
      }
    }
  }

  return img;
}


int ImageItem_Overlay::get_luma_bits_per_pixel() const
{
  heif_item_id child;
  Error err = get_context()->get_id_of_non_virtual_child_image(get_id(), child);
  if (err) {
    return -1;
  }

  auto image = get_context()->get_image(child);
  return image->get_luma_bits_per_pixel();
}


int ImageItem_Overlay::get_chroma_bits_per_pixel() const
{
  heif_item_id child;
  Error err = get_context()->get_id_of_non_virtual_child_image(get_id(), child);
  if (err) {
    return -1;
  }

  auto image = get_context()->get_image(child);
  return image->get_chroma_bits_per_pixel();
}
