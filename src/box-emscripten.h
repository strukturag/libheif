#ifndef LIBHEIF_BOX_EMSCRIPTEN_H
#define LIBHEIF_BOX_EMSCRIPTEN_H

#include <emscripten/bind.h>

#include <memory>
#include <string>
#include <sstream>
#include <vector>

#include "box.h"

namespace heif {

static std::vector<std::string> Box_meta_get_images(Box_meta* box,
    const std::string& data) {
  std::vector<std::string> result;
  if (!box) {
    return result;
  }

  std::basic_istringstream<char> s(data);
  std::vector<std::vector<uint8_t>> r;
  if (!box->get_images(s, &r)) {
    return result;
  }

  for (std::vector<uint8_t> v : r) {
    result.push_back(std::string(reinterpret_cast<char*>(v.data()), v.size()));
  }
  return result;
}

static std::string get_headers_string(Box_hvcC* box) {
  if (!box) {
    return "";
  }

  std::vector<uint8_t> r;
  if (!box->get_headers(&r)) {
    return "";
  }
  return std::string(reinterpret_cast<const char*>(r.data()), r.size());
}

static std::string read_all_data_string(Box_iloc* box,
    const std::string& data) {
  if (!box) {
    return "";
  }

  std::basic_istringstream<char> s(data);
  std::vector<uint8_t> r;
  if (!box->read_all_data(s, &r)) {
    return "";
  }
  return std::string(reinterpret_cast<const char*>(r.data()), r.size());
}

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

EMSCRIPTEN_BINDINGS(libheif) {
  emscripten::class_<Error>("Error")
    .constructor<>()
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
    .smart_ptr<std::shared_ptr<Box>>()
    ;

  emscripten::class_<Box_meta, emscripten::base<Box>>("Box_meta")
    .function("get_images", &Box_meta_get_images,
        emscripten::allow_raw_pointers())
    ;

  emscripten::class_<Box_iloc, emscripten::base<Box>>("Box_iloc")
    .function("read_all_data", &read_all_data_string,
        emscripten::allow_raw_pointers())
    ;

  emscripten::class_<Box_hvcC, emscripten::base<Box>>("Box_hvcC")
    .function("get_headers", &get_headers_string,
        emscripten::allow_raw_pointers())
    ;

  emscripten::register_vector<std::string>("StringVector");
}

}  // namespace heif

#endif  // LIBHEIF_BOX_EMSCRIPTEN_H
