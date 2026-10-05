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

#ifndef LIBHEIF_IMAGEITEM_GRID_H
#define LIBHEIF_IMAGEITEM_GRID_H

#include "image_item.h"
#include "image/image_description.h"
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <optional>
#include <set>


class ImageGrid
{
public:
  Error parse(const std::vector<uint8_t>& data);

  std::vector<uint8_t> write() const;

  std::string dump() const;

  uint32_t get_width() const { return m_output_width; }

  uint32_t get_height() const { return m_output_height; }

  uint16_t get_rows() const
  {
    return m_rows;
  }

  uint16_t get_columns() const
  {
    return m_columns;
  }

  void set_num_tiles(uint16_t columns, uint16_t rows)
  {
    m_rows = rows;
    m_columns = columns;
  }

  void set_output_size(uint32_t width, uint32_t height)
  {
    m_output_width = width;
    m_output_height = height;
  }

private:
  uint16_t m_rows = 0;
  uint16_t m_columns = 0;
  uint32_t m_output_width = 0;
  uint32_t m_output_height = 0;
};





class ImageItem_Grid : public ImageItem
{
public:
  ImageItem_Grid(HeifContext* ctx, heif_item_id id);

  ImageItem_Grid(HeifContext* ctx);

  ~ImageItem_Grid() override;

  uint32_t get_infe_type() const override { return fourcc("grid"); }

  static Result<std::shared_ptr<ImageItem_Grid>> add_new_grid_item(HeifContext* ctx,
                                                                   uint32_t output_width,
                                                                   uint32_t output_height,
                                                                   uint16_t tile_rows,
                                                                   uint16_t tile_columns,
                                                                   const heif_encoding_options* encoding_options);

  Error add_image_tile(uint32_t tile_x, uint32_t tile_y,
                       const std::shared_ptr<HeifPixelImage>& image,
                       heif_encoder* encoder);

  static Result<std::shared_ptr<ImageItem_Grid>> add_and_encode_full_grid(HeifContext* ctx,
                                                                          const std::vector<std::shared_ptr<HeifPixelImage>>& tiles,
                                                                          uint16_t rows,
                                                                          uint16_t columns,
                                                                          heif_encoder* encoder,
                                                                          const heif_encoding_options& options);


  // TODO: nclx depends on contained format
  // const heif_color_profile_nclx* get_forced_output_nclx() const override { return nullptr; }

  // heif_compression_format get_compression_format() const override { return heif_compression_HEVC; }

  Error initialize_decoder() override;

  // Delegates to the first grid tile's already-populated descriptions and
  // rescales per-component dims to the grid's full ispe size, so the handle
  // exposes the correct datatype/bit-depth even for unci float tiles
  // (which the base populate would mis-tag as unsigned_integer).
  void populate_component_descriptions() override;

  int get_luma_bits_per_pixel() const override;

  int get_chroma_bits_per_pixel() const override;

  void set_tile_encoding_options(const heif_encoding_options* options);

  Result<Encoder::CodedImageData> encode(const std::shared_ptr<HeifPixelImage>& image,
                                         heif_encoder* encoder,
                                         const heif_encoding_options& options,
                                         heif_image_input_class input_class) override
  {
    return Error{heif_error_Unsupported_feature,
                 heif_suberror_Unspecified, "Cannot encode image to 'grid'"};
  }

  Result<std::shared_ptr<HeifPixelImage>> decode_compressed_image(const heif_decoding_options& options,
                                                                  bool decode_tile_only, uint32_t tile_x0, uint32_t tile_y0,
                                                                  DecodeTraversalState decode_state) const override;

  heif_brand2 get_compatible_brand() const override;

  bool is_coded_in_miaf_profile() const override;

protected:
  Result<std::shared_ptr<Decoder>> get_decoder() const override;

public:

  // --- grid specific

  const ImageGrid& get_grid_spec() const { return m_grid_spec; }

  void set_grid_spec(const ImageGrid& grid) { m_grid_spec = grid; m_grid_tile_ids.resize(size_t{grid.get_rows()} * grid.get_columns()); }

  const std::vector<heif_item_id>& get_grid_tiles() const { return m_grid_tile_ids; }

  void set_grid_tile_id(uint32_t tile_x, uint32_t tile_y, heif_item_id);

  heif_image_tiling get_heif_image_tiling() const override;

  void get_tile_size(uint32_t& w, uint32_t& h) const override;

private:
  ImageGrid m_grid_spec;
  std::vector<heif_item_id> m_grid_tile_ids;

  // --- the format that all tiles of the grid have to share

  // All tiles of a grid have to decode to images of the same format. The first tile that
  // is decoded (in whatever order, and through whichever decoding interface) stores its
  // description here, and every tile that is decoded afterwards is compared with it.
  //
  // TODO: move the colorspace and the chroma format into ImageDescription.
  //   They are members of HeifPixelImage only, although they are the top level of the
  //   structure that ImageDescription describes: without them, a list of components does
  //   not tell whether the components are subsampled or interleaved. That is why this
  //   struct has to carry them next to the description. With both in ImageDescription,
  //   TileFormat becomes a plain ImageDescription, and check_tile_format() becomes a
  //   function of ImageDescription that compares two descriptions for having the same
  //   format. That function can then also compare a decoded image with what its image item
  //   advertises, of which ImageItem::check_decoded_image_bit_depth() covers only the luma
  //   and chroma bit depths today (not, e.g., a configuration box that claims another
  //   chroma format than the bitstream has).
  //   Things to take care of:
  //   - ImageDescription::copy_metadata_from() must not copy them. It copies the metadata
  //     (colour profile, light levels, ...), but not the component list, because the
  //     target image can have another layout, e.g. after a colour conversion. Colorspace
  //     and chroma format belong to that structural part.
  //   - An image item knows two colorspaces: the coded one, and the preferred decoding
  //     colorspace, which proposes RGB for matrix_coefficients=0 while the decoded image is
  //     still tagged as YCbCr. The description has to hold the coded one, otherwise it
  //     does not match the decoded image.
  //   - Every implementation of populate_component_descriptions() has to set them:
  //     ImageItem, ImageItem_iden, ImageItem_Grid, ImageItem_uncompressed, ImageItem_Tiled
  //     and ImageItem_Overlay. Where the format is not known, they stay undefined, and a
  //     comparison has to treat that as unknown, not as a mismatch.
  //   - In HeifPixelImage, the two values are tied to the planes of the image and are set
  //     in create() only. They should not become freely settable through the base class.
  struct TileFormat
  {
    heif_colorspace colorspace = heif_colorspace_undefined;
    heif_chroma chroma = heif_chroma_undefined;
    ImageDescription description;
  };

  mutable std::mutex m_reference_tile_format_mutex; // tiles are decoded in parallel
  mutable std::optional<TileFormat> m_reference_tile_format;

  // Stores the format of the first decoded tile, or compares the tile with the stored format.
  Error check_tile_format(const HeifPixelImage& tile_img) const;

  heif_orientation m_grid_orientation = heif_orientation_normal;
  heif_encoding_options* m_tile_encoding_options = nullptr;

  Error read_grid_spec();

  Result<std::shared_ptr<HeifPixelImage>> decode_full_grid_image(const heif_decoding_options& options, const DecodeTraversalState& decode_state) const;

  Result<std::shared_ptr<HeifPixelImage>> decode_grid_tile(const heif_decoding_options& options, uint32_t tx, uint32_t ty, DecodeTraversalState decode_state) const;

  Error decode_and_paste_tile_image(heif_item_id tileID, uint32_t x0, uint32_t y0,
                                    std::shared_ptr<HeifPixelImage>& inout_image,
                                    const heif_decoding_options& options, int& progress_counter,
                                    const std::shared_ptr<std::vector<Error> >& warnings,
                                    DecodeTraversalState decode_state) const;
};


#endif //LIBHEIF_GRID_H
