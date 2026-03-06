/*
  libheif example application "heif".

  MIT License

  Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>

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

#include "decoder_y4m.h"
#include <iostream>
#include <fstream>
#include <memory>
#include <string>

static struct heif_error heif_error_ok = {heif_error_Ok, heif_suberror_Unspecified, "Success"};

heif_error loadY4M(const char *filename, InputImage *input_image)
{

  struct heif_image* image = nullptr;


  // open input file

  std::ifstream istr(filename, std::ios_base::binary);
  if (istr.fail()) {
    struct heif_error err = {
      .code = heif_error_Invalid_input,
      .subcode = heif_suberror_Unspecified,
      .message = "Cannot open Y4M file"};
    return err;
  }


  std::string header;
  getline(istr, header);

  if (header.find("YUV4MPEG2 ") != 0) {
    struct heif_error err = {
      .code = heif_error_Unsupported_feature,
      .subcode = heif_suberror_Unspecified,
      .message = "Input is not a Y4M file."};
    return err;
  }

  int w = -1;
  int h = -1;

  size_t pos = 0;
  for (;;) {
    pos = header.find(' ', pos + 1) + 1;
    if (pos == std::string::npos) {
      break;
    }

    size_t end = header.find_first_of(" \n", pos + 1);
    if (end == std::string::npos) {
      break;
    }

    if (end - pos <= 1) {
      struct heif_error err = {
        .code = heif_error_Unsupported_feature,
        .subcode = heif_suberror_Unspecified,
        .message = "Header format error in Y4M file."};
      return err;
    }

    char tag = header[pos];
    std::string value = header.substr(pos + 1, end - pos - 1);
    if (tag == 'W') {
      w = atoi(value.c_str());
    }
    else if (tag == 'H') {
      h = atoi(value.c_str());
    }
  }

  std::string frameheader;
  getline(istr, frameheader);

  if (frameheader != "FRAME") {
    struct heif_error err = {
        .code = heif_error_Unsupported_feature,
        .subcode = heif_suberror_Unspecified,
        .message = "Y4M misses the frame header."};
    return err;
  }

  if (w < 0 || h < 0) {
    struct heif_error err = {
        .code = heif_error_Unsupported_feature,
        .subcode = heif_suberror_Unspecified,
        .message = "Y4M has invalid frame size."};
    return err;
  }

  struct heif_error err = heif_image_create(w, h,
                                            heif_colorspace_YCbCr,
                                            heif_chroma_420,
                                            &image);
  if (err.code != heif_error_Ok) {
    return err;
  }

  heif_image_add_plane(image, heif_channel_Y, w, h, 8);
  heif_image_add_plane(image, heif_channel_Cb, (w + 1) / 2, (h + 1) / 2, 8);
  heif_image_add_plane(image, heif_channel_Cr, (w + 1) / 2, (h + 1) / 2, 8);

  size_t y_stride, cb_stride, cr_stride;
  uint8_t* py = heif_image_get_plane2(image, heif_channel_Y, &y_stride);
  uint8_t* pcb = heif_image_get_plane2(image, heif_channel_Cb, &cb_stride);
  uint8_t* pcr = heif_image_get_plane2(image, heif_channel_Cr, &cr_stride);

  for (int y = 0; y < h; y++) {
    istr.read((char*) (py + y * y_stride), w);
  }

  for (int y = 0; y < (h + 1) / 2; y++) {
    istr.read((char*) (pcb + y * cb_stride), (w + 1) / 2);
  }

  for (int y = 0; y < (h + 1) / 2; y++) {
    istr.read((char*) (pcr + y * cr_stride), (w + 1) / 2);
  }

  input_image->image = std::shared_ptr<heif_image>(image,
                                                   [](heif_image* img) { heif_image_release(img); });

  return heif_error_ok;
}
