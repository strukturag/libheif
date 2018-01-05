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
  m_all_images.clear();
  m_top_level_images.clear();
  m_primary_image.reset();


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


  if (!m_primary_image) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Nonexisting_image_referenced,
                 "'pitm' box references a non-existing image");
  }


  // --- remove thumbnails from top-level images and assign to their respective image

  auto iref_box = m_heif_file->get_iref_box();
  if (iref_box) {
    m_top_level_images.clear();

    for (auto& pair : m_all_images) {
      auto& image = pair.second;

      uint32_t type = iref_box->get_reference_type(image->get_id());

      if (type==fourcc("thmb")) {
        std::vector<uint32_t> refs = iref_box->get_references(image->get_id());
        if (refs.size() != 1) {
          return Error(heif_error_Invalid_input,
                       heif_suberror_Unspecified,
                       "Too many thumbnail references");
        }

        image->set_is_thumbnail_of(refs[0]);

        auto master_iter = m_all_images.find(refs[0]);
        if (master_iter == m_all_images.end()) {
          return Error(heif_error_Invalid_input,
                       heif_suberror_Nonexisting_image_referenced,
                       "Thumbnail references a non-existing image");
        }

        if (master_iter->second->is_thumbnail()) {
          return Error(heif_error_Invalid_input,
                       heif_suberror_Nonexisting_image_referenced,
                       "Thumbnail references another thumbnail");
        }

        master_iter->second->add_thumbnail(image);
      }
      else {
        // 'image' is a normal image, add it as a top-level image

        m_top_level_images.push_back(image);
      }
    }
  }


  // --- read through properties for each image and extract image resolutions

  for (auto& pair : m_all_images) {
    auto& image = pair.second;

    std::vector<Box_ipco::Property> properties;

    Error err = m_heif_file->get_properties(pair.first, properties);
    if (err) {
      return err;
    }

    for (const auto& prop : properties) {
      auto ispe = std::dynamic_pointer_cast<Box_ispe>(prop.property);
      if (ispe) {
        uint32_t width = ispe->get_width();
        uint32_t height = ispe->get_height();


        // --- check whether the image size is "too large"

        if (width  >= std::numeric_limits<int>::max() ||
            height >= std::numeric_limits<int>::max()) {
          std::stringstream sstr;
          sstr << "Image size " << width << "x" << height << " exceeds the maximum image size "
               << std::numeric_limits<int>::max() << "x"
               << std::numeric_limits<int>::max() << "\n";

          return Error(heif_error_Memory_allocation_error,
                       heif_suberror_Security_limit_exceeded,
                       sstr.str());
        }

        image->set_resolution(width, height);
      }
    }
  }

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
                                       heif_colorspace colorspace,
                                       heif_chroma chroma,
                                       HeifColorConversionParams* config) const
{
  Error err = m_heif_file->decode_image(m_id, img);
  if (err) {
    return err;
  }

  heif_chroma target_chroma = (chroma == heif_chroma_undefined ?
                               img->get_chroma_format() :
                               chroma);
  heif_colorspace target_colorspace = (colorspace == heif_colorspace_undefined ?
                                       img->get_colorspace() :
                                       colorspace);

  bool different_chroma = (target_chroma != img->get_chroma_format());
  bool different_colorspace = (target_colorspace != img->get_colorspace());

  if (different_chroma || different_colorspace) {
    img = img->convert_colorspace(target_colorspace, target_chroma);
    if (!img) {
      // TODO: error: unsupported conversion

      assert(false);
    }
  }

  return err;
}
