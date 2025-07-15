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
#include "api_structs.h"
#include "security_limits.h"


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
  m_encoding_options = heif_encoding_options_alloc();
}


ImageItem_Grid::ImageItem_Grid(HeifContext* ctx, heif_item_id id)
    : ImageItem(ctx, id)
{
  m_encoding_options = heif_encoding_options_alloc();
}


ImageItem_Grid::~ImageItem_Grid()
{
  heif_encoding_options_free(m_encoding_options);
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

  Error err = check_for_valid_image_size(get_context()->get_security_limits(), w, h);
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
    tiles.resize(static_cast<size_t>(grid.get_rows()) * static_cast<size_t>(grid.get_columns()));

  std::deque<std::future<Error> > errs;
#endif

  uint32_t tile_width = 0;
  uint32_t tile_height = 0;

  if (options.start_progress) {
    options.start_progress(heif_progress_step_total, grid.get_rows() * grid.get_columns(), options.progress_user_data);
  }
  if (options.on_progress) {
    options.on_progress(heif_progress_step_total, 0, options.progress_user_data);
  }

  int progress_counter = 0;
  bool cancelled = false;

  for (uint32_t y = 0; y < grid.get_rows() && !cancelled; y++) {
    uint32_t x0 = 0;

    for (uint32_t x = 0; x < grid.get_columns() && !cancelled; x++) {

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
      err = check_for_valid_image_size(get_context()->get_security_limits(), src_width, src_height);
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
        if (options.cancel_decoding) {
          if (options.cancel_decoding(options.progress_user_data)) {
            cancelled = true;
          }
        }

        err = decode_and_paste_tile_image(tileID, x0, y0, img, options, progress_counter);
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

    while (!tiles.empty() && !cancelled) {

      // If maximum number of threads running, wait until first thread finishes

      if (errs.size() >= (size_t) get_context()->get_max_decoding_threads()) {
        Error e = errs.front().get();
        if (e) {
          return e;
        }

        errs.pop_front();
      }


      if (options.cancel_decoding) {
        if (options.cancel_decoding(options.progress_user_data)) {
          cancelled = true;
        }
      }


      // Start a new decoding thread

      tile_data data = tiles.front();
      tiles.pop_front();

      errs.push_back(std::async(std::launch::async,
                                &ImageItem_Grid::decode_and_paste_tile_image, this,
                                data.tileID, data.x_origin, data.y_origin, std::ref(img), options,
                                std::ref(progress_counter)));
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

  if (options.end_progress) {
    options.end_progress(heif_progress_step_total, options.progress_user_data);
  }

  if (cancelled) {
    return Error{heif_error_Canceled, heif_suberror_Unspecified, "Decoding the image was canceled"};
  }

  return img;
}

Error ImageItem_Grid::decode_and_paste_tile_image(heif_item_id tileID, uint32_t x0, uint32_t y0,
                                                  std::shared_ptr<HeifPixelImage>& inout_image,
                                                  const heif_decoding_options& options,
                                                  int& progress_counter) const
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

  if (!inout_image) { // this avoids that we normally have to lock a mutex
#if ENABLE_PARALLEL_TILE_DECODING
    static std::mutex createImageMutex;
    std::lock_guard<std::mutex> lock(createImageMutex);
#endif

    if (!inout_image) {
      auto grid_image = std::make_shared<HeifPixelImage>();
      auto err = grid_image->create_clone_image_at_new_size(tile_img, w, h, get_context()->get_security_limits());
      if (err) {
        return err;
      }

      // Fill alpha plane with opaque in case not all tiles have alpha planes

      if (grid_image->has_channel(heif_channel_Alpha)) {
        uint16_t alpha_bpp = grid_image->get_bits_per_pixel(heif_channel_Alpha);
        assert(alpha_bpp <= 16);

        auto alpha_default_value = static_cast<uint16_t>((1UL << alpha_bpp) - 1UL);
        grid_image->fill_plane(heif_channel_Alpha, alpha_default_value);
      }

      grid_image->forward_all_metadata_from(tile_img);

      inout_image = grid_image; // We have to set this at the very end because of the unlocked check to `inout_image` above.
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

  if (options.on_progress) {
#if ENABLE_PARALLEL_TILE_DECODING
    static std::mutex progressMutex;
    std::lock_guard<std::mutex> lock(progressMutex);
#endif

    options.on_progress(heif_progress_step_total, ++progress_counter, options.progress_user_data);
  }

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


void ImageItem_Grid::set_grid_tile_id(uint32_t tile_x, uint32_t tile_y, heif_item_id id)
{
  uint32_t idx = tile_y * m_grid_spec.get_columns() + tile_x;
  m_grid_tile_ids[idx] = id;
}


heif_image_tiling ImageItem_Grid::get_heif_image_tiling() const
{
  heif_image_tiling tiling{};

  const ImageGrid& gridspec = get_grid_spec();
  tiling.num_columns = gridspec.get_columns();
  tiling.num_rows = gridspec.get_rows();

  tiling.image_width = gridspec.get_width();
  tiling.image_height = gridspec.get_height();
  tiling.number_of_extra_dimensions = 0;

  auto tile_ids = get_grid_tiles();
  if (!tile_ids.empty() && tile_ids[0] != 0) {
    heif_item_id tile0_id = tile_ids[0];
    auto tile0 = get_context()->get_image(tile0_id, true);
    if (tile0 == nullptr || tile0->get_item_error()) {
      return tiling;
    }

    tiling.tile_width = tile0->get_width();
    tiling.tile_height = tile0->get_height();
  }
  else {
    tiling.tile_width = 0;
    tiling.tile_height = 0;
  }

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

Result<std::shared_ptr<Decoder>> ImageItem_Grid::get_decoder() const
{
  heif_item_id child;
  Error err = get_context()->get_id_of_non_virtual_child_image(get_id(), child);
  if (err) {
    return {err};
  }

  auto image = get_context()->get_image(child, true);
  if (!image) {
    return Error{heif_error_Invalid_input,
      heif_suberror_Nonexisting_item_referenced};
  }
  else if (auto err = image->get_item_error()) {
    return err;
  }

  return image->get_decoder();
}


Result<std::shared_ptr<ImageItem_Grid>> ImageItem_Grid::add_new_grid_item(HeifContext* ctx,
                                                                          uint32_t output_width,
                                                                          uint32_t output_height,
                                                                          uint16_t tile_rows,
                                                                          uint16_t tile_columns,
                                                                          const struct heif_encoding_options* encoding_options)
{
  std::shared_ptr<ImageItem_Grid> grid_image;
  if (tile_rows > 0xFFFF / tile_columns) {
    return Error{heif_error_Usage_error,
                 heif_suberror_Unspecified,
                 "Too many tiles (maximum: 65535)"};
  }

  // Create ImageGrid

  ImageGrid grid;
  grid.set_num_tiles(tile_columns, tile_rows);
  grid.set_output_size(output_width, output_height); // TODO: MIAF restricts the output size to be a multiple of the chroma subsampling (7.3.11.4.2)
  std::vector<uint8_t> grid_data = grid.write();

  // Create Grid Item

  std::shared_ptr<HeifFile> file = ctx->get_heif_file();
  heif_item_id grid_id = file->add_new_image(fourcc("grid"));
  grid_image = std::make_shared<ImageItem_Grid>(ctx, grid_id);
  grid_image->set_encoding_options(encoding_options);
  grid_image->set_grid_spec(grid);
  grid_image->set_resolution(output_width, output_height);

  ctx->insert_image_item(grid_id, grid_image);
  const int construction_method = 1; // 0=mdat 1=idat
  file->append_iloc_data(grid_id, grid_data, construction_method);

  // generate dummy grid item IDs (0)
  std::vector<heif_item_id> tile_ids;
  tile_ids.resize(static_cast<size_t>(tile_rows) * static_cast<size_t>(tile_columns));

  // Connect tiles to grid
  file->add_iref_reference(grid_id, fourcc("dimg"), tile_ids);

  // Add ISPE property
  file->add_ispe_property(grid_id, output_width, output_height, false);

  // PIXI property will be added when the first tile is set

  // Set Brands
  //m_heif_file->set_brand(encoder->plugin->compression_format,
  //                       grid_image->is_miaf_compatible());

  return grid_image;
}


Error ImageItem_Grid::add_image_tile(uint32_t tile_x, uint32_t tile_y,
                                     const std::shared_ptr<HeifPixelImage>& image,
                                     struct heif_encoder* encoder)
{
  auto encoding_options = get_encoding_options();

  auto encodingResult = get_context()->encode_image(image,
                                            encoder,
                                            *encoding_options,
                                            heif_image_input_class_normal);
  if (encodingResult.error != Error::Ok) {
    return encodingResult.error;
  }

  std::shared_ptr<ImageItem> encoded_image = *encodingResult;

  auto file = get_file();
  file->get_infe_box(encoded_image->get_id())->set_hidden_item(true); // grid tiles are hidden items

  // Assign tile to grid
  heif_image_tiling tiling = get_heif_image_tiling();
  file->set_iref_reference(get_id(), fourcc("dimg"), tile_y * tiling.num_columns + tile_x, encoded_image->get_id());

  set_grid_tile_id(tile_x, tile_y, encoded_image->get_id());

  // Add PIXI property (copy from first tile)
  auto pixi = encoded_image->get_property<Box_pixi>();
  add_property(pixi, true);

  return Error::Ok;
}


Result<std::shared_ptr<ImageItem_Grid>> ImageItem_Grid::add_and_encode_full_grid(HeifContext* ctx,
                                                                                 const std::vector<std::shared_ptr<HeifPixelImage>>& tiles,
                                                                                 uint16_t rows,
                                                                                 uint16_t columns,
                                                                                 struct heif_encoder* encoder,
                                                                                 const struct heif_encoding_options& options)
{
  std::shared_ptr<ImageItem_Grid> griditem;

  // Create ImageGrid

  ImageGrid grid;
  grid.set_num_tiles(columns, rows);
  uint32_t tile_width = tiles[0]->get_width(heif_channel_interleaved);
  uint32_t tile_height = tiles[0]->get_height(heif_channel_interleaved);
  grid.set_output_size(tile_width * columns, tile_height * rows);
  std::vector<uint8_t> grid_data = grid.write();

  auto file = ctx->get_heif_file();

  // Encode Tiles

  std::vector<heif_item_id> tile_ids;

  std::shared_ptr<Box_pixi> pixi_property;

  for (int i=0; i<rows*columns; i++) {
    std::shared_ptr<ImageItem> out_tile;
    auto encodingResult = ctx->encode_image(tiles[i],
                                            encoder,
                                            options,
                                            heif_image_input_class_normal);
    if (encodingResult.error) {
      return encodingResult.error;
    }
    else {
      out_tile = *encodingResult;
    }

    heif_item_id tile_id = out_tile->get_id();
    file->get_infe_box(tile_id)->set_hidden_item(true); // only show the full grid
    tile_ids.push_back(out_tile->get_id());

    if (!pixi_property) {
      pixi_property = out_tile->get_property<Box_pixi>();
    }
  }

  // Create Grid Item

  heif_item_id grid_id = file->add_new_image(fourcc("grid"));
  griditem = std::make_shared<ImageItem_Grid>(ctx, grid_id);
  ctx->insert_image_item(grid_id, griditem);
  const int construction_method = 1; // 0=mdat 1=idat
  file->append_iloc_data(grid_id, grid_data, construction_method);

  // Connect tiles to grid

  file->add_iref_reference(grid_id, fourcc("dimg"), tile_ids);

  // Add ISPE property

  uint32_t image_width = tile_width * columns;
  uint32_t image_height = tile_height * rows;

  auto ispe = std::make_shared<Box_ispe>();
  ispe->set_size(image_width, image_height);
  griditem->add_property(ispe, false);

  // Add PIXI property (copy from first tile)

  griditem->add_property(pixi_property, true);

  // Set Brands

  //file->set_brand(encoder->plugin->compression_format,
  //                griditem->is_miaf_compatible());

  return griditem;
}

heif_brand2 ImageItem_Grid::get_compatible_brand() const
{
  if (m_grid_tile_ids.empty()) { return 0; }

  heif_item_id child_id = m_grid_tile_ids[0];
  auto child = get_context()->get_image(child_id, false);
  if (!child) { return 0; }

  return child->get_compatible_brand();
}
