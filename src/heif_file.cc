/*
 * HEIF codec.
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
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

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include "heif.h"
#include "heif_file.h"
#include "heif_image.h"
#include "heif_api_structs.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>
#include <assert.h>
#include <string.h>

#if HAVE_LIBDE265
#include "heif_decoder_libde265.h"
#endif

using namespace heif;


class ImageGrid
{
public:
  Error parse(const std::vector<uint8_t>& data);

  std::string dump() const;

  uint32_t get_width() const { return m_output_width; }
  uint32_t get_height() const { return m_output_height; }
  uint16_t get_rows() const { return m_rows; }
  uint16_t get_columns() const { return m_columns; }

private:
  uint16_t m_rows;
  uint16_t m_columns;
  uint32_t m_output_width;
  uint32_t m_output_height;
};


Error ImageGrid::parse(const std::vector<uint8_t>& data)
{
  if (data.size() < 8) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Invalid_grid_data,
                 "Less than 8 bytes of data");
  }

  uint8_t version = data[0];
  (void)version; // version is unused

  uint8_t flags = data[1];
  int field_size = ((flags & 1) ? 32 : 16);

  m_rows    = data[2] +1;
  m_columns = data[3] +1;

  if (field_size == 32) {
    if (data.size() < 12) {
      return Error(heif_error_Invalid_input,
                   heif_suberror_Invalid_grid_data,
                   "Grid image data incomplete");
    }

    m_output_width = ((data[4] << 24) |
                      (data[5] << 16) |
                      (data[6] <<  8) |
                      (data[7]));

    m_output_height = ((data[ 8] << 24) |
                       (data[ 9] << 16) |
                       (data[10] <<  8) |
                       (data[11]));
  }
  else {
    m_output_width = ((data[4] << 8) |
                      (data[5]));

    m_output_height = ((data[ 6] << 8) |
                       (data[ 7]));
  }

  return Error::Ok;
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




HeifFile::HeifFile()
{
#if HAVE_LIBDE265
  m_decoder_plugin = get_decoder_plugin_libde265();
#endif
}


HeifFile::~HeifFile()
{
}


std::vector<uint32_t> HeifFile::get_image_IDs() const
{
  std::vector<uint32_t> IDs;

  for (const auto& image : m_images) {
    IDs.push_back(image.second.m_infe_box->get_item_ID());
  }

  return IDs;
}


Error HeifFile::read_from_file(const char* input_filename)
{
  m_input_stream = std::unique_ptr<std::istream>(new std::ifstream(input_filename));

  uint64_t maxSize = std::numeric_limits<uint64_t>::max();
  heif::BitstreamRange range(m_input_stream.get(), maxSize);


  Error error = parse_heif_file(range);
  return error;
}



Error HeifFile::read_from_memory(const void* data, size_t size)
{
  // TODO: Work on passed memory directly instead of creating a copy here.
  // Note: we cannot use basic_streambuf for this, because it does not support seeking
  std::string s(static_cast<const char*>(data), size);

  m_input_stream = std::unique_ptr<std::istream>(new std::istringstream(std::move(s)));

  heif::BitstreamRange range(m_input_stream.get(), size);

  Error error = parse_heif_file(range);
  return error;
}


std::string HeifFile::debug_dump_boxes() const
{
  std::stringstream sstr;

  bool first=true;

  for (const auto& box : m_top_level_boxes) {
    // dump box content for debugging

    if (first) {
      first = false;
    }
    else {
      sstr << "\n";
    }

    heif::Indent indent;
    sstr << box->dump(indent);
  }

  return sstr.str();
}


Error HeifFile::parse_heif_file(BitstreamRange& range)
{
  // --- read all top-level boxes

  for (;;) {
    std::shared_ptr<Box> box;
    Error error = Box::read(range, &box);
    if (error != Error::Ok || range.error() || range.eof()) {
      break;
    }

    m_top_level_boxes.push_back(box);


    // extract relevant boxes (ftyp, meta)

    if (box->get_short_type() == fourcc("meta")) {
      m_meta_box = std::dynamic_pointer_cast<Box_meta>(box);
    }

    if (box->get_short_type() == fourcc("ftyp")) {
      m_ftyp_box = std::dynamic_pointer_cast<Box_ftyp>(box);
    }
  }



  // --- check whether this is a HEIF file and its structural format

  if (!m_ftyp_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_ftyp_box);
  }

  if (!m_ftyp_box->has_compatible_brand(fourcc("heic"))) {
    std::stringstream sstr;
    sstr << "File does not support the 'heic' brand.\n";

    return Error(heif_error_Unsupported_filetype,
                 heif_suberror_Unspecified,
                 sstr.str());
  }

  if (!m_meta_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_meta_box);
  }


  auto hdlr_box = std::dynamic_pointer_cast<Box_hdlr>(m_meta_box->get_child_box(fourcc("hdlr")));
  if (!hdlr_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_hdlr_box);
  }

  if (hdlr_box->get_handler_type() != fourcc("pict")) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_pict_handler);
  }


  // --- find mandatory boxes needed for image decoding

  auto pitm_box = std::dynamic_pointer_cast<Box_pitm>(m_meta_box->get_child_box(fourcc("pitm")));
  if (!pitm_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_pitm_box);
  }

  std::shared_ptr<Box> iprp_box = m_meta_box->get_child_box(fourcc("iprp"));
  if (!iprp_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_iprp_box);
  }

  m_ipco_box = std::dynamic_pointer_cast<Box_ipco>(iprp_box->get_child_box(fourcc("ipco")));
  if (!m_ipco_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_ipco_box);
  }

  m_ipma_box = std::dynamic_pointer_cast<Box_ipma>(iprp_box->get_child_box(fourcc("ipma")));
  if (!m_ipma_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_ipma_box);
  }

  m_iloc_box = std::dynamic_pointer_cast<Box_iloc>(m_meta_box->get_child_box(fourcc("iloc")));
  if (!m_iloc_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_iloc_box);
  }

  m_idat_box = std::dynamic_pointer_cast<Box_idat>(m_meta_box->get_child_box(fourcc("idat")));

  m_iref_box = std::dynamic_pointer_cast<Box_iref>(m_meta_box->get_child_box(fourcc("iref")));

  std::shared_ptr<Box> iinf_box = m_meta_box->get_child_box(fourcc("iinf"));
  if (!iinf_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_iinf_box);
  }



  // --- build list of images

  m_primary_image_ID = pitm_box->get_item_ID();

  std::vector<std::shared_ptr<Box>> infe_boxes = iinf_box->get_child_boxes(fourcc("infe"));

  for (auto& box : infe_boxes) {
    std::shared_ptr<Box_infe> infe_box = std::dynamic_pointer_cast<Box_infe>(box);
    assert(infe_box);

    Image img;
    img.m_infe_box = infe_box;

    m_images.insert( std::make_pair(infe_box->get_item_ID(), img) );
  }

  return Error::Ok;
}


bool HeifFile::image_exists(uint32_t ID) const
{
  auto image_iter = m_images.find(ID);
  return image_iter != m_images.end();
}


const HeifFile::Image& HeifFile::get_image_info(uint32_t ID) const
{
  // --- get the image from the list of all images

  auto image_iter = m_images.find(ID);
  assert(image_iter != m_images.end());

  return image_iter->second;
}


std::string HeifFile::get_image_type(uint32_t ID) const
{
  const Image& img = get_image_info(ID);
  return img.m_infe_box->get_item_type();
}


Error HeifFile::get_properties(uint32_t imageID,
                               std::vector<Box_ipco::Property>& properties) const
{
  Error err;
  if (!m_ipco_box || !m_ipma_box) {
    // TODO: error
  }

  err = m_ipco_box->get_properties_for_item_ID(imageID, m_ipma_box, properties);

  return err;
}


Error HeifFile::get_compressed_image_data(uint16_t ID, std::vector<uint8_t>* data) const {

  if (!image_exists(ID)) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Nonexisting_image_referenced);
  }

  const Image& image = get_image_info(ID);


  // --- get properties for this image

  std::vector<Box_ipco::Property> properties;
  Error err = m_ipco_box->get_properties_for_item_ID(ID, m_ipma_box, properties);
  if (err) {
    return err;
  }

  std::string item_type = image.m_infe_box->get_item_type();

  // --- get coded image data pointers

  auto items = m_iloc_box->get_items();
  const Box_iloc::Item* item = nullptr;
  for (const auto& i : items) {
    if (i.item_ID == ID) {
      item = &i;
      break;
    }
  }
  if (!item) {
    std::stringstream sstr;
    sstr << "Item with ID " << ID << " has no compressed data";

    return Error(heif_error_Invalid_input,
                 heif_suberror_No_item_data);
  }

  Error error = Error(heif_error_Unsupported_feature,
                      heif_suberror_Unsupported_codec);
  if (item_type == "hvc1") {
    // --- --- --- HEVC

    // --- get codec configuration

    std::shared_ptr<Box_hvcC> hvcC_box;
    for (auto& prop : properties) {
      if (prop.property->get_short_type() == fourcc("hvcC")) {
        hvcC_box = std::dynamic_pointer_cast<Box_hvcC>(prop.property);
        assert(hvcC_box);
      }
    }

    if (!hvcC_box->get_headers(data)) {
      // TODO
    }

    error = m_iloc_box->read_data(*item, *m_input_stream.get(), m_idat_box, data);
  } else if (item_type == "grid") {
    error = m_iloc_box->read_data(*item, *m_input_stream.get(), m_idat_box, data);
  }

  if (error != Error::Ok) {
    return error;
  }

  return Error::Ok;
}


Error HeifFile::decode_image(struct heif_context* ctx, uint32_t ID,
                             std::shared_ptr<HeifPixelImage>& img) const
{
  std::string image_type = get_image_type(ID);

  std::vector<uint8_t> data;
  Error error = get_compressed_image_data(ID, &data);
  if (error) {
    return error;
  }

  // --- decode image, depending on its type

  if (image_type == "hvc1") {
    assert(m_decoder_plugin); // TODO

    void* decoder;
    struct heif_error err = m_decoder_plugin->new_decoder(ctx, &decoder);
    if (err.code != heif_error_Ok) {
      return Error(err.code, err.subcode, err.message);
    }

    err = m_decoder_plugin->push_data(decoder, data.data(), data.size());
    if (err.code != heif_error_Ok) {
      m_decoder_plugin->free_decoder(decoder);
      return Error(err.code, err.subcode, err.message);
    }

    //std::shared_ptr<HeifPixelImage>* decoded_img;

    heif_image* decoded_img = nullptr;
    err = m_decoder_plugin->decode_image(decoder, &decoded_img);
    if (err.code != heif_error_Ok) {
      m_decoder_plugin->free_decoder(decoder);
      return Error(err.code, err.subcode, err.message);
    }

    m_decoder_plugin->free_decoder(decoder);
    if (!decoded_img) {
      // TODO(farindk): Return dedicated error or better let decoder return the
      // actual error from "decode_image".
      return Error(heif_error_Unsupported_feature,
                   heif_suberror_Unsupported_image_type);
    }

    img = std::move(decoded_img->image);
    delete decoded_img;

#if 0
    FILE* fh = fopen("out.bin", "wb");
    fwrite(data.data(), 1, data.size(), fh);
    fclose(fh);
#endif
  }
  else if (image_type == "grid") {
    error = decode_full_grid_image(ctx, ID, img, data);
    if (error) {
      return error;
    }
  }
  else if (image_type == "iden") {
    //return decode_derived_image(ID, img);
  }
  else {
    // Should not reach this, was already rejected by "get_image_data".
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_image_type);
  }


  // --- apply image transformations

  std::vector<Box_ipco::Property> properties;
  error = m_ipco_box->get_properties_for_item_ID(ID, m_ipma_box, properties);

  for (const auto& property : properties) {
    auto rot = std::dynamic_pointer_cast<Box_irot>(property.property);
    if (rot) {
      std::shared_ptr<HeifPixelImage> rotated_img;
      error = img->rotate(rot->get_rotation(), rotated_img);
      if (error) {
        return error;
      }

      img = rotated_img;
    }
  }

  return Error::Ok;
}


// TODO: this function only works with YCbCr images, chroma 4:2:0, and 8 bpp at the moment
// It will crash badly if we get anything else.
Error HeifFile::decode_full_grid_image(struct heif_context* ctx, uint16_t ID,
                                       std::shared_ptr<HeifPixelImage>& img,
                                       const std::vector<uint8_t>& grid_data) const
{
  ImageGrid grid;
  grid.parse(grid_data);
  // std::cout << grid.dump();


  if (!m_iref_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_iref_box,
                 "No iref box available, but needed for grid image");
  }

  std::vector<uint32_t> image_references = m_iref_box->get_references(ID);

  if ((int)image_references.size() != grid.get_rows() * grid.get_columns()) {
    std::stringstream sstr;
    sstr << "Tiled image with " << grid.get_rows() << "x" <<  grid.get_columns() << "="
         << (grid.get_rows() * grid.get_columns()) << " tiles, but only "
         << image_references.size() << " tile images in file";

    return Error(heif_error_Invalid_input,
                 heif_suberror_Missing_grid_images,
                 sstr.str());
  }

  // --- generate image of full output size

  int w = grid.get_width();
  int h = grid.get_height();
  int bpp = 8; // TODO: how do we know ?

  img = std::make_shared<HeifPixelImage>(ctx->context);
  img->create(w,h,
              heif_colorspace_YCbCr, // TODO: how do we know ?
              heif_chroma_420); // TODO: how do we know ?

  img->add_plane(heif_channel_Y,  w,h, bpp);
  img->add_plane(heif_channel_Cb, w/2,h/2, bpp);
  img->add_plane(heif_channel_Cr, w/2,h/2, bpp);

  int y0=0;
  int reference_idx = 0;

  for (int y=0;y<grid.get_rows();y++) {
    int x0=0;
    int tile_height=0;

    for (int x=0;x<grid.get_columns();x++) {

      std::shared_ptr<HeifPixelImage> tile_img;

      Error err = decode_image(ctx, image_references[reference_idx],
                               tile_img);
      if (err != Error::Ok) {
        return err;
      }


      // --- copy tile into output image

      int src_width  = tile_img->get_width();
      int src_height = tile_img->get_height();

      tile_height = src_height;

      for (heif_channel channel : { heif_channel_Y, heif_channel_Cb, heif_channel_Cr }) {
        int tile_stride;
        uint8_t* tile_data = tile_img->get_plane(channel, &tile_stride);

        int out_stride;
        uint8_t* out_data = img->get_plane(channel, &out_stride);

        int copy_width  = std::min(src_width, w - x0);
        int copy_height = std::min(src_height, h - y0);

        int xs=x0, ys=y0;

        if (channel != heif_channel_Y) {
          copy_width /= 2;
          copy_height /= 2;
          xs /= 2;
          ys /= 2;
        }

        for (int py=0;py<copy_height;py++) {
          memcpy(out_data + xs + (ys+py)*out_stride,
                 tile_data + py*tile_stride,
                 copy_width);
        }
      }

      x0 += src_width;

      reference_idx++;
    }

    y0 += tile_height;
  }

  return Error::Ok;
}
