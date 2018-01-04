#ifndef LIBHEIF_BOX_EMSCRIPTEN_H
#define LIBHEIF_BOX_EMSCRIPTEN_H

#include <emscripten/bind.h>

#include <memory>
#include <string>
#include <sstream>
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

  result.set("type", file->get_image_type(ID));
  result.set("data", std::string(reinterpret_cast<char*>(image_data.data()),
      image_data.size()));
  return result;
}

EMSCRIPTEN_BINDINGS(libheif) {
  emscripten::class_<Error>("Error")
    .constructor<>()
    .class_property("OK", &Error::OK)
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
    .function("get_image_IDs", &HeifFile::get_image_IDs)
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

  emscripten::register_vector<std::string>("StringVector");
  emscripten::register_vector<uint32_t>("UInt32Vector");
}

}  // namespace heif

#endif  // LIBHEIF_BOX_EMSCRIPTEN_H
