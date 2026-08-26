/*
  libheif test support utilities

  MIT License

  Copyright (c) 2023 Brad Hards <bradh@frogmouth.net>

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

#include <cstdint>
#include <string>
#include <vector>
#include "libheif/heif.h"

#include <filesystem>

namespace fs = std::filesystem;

heif_context * get_context_for_test_file(std::string filename);
heif_context * get_context_for_local_file(std::string filename);

heif_image_handle * get_primary_image_handle(heif_context *context);

heif_image * get_primary_image(heif_image_handle * handle);
heif_image * get_primary_image_mono(heif_image_handle * handle);
heif_image * get_primary_image_ycbcr(heif_image_handle * handle, heif_chroma chroma);

void fill_new_plane(heif_image* img, heif_channel channel, int w, int h);

heif_image * createImage_RGB_planar();

std::string get_path_for_heifio_test_file(std::string filename);

heif_encoder* get_encoder_or_skip_test(heif_compression_format format);

fs::path get_tests_output_dir();

std::string get_tests_output_file_path(const char* filename);


// --- Helpers to build ISOBMFF byte streams for synthetic test files ---

// Append a big-endian integer.
void put_u32_be(std::vector<uint8_t>& out, uint32_t v);
void put_u16_be(std::vector<uint8_t>& out, uint16_t v);

// Append the four characters of a box type or brand.
void append_fourcc(std::vector<uint8_t>& out, const char fourcc[4]);

// Append a null-terminated string, including the terminator.
void append_cstr(std::vector<uint8_t>& out, const char* s);

// Append the contents of another byte vector.
void append(std::vector<uint8_t>& out, const std::vector<uint8_t>& v);

// Build a box, or a full box with version and flags, from its type and payload.
std::vector<uint8_t> make_box(const char fourcc[4],
                              const std::vector<uint8_t>& payload,
                              bool is_full_box = false,
                              uint8_t version = 0,
                              uint32_t flags = 0);
