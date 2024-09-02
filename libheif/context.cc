/*
 * HEIF codec.
 * Copyright (c) 2017 Dirk Farin <dirk.farin@gmail.com>
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

#include "box.h"
#include "error.h"
#include "libheif/heif.h"
#include "region.h"
#include <cstdint>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <limits>
#include <cmath>
#include <deque>
#include <codecs/image_item.h>

#if ENABLE_PARALLEL_TILE_DECODING
#include <future>
#endif

#include "context.h"
#include "file.h"
#include "pixelimage.h"
#include "libheif/api_structs.h"
#include "security_limits.h"
#include "compression.h"
#include "color-conversion/colorconversion.h"
#include "plugin_registry.h"
#include "codecs/hevc.h"
#include "codecs/vvc.h"
#include "codecs/avif.h"
#include "codecs/jpeg.h"
#include "codecs/mask_image.h"
#include "codecs/jpeg2000.h"
#include "codecs/grid.h"
#include "codecs/overlay.h"
#include "codecs/tild.h"

#if WITH_UNCOMPRESSED_CODEC
#include "codecs/uncompressed_image.h"
#endif


heif_encoder::heif_encoder(const struct heif_encoder_plugin* _plugin)
    : plugin(_plugin)
{

}

heif_encoder::~heif_encoder()
{
  release();
}

void heif_encoder::release()
{
  if (encoder) {
    plugin->free_encoder(encoder);
    encoder = nullptr;
  }
}


struct heif_error heif_encoder::alloc()
{
  if (encoder == nullptr) {
    struct heif_error error = plugin->new_encoder(&encoder);
    // TODO: error handling
    return error;
  }

  struct heif_error err = {heif_error_Ok, heif_suberror_Unspecified, kSuccess};
  return err;
}


HeifContext::HeifContext()
{
  m_maximum_image_size_limit = MAX_IMAGE_SIZE;

  reset_to_empty_heif();
}

HeifContext::~HeifContext()
{
  // Break circular references between Images (when a faulty input image has circular image references)
  for (auto& it : m_all_images) {
    std::shared_ptr<ImageItem> image = it.second;
    image->clear();
  }
}

Error HeifContext::read(const std::shared_ptr<StreamReader>& reader)
{
  m_heif_file = std::make_shared<HeifFile>();
  Error err = m_heif_file->read(reader);
  if (err) {
    return err;
  }

  return interpret_heif_file();
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

Error HeifContext::read_from_memory(const void* data, size_t size, bool copy)
{
  m_heif_file = std::make_shared<HeifFile>();
  Error err = m_heif_file->read_from_memory(data, size, copy);
  if (err) {
    return err;
  }

  return interpret_heif_file();
}

void HeifContext::reset_to_empty_heif()
{
  m_heif_file = std::make_shared<HeifFile>();
  m_heif_file->new_empty_file();

  m_all_images.clear();
  m_top_level_images.clear();
  m_primary_image.reset();
}

Error HeifContext::check_resolution(uint32_t width, uint32_t height) const {

  // TODO: remove this. Has been moved to ImageItem::check_for_valid_image_size()

  // --- check whether the image size is "too large"
  uint32_t max_width_height = static_cast<uint32_t>(std::numeric_limits<int>::max());
  if ((width > max_width_height || height > max_width_height) ||
      (height != 0 && width > m_maximum_image_size_limit / height)) {
    std::stringstream sstr;
    sstr << "Image size " << width << "x" << height << " exceeds the maximum image size "
          << m_maximum_image_size_limit << "\n";

    return Error(heif_error_Memory_allocation_error,
                  heif_suberror_Security_limit_exceeded,
                  sstr.str());
  }

  if (width==0 || height==0) {
    return Error(heif_error_Memory_allocation_error,
                 heif_suberror_Invalid_image_size,
                 "zero width or height");
  }

  return Error::Ok;
}


std::shared_ptr<RegionItem> HeifContext::add_region_item(uint32_t reference_width, uint32_t reference_height)
{
  std::shared_ptr<Box_infe> box = m_heif_file->add_new_infe_box("rgan");
  box->set_hidden_item(true);

  auto regionItem = std::make_shared<RegionItem>(box->get_item_ID(), reference_width, reference_height);
  add_region_item(regionItem);

  return regionItem;
}

void HeifContext::add_region_referenced_mask_ref(heif_item_id region_item_id, heif_item_id mask_item_id)
{
  m_heif_file->add_iref_reference(region_item_id, fourcc("mask"), {mask_item_id});
}

void HeifContext::write(StreamWriter& writer)
{
  // --- serialize regions

  for (auto& image : m_all_images) {
    for (auto region : image.second->get_region_item_ids()) {
      m_heif_file->add_iref_reference(region,
                                      fourcc("cdsc"), {image.first});
    }
  }

  for (auto& region : m_region_items) {
    std::vector<uint8_t> data_array;
    Error err = region->encode(data_array);
    // TODO: err

    m_heif_file->append_iloc_data(region->item_id, data_array, 0);
  }

  // --- post-process images

  for (auto& img : m_all_images) {
    img.second->process_before_write();
  }

  // --- write to file

  m_heif_file->write(writer);
}

std::string HeifContext::debug_dump_boxes() const
{
  return m_heif_file->debug_dump_boxes();
}


static bool item_type_is_image(const std::string& item_type, const std::string& content_type)
{
  return (item_type == "hvc1" ||
          item_type == "grid" ||
          item_type == "tild" ||
          item_type == "iden" ||
          item_type == "iovl" ||
          item_type == "avc1" ||
          item_type == "unci" ||
          item_type == "vvc1" ||
          item_type == "jpeg" ||
          (item_type == "mime" && content_type == "image/jpeg") ||
          item_type == "j2k1" ||
          item_type == "mski");
}


void HeifContext::remove_top_level_image(const std::shared_ptr<ImageItem>& image)
{
  std::vector<std::shared_ptr<ImageItem>> new_list;

  for (const auto& img : m_top_level_images) {
    if (img != image) {
      new_list.push_back(img);
    }
  }

  m_top_level_images = new_list;
}


Error HeifContext::interpret_heif_file()
{
  m_all_images.clear();
  m_top_level_images.clear();
  m_primary_image.reset();


  // --- reference all non-hidden images

  std::vector<heif_item_id> image_IDs = m_heif_file->get_item_IDs();

  for (heif_item_id id : image_IDs) {
    auto infe_box = m_heif_file->get_infe_box(id);
    if (!infe_box) {
      // TODO(farindk): Should we return an error instead of skipping the invalid id?
      continue;
    }

    auto image = ImageItem::alloc_for_infe_box(this, infe_box);
    if (image) {
      m_all_images.insert(std::make_pair(id, image));

      if (!infe_box->is_hidden_item()) {
        if (id == m_heif_file->get_primary_image_ID()) {
          image->set_primary(true);
          m_primary_image = image;
        }

        m_top_level_images.push_back(image);
      }

      Error err = image->on_load_file();
      if (err) {
        return err;
      }

#if 0
      if (infe_box->get_item_type() == "grid") {
        Error err = image->read_grid_spec();
        if (err) {
          return err;
        }
      }
#endif
    }
  }

  if (!m_primary_image) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Nonexisting_item_referenced,
                 "'pitm' box references a non-existing image");
  }


  // --- process image properties

  for (auto& pair : m_all_images) {
    auto& image = pair.second;

    std::vector<std::shared_ptr<Box>> properties;

    Error err = m_heif_file->get_properties(pair.first, properties);
    if (err) {
      return err;
    }


    // --- are there any 'essential' properties that we did not parse?


    for (const auto& prop : properties) {
      if (std::dynamic_pointer_cast<Box_other>(prop) &&
          get_heif_file()->get_ipco_box()->is_property_essential_for_item(pair.first, prop, get_heif_file()->get_ipma_box())) {

        std::stringstream sstr;
        sstr << "could not parse item property '" << prop->get_type_string() << "'";
        return {heif_error_Unsupported_feature, heif_suberror_Unsupported_essential_property, sstr.str()};
      }
    }


    // --- extract image resolution

    bool ispe_read = false;
    for (const auto& prop : properties) {
      auto ispe = std::dynamic_pointer_cast<Box_ispe>(prop);
      if (ispe) {
        uint32_t width = ispe->get_width();
        uint32_t height = ispe->get_height();

        uint32_t max_width_height = static_cast<uint32_t>(std::numeric_limits<int>::max());
        if (width >= max_width_height || height >= max_width_height) {
          std::stringstream sstr;
          sstr << "Image size " << width << "x" << height << " exceeds the maximum image size "
                << m_maximum_image_size_limit << "\n";

          return Error(heif_error_Memory_allocation_error,
                        heif_suberror_Security_limit_exceeded,
                        sstr.str());
        }

        image->set_resolution(width, height);
        ispe_read = true;
      }
    }

    if (!ispe_read) {
      return Error(heif_error_Invalid_input,
                   heif_suberror_No_ispe_property,
                   "Image has no 'ispe' property");
    }

    for (const auto& prop : properties) {
      auto colr = std::dynamic_pointer_cast<Box_colr>(prop);
      if (colr) {
        auto profile = colr->get_color_profile();
        image->set_color_profile(profile);
        continue;
      }

      auto cmin = std::dynamic_pointer_cast<Box_cmin>(prop);
      if (cmin) {
        image->set_intrinsic_matrix(cmin->get_intrinsic_matrix());
      }

      auto cmex = std::dynamic_pointer_cast<Box_cmex>(prop);
      if (cmex) {
        image->set_extrinsic_matrix(cmex->get_extrinsic_matrix());
      }
    }


    for (const auto& prop : properties) {
      auto clap = std::dynamic_pointer_cast<Box_clap>(prop);
      if (clap) {
        image->set_resolution(clap->get_width_rounded(),
                              clap->get_height_rounded());

        if (image->has_intrinsic_matrix()) {
          image->get_intrinsic_matrix().apply_clap(clap.get(), image->get_width(), image->get_height());
        }
      }

      auto imir = std::dynamic_pointer_cast<Box_imir>(prop);
      if (imir) {
        image->get_intrinsic_matrix().apply_imir(imir.get(), image->get_width(), image->get_height());
      }

      auto irot = std::dynamic_pointer_cast<Box_irot>(prop);
      if (irot) {
        if (irot->get_rotation() == 90 ||
            irot->get_rotation() == 270) {
          // swap width and height
          image->set_resolution(image->get_height(),
                                image->get_width());
        }

        // TODO: apply irot to camera extrinsic matrix
      }
    }
  }


  // --- remove auxiliary from top-level images and assign to their respective image

  auto iref_box = m_heif_file->get_iref_box();
  if (iref_box) {
    // m_top_level_images.clear();

    for (auto& pair : m_all_images) {
      auto& image = pair.second;

      std::vector<Box_iref::Reference> references = iref_box->get_references_from(image->get_id());

      for (const Box_iref::Reference& ref : references) {
        uint32_t type = ref.header.get_short_type();

        if (type == fourcc("thmb")) {
          // --- this is a thumbnail image, attach to the main image

          std::vector<heif_item_id> refs = ref.to_item_ID;
          for (heif_item_id ref: refs) {
            image->set_is_thumbnail();

            auto master_iter = m_all_images.find(ref);
            if (master_iter == m_all_images.end()) {
              return Error(heif_error_Invalid_input,
                          heif_suberror_Nonexisting_item_referenced,
                          "Thumbnail references a non-existing image");
            }

            if (master_iter->second->is_thumbnail()) {
              return Error(heif_error_Invalid_input,
                          heif_suberror_Nonexisting_item_referenced,
                          "Thumbnail references another thumbnail");
            }

            if (image.get() == master_iter->second.get()) {
              return Error(heif_error_Invalid_input,
                          heif_suberror_Nonexisting_item_referenced,
                          "Recursive thumbnail image detected");
            }
            master_iter->second->add_thumbnail(image);
          }
          remove_top_level_image(image);
        }
        else if (type == fourcc("auxl")) {

          // --- this is an auxiliary image
          //     check whether it is an alpha channel and attach to the main image if yes

          std::vector<std::shared_ptr<Box>> properties;
          Error err = m_heif_file->get_properties(image->get_id(), properties);
          if (err) {
            return err;
          }

          std::shared_ptr<Box_auxC> auxC_property;
          for (const auto& property : properties) {
            auto auxC = std::dynamic_pointer_cast<Box_auxC>(property);
            if (auxC) {
              auxC_property = auxC;
            }
          }

          if (!auxC_property) {
            std::stringstream sstr;
            sstr << "No auxC property for image " << image->get_id();
            return Error(heif_error_Invalid_input,
                         heif_suberror_Auxiliary_image_type_unspecified,
                         sstr.str());
          }

          std::vector<heif_item_id> refs = ref.to_item_ID;

          // alpha channel

          if (auxC_property->get_aux_type() == "urn:mpeg:avc:2015:auxid:1" ||   // HEIF (avc)
              auxC_property->get_aux_type() == "urn:mpeg:hevc:2015:auxid:1" ||  // HEIF (h265)
              auxC_property->get_aux_type() == "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha") { // MIAF

            for (heif_item_id ref: refs) {
              auto master_iter = m_all_images.find(ref);
              if (master_iter == m_all_images.end()) {

                if (!m_heif_file->has_item_with_id(ref)) {
                  return Error(heif_error_Invalid_input,
                               heif_suberror_Nonexisting_item_referenced,
                               "Non-existing alpha image referenced");
                }

                continue;
              }

              auto master_img = master_iter->second;

              if (image.get() == master_img.get()) {
                return Error(heif_error_Invalid_input,
                            heif_suberror_Nonexisting_item_referenced,
                            "Recursive alpha image detected");
              }

              image->set_is_alpha_channel();
              master_img->set_alpha_channel(image);
            }
          }


          // depth channel

          if (auxC_property->get_aux_type() == "urn:mpeg:hevc:2015:auxid:2" || // HEIF
              auxC_property->get_aux_type() == "urn:mpeg:mpegB:cicp:systems:auxiliary:depth") { // AVIF
            image->set_is_depth_channel();

            for (heif_item_id ref: refs) {
              auto master_iter = m_all_images.find(ref);
              if (master_iter == m_all_images.end()) {

                if (!m_heif_file->has_item_with_id(ref)) {
                  return Error(heif_error_Invalid_input,
                               heif_suberror_Nonexisting_item_referenced,
                               "Non-existing depth image referenced");
                }

                continue;
              }
              if (image.get() == master_iter->second.get()) {
                return Error(heif_error_Invalid_input,
                            heif_suberror_Nonexisting_item_referenced,
                            "Recursive depth image detected");
              }
              master_iter->second->set_depth_channel(image);

              auto subtypes = auxC_property->get_subtypes();

              std::vector<std::shared_ptr<SEIMessage>> sei_messages;
              err = decode_hevc_aux_sei_messages(subtypes, sei_messages);

              for (auto& msg : sei_messages) {
                auto depth_msg = std::dynamic_pointer_cast<SEIMessage_depth_representation_info>(msg);
                if (depth_msg) {
                  image->set_depth_representation_info(*depth_msg);
                }
              }
            }
          }


          // --- generic aux image

          image->set_is_aux_image(auxC_property->get_aux_type());

          for (heif_item_id ref: refs) {
            auto master_iter = m_all_images.find(ref);
            if (master_iter == m_all_images.end()) {

              if (!m_heif_file->has_item_with_id(ref)) {
                return Error(heif_error_Invalid_input,
                             heif_suberror_Nonexisting_item_referenced,
                             "Non-existing aux image referenced");
              }

              continue;
            }
            if (image.get() == master_iter->second.get()) {
              return Error(heif_error_Invalid_input,
                          heif_suberror_Nonexisting_item_referenced,
                          "Recursive aux image detected");
            }

            master_iter->second->add_aux_image(image);

            remove_top_level_image(image);
          }
        }
        else {
          // 'image' is a normal image, keep it as a top-level image
        }
      }
    }
  }


  // --- check that HEVC images have an hvcC property

  for (auto& pair : m_all_images) {
    auto& image = pair.second;

    std::shared_ptr<Box_infe> infe = m_heif_file->get_infe_box(image->get_id());
    if (infe->get_item_type() == "hvc1") {

      auto ipma = m_heif_file->get_ipma_box();
      auto ipco = m_heif_file->get_ipco_box();

      if (!ipco->get_property_for_item_ID(image->get_id(), ipma, fourcc("hvcC"))) {
        return Error(heif_error_Invalid_input,
                     heif_suberror_No_hvcC_box,
                     "No hvcC property in hvc1 type image");
      }
    }
    if (infe->get_item_type() == "vvc1") {

      auto ipma = m_heif_file->get_ipma_box();
      auto ipco = m_heif_file->get_ipco_box();

      if (!ipco->get_property_for_item_ID(image->get_id(), ipma, fourcc("vvcC"))) {
        return Error(heif_error_Invalid_input,
                     heif_suberror_No_vvcC_box,
                     "No vvcC property in vvc1 type image");
      }
    }
  }


  // --- assign color profile from grid tiles to main image when main image has no profile assigned

  for (auto& pair : m_all_images) {
    auto& image = pair.second;
    auto id = pair.first;

    auto infe_box = m_heif_file->get_infe_box(id);
    if (!infe_box) {
      continue;
    }

    if (!iref_box) {
      break;
    }

    if (infe_box->get_item_type() == "grid") {
      std::vector<heif_item_id> image_references = iref_box->get_references(id, fourcc("dimg"));

      if (image_references.empty()) {
        continue; // TODO: can this every happen?
      }

      auto tileId = image_references.front();

      auto iter = m_all_images.find(tileId);
      if (iter == m_all_images.end()) {
        continue; // invalid grid entry
      }

      auto tile_img = iter->second;
      if (image->get_color_profile_icc() == nullptr && tile_img->get_color_profile_icc()) {
        image->set_color_profile(tile_img->get_color_profile_icc());
      }

      if (image->get_color_profile_nclx() == nullptr && tile_img->get_color_profile_nclx()) {
        image->set_color_profile(tile_img->get_color_profile_nclx());
      }
    }
  }


  // --- read metadata and assign to image

  for (heif_item_id id : image_IDs) {
    std::string item_type = m_heif_file->get_item_type(id);
    std::string content_type = m_heif_file->get_content_type(id);

    // 'rgan': skip region annotations, handled next
    // 'iden': iden images are no metadata
    if (item_type_is_image(item_type, content_type) || item_type == "rgan") {
      continue;
    }

    std::string item_uri_type = m_heif_file->get_item_uri_type(id);

    // we now assign all kinds of metadata to the image, not only 'Exif' and 'XMP'

    std::shared_ptr<ImageMetadata> metadata = std::make_shared<ImageMetadata>();
    metadata->item_id = id;
    metadata->item_type = item_type;
    metadata->content_type = content_type;
    metadata->item_uri_type = item_uri_type;

    Error err = m_heif_file->get_compressed_image_data(id, &(metadata->m_data));
    if (err) {
      if (item_type == "Exif" || item_type == "mime") {
        // these item types should have data
        return err;
      }
      else {
        // anything else is probably something that we don't understand yet
        continue;
      }
    }


    // --- assign metadata to the image

    if (iref_box) {
      std::vector<Box_iref::Reference> references = iref_box->get_references_from(id);
      for (const auto& ref : references) {
        if (ref.header.get_short_type() == fourcc("cdsc")) {
          std::vector<uint32_t> refs = ref.to_item_ID;

          for(uint32_t ref: refs) {
            uint32_t exif_image_id = ref;
            auto img_iter = m_all_images.find(exif_image_id);
            if (img_iter == m_all_images.end()) {
              if (!m_heif_file->has_item_with_id(exif_image_id)) {
                return Error(heif_error_Invalid_input,
                             heif_suberror_Nonexisting_item_referenced,
                             "Metadata assigned to non-existing image");
              }

              continue;
            }
            img_iter->second->add_metadata(metadata);
          }
        }
        else if (ref.header.get_short_type() == fourcc("prem")) {
          uint32_t color_image_id = ref.from_item_ID;
          auto img_iter = m_all_images.find(color_image_id);
          if (img_iter == m_all_images.end()) {
            return Error(heif_error_Invalid_input,
                         heif_suberror_Nonexisting_item_referenced,
                         "`prem` link assigned to non-existing image");
          }

          img_iter->second->set_is_premultiplied_alpha(true);;
        }
      }
    }
  }

  // --- read region item and assign to image(s)

  for (heif_item_id id : image_IDs) {
    std::string item_type = m_heif_file->get_item_type(id);
    if (item_type != "rgan") {
      continue;
    }

    std::shared_ptr<RegionItem> region_item = std::make_shared<RegionItem>();
    region_item->item_id = id;
    std::vector<uint8_t> region_data;
    Error err = m_heif_file->get_compressed_image_data(id, &(region_data));
    if (err) {
      return err;
    }
    region_item->parse(region_data);
    if (iref_box) {
      std::vector<Box_iref::Reference> references = iref_box->get_references_from(id);
      for (const auto& ref : references) {
        if (ref.header.get_short_type() == fourcc("cdsc")) {
          std::vector<uint32_t> refs = ref.to_item_ID;
          for (uint32_t ref : refs) {
            uint32_t image_id = ref;
            auto img_iter = m_all_images.find(image_id);
            if (img_iter == m_all_images.end()) {
              return Error(heif_error_Invalid_input,
                           heif_suberror_Nonexisting_item_referenced,
                           "Region item assigned to non-existing image");
            }
            img_iter->second->add_region_item_id(id);
            m_region_items.push_back(region_item);
          }
        }

        /* When the geometry 'mask' of a region is represented by a mask stored in
        * another image item the image item containing the mask shall be identified
        * by an item reference of type 'mask' from the region item to the image item
        * containing the mask. */
        if (ref.header.get_short_type() == fourcc("mask")) {
          std::vector<uint32_t> refs = ref.to_item_ID;
          size_t mask_index = 0;
          for (int j = 0; j < region_item->get_number_of_regions(); j++) {
            if (region_item->get_regions()[j]->getRegionType() == heif_region_type_referenced_mask) {
              std::shared_ptr<RegionGeometry_ReferencedMask> mask_geometry = std::dynamic_pointer_cast<RegionGeometry_ReferencedMask>(region_item->get_regions()[j]);

              if (mask_index >= refs.size()) {
                return Error(heif_error_Invalid_input,
                             heif_suberror_Unspecified,
                             "Region mask reference with non-existing mask image reference");
              }

              uint32_t mask_image_id = refs[mask_index];
              if (!is_image(mask_image_id)) {
                return Error(heif_error_Invalid_input,
                             heif_suberror_Unspecified,
                             "Region mask referenced item is not an image");
              }

              auto mask_image = m_all_images.find(mask_image_id)->second;
              mask_geometry->referenced_item = mask_image_id;
              if (mask_geometry->width == 0) {
                mask_geometry->width = mask_image->get_ispe_width();
              }
              if (mask_geometry->height == 0) {
                mask_geometry->height = mask_image->get_ispe_height();
              }
              mask_index += 1;
              remove_top_level_image(mask_image);
            }
          }
        }
      }
    }
  }

  return Error::Ok;
}


bool HeifContext::has_alpha(heif_item_id ID) const
{

  assert(is_image(ID));
  auto img = m_all_images.find(ID)->second;

  // --- has the image an auxiliary alpha image?

  if (img->get_alpha_channel() != nullptr) {
    return true;
  }

  heif_colorspace colorspace;
  heif_chroma chroma;
  img->get_coded_image_colorspace(&colorspace, &chroma);

  if (chroma == heif_chroma_interleaved_RGBA ||
      chroma == heif_chroma_interleaved_RRGGBBAA_BE ||
      chroma == heif_chroma_interleaved_RRGGBBAA_LE) {
    return true;
  }

  // --- if the image is a 'grid', check if there is alpha in any of the tiles

  std::string image_type = m_heif_file->get_item_type(ID);
  if (image_type == "grid") {
    std::vector<uint8_t> grid_data;
    Error error = m_heif_file->get_compressed_image_data(ID, &grid_data);
    if (error) {
      return false;
    }

    ImageGrid grid;
    Error err = grid.parse(grid_data);
    if (err) {
      return false;
    }


    auto iref_box = m_heif_file->get_iref_box();

    if (!iref_box) {
      return false;
    }

    std::vector<heif_item_id> image_references = iref_box->get_references(ID, fourcc("dimg"));

    if ((int) image_references.size() != grid.get_rows() * grid.get_columns()) {
      return false;
    }


    // --- check that all image IDs are valid images

    for (heif_item_id tile_id : image_references) {
      if (!is_image(tile_id)) {
        return false;
      }
    }

    // --- check whether at least one tile has an alpha channel

    bool has_alpha = false;

    for (heif_item_id tile_id : image_references) {
      auto iter = m_all_images.find(tile_id);
      if (iter == m_all_images.end()) {
        return false;
      }

      const std::shared_ptr<ImageItem> tileImg = iter->second;

      has_alpha |= tileImg->get_alpha_channel() != nullptr;
    }

    return has_alpha;
  }
  else {
    // TODO: what about overlays ?
    return false;
  }
}


Error HeifContext::get_id_of_non_virtual_child_image(heif_item_id id, heif_item_id& out) const
{
  std::string image_type = m_heif_file->get_item_type(id);
  if (image_type == "grid" ||
      image_type == "iden" ||
      image_type == "iovl") {
    auto iref_box = m_heif_file->get_iref_box();
    if (!iref_box) {
      return Error(heif_error_Invalid_input,
                   heif_suberror_No_item_data,
                   "Derived image does not reference any other image items");
    }

    std::vector<heif_item_id> image_references = iref_box->get_references(id, fourcc("dimg"));

    // TODO: check whether this really can be recursive (e.g. overlay of grid images)

    if (image_references.empty() || image_references[0] == id) {
      return Error(heif_error_Invalid_input,
                   heif_suberror_No_item_data,
                   "Derived image does not reference any other image items");
    }
    else {
      return get_id_of_non_virtual_child_image(image_references[0], out);
    }
  }
  else {
    out = id;
    return Error::Ok;
  }
}


Result<std::shared_ptr<HeifPixelImage>> HeifContext::decode_image(heif_item_id ID,
                                                                  heif_colorspace out_colorspace,
                                                                  heif_chroma out_chroma,
                                                                  const struct heif_decoding_options& options,
                                                                  bool decode_only_tile, uint32_t tx, uint32_t ty) const
{
  std::string image_type = m_heif_file->get_item_type(ID);

  std::shared_ptr<ImageItem> imginfo;
  if (m_all_images.find(ID) != m_all_images.end()) {
    imginfo = m_all_images.find(ID)->second;
  }

  // Note: this may happen, for example when an 'iden' image references a non-existing image item.
  if (imginfo == nullptr) {
    return Error(heif_error_Invalid_input, heif_suberror_Nonexisting_item_referenced);
  }


  auto decodingResult = imginfo->decode_image(options, decode_only_tile, tx, ty);
  if (decodingResult.error) {
    return decodingResult.error;
  }

  std::shared_ptr<HeifPixelImage> img = decodingResult.value;


  // --- convert to output chroma format

  heif_colorspace target_colorspace = (out_colorspace == heif_colorspace_undefined ?
                                       img->get_colorspace() :
                                       out_colorspace);

  heif_chroma target_chroma = (out_chroma == heif_chroma_undefined ?
                               img->get_chroma_format() : out_chroma);

  bool different_chroma = (target_chroma != img->get_chroma_format());
  bool different_colorspace = (target_colorspace != img->get_colorspace());

  int bpp = options.convert_hdr_to_8bit ? 8 : 0;
  // TODO: check BPP changed
  if (different_chroma || different_colorspace) {

    img = convert_colorspace(img, target_colorspace, target_chroma, nullptr, bpp, options.color_conversion_options);
    if (!img) {
      return Error(heif_error_Unsupported_feature, heif_suberror_Unsupported_color_conversion);
    }
  }

  return img;
}


static std::shared_ptr<HeifPixelImage>
create_alpha_image_from_image_alpha_channel(const std::shared_ptr<HeifPixelImage>& image)
{
  // --- generate alpha image

  std::shared_ptr<HeifPixelImage> alpha_image = std::make_shared<HeifPixelImage>();
  alpha_image->create(image->get_width(), image->get_height(),
                      heif_colorspace_monochrome, heif_chroma_monochrome);

  if (image->has_channel(heif_channel_Alpha)) {
    alpha_image->copy_new_plane_from(image, heif_channel_Alpha, heif_channel_Y);
  }
  else if (image->get_chroma_format() == heif_chroma_interleaved_RGBA) {
    alpha_image->extract_alpha_from_RGBA(image);
  }
  // TODO: 16 bit

  // --- set nclx profile with full-range flag

  auto nclx = std::make_shared<color_profile_nclx>();
  nclx->set_undefined();
  nclx->set_full_range_flag(true); // this is the default, but just to be sure in case the defaults change
  alpha_image->set_color_profile_nclx(nclx);

  return alpha_image;
}


Error HeifContext::encode_image(const std::shared_ptr<HeifPixelImage>& pixel_image,
                                struct heif_encoder* encoder,
                                const struct heif_encoding_options& in_options,
                                enum heif_image_input_class input_class,
                                std::shared_ptr<ImageItem>& out_image)
{
  Error error;


  std::shared_ptr<ImageItem> image_item = ImageItem::alloc_for_compression_format(this, encoder->plugin->compression_format);


#if 0
  // TODO: the hdlr box is not the right place for comments
  // m_heif_file->set_hdlr_library_info(encoder->plugin->get_plugin_name());

    case heif_compression_mask: {
      error = encode_image_as_mask(pixel_image,
                                  encoder,
                                  options,
                                  input_class,
                                  out_image);
    }
      break;

    default:
      return Error(heif_error_Encoder_plugin_error, heif_suberror_Unsupported_codec);
  }
#endif


  // --- check whether we have to convert the image color space

  // The reason for doing the color conversion here is that the input might be an RGBA image and the color conversion
  // will extract the alpha plane anyway. We can reuse that plane below instead of having to do a new conversion.

  heif_encoding_options options = in_options;

  if (const auto* nclx = image_item->get_forced_output_nclx()) {
    options.output_nclx_profile = nclx;
  }

  Result<std::shared_ptr<HeifPixelImage>> srcImageResult = image_item->convert_colorspace_for_encoding(pixel_image,
                                                                                                       encoder,
                                                                                                       options);
  if (srcImageResult.error) {
    return srcImageResult.error;
  }

  std::shared_ptr<HeifPixelImage> colorConvertedImage = srcImageResult.value;


  Error err = image_item->encode_to_item(this,
                                         colorConvertedImage,
                                         encoder, options, input_class);
  if (err) {
    return err;
  }

  out_image = image_item;

  insert_new_image(image_item->get_id(), image_item);


  // --- if there is an alpha channel, add it as an additional image

  if (options.save_alpha_channel &&
      colorConvertedImage->has_alpha() &&
      image_item->get_auxC_alpha_channel_type() != nullptr) { // does not need a separate alpha aux image

    // --- generate alpha image
    // TODO: can we directly code a monochrome image instead of the dummy color channels?

    std::shared_ptr<HeifPixelImage> alpha_image;
    alpha_image = create_alpha_image_from_image_alpha_channel(colorConvertedImage);


    // --- encode the alpha image

    std::shared_ptr<ImageItem> heif_alpha_image;

    error = encode_image(alpha_image, encoder, options,
                         heif_image_input_class_alpha,
                         heif_alpha_image);
    if (error) {
      return error;
    }

    m_heif_file->add_iref_reference(heif_alpha_image->get_id(), fourcc("auxl"), {out_image->get_id()});
    m_heif_file->set_auxC_property(heif_alpha_image->get_id(), out_image->get_auxC_alpha_channel_type());

    if (pixel_image->is_premultiplied_alpha()) {
      m_heif_file->add_iref_reference(out_image->get_id(), fourcc("prem"), {heif_alpha_image->get_id()});
    }
  }


  m_heif_file->set_brand(encoder->plugin->compression_format,
                         out_image->is_miaf_compatible());

  return error;
}

Error HeifContext::encode_grid(const std::vector<std::shared_ptr<HeifPixelImage>>& tiles,
                               uint16_t rows,
                               uint16_t columns,
                               struct heif_encoder* encoder,
                               const struct heif_encoding_options& options,
                               std::shared_ptr<ImageItem>& out_grid_image)
{
  // Create ImageGrid
  ImageGrid grid;
  grid.set_num_tiles(columns, rows);
  int tile_width = tiles[0]->get_width(heif_channel_interleaved);
  int tile_height = tiles[0]->get_height(heif_channel_interleaved);
  grid.set_output_size(tile_width * columns, tile_height * rows);
  std::vector<uint8_t> grid_data = grid.write();

  // Encode Tiles
  Error error;
  std::vector<heif_item_id> tile_ids;
  for (int i=0; i<rows*columns; i++) {
    std::shared_ptr<ImageItem> out_tile;
    error = encode_image(tiles[i],
                         encoder,
                         options,
                         heif_image_input_class_normal,
                         out_tile);
    heif_item_id tile_id = out_tile->get_id();
    m_heif_file->get_infe_box(tile_id)->set_hidden_item(true); // only show the full grid
    tile_ids.push_back(out_tile->get_id());
  }

  // Create Grid Item
  heif_item_id grid_id = m_heif_file->add_new_image("grid");
  out_grid_image = std::make_shared<ImageItem>(this, grid_id);
  m_all_images.insert(std::make_pair(grid_id, out_grid_image));
  const int construction_method = 1; // 0=mdat 1=idat
  m_heif_file->append_iloc_data(grid_id, grid_data, construction_method);

  // Connect tiles to grid
  m_heif_file->add_iref_reference(grid_id, fourcc("dimg"), tile_ids);

  // Add ISPE property
  int image_width = tile_width * columns;
  int image_height = tile_height * rows;
  m_heif_file->add_ispe_property(grid_id, image_width, image_height);

  // Add PIXI property (copy from first tile)
  auto pixi = m_heif_file->get_property<Box_pixi>(tile_ids[0]);
  m_heif_file->add_property(grid_id, pixi, true);

  // Set Brands
  m_heif_file->set_brand(encoder->plugin->compression_format,
                         out_grid_image->is_miaf_compatible());

  return error;
}


Error HeifContext::add_grid_item(const std::vector<heif_item_id>& tile_ids,
                               uint32_t output_width,
                               uint32_t output_height,
                               uint16_t tile_rows,
                               uint16_t tile_columns,
                               std::shared_ptr<ImageItem>& out_grid_image)
{
  if (tile_ids.size() > 0xFFFF) {
    return {heif_error_Usage_error,
            heif_suberror_Unspecified,
            "Too many tiles (maximum: 65535)"};
  }

#if 1
  for (heif_item_id tile_id : tile_ids) {
    m_heif_file->get_infe_box(tile_id)->set_hidden_item(true); // only show the full grid
  }
#endif


  // Create ImageGrid

  ImageGrid grid;
  grid.set_num_tiles(tile_columns, tile_rows);
  grid.set_output_size(output_width, output_height);
  std::vector<uint8_t> grid_data = grid.write();

  // Create Grid Item

  heif_item_id grid_id = m_heif_file->add_new_image("grid");
  out_grid_image = std::make_shared<ImageItem>(this, grid_id);
  m_all_images.insert(std::make_pair(grid_id, out_grid_image));
  const int construction_method = 1; // 0=mdat 1=idat
  m_heif_file->append_iloc_data(grid_id, grid_data, construction_method);

  // Connect tiles to grid
  m_heif_file->add_iref_reference(grid_id, fourcc("dimg"), tile_ids);

  // Add ISPE property
  m_heif_file->add_ispe_property(grid_id, output_width, output_height);

  // Add PIXI property (copy from first tile)
  auto pixi = m_heif_file->get_property<Box_pixi>(tile_ids[0]);
  m_heif_file->add_property(grid_id, pixi, true);

  // Set Brands
  //m_heif_file->set_brand(encoder->plugin->compression_format,
  //                       out_grid_image->is_miaf_compatible());

  return Error::Ok;
}


Result<std::shared_ptr<ImageItem_Overlay>> HeifContext::add_iovl_item(const ImageOverlay& overlayspec)
{
  if (overlayspec.get_num_offsets() > 0xFFFF) {
    return Error{heif_error_Usage_error,
                 heif_suberror_Unspecified,
                 "Too many overlay images (maximum: 65535)"};
  }

  std::vector<heif_item_id> ref_ids;

  for (const auto& overlay : overlayspec.get_overlay_stack()) {
    m_heif_file->get_infe_box(overlay.image_id)->set_hidden_item(true); // only show the full overlay
    ref_ids.push_back(overlay.image_id);
  }


  // Create ImageOverlay

  std::vector<uint8_t> iovl_data = overlayspec.write();

  // Create IOVL Item

  heif_item_id iovl_id = m_heif_file->add_new_image("iovl");
  std::shared_ptr<ImageItem_Overlay> iovl_image = std::make_shared<ImageItem_Overlay>(this, iovl_id);
  m_all_images.insert(std::make_pair(iovl_id, iovl_image));
  const int construction_method = 1; // 0=mdat 1=idat
  m_heif_file->append_iloc_data(iovl_id, iovl_data, construction_method);

  // Connect images to overlay
  m_heif_file->add_iref_reference(iovl_id, fourcc("dimg"), ref_ids);

  // Add ISPE property
  m_heif_file->add_ispe_property(iovl_id, overlayspec.get_canvas_width(), overlayspec.get_canvas_height());

  // Add PIXI property (copy from first image) - According to MIAF, all images shall have the same color information.
  auto pixi = m_heif_file->get_property<Box_pixi>(ref_ids[0]);
  m_heif_file->add_property(iovl_id, pixi, true);

  // Set Brands
  //m_heif_file->set_brand(encoder->plugin->compression_format,
  //                       out_grid_image->is_miaf_compatible());

  return iovl_image;
}


Result<std::shared_ptr<ImageItem_Tild>> HeifContext::add_tild_item(const heif_tild_image_parameters* parameters)
{
  return ImageItem_Tild::add_new_tild_item(this, parameters);
}


Error HeifContext::add_tild_image_tile(heif_item_id tild_id, uint32_t tile_x, uint32_t tile_y,
                                       const std::shared_ptr<HeifPixelImage>& image,
                                       struct heif_encoder* encoder)
{
  auto item = ImageItem::alloc_for_compression_format(this, encoder->plugin->compression_format);

  heif_encoding_options* options = heif_encoding_options_alloc();

  Result<std::shared_ptr<HeifPixelImage>> colorConversionResult = item->convert_colorspace_for_encoding(image, encoder, *options);
  if (colorConversionResult.error) {
    return colorConversionResult.error;
  }

  std::shared_ptr<HeifPixelImage> colorConvertedImage = colorConversionResult.value;

  Result<ImageItem::CodedImageData> encodeResult = item->encode_to_bitstream_and_boxes(colorConvertedImage, encoder, *options, heif_image_input_class_normal); // TODO (other than JPEG)
  heif_encoding_options_free(options);

  if (encodeResult.error) {
    return encodeResult.error;
  }

  const int construction_method = 0; // 0=mdat 1=idat
  m_heif_file->append_iloc_data(tild_id, encodeResult.value.bitstream, construction_method);

  auto imgItem = get_image(tild_id);
  auto tildImg = std::dynamic_pointer_cast<ImageItem_Tild>(imgItem);
  if (!tildImg) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value, "item ID for add_tild_image_tile() is no 'tild' image."};
  }

  auto& header = tildImg->get_tild_header();

  uint64_t offset = tildImg->get_next_tild_position();
  size_t dataSize = encodeResult.value.bitstream.size();
  if (dataSize > 0xFFFFFFFF) {
    return {heif_error_Encoding_error, heif_suberror_Unspecified, "Compressed tile size exceeds maximum tile size."};
  }
  header.set_tild_tile_range(tile_x, tile_y, offset, static_cast<uint32_t>(dataSize));
  tildImg->set_next_tild_position(offset + encodeResult.value.bitstream.size());

  std::vector<std::shared_ptr<Box>> existing_properties;
  Error err = m_heif_file->get_properties(tild_id, existing_properties);
  if (err) {
    return err;
  }

  for (auto& propertyBox : encodeResult.value.properties) {
    if (propertyBox->get_short_type() == fourcc("ispe")) {
      continue;
    }

    // skip properties that exist already

    bool exists = std::any_of(existing_properties.begin(),
                              existing_properties.end(),
                              [&propertyBox](const std::shared_ptr<Box>& p) { return p->get_short_type() == propertyBox->get_short_type();});
    if (exists) {
      continue;
    }

    m_heif_file->add_property(tild_id, propertyBox, propertyBox->is_essential());
  }

  m_heif_file->set_brand(encoder->plugin->compression_format,
                         true); // TODO: out_grid_image->is_miaf_compatible());

  return Error::Ok;
}


void HeifContext::set_primary_image(const std::shared_ptr<ImageItem>& image)
{
  // update heif context

  if (m_primary_image) {
    m_primary_image->set_primary(false);
  }

  image->set_primary(true);
  m_primary_image = image;


  // update pitm box in HeifFile

  m_heif_file->set_primary_item_id(image->get_id());
}


Error HeifContext::assign_thumbnail(const std::shared_ptr<ImageItem>& master_image,
                                    const std::shared_ptr<ImageItem>& thumbnail_image)
{
  m_heif_file->add_iref_reference(thumbnail_image->get_id(),
                                  fourcc("thmb"), {master_image->get_id()});

  return Error::Ok;
}


Error HeifContext::encode_thumbnail(const std::shared_ptr<HeifPixelImage>& image,
                                    struct heif_encoder* encoder,
                                    const struct heif_encoding_options& options,
                                    int bbox_size,
                                    std::shared_ptr<ImageItem>& out_thumbnail_handle)
{
  Error error;

  int orig_width = image->get_width();
  int orig_height = image->get_height();

  int thumb_width, thumb_height;

  if (orig_width <= bbox_size && orig_height <= bbox_size) {
    // original image is smaller than thumbnail size -> do not encode any thumbnail

    out_thumbnail_handle.reset();
    return Error::Ok;
  }
  else if (orig_width > orig_height) {
    thumb_height = orig_height * bbox_size / orig_width;
    thumb_width = bbox_size;
  }
  else {
    thumb_width = orig_width * bbox_size / orig_height;
    thumb_height = bbox_size;
  }


  // round size to even width and height

  thumb_width &= ~1;
  thumb_height &= ~1;


  std::shared_ptr<HeifPixelImage> thumbnail_image;
  error = image->scale_nearest_neighbor(thumbnail_image, thumb_width, thumb_height);
  if (error) {
    return error;
  }

  error = encode_image(thumbnail_image,
                       encoder, options,
                       heif_image_input_class_thumbnail,
                       out_thumbnail_handle);
  if (error) {
    return error;
  }

  return error;
}


Error HeifContext::add_exif_metadata(const std::shared_ptr<ImageItem>& master_image, const void* data, int size)
{
  // find location of TIFF header
  uint32_t offset = 0;
  const char* tiffmagic1 = "MM\0*";
  const char* tiffmagic2 = "II*\0";
  while (offset + 4 < (unsigned int) size) {
    if (!memcmp((uint8_t*) data + offset, tiffmagic1, 4)) break;
    if (!memcmp((uint8_t*) data + offset, tiffmagic2, 4)) break;
    offset++;
  }
  if (offset >= (unsigned int) size) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Invalid_parameter_value,
                 "Could not find location of TIFF header in Exif metadata.");
  }


  std::vector<uint8_t> data_array;
  data_array.resize(size + 4);
  data_array[0] = (uint8_t) ((offset >> 24) & 0xFF);
  data_array[1] = (uint8_t) ((offset >> 16) & 0xFF);
  data_array[2] = (uint8_t) ((offset >> 8) & 0xFF);
  data_array[3] = (uint8_t) ((offset) & 0xFF);
  memcpy(data_array.data() + 4, data, size);


  return add_generic_metadata(master_image,
                              data_array.data(), (int) data_array.size(),
                              "Exif", nullptr, nullptr, heif_metadata_compression_off, nullptr);
}


Error HeifContext::add_XMP_metadata(const std::shared_ptr<ImageItem>& master_image, const void* data, int size,
                                    heif_metadata_compression compression)
{
  return add_generic_metadata(master_image, data, size, "mime", "application/rdf+xml", nullptr, compression, nullptr);
}


Error HeifContext::add_generic_metadata(const std::shared_ptr<ImageItem>& master_image, const void* data, int size,
                                        const char* item_type, const char* content_type, const char* item_uri_type, heif_metadata_compression compression,
                                        heif_item_id* out_item_id)
{
  // create an infe box describing what kind of data we are storing (this also creates a new ID)

  auto metadata_infe_box = m_heif_file->add_new_infe_box(item_type);
  metadata_infe_box->set_hidden_item(true);
  if (content_type != nullptr) {
    metadata_infe_box->set_content_type(content_type);
  }

  heif_item_id metadata_id = metadata_infe_box->get_item_ID();
  if (out_item_id) {
    *out_item_id = metadata_id;
  }


  // we assign this data to the image

  m_heif_file->add_iref_reference(metadata_id,
                                  fourcc("cdsc"), {master_image->get_id()});


  // --- metadata compression

  if (compression == heif_metadata_compression_auto) {
    compression = heif_metadata_compression_off; // currently, we don't use header compression by default
  }

  // only set metadata compression for MIME type data which has 'content_encoding' field
  if (compression != heif_metadata_compression_off &&
      strcmp(item_type, "mime") != 0) {
    // TODO: error, compression not supported
  }


  std::vector<uint8_t> data_array;
  if (compression == heif_metadata_compression_zlib) {
#if HAVE_ZLIB
    data_array = compress_zlib((const uint8_t*) data, size);
    metadata_infe_box->set_content_encoding("compress_zlib");
#else
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_header_compression_method);
#endif
  }
  else if (compression == heif_metadata_compression_deflate) {
#if HAVE_ZLIB
    data_array = compress_zlib((const uint8_t*) data, size);
    metadata_infe_box->set_content_encoding("deflate");
#else
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_header_compression_method);
#endif
  }
  else {
    // uncompressed data, plain copy

    data_array.resize(size);
    memcpy(data_array.data(), data, size);
  }

  // copy the data into the file, store the pointer to it in an iloc box entry

  m_heif_file->append_iloc_data(metadata_id, data_array, 0);

  return Error::Ok;
}


heif_property_id HeifContext::add_property(heif_item_id targetItem, std::shared_ptr<Box> property, bool essential)
{
  heif_property_id id = m_heif_file->add_property(targetItem, property, essential);

  return id;
}


Result<heif_item_id> HeifContext::add_pyramid_group(uint16_t tile_size_x, uint16_t tile_size_y,
                                                    std::vector<heif_pyramid_layer_info> in_layers)
{
  auto pymd = std::make_shared<Box_pymd>();
  std::vector<Box_pymd::LayerInfo> layers;
  std::vector<heif_item_id> ids;

  for (const auto& l : in_layers) {
    if (l.tiles_in_layer_row==0 || l.tiles_in_layer_column==0 ||
        l.tiles_in_layer_row - 1 > 0xFFFF || l.tiles_in_layer_column - 1 > 0xFFFF) {

      return {Error(heif_error_Invalid_input,
                    heif_suberror_Invalid_parameter_value,
                    "Invalid number of tiles in layer.")};
    }

    Box_pymd::LayerInfo layer{};
    layer.layer_binning = l.layer_binning;
    layer.tiles_in_layer_row_minus1 = static_cast<uint16_t>(l.tiles_in_layer_row - 1);
    layer.tiles_in_layer_column_minus1 = static_cast<uint16_t>(l.tiles_in_layer_column - 1);
    layers.push_back(layer);
    ids.push_back(l.layer_image_id);
  }

  heif_item_id group_id = m_heif_file->get_unused_item_id();

  pymd->set_group_id(group_id);
  pymd->set_layers(tile_size_x, tile_size_y, layers, ids);

  m_heif_file->add_entity_group_box(pymd);

  // add back-references to base image

  for (size_t i = 0; i < ids.size() - 1; i++) {
    m_heif_file->add_iref_reference(ids[i], fourcc("base"), {ids.back()});
  }

  return {group_id};
}
