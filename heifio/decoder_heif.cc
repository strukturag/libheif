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

#include <memory>
#include <string>
#include <vector>

#include "decoder_heif.h"

static struct heif_error heif_error_ok = {heif_error_Ok, heif_suberror_Unspecified, "Success"};

// libheif's heif_error.message is owned by the heif_context (or heif_image_handle)
// and becomes invalid once the owning object is released. Copy the message into a
// thread-local string so it stays valid for the caller after we have released the
// context, and so concurrent callers on different threads do not race on the buffer.
// The caller is expected to log the error promptly (heif-enc calls exit() immediately).
static heif_error stable_error(const heif_error& err)
{
  static thread_local std::string msg;
  msg = err.message ? err.message : "";
  return {err.code, err.subcode, msg.c_str()};
}


heif_error loadHEIF(const char* filename, InputImage* input_image)
{
  heif_context* ctx = heif_context_alloc();
  if (!ctx) {
    return {heif_error_Memory_allocation_error, heif_suberror_Unspecified,
            "Cannot allocate HEIF context"};
  }

  heif_error err = heif_context_read_from_file(ctx, filename, nullptr);
  if (err.code != heif_error_Ok) {
    heif_error stable = stable_error(err);
    heif_context_free(ctx);
    return stable;
  }

  heif_item_id primary_id;
  err = heif_context_get_primary_image_ID(ctx, &primary_id);
  if (err.code != heif_error_Ok) {
    heif_error stable = stable_error(err);
    heif_context_free(ctx);
    return stable;
  }

  heif_image_handle* handle = nullptr;
  err = heif_context_get_image_handle(ctx, primary_id, &handle);
  if (err.code != heif_error_Ok) {
    heif_error stable = stable_error(err);
    heif_context_free(ctx);
    return stable;
  }

  // Decode in the file's native colorspace/chroma so the encoder side does not have
  // to perform any extra colorspace conversion. Transforms (irot/imir) are applied
  // during decode (default ignore_transformations=0), so the resulting pixels are
  // already in display orientation and input_image->orientation stays 'normal'.

  heif_decoding_options* opts = heif_decoding_options_alloc();
  opts->output_image_nclx_profile_passthrough = true;

  heif_image* image = nullptr;
  err = heif_decode_image(handle, &image,
                          heif_colorspace_undefined, heif_chroma_undefined,
                          opts);

  heif_decoding_options_free(opts);

  if (err.code != heif_error_Ok) {
    heif_error stable = stable_error(err);
    heif_image_handle_release(handle);
    heif_context_free(ctx);
    return stable;
  }

  input_image->image = std::shared_ptr<heif_image>(image,
                                                   [](heif_image* img) { heif_image_release(img); });

  // Extract EXIF and XMP metadata. Pass the bytes through verbatim, including the
  // 4-byte TIFF-offset prefix that libheif's EXIF item format carries — the encoder
  // side (heif_context_add_exif_metadata) expects the same layout.

  int numMetadata = heif_image_handle_get_number_of_metadata_blocks(handle, nullptr);
  if (numMetadata > 0) {
    std::vector<heif_item_id> ids(numMetadata);
    heif_image_handle_get_list_of_metadata_block_IDs(handle, nullptr, ids.data(), numMetadata);

    for (int n = 0; n < numMetadata; n++) {
      std::string itemtype = heif_image_handle_get_metadata_type(handle, ids[n]);
      std::string contenttype = heif_image_handle_get_metadata_content_type(handle, ids[n]);

      if (itemtype == "Exif" && input_image->exif.empty()) {
        size_t size = heif_image_handle_get_metadata_size(handle, ids[n]);
        input_image->exif.resize(size);
        heif_error e = heif_image_handle_get_metadata(handle, ids[n], input_image->exif.data());
        if (e.code != heif_error_Ok) {
          input_image->exif.clear();
        }
      }
      else if (contenttype == "application/rdf+xml" && input_image->xmp.empty()) {
        size_t size = heif_image_handle_get_metadata_size(handle, ids[n]);
        input_image->xmp.resize(size);
        heif_error e = heif_image_handle_get_metadata(handle, ids[n], input_image->xmp.data());
        if (e.code != heif_error_Ok) {
          input_image->xmp.clear();
        }
      }
    }
  }

  heif_image_handle_release(handle);
  heif_context_free(ctx);

  return heif_error_ok;
}
