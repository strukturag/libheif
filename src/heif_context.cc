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

#include "heif_context.h"
#include "heif_image.h"


using namespace heif;


HeifContext::HeifContext()
{
}

HeifContext::~HeifContext()
{
}

Error HeifContext::read_from_file(const char* input_filename)
{
  m_heif_file = std::make_shared<HeifFile>();
  Error err = m_heif_file->read_from_file(input_filename);
  if (err) {
    return err;
  }

  return interpret_heif_file();
}

Error HeifContext::read_from_memory(const void* data, size_t size)
{
  m_heif_file = std::make_shared<HeifFile>();
  Error err = m_heif_file->read_from_memory(data,size);
  if (err) {
    return err;
  }

  return interpret_heif_file();
}


Error HeifContext::interpret_heif_file()
{
  // --- reference all non-hidden images

  std::vector<uint32_t> image_IDs = m_heif_file->get_image_IDs();

  for (uint32_t id : image_IDs) {
    auto infe_box = m_heif_file->get_infe_box(id);

    if (!infe_box->is_hidden_item()) {
      auto image = std::make_shared<Image>(m_heif_file, id);

      if (id==m_heif_file->get_primary_image_ID()) {
        image->set_primary(true);
        m_primary_image = image;
      }

      m_all_images.insert(std::make_pair(id, image));

      m_top_level_images.push_back(image);
    }
  }


  // --- remove thumbnails from top-level images and assign to their respective image

  //m_top_level_images = m_all_images;
  // TODO

  return Error::Ok;
}


HeifContext::Image::Image(std::shared_ptr<HeifFile> file, uint32_t id)
  : m_heif_file(file),
    m_id(id)
{
}

HeifContext::Image::~Image()
{
}

Error HeifContext::Image::decode_image(std::shared_ptr<HeifPixelImage>& img,
                                       heif_chroma chroma,
                                       heif_colorspace colorspace) const
{
  Error err = m_heif_file->decode_image(m_id, img);

  // TODO: color space conversion

  return err;
}
