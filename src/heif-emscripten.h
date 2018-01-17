#ifndef LIBHEIF_BOX_EMSCRIPTEN_H
#define LIBHEIF_BOX_EMSCRIPTEN_H

#include <emscripten/bind.h>

#include <memory>
#include <string>
#include <sstream>
#include <utility>
#include <vector>

#include "box.h"
#include "heif_file.h"

namespace heif {

static std::string dump_box_header(BoxHeader* header) {
  if (!header) {
    return "";
  }

  Indent indent;
  return header->dump(indent);
}

static std::string dump_box(Box* box) {
  if (!box) {
    return "";
  }

  Indent indent;
  return box->dump(indent);
}

static std::shared_ptr<Box> Box_read(BitstreamRange& range) {
  std::shared_ptr<Box> box;
  Error error = Box::read(range, &box);
  if (error) {
    return nullptr;
  }

  return box;
}

class EmscriptenBitstreamRange : public BitstreamRange {
 public:
  explicit EmscriptenBitstreamRange(const std::string& data)
    : BitstreamRange(nullptr, 0),
      data_(data),
      stream_(std::move(data_)) {
    construct(&stream_, data.size(), nullptr);
  }
  bool error() const {
    return BitstreamRange::error();
  }

 private:
  std::string data_;
  std::basic_istringstream<char> stream_;
};

static Error HeifFile_read_from_memory(HeifFile* file,
    const std::string& data) {
  if (!file) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Null_pointer_argument);
  }

  return file->read_from_memory(data.data(), data.size());
}

static emscripten::val HeifFile_get_compressed_image_data(HeifFile* file,
    uint16_t ID, const std::string& data) {
  emscripten::val result = emscripten::val::object();
  if (!file) {
    return result;
  }

  std::vector<uint8_t> image_data;
  Error err = file->get_compressed_image_data(ID, &image_data);
  if (err) {
    return emscripten::val(err);
  }

  result.set("type", file->get_item_type(ID));
  result.set("data", std::string(reinterpret_cast<char*>(image_data.data()),
      image_data.size()));
  return result;
}

static std::string heif_get_version() {
  return ::heif_get_version();
}

static struct heif_error _heif_context_read_from_memory(
    struct heif_context* context, const std::string& data) {
  return heif_context_read_from_memory(context, data.data(), data.size());
}

static void strided_copy(void* dest, const void* src, int width, int height,
    int stride) {
  if (width == stride) {
    memcpy(dest, src, width * height);
  } else {
    const uint8_t* _src = static_cast<const uint8_t*>(src);
    uint8_t* _dest = static_cast<uint8_t*>(dest);
    for (int y = 0; y < height; y++, _dest += width, _src += stride) {
      memcpy(_dest, _src, stride);
    }
  }
}

static emscripten::val heif_js_context_get_image_handle(
    struct heif_context* context, int idx) {
  emscripten::val result = emscripten::val::object();
  if (!context) {
    return result;
  }

  struct heif_image_handle* handle;
  struct heif_error err = heif_context_get_image_handle(context, idx, &handle);
  if (err.code != heif_error_Ok) {
    return emscripten::val(err);
  }

  return emscripten::val(handle);
}

static emscripten::val heif_js_decode_image(struct heif_image_handle* handle,
    enum heif_colorspace colorspace, enum heif_chroma chroma) {
  emscripten::val result = emscripten::val::object();
  if (!handle) {
    return result;
  }

  struct heif_image* image;
  struct heif_error err = heif_decode_image(handle, colorspace, chroma, &image);
  if (err.code != heif_error_Ok) {
    return emscripten::val(err);
  }

  result.set("is_primary", heif_image_handle_is_primary_image(handle));
  result.set("thumbnails", heif_image_handle_get_number_of_thumbnails(handle));
  int width = heif_image_get_width(image, heif_channel_Y);
  result.set("width", width);
  int height = heif_image_get_height(image, heif_channel_Y);
  result.set("height", height);
  std::string data;
  result.set("chroma", heif_image_get_chroma_format(image));
  result.set("colorspace", heif_image_get_colorspace(image));
  switch (heif_image_get_colorspace(image)) {
    case heif_colorspace_YCbCr:
      {
        int stride_y;
        const uint8_t* plane_y = heif_image_get_plane_readonly(image,
            heif_channel_Y, &stride_y);
        int stride_u;
        const uint8_t* plane_u = heif_image_get_plane_readonly(image,
            heif_channel_Cb, &stride_u);
        int stride_v;
        const uint8_t* plane_v = heif_image_get_plane_readonly(image,
            heif_channel_Cr, &stride_v);
        data.resize((width * height) + (width * height / 2));
        char* dest = const_cast<char*>(data.data());
        strided_copy(dest, plane_y, width, height, stride_y);
        strided_copy(dest + (width * height), plane_u,
            width / 2, height / 2, stride_u);
        strided_copy(dest + (width * height) + (width * height / 4),
            plane_v, width / 2, height / 2, stride_v);
      }
      break;
    case heif_colorspace_RGB:
      {
        assert(heif_image_get_chroma_format(image) ==
            heif_chroma_interleaved_24bit);
        int stride_rgb;
        const uint8_t* plane_rgb = heif_image_get_plane_readonly(image,
            heif_channel_interleaved, &stride_rgb);
        data.resize(width * height * 3);
        char* dest = const_cast<char*>(data.data());
        strided_copy(dest, plane_rgb, width * 3, height, stride_rgb);
      }
      break;
    case heif_colorspace_monochrome:
      {
        assert(heif_image_get_chroma_format(image) ==
            heif_chroma_monochrome);
        int stride_grey;
        const uint8_t* plane_grey = heif_image_get_plane_readonly(image,
            heif_channel_Y, &stride_grey);
        data.resize(width * height);
        char* dest = const_cast<char*>(data.data());
        strided_copy(dest, plane_grey, width, height, stride_grey);
      }
      break;
    default:
      // Should never reach here.
      break;
  }
  result.set("data", std::move(data));
  heif_image_release(image);
  return result;
}

#define EXPORT_HEIF_FUNCTION(name) \
  emscripten::function(#name, &name, emscripten::allow_raw_pointers())

EMSCRIPTEN_BINDINGS(libheif) {
  EXPORT_HEIF_FUNCTION(heif_get_version);
  EXPORT_HEIF_FUNCTION(heif_get_version_number);

  EXPORT_HEIF_FUNCTION(heif_context_alloc);
  EXPORT_HEIF_FUNCTION(heif_context_free);
  emscripten::function("heif_context_read_from_memory",
      &_heif_context_read_from_memory, emscripten::allow_raw_pointers());
  EXPORT_HEIF_FUNCTION(heif_context_get_number_of_top_level_images);
  emscripten::function("heif_js_context_get_image_handle",
      &heif_js_context_get_image_handle, emscripten::allow_raw_pointers());
  emscripten::function("heif_js_decode_image",
      &heif_js_decode_image, emscripten::allow_raw_pointers());
  EXPORT_HEIF_FUNCTION(heif_image_handle_release);

  emscripten::class_<Error>("Error")
    .constructor<>()
    .class_property("Ok", &Error::Ok)
    .property("error_code", &Error::error_code)
    .property("sub_error_code", &Error::sub_error_code)
    ;

  emscripten::class_<BitstreamRange>("BitstreamRangeBase")
    ;

  emscripten::class_<EmscriptenBitstreamRange,
      emscripten::base<BitstreamRange>>("BitstreamRange")
    .constructor<const std::string&>()
    .function("error", &EmscriptenBitstreamRange::error)
    ;

  emscripten::class_<Indent>("Indent")
    .constructor<>()
    .function("get_indent", &Indent::get_indent)
    ;

  emscripten::class_<BoxHeader>("BoxHeader")
    .function("get_box_size", &BoxHeader::get_box_size)
    .function("get_header_size", &BoxHeader::get_header_size)
    .function("get_short_type", &BoxHeader::get_short_type)
    .function("get_type_string", &BoxHeader::get_type_string)
    .function("dump", &dump_box_header, emscripten::allow_raw_pointers())
    ;

  emscripten::class_<Box, emscripten::base<BoxHeader>>("Box")
    .class_function("read", &Box_read, emscripten::allow_raw_pointers())
    .function("get_child_box", &Box::get_child_box)
    .function("dump", &dump_box, emscripten::allow_raw_pointers())
    .smart_ptr<std::shared_ptr<Box>>("Box")
    ;

  emscripten::class_<HeifFile>("HeifFile")
    .constructor<>()
    .function("read_from_memory", &HeifFile_read_from_memory,
        emscripten::allow_raw_pointers())
    .function("get_num_images", &HeifFile::get_num_images)
    .function("get_primary_image_ID", &HeifFile::get_primary_image_ID)
    .function("get_item_IDs", &HeifFile::get_item_IDs)
    .function("get_compressed_image_data", &HeifFile_get_compressed_image_data,
        emscripten::allow_raw_pointers())
    ;

  emscripten::enum_<heif_error_code>("heif_error_code")
    .value("heif_error_Ok", heif_error_Ok)
    .value("heif_error_Input_does_not_exist", heif_error_Input_does_not_exist)
    .value("heif_error_Invalid_input", heif_error_Invalid_input)
    .value("heif_error_Unsupported_filetype", heif_error_Unsupported_filetype)
    .value("heif_error_Unsupported_feature", heif_error_Unsupported_feature)
    .value("heif_error_Usage_error", heif_error_Usage_error)
    .value("heif_error_Memory_allocation_error", heif_error_Memory_allocation_error)
    ;
  emscripten::enum_<heif_suberror_code>("heif_suberror_code")
    .value("heif_suberror_Unspecified", heif_suberror_Unspecified)
    .value("heif_suberror_End_of_data", heif_suberror_End_of_data)
    .value("heif_suberror_Invalid_box_size", heif_suberror_Invalid_box_size)
    .value("heif_suberror_No_ftyp_box", heif_suberror_No_ftyp_box)
    .value("heif_suberror_No_idat_box", heif_suberror_No_idat_box)
    .value("heif_suberror_No_meta_box", heif_suberror_No_meta_box)
    .value("heif_suberror_No_hdlr_box", heif_suberror_No_hdlr_box)
    .value("heif_suberror_No_pitm_box", heif_suberror_No_pitm_box)
    .value("heif_suberror_No_ipco_box", heif_suberror_No_ipco_box)
    .value("heif_suberror_No_ipma_box", heif_suberror_No_ipma_box)
    .value("heif_suberror_No_iloc_box", heif_suberror_No_iloc_box)
    .value("heif_suberror_No_iinf_box", heif_suberror_No_iinf_box)
    .value("heif_suberror_No_iprp_box", heif_suberror_No_iprp_box)
    .value("heif_suberror_No_iref_box", heif_suberror_No_iref_box)
    .value("heif_suberror_No_pict_handler", heif_suberror_No_pict_handler)
    .value("heif_suberror_Ipma_box_references_nonexisting_property",heif_suberror_Ipma_box_references_nonexisting_property)
    .value("heif_suberror_No_properties_assigned_to_item",heif_suberror_No_properties_assigned_to_item)
    .value("heif_suberror_No_item_data",heif_suberror_No_item_data)
    .value("heif_suberror_Invalid_grid_data",heif_suberror_Invalid_grid_data)
    .value("heif_suberror_Missing_grid_images",heif_suberror_Missing_grid_images)
    .value("heif_suberror_Security_limit_exceeded",heif_suberror_Security_limit_exceeded)
    .value("heif_suberror_Nonexisting_image_referenced",heif_suberror_Nonexisting_image_referenced)
    .value("heif_suberror_Null_pointer_argument",heif_suberror_Null_pointer_argument)
    .value("heif_suberror_Unsupported_codec",heif_suberror_Unsupported_codec)
    .value("heif_suberror_Unsupported_image_type",heif_suberror_Unsupported_image_type)
    ;
  emscripten::enum_<heif_compression_format>("heif_compression_format")
    .value("heif_compression_undefined", heif_compression_undefined)
    .value("heif_compression_HEVC", heif_compression_HEVC)
    .value("heif_compression_AVC", heif_compression_AVC)
    .value("heif_compression_JPEG", heif_compression_JPEG)
    ;
  emscripten::enum_<heif_chroma>("heif_chroma")
    .value("heif_chroma_undefined", heif_chroma_undefined)
    .value("heif_chroma_monochrome", heif_chroma_monochrome)
    .value("heif_chroma_420", heif_chroma_420)
    .value("heif_chroma_422", heif_chroma_422)
    .value("heif_chroma_444", heif_chroma_444)
    .value("heif_chroma_interleaved_24bit", heif_chroma_interleaved_24bit)
    ;
  emscripten::enum_<heif_colorspace>("heif_colorspace")
    .value("heif_colorspace_undefined", heif_colorspace_undefined)
    .value("heif_colorspace_YCbCr", heif_colorspace_YCbCr)
    .value("heif_colorspace_RGB", heif_colorspace_RGB)
    .value("heif_colorspace_monochrome", heif_colorspace_monochrome)
    ;
  emscripten::enum_<heif_channel>("heif_channel")
    .value("heif_channel_Y", heif_channel_Y)
    .value("heif_channel_Cr", heif_channel_Cr)
    .value("heif_channel_Cb", heif_channel_Cb)
    .value("heif_channel_R", heif_channel_R)
    .value("heif_channel_G", heif_channel_G)
    .value("heif_channel_B", heif_channel_B)
    .value("heif_channel_Alpha", heif_channel_Alpha)
    .value("heif_channel_interleaved", heif_channel_interleaved)
    ;

  emscripten::register_vector<std::string>("StringVector");
  emscripten::register_vector<uint32_t>("UInt32Vector");
  emscripten::class_<heif_context>("heif_context");
  emscripten::class_<heif_image_handle>("heif_image_handle");
  emscripten::class_<heif_image>("heif_image");
  emscripten::value_object<heif_error>("heif_error")
    .field("code", &heif_error::code)
    .field("subcode", &heif_error::subcode)
    ;
}

}  // namespace heif

#endif  // LIBHEIF_BOX_EMSCRIPTEN_H
