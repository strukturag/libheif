/*
 * libheif example application "convert".
 * Copyright (c) 2017 struktur AG, Joachim Bauch <bauch@struktur.de>
 *
 * This file is part of convert, an example application using libheif.
 *
 * convert is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * convert is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with convert.  If not, see <http://www.gnu.org/licenses/>.
 */
#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <fstream>
#include <iostream>
#include <sstream>

#include "box.h"
#include "heif_file.h"
#include "libde265/de265.h"

#include "encoder.h"
#if HAVE_LIBJPEG
#include "encoder_jpeg.h"
#endif
#if HAVE_LIBPNG
#include "encoder_png.h"
#endif

using namespace heif;

int main(int argc, char** argv)
{
  using heif::BoxHeader;
  using heif::Box;
  using heif::fourcc;

  if (argc < 3) {
    fprintf(stderr, "USAGE: %s <filename> <output>\n", argv[0]);
    return 1;
  }

  std::string input_filename(argv[1]);
  std::string output_filename(argv[2]);
  std::ifstream istr(input_filename.c_str());

  std::unique_ptr<Encoder> encoder;
#if HAVE_LIBJPEG
  if (output_filename.find(".jpg") == output_filename.size() - 4) {
    // TODO(jojo): Should this be configurable?
    static const int kJpegQuality = 90;
    encoder.reset(new JpegEncoder(kJpegQuality));
  }
#endif  // HAVE_LIBJPEG
#if HAVE_LIBPNG
  if (output_filename.find(".png") == output_filename.size() - 4) {
    encoder.reset(new PngEncoder());
  }
#endif  // HAVE_LIBPNG
  if (!encoder) {
    fprintf(stderr, "Unknown file type in %s\n", output_filename.c_str());
    return 1;
  }

  uint64_t maxSize = std::numeric_limits<uint64_t>::max();
  heif::BitstreamRange range(&istr, maxSize);

  std::shared_ptr<Box_meta> meta_box;

  for (;;) {
    std::shared_ptr<Box> box;
    Error error = Box::read(range, &box);
    if (error != Error::OK || range.error() || range.eof()) {
      break;
    }

    if (box->get_short_type() == fourcc("meta")) {
      meta_box = std::dynamic_pointer_cast<Box_meta>(box);
      break;
    }
  }
  if (!meta_box) {
    fprintf(stderr, "Not a valid HEIF file (no 'meta' box found)\n");
    return 1;
  }

  std::vector<std::vector<uint8_t>> images;
  std::ifstream istr2(input_filename.c_str());
  if (!meta_box->get_images(istr2, &images)) {
    fprintf(stderr, "Not a valid HEIF file (could not get images)\n");
    return 1;
  }

  if (images.empty()) {
    fprintf(stderr, "File doesn't contain any images\n");
    return 1;
  }

  de265_decoder_context* ctx = de265_new_decoder();
  de265_start_worker_threads(ctx,1);
  for (const auto& item : images) {
    de265_push_data(ctx, item.data(), item.size(), 0, nullptr);
  }
#if LIBDE265_NUMERIC_VERSION >= 0x02000000
  de265_push_end_of_stream(ctx);
#else
  de265_flush_data(ctx);
#endif

  std::string filename;
  size_t image_index = 1;  // Image filenames are "1" based.
  printf("File contains %zu images\n", images.size());
#if LIBDE265_NUMERIC_VERSION >= 0x02000000
  #error "Decoding with newer versions of libde265 is not implemented yet."
#else
  int more;
  de265_error err;
  do {
    more = 0;
    err = de265_decode(ctx, &more);
    if (err != DE265_OK) {
      printf("Error decoding: %s (%d)\n", de265_get_error_text(err), err);
      break;
    }

    const struct de265_image* image = de265_get_next_picture(ctx);
    if (image) {
      printf("Decoded image: %d/%d\n", de265_get_image_width(image, 0),
          de265_get_image_height(image, 0));
      if (images.size() > 1) {
        std::ostringstream s;
        s << output_filename.substr(0, output_filename.find('.'));
        s << "-" << image_index;
        s << output_filename.substr(output_filename.find('.'));
        filename.assign(s.str());
      } else {
        filename.assign(output_filename);
      }
      bool written = encoder->Encode(image, filename.c_str());
      de265_release_next_picture(ctx);
      if (!written) {
        break;
      }

      printf("Written to %s\n", filename.c_str());
      image_index++;
    }
  } while (more);
#endif
  de265_free_decoder(ctx);
  return 0;
}
