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
  if (error != Error::OK) {
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
    return Error(Error::Unsupported, Error::Unspecified);
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
  if (err != Error::OK) {
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

  emscripten::enum_<Error::ErrorCode>("ErrorCode")
    .value("Ok", Error::ErrorCode::Ok)
    .value("InvalidInput", Error::ErrorCode::InvalidInput)
    .value("NonexistingImage", Error::ErrorCode::NonexistingImage)
    .value("Unsupported", Error::ErrorCode::Unsupported)
    .value("MemoryAllocationError", Error::ErrorCode::MemoryAllocationError)
    ;
  emscripten::enum_<Error::SubErrorCode>("SubErrorCode")
    .value("Unspecified", Error::SubErrorCode::Unspecified)
    .value("ParseError", Error::SubErrorCode::ParseError)
    .value("EndOfData", Error::SubErrorCode::EndOfData)
    .value("NoCompatibleBrandType", Error::SubErrorCode::NoCompatibleBrandType)
    .value("NoMetaBox", Error::SubErrorCode::NoMetaBox)
    .value("NoHdlrBox", Error::SubErrorCode::NoHdlrBox)
    .value("NoPitmBox", Error::SubErrorCode::NoPitmBox)
    .value("NoIprpBox", Error::SubErrorCode::NoIprpBox)
    .value("NoIpcoBox", Error::SubErrorCode::NoIpcoBox)
    .value("NoIpmaBox", Error::SubErrorCode::NoIpmaBox)
    .value("NoIlocBox", Error::SubErrorCode::NoIlocBox)
    .value("NoIinfBox", Error::SubErrorCode::NoIinfBox)
    .value("NoIdatBox", Error::SubErrorCode::NoIdatBox)
    .value("NoPictHandler", Error::SubErrorCode::NoPictHandler)
    .value("NoPropertiesForItemID", Error::SubErrorCode::NoPropertiesForItemID)
    .value("NonexistingPropertyReferenced", Error::SubErrorCode::NonexistingPropertyReferenced)
    .value("UnsupportedImageType", Error::SubErrorCode::UnsupportedImageType)
    .value("NoInputDataInFile", Error::SubErrorCode::NoInputDataInFile)
    ;

  emscripten::register_vector<std::string>("StringVector");
  emscripten::register_vector<uint32_t>("UInt32Vector");
}

}  // namespace heif

#endif  // LIBHEIF_BOX_EMSCRIPTEN_H
