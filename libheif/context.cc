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

#include "libheif/box.h"
#include "libheif/error.h"
#include "libheif/heif.h"
#include "libheif/region.h"
#include <cstdint>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <limits>
#include <cmath>
#include <deque>

#if ENABLE_PARALLEL_TILE_DECODING
#include <future>
#endif

#include "context.h"
#include "file.h"
#include "pixelimage.h"
#include "api_structs.h"
#include "security_limits.h"
#include "hevc.h"
#include "avif.h"
#include "jpeg.h"
#include "plugin_registry.h"
#include "libheif/color-conversion/colorconversion.h"
#include "mask_image.h"
#include "metadata_compression.h"
#include "jpeg2000.h"

#if WITH_UNCOMPRESSED_CODEC
#include "uncompressed_image.h"
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
    assert(m_rows <= 256);
    return m_rows;
  }

  uint16_t get_columns() const
  {
    assert(m_columns <= 256);
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


Error ImageGrid::parse(const std::vector<uint8_t>& data)
{
  if (data.size() < 8) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Invalid_grid_data,
                 "Less than 8 bytes of data");
  }

  uint8_t version = data[0];
  (void) version; // version is unused

  uint8_t flags = data[1];
  int field_size = ((flags & 1) ? 32 : 16);

  m_rows = static_cast<uint16_t>(data[2] + 1);
  m_columns = static_cast<uint16_t>(data[3] + 1);

  if (field_size == 32) {
    if (data.size() < 12) {
      return Error(heif_error_Invalid_input,
                   heif_suberror_Invalid_grid_data,
                   "Grid image data incomplete");
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


class ImageOverlay
{
public:
  Error parse(size_t num_images, const std::vector<uint8_t>& data);

  std::string dump() const;

  void get_background_color(uint16_t col[4]) const;

  uint32_t get_canvas_width() const { return m_width; }

  uint32_t get_canvas_height() const { return m_height; }

  size_t get_num_offsets() const { return m_offsets.size(); }

  void get_offset(size_t image_index, int32_t* x, int32_t* y) const;

private:
  uint8_t m_version;
  uint8_t m_flags;
  uint16_t m_background_color[4];
  uint32_t m_width;
  uint32_t m_height;

  struct Offset
  {
    int32_t x, y;
  };

  std::vector<Offset> m_offsets;
};


Error ImageOverlay::parse(size_t num_images, const std::vector<uint8_t>& data)
{
  Error eofError(heif_error_Invalid_input,
                 heif_suberror_Invalid_grid_data,
                 "Overlay image data incomplete");

  if (data.size() < 2 + 4 * 2) {
    return eofError;
  }

  m_version = data[0];
  m_flags = data[1];

  if (m_version != 0) {
    std::stringstream sstr;
    sstr << "Overlay image data version " << ((int) m_version) << " is not implemented yet";

    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 sstr.str());
  }

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

  m_offsets.resize(num_images);

  for (size_t i = 0; i < num_images; i++) {
    m_offsets[i].x = readvec_signed(data, ptr, field_len);
    m_offsets[i].y = readvec_signed(data, ptr, field_len);
  }

  return Error::Ok;
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

  for (const Offset& offset : m_offsets) {
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


HeifContext::HeifContext()
{
  m_maximum_image_width_limit = MAX_IMAGE_WIDTH;
  m_maximum_image_height_limit = MAX_IMAGE_HEIGHT;

  reset_to_empty_heif();
}

HeifContext::~HeifContext()
{
  // Break circular references between Images (when a faulty input image has circular image references)
  for (auto& it : m_all_images) {
    std::shared_ptr<Image> image = it.second;
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

    m_heif_file->append_iloc_data(region->item_id, data_array);
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
          item_type == "iden" ||
          item_type == "iovl" ||
          item_type == "av01" ||
          item_type == "unci" ||
          item_type == "vvc1" ||
          item_type == "jpeg" ||
          (item_type == "mime" && content_type == "image/jpeg") ||
          item_type == "j2k1" ||
          item_type == "mski");
}


void HeifContext::remove_top_level_image(const std::shared_ptr<Image>& image)
{
  std::vector<std::shared_ptr<Image>> new_list;

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

    if (item_type_is_image(infe_box->get_item_type(), infe_box->get_content_type())) {
      auto image = std::make_shared<Image>(this, id);
      m_all_images.insert(std::make_pair(id, image));

      if (!infe_box->is_hidden_item()) {
        if (id == m_heif_file->get_primary_image_ID()) {
          image->set_primary(true);
          m_primary_image = image;
        }

        m_top_level_images.push_back(image);
      }
    }
  }

  if (!m_primary_image) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Nonexisting_item_referenced,
                 "'pitm' box references a non-existing image");
  }


  // --- read through properties for each image and extract image resolutions
  // Note: this has to be executed before assigning the auxiliary images below because we will only
  // merge the alpha image with the main image when their resolutions are the same.

  for (auto& pair : m_all_images) {
    auto& image = pair.second;

    std::vector<std::shared_ptr<Box>> properties;

    Error err = m_heif_file->get_properties(pair.first, properties);
    if (err) {
      return err;
    }

    bool ispe_read = false;
    for (const auto& prop : properties) {
      auto ispe = std::dynamic_pointer_cast<Box_ispe>(prop);
      if (ispe) {
        uint32_t width = ispe->get_width();
        uint32_t height = ispe->get_height();


        // --- check whether the image size is "too large"

        if (width > m_maximum_image_width_limit ||
            height > m_maximum_image_height_limit) {
          std::stringstream sstr;
          sstr << "Image size " << width << "x" << height << " exceeds the maximum image size "
               << m_maximum_image_width_limit << "x" << m_maximum_image_height_limit << "\n";

          return Error(heif_error_Memory_allocation_error,
                       heif_suberror_Security_limit_exceeded,
                       sstr.str());
        }

        image->set_resolution(width, height);
        ispe_read = true;
      }

      if (ispe_read) {
        auto clap = std::dynamic_pointer_cast<Box_clap>(prop);
        if (clap) {
          image->set_resolution(clap->get_width_rounded(),
                                clap->get_height_rounded());
        }

        auto irot = std::dynamic_pointer_cast<Box_irot>(prop);
        if (irot) {
          if (irot->get_rotation() == 90 ||
              irot->get_rotation() == 270) {
            // swap width and height
            image->set_resolution(image->get_height(),
                                  image->get_width());
          }
        }
      }

      auto colr = std::dynamic_pointer_cast<Box_colr>(prop);
      if (colr) {
        auto profile = colr->get_color_profile();
        image->set_color_profile(profile);
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
          if (refs.size() != 1) {
            return Error(heif_error_Invalid_input,
                         heif_suberror_Unspecified,
                         "Too many thumbnail references");
          }

          image->set_is_thumbnail_of(refs[0]);

          auto master_iter = m_all_images.find(refs[0]);
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
          if (refs.size() != 1) {
            return Error(heif_error_Invalid_input,
                         heif_suberror_Unspecified,
                         "Too many auxiliary image references");
          }


          // alpha channel

          if (auxC_property->get_aux_type() == "urn:mpeg:avc:2015:auxid:1" ||   // HEIF (avc)
              auxC_property->get_aux_type() == "urn:mpeg:hevc:2015:auxid:1" ||  // HEIF (h265)
              auxC_property->get_aux_type() == "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha") { // MIAF

            auto master_iter = m_all_images.find(refs[0]);
            if (master_iter == m_all_images.end()) {
              return Error(heif_error_Invalid_input,
                           heif_suberror_Nonexisting_item_referenced,
                           "Non-existing alpha image referenced");
            }

            auto master_img = master_iter->second;

            if (image.get() == master_img.get()) {
              return Error(heif_error_Invalid_input,
                           heif_suberror_Nonexisting_item_referenced,
                           "Recursive alpha image detected");
            }


            if (image->get_width() == master_img->get_width() &&
                image->get_height() == master_img->get_height()) {

              image->set_is_alpha_channel_of(refs[0], true);
              master_img->set_alpha_channel(image);
            }
          }


          // depth channel

          if (auxC_property->get_aux_type() == "urn:mpeg:hevc:2015:auxid:2" || // HEIF
              auxC_property->get_aux_type() == "urn:mpeg:mpegB:cicp:systems:auxiliary:depth") { // AVIF
            image->set_is_depth_channel_of(refs[0]);

            auto master_iter = m_all_images.find(refs[0]);
            if (master_iter == m_all_images.end()) {
              return Error(heif_error_Invalid_input,
                           heif_suberror_Nonexisting_item_referenced,
                           "Non-existing depth image referenced");
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


          // --- generic aux image

          image->set_is_aux_image_of(refs[0], auxC_property->get_aux_type());

          auto master_iter = m_all_images.find(refs[0]);
          if (master_iter == m_all_images.end()) {
            return Error(heif_error_Invalid_input,
                         heif_suberror_Nonexisting_item_referenced,
                         "Non-existing aux image referenced");
          }
          if (image.get() == master_iter->second.get()) {
            return Error(heif_error_Invalid_input,
                         heif_suberror_Nonexisting_item_referenced,
                         "Recursive aux image detected");
          }

          master_iter->second->add_aux_image(image);

          remove_top_level_image(image);
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
    // skip region annotations, handled next
    if (item_type == "rgan") {
      continue;
    }
    std::string content_type = m_heif_file->get_content_type(id);

    std::string item_uri_type = m_heif_file->get_item_uri_type(id);

    // we now assign all kinds of metadata to the image, not only 'Exif' and 'XMP'

    std::shared_ptr<ImageMetadata> metadata = std::make_shared<ImageMetadata>();
    metadata->item_id = id;
    metadata->item_type = item_type;
    metadata->content_type = content_type;
    metadata->item_uri_type = item_uri_type;

    Error err = m_heif_file->get_compressed_image_data(id, &(metadata->m_data));
    if (err) {
      return err;
    }

    //std::cerr.write((const char*)data.data(), data.size());


    // --- assign metadata to the image

    if (iref_box) {
      std::vector<Box_iref::Reference> references = iref_box->get_references_from(id);
      for (const auto& ref : references) {
        if (ref.header.get_short_type() == fourcc("cdsc")) {
          std::vector<uint32_t> refs = ref.to_item_ID;
          if (refs.size() != 1) {
            return Error(heif_error_Invalid_input,
                         heif_suberror_Unspecified,
                         "Metadata not correctly assigned to image");
          }

          uint32_t exif_image_id = refs[0];
          auto img_iter = m_all_images.find(exif_image_id);
          if (img_iter == m_all_images.end()) {
            return Error(heif_error_Invalid_input,
                         heif_suberror_Nonexisting_item_referenced,
                         "Metadata assigned to non-existing image");
          }

          img_iter->second->add_metadata(metadata);
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
    if (item_type == "rgan") {
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
            if (refs.size() != 1) {
              return Error(heif_error_Invalid_input,
                           heif_suberror_Unspecified,
                           "Region item not correctly assigned to image");
            }
            uint32_t image_id = refs[0];
            auto img_iter = m_all_images.find(image_id);
            if (img_iter == m_all_images.end()) {
              return Error(heif_error_Invalid_input,
                           heif_suberror_Nonexisting_item_referenced,
                           "Region item assigned to non-existing image");
            }
            img_iter->second->add_region_item_id(id);
            m_region_items.push_back(region_item);
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
  }

  return Error::Ok;
}


HeifContext::Image::Image(HeifContext* context, heif_item_id id)
    : m_heif_context(context),
      m_id(id)
{
  memset(&m_depth_representation_info, 0, sizeof(m_depth_representation_info));
}

HeifContext::Image::~Image() = default;

bool HeifContext::is_image(heif_item_id ID) const
{
  for (const auto& img : m_all_images) {
    if (img.first == ID)
      return true;
  }

  return false;
}


bool HeifContext::has_alpha(heif_item_id ID) const
{

  assert(is_image(ID));
  auto img = m_all_images.find(ID)->second;

  // --- has the image an auxiliary alpha image?

  if (img->get_alpha_channel() != nullptr) {
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

      const std::shared_ptr<Image> tileImg = iter->second;

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


int HeifContext::Image::get_ispe_width() const
{
  auto ispe = m_heif_context->m_heif_file->get_property<Box_ispe>(m_id);
  if (!ispe) {
    return 0;
  }
  else {
    return ispe->get_width();
  }
}


int HeifContext::Image::get_ispe_height() const
{
  auto ispe = m_heif_context->m_heif_file->get_property<Box_ispe>(m_id);
  if (!ispe) {
    return 0;
  }
  else {
    return ispe->get_height();
  }
}


Error HeifContext::Image::get_preferred_decoding_colorspace(heif_colorspace* out_colorspace, heif_chroma* out_chroma) const
{
  heif_item_id id;
  Error err = m_heif_context->get_id_of_non_virtual_child_image(m_id, id);
  if (err) {
    return err;
  }

  auto pixi = m_heif_context->m_heif_file->get_property<Box_pixi>(id);
  if (pixi && pixi->get_num_channels() == 1) {
    *out_colorspace = heif_colorspace_monochrome;
    *out_chroma = heif_chroma_monochrome;
    return err;
  }

  auto nclx = get_color_profile_nclx();
  if (nclx && nclx->get_matrix_coefficients() == 0) {
    *out_colorspace = heif_colorspace_RGB;
    *out_chroma = heif_chroma_444;
    return err;
  }

  // TODO: this should be codec specific. JPEG 2000, for example, can use RGB internally.

  *out_colorspace = heif_colorspace_YCbCr;
  *out_chroma = heif_chroma_undefined;

  if (auto hvcC = m_heif_context->m_heif_file->get_property<Box_hvcC>(id)) {
    *out_chroma = (heif_chroma)(hvcC->get_configuration().chroma_format);
  }
  else if (auto av1C = m_heif_context->m_heif_file->get_property<Box_av1C>(id)) {
    *out_chroma = (heif_chroma)(av1C->get_configuration().get_heif_chroma());
  }
  else if (auto j2kH = m_heif_context->m_heif_file->get_property<Box_j2kH>(id)) {
    JPEG2000MainHeader jpeg2000Header;
    err = jpeg2000Header.parseHeader(*m_heif_context->m_heif_file, id);
    if (err) {
      return err;
    }
    *out_chroma = jpeg2000Header.get_chroma_format();
  }

  return err;
}


int HeifContext::Image::get_luma_bits_per_pixel() const
{
  heif_item_id id;
  Error err = m_heif_context->get_id_of_non_virtual_child_image(m_id, id);
  if (err) {
    return -1;
  }

  // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
  return m_heif_context->m_heif_file->get_luma_bits_per_pixel_from_configuration(id);
}


int HeifContext::Image::get_chroma_bits_per_pixel() const
{
  heif_item_id id;
  Error err = m_heif_context->get_id_of_non_virtual_child_image(m_id, id);
  if (err) {
    return -1;
  }

  // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
  return m_heif_context->m_heif_file->get_chroma_bits_per_pixel_from_configuration(id);
}


Error HeifContext::decode_image_user(heif_item_id ID,
                                     std::shared_ptr<HeifPixelImage>& img,
                                     heif_colorspace out_colorspace,
                                     heif_chroma out_chroma,
                                     const struct heif_decoding_options& options) const
{
  Error err = decode_image_planar(ID, img, out_colorspace, options, false);
  if (err) {
    return err;
  }

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

  return Error::Ok;
}


Error HeifContext::decode_image_planar(heif_item_id ID,
                                       std::shared_ptr<HeifPixelImage>& img,
                                       heif_colorspace out_colorspace,
                                       const struct heif_decoding_options& options, bool alphaImage) const
{
  std::string image_type = m_heif_file->get_item_type(ID);

  std::shared_ptr<Image> imginfo;
  if (m_all_images.find(ID) != m_all_images.end()) {
    imginfo = m_all_images.find(ID)->second;
  }

  // Note: this may happen, for example when an 'iden' image references a non-existing image item.
  if (imginfo == nullptr) {
    return Error(heif_error_Invalid_input, heif_suberror_Nonexisting_item_referenced);
  }

  Error error;


  // --- decode image, depending on its type

  if (image_type == "hvc1" ||
      image_type == "av01" ||
      image_type == "j2k1" ||
      image_type == "jpeg" ||
      (image_type == "mime" && m_heif_file->get_content_type(ID) == "image/jpeg")) {

    heif_compression_format compression = heif_compression_undefined;
    if (image_type == "hvc1") {
      compression = heif_compression_HEVC;
    }
    else if (image_type == "av01") {
      compression = heif_compression_AV1;
    }
    else if (image_type == "jpeg" ||
             (image_type == "mime" && m_heif_file->get_content_type(ID) == "image/jpeg")) {
      compression = heif_compression_JPEG;
    }
    else if (image_type == "j2k1") {
      compression = heif_compression_JPEG2000;
    }

    const struct heif_decoder_plugin* decoder_plugin = get_decoder(compression, options.decoder_id);
    if (!decoder_plugin) {
      return Error(heif_error_Plugin_loading_error, heif_suberror_No_matching_decoder_installed);
    }

    std::vector<uint8_t> data;
    error = m_heif_file->get_compressed_image_data(ID, &data);
    if (error) {
      return error;
    }

    void* decoder;
    struct heif_error err = decoder_plugin->new_decoder(&decoder);
    if (err.code != heif_error_Ok) {
      return Error(err.code, err.subcode, err.message);
    }

    if (decoder_plugin->plugin_api_version >= 2) {
      if (decoder_plugin->set_strict_decoding) {
        decoder_plugin->set_strict_decoding(decoder, options.strict_decoding);
      }
    }

    err = decoder_plugin->push_data(decoder, data.data(), data.size());
    if (err.code != heif_error_Ok) {
      decoder_plugin->free_decoder(decoder);
      return Error(err.code, err.subcode, err.message);
    }

    //std::shared_ptr<HeifPixelImage>* decoded_img;

    heif_image* decoded_img = nullptr;

    err = decoder_plugin->decode_image(decoder, &decoded_img);
    if (err.code != heif_error_Ok) {
      decoder_plugin->free_decoder(decoder);
      return Error(err.code, err.subcode, err.message);
    }

    if (!decoded_img) {
      // TODO(farindk): The plugin should return an error in this case.
      decoder_plugin->free_decoder(decoder);
      return Error(heif_error_Decoder_plugin_error, heif_suberror_Unspecified);
    }

    img = std::move(decoded_img->image);
    heif_image_release(decoded_img);

    decoder_plugin->free_decoder(decoder);



    // --- convert to output chroma format

    // If there is an NCLX profile in the HEIF/AVIF metadata, use this for the color conversion.
    // Otherwise, use the profile that is stored in the image stream itself and then set the
    // (non-NCLX) profile later.
    auto nclx = imginfo->get_color_profile_nclx();
    if (nclx) {
      img->set_color_profile_nclx(nclx);
    }

    auto icc = imginfo->get_color_profile_icc();
    if (icc) {
      img->set_color_profile_icc(icc);
    }

    if (alphaImage) {
      // no color conversion required
    }
    else {
      heif_colorspace target_colorspace = (out_colorspace == heif_colorspace_undefined ?
                                           img->get_colorspace() :
                                           out_colorspace);

      if (!alphaImage && target_colorspace == heif_colorspace_YCbCr) {
        target_colorspace = heif_colorspace_RGB;
      }

      heif_chroma target_chroma = (target_colorspace == heif_colorspace_monochrome ?
                                   heif_chroma_monochrome : heif_chroma_444);

      bool different_chroma = (target_chroma != img->get_chroma_format());
      bool different_colorspace = (target_colorspace != img->get_colorspace());

      if (different_chroma || different_colorspace) {
        img = convert_colorspace(img, target_colorspace, target_chroma, nullptr, 0, options.color_conversion_options);
        if (!img) {
          return Error(heif_error_Unsupported_feature, heif_suberror_Unsupported_color_conversion);
        }
      }
    }
  }
  else if (image_type == "grid") {
    std::vector<uint8_t> data;
    error = m_heif_file->get_compressed_image_data(ID, &data);
    if (error) {
      return error;
    }

    error = decode_full_grid_image(ID, img, data, options);
    if (error) {
      return error;
    }
  }
  else if (image_type == "iden") {
    error = decode_derived_image(ID, img, options);
    if (error) {
      return error;
    }
  }
  else if (image_type == "iovl") {
    std::vector<uint8_t> data;
    error = m_heif_file->get_compressed_image_data(ID, &data);
    if (error) {
      return error;
    }

    error = decode_overlay_image(ID, img, data, options);
    if (error) {
      return error;
    }
#if WITH_UNCOMPRESSED_CODEC
  }
  else if (image_type == "unci") {
    std::vector<uint8_t> data;
    error = m_heif_file->get_compressed_image_data(ID, &data);
    if (error) {
      return error;
    }
    error = UncompressedImageCodec::decode_uncompressed_image(m_heif_file,
                                                              ID,
                                                              img,
                                                              m_maximum_image_width_limit,
                                                              m_maximum_image_height_limit,
                                                              data);
    if (error) {
      return error;
    }
#endif
  }
  else if (image_type == "mski") {
    std::vector<uint8_t> data;
    error = m_heif_file->get_compressed_image_data(ID, &data);
    if (error) {
      std::cout << "mski error 1" << std::endl;
      return error;
    }
    error = MaskImageCodec::decode_mask_image(m_heif_file,
                                              ID,
                                              img,
                                              m_maximum_image_width_limit,
                                              m_maximum_image_height_limit,
                                              data);
    if (error) {
      return error;
    }
  }
  else {
    // Should not reach this, was already rejected by "get_image_data".
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_image_type);
  }



  // --- apply image transformations

  if (options.ignore_transformations == false) {
    std::vector<std::shared_ptr<Box>> properties;
    auto ipco_box = m_heif_file->get_ipco_box();
    auto ipma_box = m_heif_file->get_ipma_box();
    error = ipco_box->get_properties_for_item_ID(ID, ipma_box, properties);

    for (const auto& property : properties) {
      if (property->get_short_type() == fourcc("irot")) {
        auto rot = std::dynamic_pointer_cast<Box_irot>(property);
        std::shared_ptr<HeifPixelImage> rotated_img;
        error = img->rotate_ccw(rot->get_rotation(), rotated_img);
        if (error) {
          return error;
        }

        img = rotated_img;
      }


      if (property->get_short_type() == fourcc("imir")) {
        auto mirror = std::dynamic_pointer_cast<Box_imir>(property);
        error = img->mirror_inplace(mirror->get_mirror_direction());
        if (error) {
          return error;
        }
      }


      if (property->get_short_type() == fourcc("clap")) {
        auto clap = std::dynamic_pointer_cast<Box_clap>(property);
        std::shared_ptr<HeifPixelImage> clap_img;

        int img_width = img->get_width();
        int img_height = img->get_height();
        assert(img_width >= 0);
        assert(img_height >= 0);

        int left = clap->left_rounded(img_width);
        int right = clap->right_rounded(img_width);
        int top = clap->top_rounded(img_height);
        int bottom = clap->bottom_rounded(img_height);

        if (left < 0) { left = 0; }
        if (top < 0) { top = 0; }

        if (right >= img_width) { right = img_width - 1; }
        if (bottom >= img_height) { bottom = img_height - 1; }

        if (left > right ||
            top > bottom) {
          return Error(heif_error_Invalid_input,
                       heif_suberror_Invalid_clean_aperture);
        }

        std::shared_ptr<HeifPixelImage> cropped_img;
        error = img->crop(left, right, top, bottom, cropped_img);
        if (error) {
          return error;
        }

        img = cropped_img;
      }
    }
  }


  // --- add alpha channel, if available

  // TODO: this if statement is probably wrong. When we have a tiled image with alpha
  // channel, then the alpha images should be associated with their respective tiles.
  // However, the tile images are not part of the m_all_images list.
  // Fix this, when we have a test image available.
  if (m_all_images.find(ID) != m_all_images.end()) {
    const auto imginfo = m_all_images.find(ID)->second;

    std::shared_ptr<Image> alpha_image = imginfo->get_alpha_channel();
    if (alpha_image) {
      std::shared_ptr<HeifPixelImage> alpha;
      Error err = decode_image_planar(alpha_image->get_id(), alpha,
                                      heif_colorspace_undefined, options, true);
      if (err) {
        return err;
      }

      // TODO: check that sizes are the same and that we have an Y channel
      // BUT: is there any indication in the standard that the alpha channel should have the same size?

      heif_channel channel;
      switch (alpha->get_colorspace()) {
        case heif_colorspace_YCbCr:
        case heif_colorspace_monochrome:
          channel = heif_channel_Y;
          break;
        case heif_colorspace_RGB:
          channel = heif_channel_R;
          break;
        case heif_colorspace_undefined:
        default:
          return Error(heif_error_Invalid_input,
                       heif_suberror_Unsupported_color_conversion);
      }

      img->transfer_plane_from_image_as(alpha, channel, heif_channel_Alpha);

      if (imginfo->is_premultiplied_alpha()) {
        img->set_premultiplied_alpha(true);
      }
    }
  }


  // --- attach metadata to image

  {
    auto ipco_box = m_heif_file->get_ipco_box();
    auto ipma_box = m_heif_file->get_ipma_box();

    // CLLI

    auto clli_box = ipco_box->get_property_for_item_ID(ID, ipma_box, fourcc("clli"));
    auto clli = std::dynamic_pointer_cast<Box_clli>(clli_box);

    if (clli) {
      img->set_clli(clli->clli);
    }

    // MDCV

    auto mdcv_box = ipco_box->get_property_for_item_ID(ID, ipma_box, fourcc("mdcv"));
    auto mdcv = std::dynamic_pointer_cast<Box_mdcv>(mdcv_box);

    if (mdcv) {
      img->set_mdcv(mdcv->mdcv);
    }

    // PASP

    auto pasp_box = ipco_box->get_property_for_item_ID(ID, ipma_box, fourcc("pasp"));
    auto pasp = std::dynamic_pointer_cast<Box_pasp>(pasp_box);

    if (pasp) {
      img->set_pixel_ratio(pasp->hSpacing, pasp->vSpacing);
    }
  }

  return Error::Ok;
}


// This function only works with RGB images.
Error HeifContext::decode_full_grid_image(heif_item_id ID,
                                          std::shared_ptr<HeifPixelImage>& img,
                                          const std::vector<uint8_t>& grid_data,
                                          const heif_decoding_options& options) const
{
  ImageGrid grid;
  Error err = grid.parse(grid_data);
  if (err) {
    return err;
  }

  //std::cout << grid.dump();


  auto iref_box = m_heif_file->get_iref_box();

  if (!iref_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_iref_box,
                 "No iref box available, but needed for grid image");
  }

  std::vector<heif_item_id> image_references = iref_box->get_references(ID, fourcc("dimg"));

  if ((int) image_references.size() != grid.get_rows() * grid.get_columns()) {
    std::stringstream sstr;
    sstr << "Tiled image with " << grid.get_rows() << "x" << grid.get_columns() << "="
         << (grid.get_rows() * grid.get_columns()) << " tiles, but only "
         << image_references.size() << " tile images in file";

    return Error(heif_error_Invalid_input,
                 heif_suberror_Missing_grid_images,
                 sstr.str());
  }


  // --- check that all image IDs are valid images

  for (heif_item_id tile_id : image_references) {
    if (!is_image(tile_id)) {
      std::stringstream sstr;
      sstr << "Tile image ID=" << tile_id << " is not a proper image.";

      return Error(heif_error_Invalid_input,
                   heif_suberror_Missing_grid_images,
                   sstr.str());
    }
  }


  auto ipma = m_heif_file->get_ipma_box();
  auto ipco = m_heif_file->get_ipco_box();
  auto pixi_box = ipco->get_property_for_item_ID(ID, ipma, fourcc("pixi"));
  auto pixi = std::dynamic_pointer_cast<Box_pixi>(pixi_box);

  const uint32_t w = grid.get_width();
  const uint32_t h = grid.get_height();


  // --- determine output image chroma size and make sure all tiles have same chroma

  assert(!image_references.empty());

  heif_chroma tile_chroma = heif_chroma_444;
  /* TODO: in the future, we might support RGB and mono as intermediate formats
  heif_chroma tile_chroma = m_heif_file->get_image_chroma_from_configuration(some_tile_id);
  if (tile_chroma != heif_chroma_monochrome) {
    tile_chroma = heif_chroma_RGB;
  }
  */

  // --- generate image of full output size

  if (w >= m_maximum_image_width_limit || h >= m_maximum_image_height_limit) {
    std::stringstream sstr;
    sstr << "Image size " << w << "x" << h << " exceeds the maximum image size "
         << m_maximum_image_width_limit << "x" << m_maximum_image_height_limit << "\n";

    return Error(heif_error_Memory_allocation_error,
                 heif_suberror_Security_limit_exceeded,
                 sstr.str());
  }


  img = std::make_shared<HeifPixelImage>();
  img->create(w, h,
              heif_colorspace_RGB,
              heif_chroma_444);

  int bpp = 0;

  if (pixi) {
    if (pixi->get_num_channels() < 1) {
      return Error(heif_error_Invalid_input,
                   heif_suberror_Invalid_pixi_box,
                   "No pixi information for luma channel.");
    }

    bpp = pixi->get_bits_per_channel(0);

    if (tile_chroma != heif_chroma_monochrome) {

      // there are broken files that save only a one-channel pixi for an RGB image (issue #283)
      if (pixi->get_num_channels() == 3) {

        int bpp_c1 = pixi->get_bits_per_channel(1);
        int bpp_c2 = pixi->get_bits_per_channel(2);

        if (bpp_c1 != bpp || bpp_c2 != bpp) {
          // TODO: is this really an error? Does the pixi depths refer to RGB or YCbCr?
          return Error(heif_error_Invalid_input,
                       heif_suberror_Invalid_pixi_box,
                       "Different number of bits per pixel in each channel.");
        }
      }
    }
  }
  else {
    // When there is no pixi-box, get the pixel-depth from one of the tile images

    heif_item_id tileID = image_references[0];

    auto iter = m_all_images.find(tileID);
    if (iter == m_all_images.end()) {
      return Error(heif_error_Invalid_input,
                   heif_suberror_Missing_grid_images,
                   "Nonexistent grid image referenced");
    }

    const std::shared_ptr<Image> tileImg = iter->second;
    bpp = tileImg->get_luma_bits_per_pixel();
  }

  if (bpp < 8 || bpp > 16) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Invalid_pixi_box,
                 "Invalid bits per pixel in pixi box.");
  }

  if (tile_chroma == heif_chroma_monochrome) {
    img->add_plane(heif_channel_Y, w, h, bpp);
  }
  else {
    img->add_plane(heif_channel_R, w, h, bpp);
    img->add_plane(heif_channel_G, w, h, bpp);
    img->add_plane(heif_channel_B, w, h, bpp);
  }

  int y0 = 0;
  int reference_idx = 0;

#if ENABLE_PARALLEL_TILE_DECODING
  // remember which tile to put where into the image
  struct tile_data
  {
    heif_item_id tileID;
    int x_origin, y_origin;
  };

  std::deque<tile_data> tiles;
  if (m_max_decoding_threads > 0)
    tiles.resize(grid.get_rows() * grid.get_columns());

  std::deque<std::future<Error> > errs;
#endif

  for (int y = 0; y < grid.get_rows(); y++) {
    int x0 = 0;
    int tile_height = 0;

    for (int x = 0; x < grid.get_columns(); x++) {

      heif_item_id tileID = image_references[reference_idx];

      auto iter = m_all_images.find(tileID);
      if (iter == m_all_images.end()) {
        return Error(heif_error_Invalid_input,
                     heif_suberror_Missing_grid_images,
                     "Nonexistent grid image referenced");
      }

      const std::shared_ptr<Image> tileImg = iter->second;
      int src_width = tileImg->get_width();
      int src_height = tileImg->get_height();

#if ENABLE_PARALLEL_TILE_DECODING
      if (m_max_decoding_threads > 0)
        tiles[x + y * grid.get_columns()] = tile_data{tileID, x0, y0};
      else
#else
        if (1)
#endif
      {
        Error err = decode_and_paste_tile_image(tileID, img, x0, y0, options);
        if (err) {
          return err;
        }
      }

      x0 += src_width;
      tile_height = src_height; // TODO: check that all tiles have the same height

      reference_idx++;
    }

    y0 += tile_height;
  }

#if ENABLE_PARALLEL_TILE_DECODING
  if (m_max_decoding_threads > 0) {
    // Process all tiles in a set of background threads.
    // Do not start more than the maximum number of threads.

    while (tiles.empty() == false) {

      // If maximum number of threads running, wait until first thread finishes

      if (errs.size() >= (size_t) m_max_decoding_threads) {
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
                                &HeifContext::decode_and_paste_tile_image, this,
                                data.tileID, img, data.x_origin, data.y_origin, options));
    }

    // check for decoding errors in remaining tiles

    while (errs.empty() == false) {
      Error e = errs.front().get();
      if (e) {
        return e;
      }

      errs.pop_front();
    }
  }
#endif

  return Error::Ok;
}


Error HeifContext::decode_and_paste_tile_image(heif_item_id tileID,
                                               const std::shared_ptr<HeifPixelImage>& img,
                                               int x0, int y0,
                                               const heif_decoding_options& options) const
{
  std::shared_ptr<HeifPixelImage> tile_img;

  Error err = decode_image_planar(tileID, tile_img, img->get_colorspace(), options, false);
  if (err != Error::Ok) {
    return err;
  }

  const int w = img->get_width();
  const int h = img->get_height();


  // --- copy tile into output image

  int src_width = tile_img->get_width();
  int src_height = tile_img->get_height();
  assert(src_width >= 0);
  assert(src_height >= 0);

  heif_chroma chroma = img->get_chroma_format();

  if (chroma != tile_img->get_chroma_format()) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Wrong_tile_image_chroma_format,
                 "Image tile has different chroma format than combined image");
  }

  // --- add alpha plane if we discovered a tile with alpha

  if (tile_img->has_alpha() && !img->has_alpha()) {
#if ENABLE_PARALLEL_TILE_DECODING
    // The mutex should probably be a member of heif_context, but since this is so infrequently locked, it probably doesn't matter.
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    if (!img->has_channel(heif_channel_Alpha))  // check again, after locking
#endif
    {
      int alpha_bpp = tile_img->get_bits_per_pixel(heif_channel_Alpha);

      assert(alpha_bpp <= 16);

      uint16_t alpha_default_value = static_cast<uint16_t>((1UL << alpha_bpp) - 1UL);

      img->fill_new_plane(heif_channel_Alpha, alpha_default_value, w, h, alpha_bpp);
    }
  }

  std::set<enum heif_channel> channels = tile_img->get_channel_set();

  for (heif_channel channel : channels) {

    int tile_stride;
    uint8_t* tile_data = tile_img->get_plane(channel, &tile_stride);

    int out_stride;
    uint8_t* out_data = img->get_plane(channel, &out_stride);

    if (w <= x0 || h <= y0) {
      return Error(heif_error_Invalid_input,
                   heif_suberror_Invalid_grid_data);
    }

    if (img->get_bits_per_pixel(channel) != tile_img->get_bits_per_pixel(channel)) {
      return Error(heif_error_Invalid_input,
                   heif_suberror_Wrong_tile_image_pixel_depth);
    }

    int copy_width = std::min(src_width, w - x0);
    int copy_height = std::min(src_height, h - y0);

    copy_width *= tile_img->get_storage_bits_per_pixel(heif_channel_R) / 8;

    int xs = x0, ys = y0;
    xs *= tile_img->get_storage_bits_per_pixel(heif_channel_R) / 8;

    for (int py = 0; py < copy_height; py++) {
      memcpy(out_data + xs + (ys + py) * out_stride,
             tile_data + py * tile_stride,
             copy_width);
    }
  }

  return Error::Ok;
}


Error HeifContext::decode_derived_image(heif_item_id ID,
                                        std::shared_ptr<HeifPixelImage>& img,
                                        const heif_decoding_options& options) const
{
  // find the ID of the image this image is derived from

  auto iref_box = m_heif_file->get_iref_box();

  if (!iref_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_iref_box,
                 "No iref box available, but needed for iden image");
  }

  std::vector<heif_item_id> image_references = iref_box->get_references(ID, fourcc("dimg"));

  if ((int) image_references.size() != 1) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Unspecified,
                 "'iden' image with more than one reference image");
  }


  heif_item_id reference_image_id = image_references[0];

  if (reference_image_id == ID) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Unspecified,
                 "'iden' image referring to itself");
  }

  Error error = decode_image_planar(reference_image_id, img,
                                    heif_colorspace_RGB, options, false); // TODO: always RGB ?
  return error;
}


Error HeifContext::decode_overlay_image(heif_item_id ID,
                                        std::shared_ptr<HeifPixelImage>& img,
                                        const std::vector<uint8_t>& overlay_data,
                                        const heif_decoding_options& options) const
{
  // find the IDs this image is composed of

  auto iref_box = m_heif_file->get_iref_box();

  if (!iref_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_iref_box,
                 "No iref box available, but needed for iovl image");
  }

  std::vector<heif_item_id> image_references = iref_box->get_references(ID, fourcc("dimg"));

  /* TODO: probably, it is valid that an iovl image has no references ?

  if (image_references.empty()) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Missing_grid_images,
                 "'iovl' image with more than one reference image");
  }
  */


  ImageOverlay overlay;
  Error err = overlay.parse(image_references.size(), overlay_data);
  if (err) {
    return err;
  }

  if (image_references.size() != overlay.get_num_offsets()) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Invalid_overlay_data,
                 "Number of image offsets does not match the number of image references");
  }

  uint32_t w = overlay.get_canvas_width();
  uint32_t h = overlay.get_canvas_height();

  if (w >= m_maximum_image_width_limit || h >= m_maximum_image_height_limit) {
    std::stringstream sstr;
    sstr << "Image size " << w << "x" << h << " exceeds the maximum image size "
         << m_maximum_image_width_limit << "x" << m_maximum_image_height_limit << "\n";

    return Error(heif_error_Memory_allocation_error,
                 heif_suberror_Security_limit_exceeded,
                 sstr.str());
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
  overlay.get_background_color(bkg_color);

  err = img->fill_RGB_16bit(bkg_color[0], bkg_color[1], bkg_color[2], bkg_color[3]);
  if (err) {
    return err;
  }


  for (size_t i = 0; i < image_references.size(); i++) {
    std::shared_ptr<HeifPixelImage> overlay_img;
    err = decode_image_planar(image_references[i], overlay_img,
                              heif_colorspace_RGB, options, false); // TODO: always RGB? Probably yes, because of RGB background color.
    if (err != Error::Ok) {
      return err;
    }

    overlay_img = convert_colorspace(overlay_img, heif_colorspace_RGB, heif_chroma_444, nullptr, 0, options.color_conversion_options);
    if (!overlay_img) {
      return Error(heif_error_Unsupported_feature, heif_suberror_Unsupported_color_conversion);
    }

    int32_t dx, dy;
    overlay.get_offset(i, &dx, &dy);

    err = img->overlay(overlay_img, dx, dy);
    if (err) {
      if (err.error_code == heif_error_Invalid_input &&
          err.sub_error_code == heif_suberror_Overlay_image_outside_of_canvas) {
        // NOP, ignore this error

        err = Error::Ok;
      }
      else {
        return err;
      }
    }
  }

  return err;
}


static std::shared_ptr<HeifPixelImage>
create_alpha_image_from_image_alpha_channel(const std::shared_ptr<HeifPixelImage>& image)
{
  // --- generate alpha image

  std::shared_ptr<HeifPixelImage> alpha_image = std::make_shared<HeifPixelImage>();
  alpha_image->create(image->get_width(), image->get_height(),
                      heif_colorspace_monochrome, heif_chroma_monochrome);
  alpha_image->copy_new_plane_from(image, heif_channel_Alpha, heif_channel_Y);


  // --- set nclx profile with full-range flag

  auto nclx = std::make_shared<color_profile_nclx>();
  nclx->set_undefined();
  nclx->set_full_range_flag(true); // this is the default, but just to be sure in case the defaults change
  alpha_image->set_color_profile_nclx(nclx);

  return alpha_image;
}


void HeifContext::Image::set_preencoded_hevc_image(const std::vector<uint8_t>& data)
{
  m_heif_context->m_heif_file->add_hvcC_property(m_id);


  // --- parse the h265 stream and set hvcC headers and compressed image data

  int state = 0;

  bool first = true;
  bool eof = false;

  int prev_start_code_start = -1; // init to an invalid value, will always be overwritten before use
  int start_code_start;
  int ptr = 0;

  for (;;) {
    bool dump_nal = false;

    uint8_t c = data[ptr++];

    if (state == 3) {
      state = 0;
    }

    if (c == 0 && state <= 1) {
      state++;
    }
    else if (c == 0) {
      // NOP
    }
    else if (c == 1 && state == 2) {
      start_code_start = ptr - 3;
      dump_nal = true;
      state = 3;
    }
    else {
      state = 0;
    }

    if (ptr == (int) data.size()) {
      start_code_start = (int) data.size();
      dump_nal = true;
      eof = true;
    }

    if (dump_nal) {
      if (first) {
        first = false;
      }
      else {
        std::vector<uint8_t> nal_data;
        size_t length = start_code_start - (prev_start_code_start + 3);

        nal_data.resize(length);

        assert(prev_start_code_start >= 0);
        memcpy(nal_data.data(), data.data() + prev_start_code_start + 3, length);

        int nal_type = (nal_data[0] >> 1);

        switch (nal_type) {
          case 0x20:
          case 0x21:
          case 0x22:
            m_heif_context->m_heif_file->append_hvcC_nal_data(m_id, nal_data);
            /*hvcC->append_nal_data(nal_data);*/
            break;

          default: {
            std::vector<uint8_t> nal_data_with_size;
            nal_data_with_size.resize(nal_data.size() + 4);

            memcpy(nal_data_with_size.data() + 4, nal_data.data(), nal_data.size());
            nal_data_with_size[0] = ((nal_data.size() >> 24) & 0xFF);
            nal_data_with_size[1] = ((nal_data.size() >> 16) & 0xFF);
            nal_data_with_size[2] = ((nal_data.size() >> 8) & 0xFF);
            nal_data_with_size[3] = ((nal_data.size() >> 0) & 0xFF);

            m_heif_context->m_heif_file->append_iloc_data(m_id, nal_data_with_size);
          }
            break;
        }
      }

      prev_start_code_start = start_code_start;
    }

    if (eof) {
      break;
    }
  }
}


Error HeifContext::encode_image(const std::shared_ptr<HeifPixelImage>& pixel_image,
                                struct heif_encoder* encoder,
                                const struct heif_encoding_options& options,
                                enum heif_image_input_class input_class,
                                std::shared_ptr<Image>& out_image)
{
  Error error;

  // TODO: the hdlr box is not the right place for comments
  // m_heif_file->set_hdlr_library_info(encoder->plugin->get_plugin_name());

  switch (encoder->plugin->compression_format) {
    case heif_compression_HEVC: {
      error = encode_image_as_hevc(pixel_image,
                                   encoder,
                                   options,
                                   heif_image_input_class_normal,
                                   out_image);
    }
      break;

    case heif_compression_AV1: {
      error = encode_image_as_av1(pixel_image,
                                  encoder,
                                  options,
                                  heif_image_input_class_normal,
                                  out_image);
    }
      break;
    case heif_compression_JPEG2000: {
      error = encode_image_as_jpeg2000(pixel_image,
                                       encoder,
                                       options,
                                       heif_image_input_class_normal,
                                       out_image);
      }
      break;

    case heif_compression_JPEG: {
      error = encode_image_as_jpeg(pixel_image,
                                   encoder,
                                   options,
                                   heif_image_input_class_normal,
                                   out_image);
    }
      break;

    case heif_compression_uncompressed: {
      error = encode_image_as_uncompressed(pixel_image,
                                           encoder,
                                           options,
                                           heif_image_input_class_normal,
                                           out_image);
    }
      break;

    case heif_compression_mask: {
      error = encode_image_as_mask(pixel_image,
                                  encoder,
                                  options,
                                  heif_image_input_class_normal,
                                  out_image);
    }
      break;

    default:
      return Error(heif_error_Encoder_plugin_error, heif_suberror_Unsupported_codec);
  }

  m_heif_file->set_brand(encoder->plugin->compression_format,
                         out_image->is_miaf_compatible());

  return error;
}

/*
static uint32_t get_rotated_width(heif_orientation orientation, uint32_t w, uint32_t h)
{
  return ((int)orientation) > 4 ? h : w;
}


static uint32_t get_rotated_height(heif_orientation orientation, uint32_t w, uint32_t h)
{
  return ((int)orientation) > 4 ? w : h;
}
*/

void HeifContext::write_image_metadata(std::shared_ptr<HeifPixelImage> src_image, int image_id)
{
  auto colorspace = src_image->get_colorspace();
  auto chroma = src_image->get_chroma_format();


  // --- write PIXI property

  if (colorspace == heif_colorspace_monochrome) {
    m_heif_file->add_pixi_property(image_id,
                                   src_image->get_bits_per_pixel(heif_channel_Y), 0, 0);
  }
  else if (colorspace == heif_colorspace_YCbCr) {
    m_heif_file->add_pixi_property(image_id,
                                   src_image->get_bits_per_pixel(heif_channel_Y),
                                   src_image->get_bits_per_pixel(heif_channel_Cb),
                                   src_image->get_bits_per_pixel(heif_channel_Cr));
  }
  else if (colorspace == heif_colorspace_RGB) {
    if (chroma == heif_chroma_444) {
      m_heif_file->add_pixi_property(image_id,
                                     src_image->get_bits_per_pixel(heif_channel_R),
                                     src_image->get_bits_per_pixel(heif_channel_G),
                                     src_image->get_bits_per_pixel(heif_channel_B));
    }
    else if (chroma == heif_chroma_interleaved_RGB ||
             chroma == heif_chroma_interleaved_RGBA) {
      m_heif_file->add_pixi_property(image_id, 8, 8, 8);
    }
  }


  // --- write PASP property

  if (src_image->has_nonsquare_pixel_ratio()) {
    auto pasp = std::make_shared<Box_pasp>();
    src_image->get_pixel_ratio(&pasp->hSpacing, &pasp->vSpacing);

    int index = m_heif_file->get_ipco_box()->append_child_box(pasp);
    m_heif_file->get_ipma_box()->add_property_for_item_ID(image_id, Box_ipma::PropertyAssociation{false, uint16_t(index + 1)});
  }


  // --- write CLLI property

  if (src_image->has_clli()) {
    auto clli = std::make_shared<Box_clli>();
    clli->clli = src_image->get_clli();

    int index = m_heif_file->get_ipco_box()->append_child_box(clli);
    m_heif_file->get_ipma_box()->add_property_for_item_ID(image_id, Box_ipma::PropertyAssociation{false, uint16_t(index + 1)});
  }


  // --- write MDCV property

  if (src_image->has_mdcv()) {
    auto mdcv = std::make_shared<Box_mdcv>();
    mdcv->mdcv = src_image->get_mdcv();

    int index = m_heif_file->get_ipco_box()->append_child_box(mdcv);
    m_heif_file->get_ipma_box()->add_property_for_item_ID(image_id, Box_ipma::PropertyAssociation{false, uint16_t(index + 1)});
  }
}


static bool nclx_profile_matches_spec(heif_colorspace colorspace,
                                      std::shared_ptr<const color_profile_nclx> image_nclx,
                                      const struct heif_color_profile_nclx* spec_nclx)
{
  if (colorspace != heif_colorspace_YCbCr) {
    return true;
  }

  // Do target specification -> always matches
  if (!spec_nclx) {
    return true;
  }

  if (!image_nclx) {
    // if no input nclx is specified, compare against default one
    image_nclx = std::make_shared<color_profile_nclx>();
  }

  if (image_nclx->get_full_range_flag() != ( spec_nclx->full_range_flag == 0 ? false : true ) ) {
    return false;
  }

  if (image_nclx->get_matrix_coefficients() != spec_nclx->matrix_coefficients) {
    return false;
  }

  // TODO: are the colour primaries relevant for matrix-coefficients != 12,13 ?
  //       If not, we should skip this test for anything else than matrix-coefficients != 12,13.
  if (image_nclx->get_colour_primaries() != spec_nclx->color_primaries) {
    return false;
  }

  return true;
}


static std::shared_ptr<color_profile_nclx> compute_target_nclx_profile(const std::shared_ptr<HeifPixelImage>& image, const heif_color_profile_nclx* output_nclx_profile)
{
  auto target_nclx_profile = std::make_shared<color_profile_nclx>();

  // If there is an output NCLX specified, use that.
  if (output_nclx_profile) {
    target_nclx_profile->set_from_heif_color_profile_nclx(output_nclx_profile);
  }
  // Otherwise, if there is an input NCLX, keep that.
  else if (auto input_nclx = image->get_color_profile_nclx()) {
    *target_nclx_profile = *input_nclx;
  }
  // Otherwise, just use the defaults (set below)
  else {
    target_nclx_profile->set_undefined();
  }

  target_nclx_profile->replace_undefined_values_with_sRGB_defaults();

  return target_nclx_profile;
}


Error HeifContext::encode_image_as_hevc(const std::shared_ptr<HeifPixelImage>& image,
                                        struct heif_encoder* encoder,
                                        const struct heif_encoding_options& options,
                                        enum heif_image_input_class input_class,
                                        std::shared_ptr<Image>& out_image)
{
  heif_item_id image_id = m_heif_file->add_new_image("hvc1");
  out_image = std::make_shared<Image>(this, image_id);


  // --- check whether we have to convert the image color space

  heif_colorspace colorspace = image->get_colorspace();
  heif_chroma chroma = image->get_chroma_format();

  auto target_nclx_profile = compute_target_nclx_profile(image, options.output_nclx_profile);

  if (encoder->plugin->plugin_api_version >= 2) {
    encoder->plugin->query_input_colorspace2(encoder->encoder, &colorspace, &chroma);
  }
  else {
    encoder->plugin->query_input_colorspace(&colorspace, &chroma);
  }

  std::shared_ptr<HeifPixelImage> src_image;
  if (colorspace != image->get_colorspace() ||
      chroma != image->get_chroma_format() ||
      !nclx_profile_matches_spec(colorspace, image->get_color_profile_nclx(), options.output_nclx_profile)) {
    // @TODO: use color profile when converting
    int output_bpp = 0; // same as input
    src_image = convert_colorspace(image, colorspace, chroma, target_nclx_profile,
                                   output_bpp, options.color_conversion_options);
    if (!src_image) {
      return Error(heif_error_Unsupported_feature, heif_suberror_Unsupported_color_conversion);
    }
  }
  else {
    src_image = image;
  }


  int input_width = src_image->get_width(heif_channel_Y);
  int input_height = src_image->get_height(heif_channel_Y);

  out_image->set_size(input_width, input_height);


  m_heif_file->add_hvcC_property(image_id);


  heif_image c_api_image;
  c_api_image.image = src_image;

  struct heif_error err = encoder->plugin->encode_image(encoder->encoder, &c_api_image, input_class);
  if (err.code) {
    return Error(err.code,
                 err.subcode,
                 err.message);
  }

  int encoded_width = 0;
  int encoded_height = 0;

  for (;;) {
    uint8_t* data;
    int size;

    encoder->plugin->get_compressed_data(encoder->encoder, &data, &size, NULL);

    if (data == NULL) {
      break;
    }


    const uint8_t NAL_SPS = 33;

    if ((data[0] >> 1) == NAL_SPS) {
      Box_hvcC::configuration config;

      parse_sps_for_hvcC_configuration(data, size, &config, &encoded_width, &encoded_height);

      m_heif_file->set_hvcC_configuration(image_id, config);
    }

    switch (data[0] >> 1) {
      case 0x20:
      case 0x21:
      case 0x22:
        m_heif_file->append_hvcC_nal_data(image_id, data, size);
        break;

      default:
        m_heif_file->append_iloc_data_with_4byte_size(image_id, data, size);
    }
  }

  if (!encoded_width || !encoded_height) {
    return Error(heif_error_Encoder_plugin_error,
                 heif_suberror_Invalid_image_size);
  }

  if (encoder->plugin->plugin_api_version >= 3 &&
      encoder->plugin->query_encoded_size != nullptr) {
    uint32_t check_encoded_width = input_width, check_encoded_height = input_height;

    encoder->plugin->query_encoded_size(encoder->encoder,
                                        input_width, input_height,
                                        &check_encoded_width,
                                        &check_encoded_height);

    assert((int)check_encoded_width == encoded_width);
    assert((int)check_encoded_height == encoded_height);
  }


  // Note: 'ispe' must be before the transformation properties
  m_heif_file->add_ispe_property(image_id, encoded_width, encoded_height);

  // if image size was rounded up to even size, add a 'clap' box to crop the
  // padding border away

  //uint32_t rotated_width = get_rotated_width(options.image_orientation, out_image->get_width(), out_image->get_height());
  //uint32_t rotated_height = get_rotated_height(options.image_orientation, out_image->get_width(), out_image->get_height());

  if (input_width != encoded_width ||
      input_height != encoded_height) {
    m_heif_file->add_clap_property(image_id,
                                   input_width,
                                   input_height,
                                   encoded_width,
                                   encoded_height);

    // MIAF 7.3.6.7
    // This is according to MIAF without Amd2. With Amd2, the restriction has been liften and the image is MIAF compatible.
    // We might remove this code at a later point in time when MIAF Amd2 is in wide use.

    if (!is_integer_multiple_of_chroma_size(input_width,
                                            input_height,
                                            src_image->get_chroma_format())) {
      out_image->mark_not_miaf_compatible();
    }
  }

  m_heif_file->add_orientation_properties(image_id, options.image_orientation);

  // --- choose which color profile to put into 'colr' box

  if (input_class == heif_image_input_class_normal || input_class == heif_image_input_class_thumbnail) {
    auto icc_profile = src_image->get_color_profile_icc();
    if (icc_profile) {
      m_heif_file->set_color_profile(image_id, icc_profile);
    }

    // save nclx profile

    bool save_nclx_profile = (options.output_nclx_profile != nullptr);

    // if there is an ICC profile, only save NCLX when we chose to save both profiles
    if (icc_profile && !(options.version >= 3 &&
                         options.save_two_colr_boxes_when_ICC_and_nclx_available)) {
      save_nclx_profile = false;
    }

    // we might have turned off nclx completely because macOS/iOS cannot read it
    if (options.version >= 4 && options.macOS_compatibility_workaround_no_nclx_profile) {
      save_nclx_profile = false;
    }

    if (save_nclx_profile) {
      m_heif_file->set_color_profile(image_id, target_nclx_profile);
    }
  }


  write_image_metadata(src_image, image_id);

  m_top_level_images.push_back(out_image);
  m_all_images[image_id] = out_image;



  // --- If there is an alpha channel, add it as an additional image.
  //     Save alpha after the color image because we need to know the final reference to the color image.

  if (options.save_alpha_channel && src_image->has_channel(heif_channel_Alpha)) {

    // --- generate alpha image
    // TODO: can we directly code a monochrome image instead of the dummy color channels?

    std::shared_ptr<HeifPixelImage> alpha_image;
    alpha_image = create_alpha_image_from_image_alpha_channel(src_image);


    // --- encode the alpha image

    std::shared_ptr<HeifContext::Image> heif_alpha_image;

    Error error = encode_image_as_hevc(alpha_image, encoder, options,
                                       heif_image_input_class_alpha,
                                       heif_alpha_image);
    if (error) {
      return error;
    }

    m_heif_file->add_iref_reference(heif_alpha_image->get_id(), fourcc("auxl"), {image_id});

    if (src_image->is_premultiplied_alpha()) {
      m_heif_file->add_iref_reference(image_id, fourcc("prem"), {heif_alpha_image->get_id()});
    }

    // TODO: MIAF says that the *:hevc:* urn is deprecated and we should use "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha"
    // Is this compatible to other decoders?
    m_heif_file->set_auxC_property(heif_alpha_image->get_id(), "urn:mpeg:hevc:2015:auxid:1");
  }


  return Error::Ok;
}


Error HeifContext::encode_image_as_av1(const std::shared_ptr<HeifPixelImage>& image,
                                       struct heif_encoder* encoder,
                                       const struct heif_encoding_options& options,
                                       enum heif_image_input_class input_class,
                                       std::shared_ptr<Image>& out_image)
{
  heif_item_id image_id = m_heif_file->add_new_image("av01");

  out_image = std::make_shared<Image>(this, image_id);
  m_top_level_images.push_back(out_image);
  m_all_images[image_id] = out_image;

  // --- check whether we have to convert the image color space

  heif_colorspace colorspace = image->get_colorspace();
  heif_chroma chroma = image->get_chroma_format();

  auto target_nclx_profile = compute_target_nclx_profile(image, options.output_nclx_profile);

  if (encoder->plugin->plugin_api_version >= 2) {
    encoder->plugin->query_input_colorspace2(encoder->encoder, &colorspace, &chroma);
  }
  else {
    encoder->plugin->query_input_colorspace(&colorspace, &chroma);
  }

  std::shared_ptr<HeifPixelImage> src_image;
  if (colorspace != image->get_colorspace() ||
      chroma != image->get_chroma_format() ||
      !nclx_profile_matches_spec(colorspace, image->get_color_profile_nclx(), options.output_nclx_profile)) {
    // @TODO: use color profile when converting
    int output_bpp = 0; // same as input
    src_image = convert_colorspace(image, colorspace, chroma, target_nclx_profile,
                                   output_bpp, options.color_conversion_options);
    if (!src_image) {
      return Error(heif_error_Unsupported_feature, heif_suberror_Unsupported_color_conversion);
    }
  }
  else {
    src_image = image;
  }


  // --- choose which color profile to put into 'colr' box

  if (input_class == heif_image_input_class_normal || input_class == heif_image_input_class_thumbnail) {
    auto icc_profile = src_image->get_color_profile_icc();
    if (icc_profile) {
      m_heif_file->set_color_profile(image_id, icc_profile);
    }

    if (// target_nclx_profile &&
        (!icc_profile || (options.version >= 3 &&
                          options.save_two_colr_boxes_when_ICC_and_nclx_available))) {
      m_heif_file->set_color_profile(image_id, target_nclx_profile);
    }
  }


  // --- if there is an alpha channel, add it as an additional image

  if (options.save_alpha_channel && src_image->has_channel(heif_channel_Alpha)) {

    // --- generate alpha image
    // TODO: can we directly code a monochrome image instead of the dummy color channels?

    std::shared_ptr<HeifPixelImage> alpha_image;
    alpha_image = create_alpha_image_from_image_alpha_channel(src_image);


    // --- encode the alpha image

    std::shared_ptr<HeifContext::Image> heif_alpha_image;


    Error error = encode_image_as_av1(alpha_image, encoder, options,
                                      heif_image_input_class_alpha,
                                      heif_alpha_image);
    if (error) {
      return error;
    }

    m_heif_file->add_iref_reference(heif_alpha_image->get_id(), fourcc("auxl"), {image_id});
    m_heif_file->set_auxC_property(heif_alpha_image->get_id(), "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha");

    if (src_image->is_premultiplied_alpha()) {
      m_heif_file->add_iref_reference(image_id, fourcc("prem"), {heif_alpha_image->get_id()});
    }
  }

  Box_av1C::configuration config;

  // Fill preliminary av1C in case we cannot parse the sequence_header() correctly in the code below.
  // TODO: maybe we can remove this later.
  fill_av1C_configuration(&config, src_image);

  heif_image c_api_image;
  c_api_image.image = src_image;

  struct heif_error err = encoder->plugin->encode_image(encoder->encoder, &c_api_image, input_class);
  if (err.code) {
    return Error(err.code,
                 err.subcode,
                 err.message);
  }

  for (;;) {
    uint8_t* data;
    int size;

    encoder->plugin->get_compressed_data(encoder->encoder, &data, &size, nullptr);

    bool found_config = fill_av1C_configuration_from_stream(&config, data, size);
    (void) found_config;

    if (data == nullptr) {
      break;
    }

    std::vector<uint8_t> vec;
    vec.resize(size);
    memcpy(vec.data(), data, size);

    m_heif_file->append_iloc_data(image_id, vec);
  }

  m_heif_file->add_av1C_property(image_id);
  m_heif_file->set_av1C_configuration(image_id, config);


  uint32_t input_width, input_height;
  input_width = src_image->get_width();
  input_height = src_image->get_height();

  uint32_t encoded_width = input_width, encoded_height = input_height;

  if (encoder->plugin->plugin_api_version >= 3 &&
      encoder->plugin->query_encoded_size != nullptr) {
    encoder->plugin->query_encoded_size(encoder->encoder,
                                        input_width, input_height,
                                        &encoded_width,
                                        &encoded_height);
  }

  // Note: 'ispe' must be before the transformation properties
  m_heif_file->add_ispe_property(image_id, encoded_width, encoded_height);

  if (input_width != encoded_width ||
      input_height != encoded_height) {
    m_heif_file->add_clap_property(image_id, input_width, input_height,
                                   encoded_width, encoded_height);

    // According to MIAF without Amd2, an image is required to be cropped to multiples of the chroma format raster.
    // However, since AVIF is based on MIAF, the whole image would be invalid in that case.
    // As this restriction was lifted with MIAF-Amd2, we include the MIAF brand for all AVIF images.

    /*
    if (!is_integer_multiple_of_chroma_size(input_width,
                                            input_height,
                                            src_image->get_chroma_format())) {
      out_image->mark_not_miaf_compatible();
    }
    */

    m_heif_file->add_orientation_properties(image_id, options.image_orientation);
  }



  write_image_metadata(src_image, image_id);

  return Error::Ok;
}

Error HeifContext::encode_image_as_jpeg2000(const std::shared_ptr<HeifPixelImage>& image,
                                            struct heif_encoder* encoder,
                                            const struct heif_encoding_options& options,
                                            enum heif_image_input_class input_class,
                                            std::shared_ptr<Image>& out_image) {

  heif_item_id image_id = m_heif_file->add_new_image("j2k1");

  out_image = std::make_shared<Image>(this, image_id);
  m_top_level_images.push_back(out_image);


  // TODO: simplify the color-conversion part. It's the same for each codec.
  // ---begin---
  heif_colorspace colorspace = image->get_colorspace();
  heif_chroma chroma = image->get_chroma_format();

  /*
  auto color_profile = image->get_color_profile_nclx();
  if (!color_profile) {
    color_profile = std::make_shared<color_profile_nclx>();
  }
  auto nclx_profile = std::dynamic_pointer_cast<const color_profile_nclx>(color_profile);
*/

  auto target_nclx_profile = compute_target_nclx_profile(image, options.output_nclx_profile);

  if (encoder->plugin->plugin_api_version >= 2) {
    encoder->plugin->query_input_colorspace2(encoder->encoder, &colorspace, &chroma);
  }
  else {
    encoder->plugin->query_input_colorspace(&colorspace, &chroma);
  }

  std::shared_ptr<HeifPixelImage> src_image;
  if (colorspace != image->get_colorspace() ||
      chroma != image->get_chroma_format() ||
      !nclx_profile_matches_spec(colorspace, image->get_color_profile_nclx(), options.output_nclx_profile)) {
    int output_bpp = 0; // same as input
    src_image = convert_colorspace(image, colorspace, chroma, target_nclx_profile,
                                   output_bpp, options.color_conversion_options);
    if (!src_image) {
      return Error(heif_error_Unsupported_feature, heif_suberror_Unsupported_color_conversion);
    }
  }
  else {
    src_image = image;
  }
  // ---end---


  // --- if there is an alpha channel, add it as an additional image

  if (options.save_alpha_channel && src_image->has_channel(heif_channel_Alpha)) {

    // --- generate alpha image
    // TODO: can we directly code a monochrome image instead of the dummy color channels?

    std::shared_ptr<HeifPixelImage> alpha_image;
    alpha_image = create_alpha_image_from_image_alpha_channel(src_image);


    // --- encode the alpha image

    std::shared_ptr<HeifContext::Image> heif_alpha_image;


    Error error = encode_image_as_jpeg2000(alpha_image, encoder, options,
                                           heif_image_input_class_alpha,
                                           heif_alpha_image);
    if (error) {
      return error;
    }

    m_heif_file->add_iref_reference(heif_alpha_image->get_id(), fourcc("auxl"), {image_id});
    m_heif_file->set_auxC_property(heif_alpha_image->get_id(), "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha");

    if (src_image->is_premultiplied_alpha()) {
      m_heif_file->add_iref_reference(image_id, fourcc("prem"), {heif_alpha_image->get_id()});
    }
  }


  //Encode Image
  heif_image c_api_image;
  c_api_image.image = src_image;
  encoder->plugin->encode_image(encoder->encoder, &c_api_image, input_class);

  //Get Compressed Data
  for (;;) {
    uint8_t* data;
    int size;

    encoder->plugin->get_compressed_data(encoder->encoder, &data, &size, nullptr);

    if (data == NULL) {
      break;
    }

    std::vector<uint8_t> vec;
    vec.resize(size);
    memcpy(vec.data(), data, size);

    m_heif_file->append_iloc_data(image_id, vec);
  }



  //Add 'ispe' Property
  m_heif_file->add_ispe_property(image_id, image->get_width(), image->get_height());

  //Add 'colr' Property
  m_heif_file->set_color_profile(image_id, target_nclx_profile);

  //Add 'j2kH' Property
  auto j2kH = m_heif_file->add_j2kH_property(image_id);

  //Add 'cdef' to 'j2kH'
  auto cdef = std::make_shared<Box_cdef>();
  cdef->set_channels(src_image->get_colorspace());
  j2kH->append_child_box(cdef);

  write_image_metadata(src_image, image_id);

  return Error::Ok;
}


static uint8_t JPEG_SOS = 0xDA;

// returns 0 if the marker_type was not found
size_t find_jpeg_marker_start(const std::vector<uint8_t>& data, uint8_t marker_type)
{
  for (size_t i = 0; i < data.size() - 1; i++) {
    if (data[i]==0xFF && data[i+1]==marker_type) {
      return i;
    }
  }

  return 0;
}


Error HeifContext::encode_image_as_jpeg(const std::shared_ptr<HeifPixelImage>& image,
                                        struct heif_encoder* encoder,
                                        const struct heif_encoding_options& options,
                                        enum heif_image_input_class input_class,
                                        std::shared_ptr<Image>& out_image)
{
  heif_item_id image_id = m_heif_file->add_new_image("jpeg");

  out_image = std::make_shared<Image>(this, image_id);
  m_top_level_images.push_back(out_image);
  m_all_images[image_id] = out_image;

  // --- check whether we have to convert the image color space

  heif_colorspace colorspace = image->get_colorspace();
  heif_chroma chroma = image->get_chroma_format();

  // JPEG always uses CCIR-601

  heif_color_profile_nclx target_heif_nclx;
  target_heif_nclx.matrix_coefficients = heif_matrix_coefficients_ITU_R_BT_601_6;
  target_heif_nclx.color_primaries = heif_color_primaries_ITU_R_BT_601_6;
  target_heif_nclx.transfer_characteristics = heif_transfer_characteristic_ITU_R_BT_601_6;
  target_heif_nclx.full_range_flag = true;

  auto target_nclx_profile = std::make_shared<color_profile_nclx>();
  target_nclx_profile->set_from_heif_color_profile_nclx(&target_heif_nclx);

  if (encoder->plugin->plugin_api_version >= 2) {
    encoder->plugin->query_input_colorspace2(encoder->encoder, &colorspace, &chroma);
  }
  else {
    encoder->plugin->query_input_colorspace(&colorspace, &chroma);
  }

  std::shared_ptr<HeifPixelImage> src_image;
  if (colorspace != image->get_colorspace() ||
      chroma != image->get_chroma_format() ||
      !nclx_profile_matches_spec(colorspace, image->get_color_profile_nclx(), &target_heif_nclx)) {
    // @TODO: use color profile when converting
    int output_bpp = 0; // same as input
    src_image = convert_colorspace(image, colorspace, chroma, target_nclx_profile,
                                   output_bpp, options.color_conversion_options);
    if (!src_image) {
      return Error(heif_error_Unsupported_feature, heif_suberror_Unsupported_color_conversion);
    }
  }
  else {
    src_image = image;
  }


  // --- choose which color profile to put into 'colr' box

  if (input_class == heif_image_input_class_normal || input_class == heif_image_input_class_thumbnail) {
    auto icc_profile = src_image->get_color_profile_icc();
    if (icc_profile) {
      m_heif_file->set_color_profile(image_id, icc_profile);
    }

    if (// target_nclx_profile &&
        (!icc_profile || (options.version >= 3 &&
                          options.save_two_colr_boxes_when_ICC_and_nclx_available))) {
      m_heif_file->set_color_profile(image_id, target_nclx_profile);
    }
  }


  // --- if there is an alpha channel, add it as an additional image

  if (options.save_alpha_channel && src_image->has_channel(heif_channel_Alpha)) {

    // --- generate alpha image
    // TODO: can we directly code a monochrome image instead of the dummy color channels?

    std::shared_ptr<HeifPixelImage> alpha_image;
    alpha_image = create_alpha_image_from_image_alpha_channel(src_image);


    // --- encode the alpha image

    std::shared_ptr<HeifContext::Image> heif_alpha_image;


    Error error = encode_image_as_jpeg(alpha_image, encoder, options,
                                       heif_image_input_class_alpha,
                                       heif_alpha_image);
    if (error) {
      return error;
    }

    m_heif_file->add_iref_reference(heif_alpha_image->get_id(), fourcc("auxl"), {image_id});
    m_heif_file->set_auxC_property(heif_alpha_image->get_id(), "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha");

    if (src_image->is_premultiplied_alpha()) {
      m_heif_file->add_iref_reference(image_id, fourcc("prem"), {heif_alpha_image->get_id()});
    }
  }

  heif_image c_api_image;
  c_api_image.image = src_image;

  struct heif_error err = encoder->plugin->encode_image(encoder->encoder, &c_api_image, input_class);
  if (err.code) {
    return Error(err.code,
                 err.subcode,
                 err.message);
  }

  std::vector<uint8_t> vec;

  for (;;) {
    uint8_t* data;
    int size;

    encoder->plugin->get_compressed_data(encoder->encoder, &data, &size, nullptr);

    if (data == nullptr) {
      break;
    }

    size_t oldsize = vec.size();
    vec.resize(oldsize + size);
    memcpy(vec.data() + oldsize, data, size);
  }

  // Optional: split the JPEG data into a jpgC box and the actual image data.
  // Currently disabled because not supported yet in other decoders.
  if (false) {
    size_t pos = find_jpeg_marker_start(vec, JPEG_SOS);
    if (pos > 0) {
      std::vector<uint8_t> jpgC_data(vec.begin(), vec.begin() + pos);
      auto jpgC = std::make_shared<Box_jpgC>();
      jpgC->set_data(jpgC_data);

      auto ipma_box = m_heif_file->get_ipma_box();
      int index = m_heif_file->get_ipco_box()->append_child_box(jpgC);
      ipma_box->add_property_for_item_ID(image_id, Box_ipma::PropertyAssociation{true, uint16_t(index + 1)});

      std::vector<uint8_t> image_data(vec.begin() + pos, vec.end());
      vec = std::move(image_data);
    }
  }

  m_heif_file->append_iloc_data(image_id, vec);

#if 0
  // TODO: extract 'jpgC' header data
#endif

  uint32_t input_width, input_height;
  input_width = src_image->get_width();
  input_height = src_image->get_height();

  // Note: 'ispe' must be before the transformation properties
  m_heif_file->add_ispe_property(image_id, input_width, input_height);

  uint32_t encoded_width = input_width, encoded_height = input_height;

  if (encoder->plugin->plugin_api_version >= 3 &&
      encoder->plugin->query_encoded_size != nullptr) {

    encoder->plugin->query_encoded_size(encoder->encoder,
                                        input_width, input_height,
                                        &encoded_width,
                                        &encoded_height);
  }

  if (input_width != encoded_width ||
      input_height != encoded_height) {
    m_heif_file->add_clap_property(image_id, input_width, input_height,
                                   encoded_width, encoded_height);

    // MIAF 7.3.6.7
    // This is according to MIAF without Amd2. With Amd2, the restriction has been liften and the image is MIAF compatible.
    // We might remove this code at a later point in time when MIAF Amd2 is in wide use.

    if (!is_integer_multiple_of_chroma_size(input_width,
                                            input_height,
                                            src_image->get_chroma_format())) {
      out_image->mark_not_miaf_compatible();
    }
  }


  m_heif_file->add_orientation_properties(image_id, options.image_orientation);

  write_image_metadata(src_image, image_id);

  return Error::Ok;
}

Error HeifContext::encode_image_as_uncompressed(const std::shared_ptr<HeifPixelImage>& src_image,
                                                struct heif_encoder* encoder,
                                                const struct heif_encoding_options& options,
                                                enum heif_image_input_class input_class,
                                                std::shared_ptr<Image>& out_image)
{
#if WITH_UNCOMPRESSED_CODEC
  heif_item_id image_id = m_heif_file->add_new_image("unci");
  out_image = std::make_shared<Image>(this, image_id);

  Error err = UncompressedImageCodec::encode_uncompressed_image(m_heif_file,
                                                                src_image,
                                                                encoder->encoder,
                                                                options,
                                                                out_image);

  m_top_level_images.push_back(out_image);
  m_all_images[image_id] = out_image;
#endif
  //write_image_metadata(src_image, image_id);

  return Error::Ok;
}


Error HeifContext::encode_image_as_mask(const std::shared_ptr<HeifPixelImage>& src_image,
                                        struct heif_encoder* encoder,
                                        const struct heif_encoding_options& options,
                                        enum heif_image_input_class input_class,
                                        std::shared_ptr<Image>& out_image)
{
  heif_item_id image_id = m_heif_file->add_new_hidden_image("mski");
  out_image = std::make_shared<Image>(this, image_id);
  Error err = MaskImageCodec::encode_mask_image(m_heif_file,
                                                src_image,
                                                encoder->encoder,
                                                options,
                                                out_image);
  m_top_level_images.push_back(out_image);
  m_all_images[image_id] = out_image;
  write_image_metadata(src_image, image_id);
  return Error::Ok;
}


void HeifContext::set_primary_image(const std::shared_ptr<Image>& image)
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


Error HeifContext::set_primary_item(heif_item_id id)
{
  auto iter = m_all_images.find(id);
  if (iter == m_all_images.end()) {
    return Error(heif_error_Usage_error,
                 heif_suberror_No_or_invalid_primary_item,
                 "Cannot set primary item as the ID does not exist.");
  }

  set_primary_image(iter->second);

  return Error::Ok;
}


Error HeifContext::assign_thumbnail(const std::shared_ptr<Image>& master_image,
                                    const std::shared_ptr<Image>& thumbnail_image)
{
  m_heif_file->add_iref_reference(thumbnail_image->get_id(),
                                  fourcc("thmb"), {master_image->get_id()});

  return Error::Ok;
}


Error HeifContext::encode_thumbnail(const std::shared_ptr<HeifPixelImage>& image,
                                    struct heif_encoder* encoder,
                                    const struct heif_encoding_options& options,
                                    int bbox_size,
                                    std::shared_ptr<Image>& out_thumbnail_handle)
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


Error HeifContext::add_exif_metadata(const std::shared_ptr<Image>& master_image, const void* data, int size)
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


Error HeifContext::add_XMP_metadata(const std::shared_ptr<Image>& master_image, const void* data, int size,
                                    heif_metadata_compression compression)
{
  return add_generic_metadata(master_image, data, size, "mime", "application/rdf+xml", nullptr, compression, nullptr);
}


Error HeifContext::add_generic_metadata(const std::shared_ptr<Image>& master_image, const void* data, int size,
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
  if (compression == heif_metadata_compression_deflate) {
#if WITH_DEFLATE_HEADER_COMPRESSION
    data_array = deflate((const uint8_t*) data, size);
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

  m_heif_file->append_iloc_data(metadata_id, data_array);

  return Error::Ok;
}


heif_property_id HeifContext::add_property(heif_item_id targetItem, std::shared_ptr<Box> property, bool essential)
{
  heif_property_id id = m_heif_file->add_property(targetItem, property, essential);

  return id;
}
