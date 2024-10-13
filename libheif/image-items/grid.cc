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

#include "grid.h"
#include "context.h"
#include "file.h"
#include <cstring>
#include <deque>
#include <future>
#include <set>
#include <algorithm>


Error ImageGrid::parse(const std::vector<uint8_t>& data)
{
  if (data.size() < 8) {
    return {heif_error_Invalid_input,
            heif_suberror_Invalid_grid_data,
            "Less than 8 bytes of data"};
  }

  uint8_t version = data[0];
  if (version != 0) {
    std::stringstream sstr;
    sstr << "Grid image version " << ((int) version) << " is not supported";
    return {heif_error_Unsupported_feature,
            heif_suberror_Unsupported_data_version,
            sstr.str()};
  }

  uint8_t flags = data[1];
  int field_size = ((flags & 1) ? 32 : 16);

  m_rows = static_cast<uint16_t>(data[2] + 1);
  m_columns = static_cast<uint16_t>(data[3] + 1);

  if (field_size == 32) {
    if (data.size() < 12) {
      return {heif_error_Invalid_input,
              heif_suberror_Invalid_grid_data,
              "Grid image data incomplete"};
    }

    m_output_width = ((data[4] << 24) |
                      (data[5] << 16) |
                      (data[6] << 8) |
                      (data[7]));

    m_output_height = ((data[8] << 24) |
                       (data[9] << 16) |
                       (data[10] << 8) |
                       (data[11]));
  }
  else {
    m_output_width = ((data[4] << 8) |
                      (data[5]));

    m_output_height = ((data[6] << 8) |
                       (data[7]));
  }

  return Error::Ok;
}


std::vector<uint8_t> ImageGrid::write() const
{
  int field_size;

  if (m_output_width > 0xFFFF ||
      m_output_height > 0xFFFF) {
    field_size = 32;
  }
  else {
    field_size = 16;
  }

  std::vector<uint8_t> data(field_size == 16 ? 8 : 12);

  data[0] = 0; // version

  uint8_t flags = 0;
  if (field_size == 32) {
    flags |= 1;
  }

  data[1] = flags;
  data[2] = (uint8_t) (m_rows - 1);
  data[3] = (uint8_t) (m_columns - 1);

  if (field_size == 32) {
    data[4] = (uint8_t) ((m_output_width >> 24) & 0xFF);
    data[5] = (uint8_t) ((m_output_width >> 16) & 0xFF);
    data[6] = (uint8_t) ((m_output_width >> 8) & 0xFF);
    data[7] = (uint8_t) ((m_output_width) & 0xFF);

    data[8] = (uint8_t) ((m_output_height >> 24) & 0xFF);
    data[9] = (uint8_t) ((m_output_height >> 16) & 0xFF);
    data[10] = (uint8_t) ((m_output_height >> 8) & 0xFF);
    data[11] = (uint8_t) ((m_output_height) & 0xFF);
  }
  else {
    data[4] = (uint8_t) ((m_output_width >> 8) & 0xFF);
    data[5] = (uint8_t) ((m_output_width) & 0xFF);

    data[6] = (uint8_t) ((m_output_height >> 8) & 0xFF);
    data[7] = (uint8_t) ((m_output_height) & 0xFF);
  }

  return data;
}


std::string ImageGrid::dump() const
{
  std::ostringstream sstr;

  sstr << "rows: " << m_rows << "\n"
       << "columns: " << m_columns << "\n"
       << "output width: " << m_output_width << "\n"
       << "output height: " << m_output_height << "\n";

  return sstr.str();
}


ImageItem_Grid::ImageItem_Grid(HeifContext* ctx)
    : ImageItem(ctx)
{
}


ImageItem_Grid::ImageItem_Grid(HeifContext* ctx, heif_item_id id)
    : ImageItem(ctx, id)
{
}


Error ImageItem_Grid::on_load_file()
{
  Error err = read_grid_spec();
  if (err) {
    return err;
  }

  return Error::Ok;
}


Error ImageItem_Grid::read_grid_spec()
{
  auto heif_file = get_context()->get_heif_file();

  std::vector<uint8_t> grid_data;
  Error err = heif_file->get_uncompressed_item_data(get_id(), &grid_data);
  if (err) {
    return err;
  }

  err = m_grid_spec.parse(grid_data);
  if (err) {
    return err;
  }

  //std::cout << grid.dump();


  auto iref_box = heif_file->get_iref_box();

  if (!iref_box) {
    return {heif_error_Invalid_input,
            heif_suberror_No_iref_box,
            "No iref box available, but needed for grid image"};
  }

  m_grid_tile_ids = iref_box->get_references(get_id(), fourcc("dimg"));

  if ((int) m_grid_tile_ids.size() != m_grid_spec.get_rows() * m_grid_spec.get_columns()) {
    std::stringstream sstr;
    sstr << "Tiled image with " << m_grid_spec.get_rows() << "x" << m_grid_spec.get_columns() << "="
         << (m_grid_spec.get_rows() * m_grid_spec.get_columns()) << " tiles, but only "
         << m_grid_tile_ids.size() << " tile images in file";

    return {heif_error_Invalid_input,
            heif_suberror_Missing_grid_images,
            sstr.str()};
  }

  return Error::Ok;
}


Result<std::shared_ptr<HeifPixelImage>> ImageItem_Grid::decode_compressed_image(const struct heif_decoding_options& options,
                                                                                bool decode_tile_only, uint32_t tile_x0, uint32_t tile_y0) const
{
  if (decode_tile_only) {
    return decode_grid_tile(options, tile_x0, tile_y0);
  }
  else {
    return decode_full_grid_image(options);
  }
}


Result<std::shared_ptr<HeifPixelImage>> ImageItem_Grid::decode_full_grid_image(const heif_decoding_options& options) const
{
  std::shared_ptr<HeifPixelImage> img; // the decoded image

  const ImageGrid& grid = get_grid_spec();


  // --- check that all image IDs are valid images

  const std::vector<heif_item_id>& image_references = get_grid_tiles();

  for (heif_item_id tile_id : image_references) {
    if (!get_context()->is_image(tile_id)) {
      std::stringstream sstr;
      sstr << "Tile image ID=" << tile_id << " is not a proper image.";

      return Error(heif_error_Invalid_input,
                   heif_suberror_Missing_grid_images,
                   sstr.str());
    }
  }

  //auto pixi = get_file()->get_property<Box_pixi>(get_id());

  const uint32_t w = grid.get_width();
  const uint32_t h = grid.get_height();

  Error err = check_resolution(w, h);
  if (err) {
    return err;
  }

  uint32_t y0 = 0;
  int reference_idx = 0;

#if ENABLE_PARALLEL_TILE_DECODING
  // remember which tile to put where into the image
  struct tile_data
  {
    heif_item_id tileID;
    uint32_t x_origin, y_origin;
  };

  std::deque<tile_data> tiles;
  if (get_context()->get_max_decoding_threads() > 0)
    tiles.resize(grid.get_rows() * grid.get_columns());

  std::deque<std::future<Error> > errs;
#endif

  uint32_t tile_width = 0;
  uint32_t tile_height = 0;

  for (uint32_t y = 0; y < grid.get_rows(); y++) {
    uint32_t x0 = 0;

    for (uint32_t x = 0; x < grid.get_columns(); x++) {

      heif_item_id tileID = image_references[reference_idx];

      std::shared_ptr<const ImageItem> tileImg = get_context()->get_image(tileID, true);
      if (!tileImg) {
        return Error{heif_error_Invalid_input,
                     heif_suberror_Missing_grid_images,
                     "Nonexistent grid image referenced"};
      }
      if (auto error = tileImg->get_item_error()) {
        return error;
      }

      uint32_t src_width = tileImg->get_width();
      uint32_t src_height = tileImg->get_height();
      err = check_resolution(src_width, src_height);
      if (err) {
        return err;
      }

      if (src_width < grid.get_width() / grid.get_columns() ||
          src_height < grid.get_height() / grid.get_rows()) {
        return Error{heif_error_Invalid_input,
                     heif_suberror_Invalid_grid_data,
                     "Grid tiles do not cover whole image"};
      }

      if (x == 0 && y == 0) {
        // remember size of first tile and compare all other tiles against this
        tile_width = src_width;
        tile_height = src_height;
      }
      else if (src_width != tile_width || src_height != tile_height) {
        return Error{heif_error_Invalid_input,
                     heif_suberror_Invalid_grid_data,
                     "Grid tiles have different sizes"};
      }

#if ENABLE_PARALLEL_TILE_DECODING
      if (get_context()->get_max_decoding_threads() > 0)
        tiles[x + y * grid.get_columns()] = tile_data{tileID, x0, y0};
      else
#else
        if (1)
#endif
      {
        err = decode_and_paste_tile_image(tileID, x0, y0, img, options);
        if (err) {
          return err;
        }
      }

      x0 += src_width;

      reference_idx++;
    }

    y0 += tile_height;
  }

#if ENABLE_PARALLEL_TILE_DECODING
  if (get_context()->get_max_decoding_threads() > 0) {
    // Process all tiles in a set of background threads.
    // Do not start more than the maximum number of threads.

    while (!tiles.empty()) {

      // If maximum number of threads running, wait until first thread finishes

      if (errs.size() >= (size_t) get_context()->get_max_decoding_threads()) {
        Error e = errs.front().get();
        if (e) {
          return e;
        }

        errs.pop_front();
      }


      // Start a new decoding thread

      tile_data data = tiles.front();
      tiles.pop_front();

      errs.push_back(std::async(std::launch::async,
                                &ImageItem_Grid::decode_and_paste_tile_image, this,
                                data.tileID, data.x_origin, data.y_origin, std::ref(img), options));
    }

    // check for decoding errors in remaining tiles

    while (!errs.empty()) {
      Error e = errs.front().get();
      if (e) {
        return e;
      }

      errs.pop_front();
    }
  }
#endif

  return img;
}

Error ImageItem_Grid::decode_and_paste_tile_image(heif_item_id tileID, uint32_t x0, uint32_t y0,
                                                  std::shared_ptr<HeifPixelImage>& inout_image,
                                                  const heif_decoding_options& options) const
{
  std::shared_ptr<HeifPixelImage> tile_img;

  auto tileItem = get_context()->get_image(tileID, true);
  assert(tileItem);
  if (auto error = tileItem->get_item_error()) {
    return error;
  }

  auto decodeResult = tileItem->decode_image(options, false, 0, 0);
  if (decodeResult.error) {
    return decodeResult.error;
  }

  tile_img = decodeResult.value;

  uint32_t w = get_grid_spec().get_width();
  uint32_t h = get_grid_spec().get_height();

  // --- generate the image canvas for combining all the tiles

  if (!inout_image) { // this if avoids that we normally have to lock a mutex
    static std::mutex createImageMutex;
    std::lock_guard<std::mutex> lock(createImageMutex);

    if (!inout_image) {
      inout_image = std::make_shared<HeifPixelImage>();
      inout_image->create_clone_image_at_new_size(tile_img, w, h);

      // Fill alpha plane with opaque in case not all tiles have alpha planes

      if (inout_image->has_channel(heif_channel_Alpha)) {
        uint16_t alpha_bpp = inout_image->get_bits_per_pixel(heif_channel_Alpha);
        assert(alpha_bpp <= 16);

        auto alpha_default_value = static_cast<uint16_t>((1UL << alpha_bpp) - 1UL);
        inout_image->fill_plane(heif_channel_Alpha, alpha_default_value);
      }
    }
  }

  // --- copy tile into output image

  heif_chroma chroma = inout_image->get_chroma_format();

  if (chroma != tile_img->get_chroma_format()) {
    return {heif_error_Invalid_input,
            heif_suberror_Wrong_tile_image_chroma_format,
            "Image tile has different chroma format than combined image"};
  }


  inout_image->copy_image_to(tile_img, x0, y0);

  return Error::Ok;
}


Result<std::shared_ptr<HeifPixelImage>> ImageItem_Grid::decode_grid_tile(const heif_decoding_options& options, uint32_t tx, uint32_t ty) const
{
  uint32_t idx = ty * m_grid_spec.get_columns() + tx;

  assert(idx < m_grid_tile_ids.size());

  heif_item_id tile_id = m_grid_tile_ids[idx];
  std::shared_ptr<const ImageItem> tile_item = get_context()->get_image(tile_id, true);
  if (auto error = tile_item->get_item_error()) {
    return error;
  }

  return tile_item->decode_compressed_image(options, true, tx, ty);
}


heif_image_tiling ImageItem_Grid::get_heif_image_tiling() const
{
  heif_image_tiling tiling{};

  const ImageGrid& gridspec = get_grid_spec();
  tiling.num_columns = gridspec.get_columns();
  tiling.num_rows = gridspec.get_rows();

  auto tile_ids = get_grid_tiles();
  if (!tile_ids.empty() && tile_ids[0] != 0) {
    heif_item_id tile0_id = tile_ids[0];
    auto tile0 = get_context()->get_image(tile0_id);
    tiling.tile_width = tile0->get_width();
    tiling.tile_height = tile0->get_height();
  }
  else {
    tiling.tile_width = 0;
    tiling.tile_height = 0;
  }

  tiling.image_width = gridspec.get_width();
  tiling.image_height = gridspec.get_height();
  tiling.number_of_extra_dimensions = 0;

  heif_item_id tile0_id = get_grid_tiles()[0];
  auto tile0 = get_context()->get_image(tile0_id, true);
  if (tile0->get_item_error()) {
    return tiling;
  }

  tiling.tile_width = tile0->get_width();
  tiling.tile_height = tile0->get_height();

  return tiling;
}


void ImageItem_Grid::get_tile_size(uint32_t& w, uint32_t& h) const
{
  heif_item_id first_tile_id = get_grid_tiles()[0];
  auto tile = get_context()->get_image(first_tile_id, true);
  if (tile->get_item_error()) {
    w = h = 0;
  }

  w = tile->get_width();
  h = tile->get_height();
}



int ImageItem_Grid::get_luma_bits_per_pixel() const
{
  heif_item_id child;
  Error err = get_context()->get_id_of_non_virtual_child_image(get_id(), child);
  if (err) {
    return -1;
  }

  auto image = get_context()->get_image(child, true);
  if (!image) {
    return -1;
  }

  return image->get_luma_bits_per_pixel();
}


int ImageItem_Grid::get_chroma_bits_per_pixel() const
{
  heif_item_id child;
  Error err = get_context()->get_id_of_non_virtual_child_image(get_id(), child);
  if (err) {
    return -1;
  }

  auto image = get_context()->get_image(child, true);
  return image->get_chroma_bits_per_pixel();
}

std::shared_ptr<Decoder> ImageItem_Grid::get_decoder() const
{
  heif_item_id child;
  Error err = get_context()->get_id_of_non_virtual_child_image(get_id(), child);
  if (err) {
    return nullptr;
  }

  auto image = get_context()->get_image(child, true);
  if (image->get_item_error()) {
    return nullptr;
  }

  return image->get_decoder();
}
