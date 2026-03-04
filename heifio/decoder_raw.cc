/*
  libheif example application "heif".

  MIT License

  Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include "decoder_raw.h"
#include <libheif/heif.h>
#include <libheif/heif_uncompressed.h>
#include <bit>
#include <cstdio>
#include <cstring>
#include <vector>
#include <memory>
#include <utility>

static struct heif_error heif_error_ok = {heif_error_Ok, heif_suberror_Unspecified, "Success"};


bool parse_raw_pixel_type(const std::string& type_string,
                          heif_channel_datatype* out_datatype,
                          int* out_bit_depth)
{
  if (type_string == "uint8") {
    *out_datatype = heif_channel_datatype_unsigned_integer;
    *out_bit_depth = 8;
  }
  else if (type_string == "sint8") {
    *out_datatype = heif_channel_datatype_signed_integer;
    *out_bit_depth = 8;
  }
  else if (type_string == "uint16") {
    *out_datatype = heif_channel_datatype_unsigned_integer;
    *out_bit_depth = 16;
  }
  else if (type_string == "sint16") {
    *out_datatype = heif_channel_datatype_signed_integer;
    *out_bit_depth = 16;
  }
  else if (type_string == "uint32") {
    *out_datatype = heif_channel_datatype_unsigned_integer;
    *out_bit_depth = 32;
  }
  else if (type_string == "sint32") {
    *out_datatype = heif_channel_datatype_signed_integer;
    *out_bit_depth = 32;
  }
  else if (type_string == "float32") {
    *out_datatype = heif_channel_datatype_floating_point;
    *out_bit_depth = 32;
  }
  else if (type_string == "float64") {
    *out_datatype = heif_channel_datatype_floating_point;
    *out_bit_depth = 64;
  }
  else {
    return false;
  }
  return true;
}


static void byte_swap_buffer(uint8_t* data, size_t num_elements, int bytes_per_element)
{
  for (size_t i = 0; i < num_elements; i++) {
    uint8_t* elem = data + i * bytes_per_element;
    for (int lo = 0, hi = bytes_per_element - 1; lo < hi; lo++, hi--) {
      std::swap(elem[lo], elem[hi]);
    }
  }
}


heif_error loadRAW(const char* filename, const RawImageParameters& params, InputImage* input_image)
{
  if (params.width < 0 || params.height < 0) {
    return {heif_error_Invalid_input, heif_suberror_Unspecified,
            "Raw image width and height must not be negative"};
  }

  if (params.width == 0 && params.height == 0) {
    return {heif_error_Invalid_input, heif_suberror_Unspecified,
            "At least one of --raw-width or --raw-height must be specified"};
  }

  if (params.datatype == heif_channel_datatype_undefined || params.bit_depth <= 0) {
    return {heif_error_Invalid_input, heif_suberror_Unspecified,
            "Raw image pixel type must be specified (use --raw-type)"};
  }

  int bytes_per_pixel = params.bit_depth / 8;

  // Open file and get size
  FILE* fp = fopen(filename, "rb");
  if (!fp) {
    return {heif_error_Invalid_input, heif_suberror_Unspecified,
            "Cannot open raw input file"};
  }

  fseek(fp, 0, SEEK_END);
  long file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (file_size <= 0) {
    fclose(fp);
    return {heif_error_Invalid_input, heif_suberror_Unspecified,
            "Raw input file is empty or unreadable"};
  }

  // Compute missing dimension from file size
  int width = params.width;
  int height = params.height;

  if (width == 0) {
    size_t row_bytes = static_cast<size_t>(height) * bytes_per_pixel;
    if (static_cast<size_t>(file_size) % row_bytes != 0) {
      fclose(fp);
      return {heif_error_Invalid_input, heif_suberror_Unspecified,
              "Raw file size is not divisible by height * bytes_per_pixel"};
    }
    width = static_cast<int>(static_cast<size_t>(file_size) / row_bytes);
  }
  else if (height == 0) {
    size_t row_bytes = static_cast<size_t>(width) * bytes_per_pixel;
    if (static_cast<size_t>(file_size) % row_bytes != 0) {
      fclose(fp);
      return {heif_error_Invalid_input, heif_suberror_Unspecified,
              "Raw file size is not divisible by width * bytes_per_pixel"};
    }
    height = static_cast<int>(static_cast<size_t>(file_size) / row_bytes);
  }

  size_t expected_size = static_cast<size_t>(width) * height * bytes_per_pixel;

  if (static_cast<size_t>(file_size) != expected_size) {
    fclose(fp);
    return {heif_error_Invalid_input, heif_suberror_Unspecified,
            "Raw file size does not match width * height * bytes_per_pixel"};
  }

  // Read entire file
  std::vector<uint8_t> buffer(expected_size);
  size_t n = fread(buffer.data(), 1, expected_size, fp);
  fclose(fp);

  if (n != expected_size) {
    return {heif_error_Invalid_input, heif_suberror_Unspecified,
            "Failed to read raw input file"};
  }

  // Byte-swap from big-endian to host byte order if needed
  if (params.big_endian && bytes_per_pixel > 1) {
    if constexpr (std::endian::native == std::endian::little) {
      byte_swap_buffer(buffer.data(), static_cast<size_t>(width) * height, bytes_per_pixel);
    }
  }

  // Create image with nonvisual colorspace for typed component API
  struct heif_image* image = nullptr;
  heif_error err = heif_image_create(width, height,
                                     heif_colorspace_nonvisual,
                                     heif_chroma_undefined,
                                     &image);
  if (err.code != heif_error_Ok) {
    return err;
  }

  uint32_t component_idx = 0;
  err = heif_image_add_component(image, width, height,
                                 heif_uncompressed_component_type_monochrome,
                                 params.datatype, params.bit_depth,
                                 &component_idx);
  if (err.code != heif_error_Ok) {
    heif_image_release(image);
    return err;
  }

  // Get writable pointer and copy data row by row
  size_t stride = 0;
  uint8_t* plane = heif_image_get_component(image, component_idx, &stride);
  if (!plane) {
    heif_image_release(image);
    return {heif_error_Invalid_input, heif_suberror_Unspecified,
            "Failed to get component plane from image"};
  }

  size_t row_bytes = static_cast<size_t>(width) * bytes_per_pixel;
  size_t stride_bytes = stride;

  for (int y = 0; y < height; y++) {
    memcpy(plane + y * stride_bytes,
           buffer.data() + y * row_bytes,
           row_bytes);
  }

  input_image->image = std::shared_ptr<heif_image>(image,
                                                    [](heif_image* img) { heif_image_release(img); });

  return heif_error_ok;
}
