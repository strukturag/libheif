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

#include <assert.h>
#include <string.h>
#include <algorithm>
#include <limits>
#include <sstream>
#include <utility>
#include <math.h>

#if ENABLE_PARALLEL_TILE_DECODING
#include <future>
#endif

#include "heif_context.h"
#include "heif_file.h"
#include "heif_image.h"
#include "heif_api_structs.h"
#include "heif_limits.h"
#include "heif_hevc.h"
#include "heif_plugin_registry.h"

#if HAVE_LIBDE265
#include "heif_decoder_libde265.h"
#endif

#if HAVE_X265
#include "heif_encoder_x265.h"
#endif


using namespace heif;



heif_encoder::heif_encoder(std::shared_ptr<heif::HeifContext> _context,
                           const struct heif_encoder_plugin* _plugin)
  : //context(_context),
    plugin(_plugin)
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

  struct heif_error err = { heif_error_Ok, heif_suberror_Unspecified, kSuccess };
  return err;
}


static int32_t readvec_signed(const std::vector<uint8_t>& data,int& ptr,int len)
{
  const uint32_t high_bit = 0x80<<((len-1)*8);

  uint32_t val=0;
  while (len--) {
    val <<= 8;
    val |= data[ptr++];
  }

  bool negative = (val & high_bit) != 0;
  val &= ~high_bit;

  if (negative) {
    return -(high_bit-val);
  }
  else {
    return val;
  }

  return val;
}


static uint32_t readvec(const std::vector<uint8_t>& data,int& ptr,int len)
{
  uint32_t val=0;
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

  m_rows    = static_cast<uint16_t>(data[2] +1);
  m_columns = static_cast<uint16_t>(data[3] +1);

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
  uint8_t  m_version;
  uint8_t  m_flags;
  uint16_t m_background_color[4];
  uint32_t m_width;
  uint32_t m_height;

  struct Offset {
    int32_t x,y;
  };

  std::vector<Offset> m_offsets;
};


Error ImageOverlay::parse(size_t num_images, const std::vector<uint8_t>& data)
{
  Error eofError(heif_error_Invalid_input,
                 heif_suberror_Invalid_grid_data,
                 "Overlay image data incomplete");

  if (data.size() < 2 + 4*2) {
    return eofError;
  }

  m_version = data[0];
  m_flags = data[1];

  if (m_version != 0) {
    std::stringstream sstr;
    sstr << "Overlay image data version " << m_version << " is not implemented yet";

    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_data_version,
                 sstr.str());
  }

  int field_len = ((m_flags & 1) ? 4 : 2);
  int ptr=2;

  if (ptr + 4*2 + 2*field_len + num_images*2*field_len > data.size()) {
    return eofError;
  }

  for (int i=0;i<4;i++) {
    uint16_t color = static_cast<uint16_t>(readvec(data,ptr,2));
    m_background_color[i] = color;
  }

  m_width  = readvec(data,ptr,field_len);
  m_height = readvec(data,ptr,field_len);

  m_offsets.resize(num_images);

  for (size_t i=0;i<num_images;i++) {
    m_offsets[i].x = readvec_signed(data,ptr,field_len);
    m_offsets[i].y = readvec_signed(data,ptr,field_len);
  }

  return Error::Ok;
}


std::string ImageOverlay::dump() const
{
  std::stringstream sstr;

  sstr << "version: " << ((int)m_version) << "\n"
       << "flags: " << ((int)m_flags) << "\n"
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
  for (int i=0;i<4;i++) {
    col[i] = m_background_color[i];
  }
}


void ImageOverlay::get_offset(size_t image_index, int32_t* x, int32_t* y) const
{
  assert(image_index>=0 && image_index<m_offsets.size());
  assert(x && y);

  *x = m_offsets[image_index].x;
  *y = m_offsets[image_index].y;
}


HeifReader::HeifReader(struct heif_context* ctx, struct heif_reader* reader,
    void* userdata)
  : m_ctx(ctx), m_reader(reader), m_userdata(userdata) {}

uint64_t HeifReader::length() const {
  return m_reader->get_length(m_ctx, m_userdata);
}

uint64_t HeifReader::position() const {
  return m_reader->get_position(m_ctx, m_userdata);
}

bool HeifReader::read(void* data, size_t size) {
  return m_reader->read(m_ctx, data, size, m_userdata);
}

bool HeifReader::seek(int64_t position, enum heif_reader_offset offset) {
  return m_reader->seek(m_ctx, position, offset, m_userdata);
}

// static
int64_t HeifContext::internal_get_length(struct heif_context* ctx,
                                          void* userdata) {
  ReaderInterface* reader = static_cast<ReaderInterface*>(userdata);
  return reader->length();
}

// static
int64_t HeifContext::internal_get_position(struct heif_context* ctx,
                                            void* userdata) {
  ReaderInterface* reader = static_cast<ReaderInterface*>(userdata);
  return reader->position();
}

// static
int HeifContext::internal_read(struct heif_context* ctx,
                               void* data,
                               size_t size,
                               void* userdata) {
  ReaderInterface* reader = static_cast<ReaderInterface*>(userdata);
  return reader->read(data, size);
}

// static
int HeifContext::internal_seek(struct heif_context* ctx,
                               int64_t position,
                               enum heif_reader_offset offset,
                               void* userdata) {
  ReaderInterface* reader = static_cast<ReaderInterface*>(userdata);
  return reader->seek(position, offset);
}

class HeifContext::MemoryReader : public HeifContext::ReaderInterface {
 public:
  MemoryReader(const void* data, size_t size)
    : data_(static_cast<const uint8_t*>(data)),
      position_(data_),
      end_(data_ + size),
      size_(size) {}

  int64_t length() const {
    return size_;
  }

  int64_t position() const  {
    return position_ - data_;
  }

  bool read(void* data, size_t size)  {
    if (size > size_ || end_ - size < position_) {
      return false;
    }

    memcpy(data, position_, size);
    position_ += size;
    return true;
  }

  bool seek(int64_t position, enum heif_reader_offset offset) {
    const uint8_t* new_position;
    switch (offset) {
      case heif_seek_start:
        if (position < 0 || static_cast<uint64_t>(position) > size_) {
          return false;
        }

        new_position = data_ + position;
        break;
      case heif_seek_current:
        new_position = position_ + position;
        if (new_position < data_ || new_position > end_) {
          return false;
        }
        break;
      case heif_seek_end:
        new_position = end_ + position;
        if (new_position < data_ || new_position > end_) {
          return false;
        }
        break;
      default:
        assert(false);
        return false;
    }

    position_ = new_position;
    return true;
  }

 private:
  const uint8_t* data_;
  const uint8_t* position_;
  const uint8_t* end_;
  size_t size_;
};

class HeifContext::FileReader : public HeifContext::ReaderInterface {
 public:
  FileReader(const char* filename) : fp_(fopen(filename, "rb")) {
    if (fp_) {
      fseek(fp_, 0, SEEK_END);
      size_ = ftell(fp_);
      fseek(fp_, 0, SEEK_SET);
    }
  }
  ~FileReader() {
    if (fp_) {
      fclose(fp_);
    }
  }

  int64_t length() const {
    return size_;
  }

  int64_t position() const  {
    return fp_ ? ftell(fp_) : 0;
  }

  bool read(void* data, size_t size)  {
    if (!fp_) {
      return false;
    }

    return fread(data, 1, size, fp_) == size;
  }

  bool seek(int64_t position, enum heif_reader_offset offset) {
    if (!fp_) {
      return false;
    }

    switch (offset) {
      case heif_seek_start:
        return fseek(fp_, position, SEEK_SET) == 0;
      case heif_seek_current:
        return fseek(fp_, position, SEEK_CUR) == 0;
      case heif_seek_end:
        return fseek(fp_, position, SEEK_END) == 0;
      default:
        assert(false);
        return false;
    }
  }

 private:
  FILE* fp_;
  uint64_t size_ = 0;
};

HeifContext::HeifContext()
{
#if HAVE_LIBDE265
  heif::register_decoder(get_decoder_plugin_libde265());
#endif

#if HAVE_X265
  heif::register_encoder(get_encoder_plugin_x265());
#endif

  reset_to_empty_heif();

  m_internal_reader.reader_api_version = 1;
  m_internal_reader.get_length = internal_get_length;
  m_internal_reader.get_position = internal_get_position;
  m_internal_reader.read = internal_read;
  m_internal_reader.seek = internal_seek;
}

HeifContext::~HeifContext()
{
}

Error HeifContext::read(struct heif_context* ctx, struct heif_reader* reader,
    void* userdata)
{
  m_heif_reader.reset(new HeifReader(ctx, reader, userdata));
  m_heif_file = std::make_shared<HeifFile>();
  Error err = m_heif_file->read(m_heif_reader.get());
  if (err) {
    return err;
  }

  return interpret_heif_file();
}

// static
std::unique_ptr<HeifContext::ReaderInterface> HeifContext::CreateReader(
    const void* data, size_t size) {
  return std::unique_ptr<ReaderInterface>(new MemoryReader(data, size));
}

// static
std::unique_ptr<HeifContext::ReaderInterface> HeifContext::CreateReader(
    const char* filename) {
  return std::unique_ptr<ReaderInterface>(new FileReader(filename));
}

Error HeifContext::read_from_file(const char* input_filename)
{
  m_temp_reader = CreateReader(input_filename);
  return read(nullptr, &m_internal_reader, m_temp_reader.get());
}

Error HeifContext::read_from_memory(const void* data, size_t size)
{
  m_temp_reader = CreateReader(data, size);
  return read(nullptr, &m_internal_reader, m_temp_reader.get());
}

void HeifContext::reset_to_empty_heif()
{
  m_heif_file = std::make_shared<HeifFile>();
  m_heif_file->new_empty_file();

  m_all_images.clear();
  m_top_level_images.clear();
  m_primary_image.reset();
}

void HeifContext::write(StreamWriter& writer)
{
  m_heif_file->write(writer);
}

std::string HeifContext::debug_dump_boxes() const
{
  return m_heif_file->debug_dump_boxes();
}

void HeifContext::register_decoder(const heif_decoder_plugin* decoder_plugin)
{
  if (decoder_plugin->init_plugin) {
    (*decoder_plugin->init_plugin)();
  }

  m_decoder_plugins.insert(decoder_plugin);
}


const struct heif_decoder_plugin* HeifContext::get_decoder(enum heif_compression_format type) const
{
  int highest_priority = 0;
  const struct heif_decoder_plugin* best_plugin = nullptr;


  // search global plugins

  for (const auto* plugin : s_decoder_plugins) {
    int priority = plugin->does_support_format(type);
    if (priority > highest_priority) {
      highest_priority = priority;
      best_plugin = plugin;
    }
  }


  // search context-local plugins (DEPRECATED)

  for (const auto* plugin : m_decoder_plugins) {
    int priority = plugin->does_support_format(type);
    if (priority > highest_priority) {
      highest_priority = priority;
      best_plugin = plugin;
    }
  }

  return best_plugin;
}


static bool item_type_is_image(const std::string& item_type)
{
  return (item_type=="hvc1" ||
          item_type=="grid" ||
          item_type=="iden" ||
          item_type=="iovl");
}


void HeifContext::remove_top_level_image(std::shared_ptr<Image> image)
{
  std::vector<std::shared_ptr<Image>> new_list;

  for (auto img : m_top_level_images) {
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

    if (item_type_is_image(infe_box->get_item_type())) {
      auto image = std::make_shared<Image>(this, id);
      m_all_images.insert(std::make_pair(id, image));

      if (!infe_box->is_hidden_item()) {
        if (id==m_heif_file->get_primary_image_ID()) {
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


  // --- remove thumbnails from top-level images and assign to their respective image

  auto iref_box = m_heif_file->get_iref_box();
  if (iref_box) {
    // m_top_level_images.clear();

    for (auto& pair : m_all_images) {
      auto& image = pair.second;

      uint32_t type = iref_box->get_reference_type(image->get_id());

      if (type==fourcc("thmb")) {
        // --- this is a thumbnail image, attach to the main image

        std::vector<heif_item_id> refs = iref_box->get_references(image->get_id());
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

        master_iter->second->add_thumbnail(image);

        remove_top_level_image(image);
      }
      else if (type==fourcc("auxl")) {

        // --- this is an auxiliary image
        //     check whether it is an alpha channel and attach to the main image if yes

        std::vector<Box_ipco::Property> properties;
        Error err = m_heif_file->get_properties(image->get_id(), properties);
        if (err) {
          return err;
        }

        std::shared_ptr<Box_auxC> auxC_property;
        for (const auto& property : properties) {
          auto auxC = std::dynamic_pointer_cast<Box_auxC>(property.property);
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

        std::vector<heif_item_id> refs = iref_box->get_references(image->get_id());
        if (refs.size() != 1) {
          return Error(heif_error_Invalid_input,
                       heif_suberror_Unspecified,
                       "Too many auxiliary image references");
        }


        // alpha channel

        if (auxC_property->get_aux_type() == "urn:mpeg:avc:2015:auxid:1" ||
            auxC_property->get_aux_type() == "urn:mpeg:hevc:2015:auxid:1") {
          image->set_is_alpha_channel_of(refs[0]);

          auto master_iter = m_all_images.find(refs[0]);
          master_iter->second->set_alpha_channel(image);
        }


        // depth channel

        if (auxC_property->get_aux_type() == "urn:mpeg:hevc:2015:auxid:2") {
          image->set_is_depth_channel_of(refs[0]);

          auto master_iter = m_all_images.find(refs[0]);
          master_iter->second->set_depth_channel(image);

          auto subtypes = auxC_property->get_subtypes();

          std::vector<std::shared_ptr<SEIMessage>> sei_messages;
          Error err = decode_hevc_aux_sei_messages(subtypes, sei_messages);

          for (auto& msg : sei_messages) {
            auto depth_msg = std::dynamic_pointer_cast<SEIMessage_depth_representation_info>(msg);
            if (depth_msg) {
              image->set_depth_representation_info(*depth_msg);
            }
          }
        }

        remove_top_level_image(image);
      }
      else {
        // 'image' is a normal image, keep it as a top-level image
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

    bool ispe_read = false;
    for (const auto& prop : properties) {
      auto ispe = std::dynamic_pointer_cast<Box_ispe>(prop.property);
      if (ispe) {
        uint32_t width = ispe->get_width();
        uint32_t height = ispe->get_height();


        // --- check whether the image size is "too large"

        if (width  >= static_cast<uint32_t>(MAX_IMAGE_WIDTH) ||
            height >= static_cast<uint32_t>(MAX_IMAGE_HEIGHT)) {
          std::stringstream sstr;
          sstr << "Image size " << width << "x" << height << " exceeds the maximum image size "
               << MAX_IMAGE_WIDTH << "x" << MAX_IMAGE_HEIGHT << "\n";

          return Error(heif_error_Memory_allocation_error,
                       heif_suberror_Security_limit_exceeded,
                       sstr.str());
        }

        image->set_resolution(width, height);
        ispe_read = true;
      }

      if (ispe_read) {
        auto clap = std::dynamic_pointer_cast<Box_clap>(prop.property);
        if (clap) {
          image->set_resolution( clap->get_width_rounded(),
                                 clap->get_height_rounded() );
        }

        auto irot = std::dynamic_pointer_cast<Box_irot>(prop.property);
        if (irot) {
          if (irot->get_rotation()==90 ||
              irot->get_rotation()==270) {
            // swap width and height
            image->set_resolution( image->get_height(),
                                   image->get_width() );
          }
        }
      }
    }
  }



  // --- read metadata and assign to image

  for (heif_item_id id : image_IDs) {
    std::string item_type = m_heif_file->get_item_type(id);
    if (item_type == "Exif") {
      std::shared_ptr<ImageMetadata> metadata = std::make_shared<ImageMetadata>();
      metadata->item_id = id;
      metadata->item_type = item_type;

      Error err = m_heif_file->get_compressed_image_data(id, &(metadata->m_data));
      if (err) {
        return err;
      }

      //std::cerr.write((const char*)data.data(), data.size());


      // --- assign metadata to the image

      if (iref_box) {
        uint32_t type = iref_box->get_reference_type(id);
        if (type == fourcc("cdsc")) {
          std::vector<uint32_t> refs = iref_box->get_references(id);
          if (refs.size() != 1) {
            return Error(heif_error_Invalid_input,
                         heif_suberror_Unspecified,
                         "Exif data not correctly assigned to image");
          }

          uint32_t exif_image_id = refs[0];
          auto img_iter = m_all_images.find(exif_image_id);
          if (img_iter == m_all_images.end()) {
            return Error(heif_error_Invalid_input,
                         heif_suberror_Nonexisting_item_referenced,
                         "Exif data assigned to non-existing image");
          }

          img_iter->second->add_metadata(metadata);
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
}

HeifContext::Image::~Image()
{
}

Error HeifContext::Image::decode_image(std::shared_ptr<HeifPixelImage>& img,
                                       heif_colorspace colorspace,
                                       heif_chroma chroma,
                                       const struct heif_decoding_options* options) const
{
  Error err = m_heif_context->decode_image(m_id, img, options);
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
      return Error(heif_error_Unsupported_feature, heif_suberror_Unsupported_color_conversion);
    }
  }

  return err;
}


Error HeifContext::decode_image(heif_item_id ID,
                                std::shared_ptr<HeifPixelImage>& img,
                                const struct heif_decoding_options* options) const
{
  std::string image_type = m_heif_file->get_item_type(ID);

  Error error;


  // --- decode image, depending on its type

  if (image_type == "hvc1") {
    const struct heif_decoder_plugin* decoder_plugin = get_decoder(heif_compression_HEVC);
    if (!decoder_plugin) {
      return Error(heif_error_Unsupported_feature, heif_suberror_Unsupported_codec);
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

#if 0
    FILE* fh = fopen("out.bin", "wb");
    fwrite(data.data(), 1, data.size(), fh);
    fclose(fh);
#endif
  }
  else if (image_type == "grid") {
    std::vector<uint8_t> data;
    error = m_heif_file->get_compressed_image_data(ID, &data);
    if (error) {
      return error;
    }

    error = decode_full_grid_image(ID, img, data);
    if (error) {
      return error;
    }
  }
  else if (image_type == "iden") {
    error = decode_derived_image(ID, img);
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

    error = decode_overlay_image(ID, img, data);
    if (error) {
      return error;
    }
  }
  else {
    // Should not reach this, was already rejected by "get_image_data".
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_image_type);
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
      Error err = alpha_image->decode_image(alpha);
      if (err) {
        return err;
      }

      // TODO: check that sizes are the same and that we have an Y channel
      // BUT: is there any indication in the standard that the alpha channel should have the same size?

      img->transfer_plane_from_image_as(alpha, heif_channel_Y, heif_channel_Alpha);
    }
  }


  // --- apply image transformations

  if (!options || options->ignore_transformations == false) {
    std::vector<Box_ipco::Property> properties;
    auto ipco_box = m_heif_file->get_ipco_box();
    auto ipma_box = m_heif_file->get_ipma_box();
    error = ipco_box->get_properties_for_item_ID(ID, ipma_box, properties);

    for (const auto& property : properties) {
      auto rot = std::dynamic_pointer_cast<Box_irot>(property.property);
      if (rot) {
        std::shared_ptr<HeifPixelImage> rotated_img;
        error = img->rotate_ccw(rot->get_rotation(), rotated_img);
        if (error) {
          return error;
        }

        img = rotated_img;
      }


      auto mirror = std::dynamic_pointer_cast<Box_imir>(property.property);
      if (mirror) {
        error = img->mirror_inplace(mirror->get_mirror_axis() == Box_imir::MirrorAxis::Horizontal);
        if (error) {
          return error;
        }
      }


      auto clap = std::dynamic_pointer_cast<Box_clap>(property.property);
      if (clap) {
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
        if (top  < 0) { top  = 0; }

        if (right >= img_width) { right = img_width-1; }
        if (bottom >= img_height) { bottom = img_height-1; }

        if (left >= right ||
            top >= bottom) {
          return Error(heif_error_Invalid_input,
                       heif_suberror_Invalid_clean_aperture);
        }

        std::shared_ptr<HeifPixelImage> cropped_img;
        error = img->crop(left,right,top,bottom, cropped_img);
        if (error) {
          return error;
        }

        img = cropped_img;
      }
    }
  }

  return Error::Ok;
}


// TODO: this function only works with YCbCr images, chroma 4:2:0, and 8 bpp at the moment
// It will crash badly if we get anything else.
Error HeifContext::decode_full_grid_image(heif_item_id ID,
                                          std::shared_ptr<HeifPixelImage>& img,
                                          const std::vector<uint8_t>& grid_data) const
{
  ImageGrid grid;
  grid.parse(grid_data);
  // std::cout << grid.dump();


  auto iref_box = m_heif_file->get_iref_box();

  if (!iref_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_iref_box,
                 "No iref box available, but needed for grid image");
  }

  std::vector<heif_item_id> image_references = iref_box->get_references(ID);

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

  if (w >= MAX_IMAGE_WIDTH || h >= MAX_IMAGE_HEIGHT) {
    std::stringstream sstr;
    sstr << "Image size " << w << "x" << h << " exceeds the maximum image size "
         << MAX_IMAGE_WIDTH << "x" << MAX_IMAGE_HEIGHT << "\n";

    return Error(heif_error_Memory_allocation_error,
                 heif_suberror_Security_limit_exceeded,
                 sstr.str());
  }

  img = std::make_shared<HeifPixelImage>();
  img->create(w,h,
              heif_colorspace_YCbCr, // TODO: how do we know ?
              heif_chroma_420); // TODO: how do we know ?

  img->add_plane(heif_channel_Y,  w,h, bpp);
  img->add_plane(heif_channel_Cb, w/2,h/2, bpp);
  img->add_plane(heif_channel_Cr, w/2,h/2, bpp);

  int y0=0;
  int reference_idx = 0;

#if ENABLE_PARALLEL_TILE_DECODING
  std::vector<std::future<Error> > errs;
  errs.resize(grid.get_rows() * grid.get_columns() );
#endif

  for (int y=0;y<grid.get_rows();y++) {
    int x0=0;
    int tile_height=0;

    for (int x=0;x<grid.get_columns();x++) {

      heif_item_id tileID = image_references[reference_idx];

      const std::shared_ptr<Image> tileImg = m_all_images.find(tileID)->second;
      int src_width = tileImg->get_width();
      int src_height = tileImg->get_height();

#if ENABLE_PARALLEL_TILE_DECODING
      errs[x+y*grid.get_columns()] = std::async(std::launch::async,
                                                &HeifContext::decode_and_paste_tile_image, this,
                                                tileID, img, x0,y0);
#else
      Error err = decode_and_paste_tile_image(tileID, img, x0,y0);
      if (err) {
        return err;
      }
#endif

      x0 += src_width;
      tile_height = src_height; // TODO: check that all tiles have the same height

      reference_idx++;
    }

    y0 += tile_height;
  }

#if ENABLE_PARALLEL_TILE_DECODING
  // check for decoding errors in all decoded tiles

  for (int i=0;i<grid.get_rows() * grid.get_columns();i++) {
    Error e = errs[i].get();
    if (e) {
      return e;
    }
  }
#endif

  return Error::Ok;
}


Error HeifContext::decode_and_paste_tile_image(heif_item_id tileID,
                                               std::shared_ptr<HeifPixelImage> img,
                                               int x0,int y0) const
{
  std::shared_ptr<HeifPixelImage> tile_img;

  Error err = decode_image(tileID, tile_img);
  if (err != Error::Ok) {
    return err;
  }


  const int w = img->get_width();
  const int h = img->get_height();


  // --- copy tile into output image

  int src_width  = tile_img->get_width();
  int src_height = tile_img->get_height();
  assert(src_width >= 0);
  assert(src_height >= 0);

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

  return Error::Ok;
}


Error HeifContext::decode_derived_image(heif_item_id ID,
                                        std::shared_ptr<HeifPixelImage>& img) const
{
  // find the ID of the image this image is derived from

  auto iref_box = m_heif_file->get_iref_box();

  if (!iref_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_iref_box,
                 "No iref box available, but needed for iden image");
  }

  std::vector<heif_item_id> image_references = iref_box->get_references(ID);

  if ((int)image_references.size() != 1) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Missing_grid_images,
                 "'iden' image with more than one reference image");
  }


  heif_item_id reference_image_id = image_references[0];


  Error error = decode_image(reference_image_id, img);
  return error;
}


Error HeifContext::decode_overlay_image(heif_item_id ID,
                                        std::shared_ptr<HeifPixelImage>& img,
                                        const std::vector<uint8_t>& overlay_data) const
{
  // find the IDs this image is composed of

  auto iref_box = m_heif_file->get_iref_box();

  if (!iref_box) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_No_iref_box,
                 "No iref box available, but needed for iovl image");
  }

  std::vector<heif_item_id> image_references = iref_box->get_references(ID);

  /* TODO: probably, it is valid that an iovl image has no references ?

  if (image_references.empty()) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Missing_grid_images,
                 "'iovl' image with more than one reference image");
  }
  */


  ImageOverlay overlay;
  overlay.parse(image_references.size(), overlay_data);
  //std::cout << overlay.dump();

  if (image_references.size() != overlay.get_num_offsets()) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Invalid_overlay_data,
                 "Number of image offsets does not match the number of image references");
  }

  int w = overlay.get_canvas_width();
  int h = overlay.get_canvas_height();

  if (w >= MAX_IMAGE_WIDTH || h >= MAX_IMAGE_HEIGHT) {
    std::stringstream sstr;
    sstr << "Image size " << w << "x" << h << " exceeds the maximum image size "
         << MAX_IMAGE_WIDTH << "x" << MAX_IMAGE_HEIGHT << "\n";

    return Error(heif_error_Memory_allocation_error,
                 heif_suberror_Security_limit_exceeded,
                 sstr.str());
  }

  // TODO: seems we always have to compose this in RGB since the background color is an RGB value
  img = std::make_shared<HeifPixelImage>();
  img->create(w,h,
              heif_colorspace_RGB,
              heif_chroma_444);
  img->add_plane(heif_channel_R,w,h,8); // TODO: other bit depths
  img->add_plane(heif_channel_G,w,h,8); // TODO: other bit depths
  img->add_plane(heif_channel_B,w,h,8); // TODO: other bit depths

  uint16_t bkg_color[4];
  overlay.get_background_color(bkg_color);

  Error err = img->fill_RGB_16bit(bkg_color[0], bkg_color[1], bkg_color[2], bkg_color[3]);
  if (err) {
    return err;
  }


  for (size_t i=0;i<image_references.size();i++) {
    std::shared_ptr<HeifPixelImage> overlay_img;
    err = decode_image(image_references[i], overlay_img);
    if (err != Error::Ok) {
      return err;
    }

    overlay_img = overlay_img->convert_colorspace(heif_colorspace_RGB, heif_chroma_444);
    if (!overlay_img) {
      return Error(heif_error_Unsupported_feature, heif_suberror_Unsupported_color_conversion);
    }

    int32_t dx,dy;
    overlay.get_offset(i, &dx,&dy);

    err = img->overlay(overlay_img, dx,dy);
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


std::shared_ptr<HeifContext::Image> HeifContext::add_new_hvc1_image()
{
  heif_item_id image_id = m_heif_file->add_new_image("hvc1");

  auto image = std::make_shared<Image>(this, image_id);

  m_top_level_images.push_back(image);

  return image;
}


Error HeifContext::add_alpha_image(std::shared_ptr<HeifPixelImage> image, heif_item_id* out_item_id,
                                   struct heif_encoder* encoder)
{
  std::shared_ptr<HeifContext::Image> heif_alpha_image;

  heif_alpha_image = add_new_hvc1_image();

  assert(out_item_id);
  *out_item_id = heif_alpha_image->get_id();


  // --- generate alpha image
  // TODO: can we directly code a monochrome image instead of the dummy color channels?

  int chroma_width  = (image->get_width() +1)/2;
  int chroma_height = (image->get_height()+1)/2;

  std::shared_ptr<HeifPixelImage> alpha_image = std::make_shared<HeifPixelImage>();
  alpha_image->create(image->get_width(), image->get_height(),
                      heif_colorspace_YCbCr, heif_chroma_420);
  alpha_image->copy_new_plane_from(image, heif_channel_Alpha, heif_channel_Y);
  alpha_image->fill_new_plane(heif_channel_Cb, 128, chroma_width, chroma_height);
  alpha_image->fill_new_plane(heif_channel_Cr, 128, chroma_width, chroma_height);


  // --- encode the alpha image

  Error error = heif_alpha_image->encode_image_as_hevc(alpha_image, encoder,
                                                       heif_image_input_class_alpha);
  return error;
}



void HeifContext::Image::set_preencoded_hevc_image(const std::vector<uint8_t>& data)
{
  m_heif_context->m_heif_file->add_hvcC_property(m_id);


  // --- parse the h265 stream and set hvcC headers and compressed image data

  int state=0;

  bool first=true;
  bool eof=false;

  int prev_start_code_start = -1; // init to an invalid value, will always be overwritten before use
  int start_code_start;
  int ptr = 0;

  for (;;) {
    bool dump_nal = false;

    uint8_t c = data[ptr++];

    if (state==3) {
      state=0;
    }

    //printf("read c=%02x\n",c);

    if (c==0 && state<=1) {
      state++;
    }
    else if (c==0) {
      // NOP
    }
    else if (c==1 && state==2) {
      start_code_start = ptr - 3;
      dump_nal = true;
      state=3;
    }
    else {
      state=0;
    }

    //printf("-> state= %d\n",state);

    if (ptr == (int)data.size()) {
      start_code_start = (int)data.size();
      dump_nal = true;
      eof = true;
    }

    if (dump_nal) {
      if (first) {
        first = false;
      }
      else {
        std::vector<uint8_t> nal_data;
        size_t length = start_code_start - (prev_start_code_start+3);

        nal_data.resize(length);

        assert(prev_start_code_start>=0);
        memcpy(nal_data.data(), data.data() + prev_start_code_start+3, length);

        int nal_type = (nal_data[0]>>1);

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

          memcpy(nal_data_with_size.data()+4, nal_data.data(), nal_data.size());
          nal_data_with_size[0] = ((nal_data.size()>>24) & 0xFF);
          nal_data_with_size[1] = ((nal_data.size()>>16) & 0xFF);
          nal_data_with_size[2] = ((nal_data.size()>> 8) & 0xFF);
          nal_data_with_size[3] = ((nal_data.size()>> 0) & 0xFF);

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


Error HeifContext::Image::encode_image_as_hevc(std::shared_ptr<HeifPixelImage> image,
                                               struct heif_encoder* encoder,
                                               enum heif_image_input_class input_class)
{
  /*
  const struct heif_encoder_plugin* encoder_plugin = nullptr;

  encoder_plugin = m_heif_context->get_encoder(heif_compression_HEVC);

  if (encoder_plugin == nullptr) {
    return Error(heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_codec);
  }
  */



  // --- check whether we have to convert the image color space

  heif_colorspace colorspace = image->get_colorspace();
  heif_chroma chroma = image->get_chroma_format();
  encoder->plugin->query_input_colorspace(&colorspace, &chroma);

  if (colorspace != image->get_colorspace() ||
      chroma != image->get_chroma_format()) {

    image = image->convert_colorspace(colorspace, chroma);
  }



  // --- if there is an alpha channel, add it as an additional image

  if (image->has_channel(heif_channel_Alpha)) {
    heif_item_id alpha_image_id;
    Error err = m_heif_context->add_alpha_image(image, &alpha_image_id, encoder);
    if (err) {
      return err;
    }

    m_heif_context->m_heif_file->add_iref_reference(alpha_image_id, fourcc("auxl"), { m_id });
    m_heif_context->m_heif_file->set_auxC_property(alpha_image_id, "urn:mpeg:hevc:2015:auxid:1");
  }



  m_heif_context->m_heif_file->add_hvcC_property(m_id);


  heif_image c_api_image;
  c_api_image.image = image;

  encoder->plugin->encode_image(encoder->encoder, &c_api_image, input_class);

  for (;;) {
    uint8_t* data;
    int size;

    encoder->plugin->get_compressed_data(encoder->encoder, &data, &size, NULL);

    if (data==NULL) {
      break;
    }


    const uint8_t NAL_SPS = 33;

    if ((data[0] >> 1) == NAL_SPS) {
      int width,height;
      Box_hvcC::configuration config;

      parse_sps_for_hvcC_configuration(data, size, &config, &width, &height);

      m_heif_context->m_heif_file->set_hvcC_configuration(m_id, config);
      m_heif_context->m_heif_file->add_ispe_property(m_id, width, height);
    }

    switch (data[0] >> 1) {
    case 0x20:
    case 0x21:
    case 0x22:
      m_heif_context->m_heif_file->append_hvcC_nal_data(m_id, data, size);
      break;

    default:
      m_heif_context->m_heif_file->append_iloc_data_with_4byte_size(m_id, data, size);
    }
  }


  return Error::Ok;
}


void HeifContext::set_primary_image(std::shared_ptr<Image> image)
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
