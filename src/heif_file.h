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

#ifndef LIBHEIF_HEIF_FILE_H
#define LIBHEIF_HEIF_FILE_H

#include "box.h"

#include <map>


namespace heif {

  class HeifFile {
  public:
    HeifFile();
    ~HeifFile();

    Error read_from_file(const char* input_filename);

    int get_num_images() const { return m_images.size(); }

    int get_primary_image_ID() const { return m_primary_image_ID; }

    struct de265_image* get_image(uint32_t ID);

  private:
    std::vector<std::shared_ptr<Box> > m_top_level_boxes;

    std::shared_ptr<Box_ftyp> m_ftyp_box;
    std::shared_ptr<Box_meta> m_meta_box;

    std::shared_ptr<Box_ipco> m_ipco_box;
    std::shared_ptr<Box_ipma> m_ipma_box;
    std::shared_ptr<Box_iloc> m_iloc_box;

    struct Image {
      std::shared_ptr<Box_infe> m_infe_box;
    };

    std::map<uint16_t, Image> m_images;  // map from image ID to info structure

    uint32_t m_primary_image_ID;


    Error parse_heif_file(BitstreamRange& bitstream);
  };

}

#endif
