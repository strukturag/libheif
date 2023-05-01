/*
  libheif example application "heif".

  MIT License

  Copyright (c) 2023, Dirk Farin <dirk.farin@gmail.com>

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


InputImage loadY4M(const char* filename)
{
  InputImage inputimage;

  struct heif_image* image = nullptr;


  // open input file

  std::ifstream istr(filename, std::ios_base::binary);
  if (istr.fail()) {
    std::cerr << "Can't open " << filename << "\n";
    exit(1);
  }


  std::string header;
  getline(istr, header);

  if (header.find("YUV4MPEG2 ") != 0) {
    std::cerr << "Input is not a Y4M file.\n";
    exit(1);
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
      std::cerr << "Header format error in Y4M file.\n";
      exit(1);
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
    std::cerr << "Y4M misses the frame header.\n";
    exit(1);
  }

  if (w < 0 || h < 0) {
    std::cerr << "Y4M has invalid frame size.\n";
    exit(1);
  }

  struct heif_error err = heif_image_create(w, h,
                                            heif_colorspace_YCbCr,
                                            heif_chroma_420,
                                            &image);
  (void) err;
  // TODO: handle error

  heif_image_add_plane(image, heif_channel_Y, w, h, 8);
  heif_image_add_plane(image, heif_channel_Cb, (w + 1) / 2, (h + 1) / 2, 8);
  heif_image_add_plane(image, heif_channel_Cr, (w + 1) / 2, (h + 1) / 2, 8);

  int y_stride, cb_stride, cr_stride;
  uint8_t* py = heif_image_get_plane(image, heif_channel_Y, &y_stride);
  uint8_t* pcb = heif_image_get_plane(image, heif_channel_Cb, &cb_stride);
  uint8_t* pcr = heif_image_get_plane(image, heif_channel_Cr, &cr_stride);

  for (int y = 0; y < h; y++) {
    istr.read((char*) (py + y * y_stride), w);
  }

  for (int y = 0; y < (h + 1) / 2; y++) {
    istr.read((char*) (pcb + y * cb_stride), (w + 1) / 2);
  }

  for (int y = 0; y < (h + 1) / 2; y++) {
    istr.read((char*) (pcr + y * cr_stride), (w + 1) / 2);
  }

  inputimage.image = std::shared_ptr<heif_image>(image,
                                                 [](heif_image* img) { heif_image_release(img); });

  return inputimage;
}
