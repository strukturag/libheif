/*
  libheif example application.

  MIT License

  Copyright (c) 2017 struktur AG, Joachim Bauch <bauch@struktur.de>

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
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <vector>
#include "webp/mux.h"
#include "webp/encode.h"

#include "encoder_webp.h"

#include "common_utils.h"
#include "exif.h"

WebpEncoder::WebpEncoder(int quality) : quality_(quality)
{
}

// Returns false if ICC profile recognized invalid and could not be fixed.
bool fix_icc_profile(const uint8_t* profile_data, size_t& profile_size)
{
  if (profile_size < 128) {
    return false;
  }


  // --- check that profile size specified in header matches the real size

  uint32_t size_in_header = four_bytes_to_uint32(profile_data[0],
                                                 profile_data[1],
                                                 profile_data[2],
                                                 profile_data[3]);

  if (size_in_header != profile_size) {

    // Size in header is smaller than actual size, but alignment indicates that it might
    // be correct. Replace real data length with size in header.
    if (size_in_header < profile_size && (size_in_header & 3)==0) {
      fprintf(stderr, "Input ICC profile has wrong size in header (%d instead of %d). Skipping extra bytes at the end. "
              "Note that this may still be incorrect and the ICC profile may be broken.\n", size_in_header, profile_size);

      profile_size = size_in_header;
    }
    else {
      return false;
    }
  }

  return true;
}

void WebpEncoder::UpdateDecodingOptions(const struct heif_image_handle* handle,
    struct heif_decoding_options* options) const
{
  options->convert_hdr_to_8bit = 1;
}

static int WebPWriter(const uint8_t* data, size_t data_size,
  const WebPPicture* picture) {
  std::vector<uint8_t>* vec = (std::vector<uint8_t>*)picture->custom_ptr;
  assert(vec);
  size_t size = vec->size();
  vec->resize(size + data_size);
  memcpy(vec->data() + size, data, data_size);
  return 1;
}

bool WebpEncoder::Encode(const heif_image_handle* handle,
                        const heif_image* image, const std::string& filename)
{
  int width = heif_image_get_primary_width(image);
  int height = heif_image_get_primary_height(image);
  if (width > WEBP_MAX_DIMENSION || height > WEBP_MAX_DIMENSION || width <= 0 || height <= 0) {
    fprintf(stderr, "Image dimension is too large for WEBP\n");
    return false;
  }
  WebPConfig cfg;
  if (!WebPConfigInit(&cfg)) {
    fprintf(stderr, "libwebp init failed!\n");
    return false;
  }
  bool islossless = quality_ == 100;
  if (islossless) {
    if(!WebPConfigLosslessPreset(&cfg, 6)){
      fprintf(stderr, "libwebp init failed!\n");
      return false;
    }
  }
  else {
    cfg.quality = quality_;
    cfg.alpha_quality = quality_;
  }
  cfg.thread_level = 1;

  WebPPicture webp;
  if (!WebPPictureInit(&webp)) {
    fprintf(stderr, "libwebp init failed!\n");
    return false;
  }
  webp.width = width;
  webp.height = height;
  if (!islossless) { // Lossy
    webp.y = (uint8_t*)heif_image_get_plane_readonly(image, heif_channel_Y,
      &webp.y_stride);
    webp.u = (uint8_t*)heif_image_get_plane_readonly(image, heif_channel_Cb,
      &webp.uv_stride);
    webp.v = (uint8_t*)heif_image_get_plane_readonly(image, heif_channel_Cr,
      &webp.uv_stride);
    webp.a = (uint8_t*)heif_image_get_plane_readonly(image, heif_channel_Alpha,
      &webp.a_stride);
    if (webp.a) {
      webp.colorspace = WEBP_YUV420A;
    }
    else {
      webp.colorspace = WEBP_YUV420;
    }
  }
  else { // Lossless
    const uint8_t* rgba;
    size_t rgba_stride;
    int ok;
    rgba = heif_image_get_plane_readonly2(image, heif_channel_interleaved,
      &rgba_stride);
    assert(rgba);
    heif_chroma format = heif_image_get_chroma_format(image);
    if(format == heif_chroma_interleaved_RGBA)
      ok = WebPPictureImportRGBA(&webp, rgba, rgba_stride);
    else if(format == heif_chroma_interleaved_RGB)
      ok = WebPPictureImportRGB(&webp, rgba, rgba_stride);
    else
      ok = 0;
    if (!ok) {
      fprintf(stderr, "libwebp framebuffer allocation failed!\n");
      return false;
    }
  }
  std::vector<uint8_t> mem;
  webp.custom_ptr = &mem;
  webp.writer = WebPWriter;
  if (!WebPEncode(&cfg, &webp)) {
    fprintf(stderr, "Error while encoding image\n");
    if (islossless)
      WebPPictureFree(&webp);
    return false;
  }
  if (islossless)
    WebPPictureFree(&webp);

  FILE* fp = fopen(filename.c_str(), "wb");
  if (!fp) {
    fprintf(stderr, "Can't open %s: %s\n", filename.c_str(), strerror(errno));
    return false;
  }
  std::unique_ptr<FILE, int(*)(FILE*)> fp_deleter(fp, &fclose);
  WebPMux* mux = WebPMuxNew();
  if (!mux) {
    fprintf(stderr, "Error creating WEBP muxer\n");
    return false;
  }
  std::unique_ptr<WebPMux, void(*)(WebPMux*)> mux_deleter(mux, &WebPMuxDelete);
  WebPData bitstream;
  WebPDataInit(&bitstream);
  bitstream.bytes = mem.data();
  bitstream.size = mem.size();
  if (WebPMuxSetImage(mux, &bitstream, 0) != WEBP_MUX_OK) {
    fprintf(stderr, "Error setting WEBP image\n");
    return false;
  }

  // --- write ICC profile

  if (handle) {
    WebPData icc_bitstream;
    WebPDataInit(&icc_bitstream);
    icc_bitstream.size = heif_image_handle_get_raw_color_profile_size(handle);
    if (icc_bitstream.size > 0) {
        icc_bitstream.bytes = static_cast<uint8_t*>(malloc(icc_bitstream.size));
      heif_image_handle_get_raw_color_profile(handle, (void*)icc_bitstream.bytes);
      if (fix_icc_profile(icc_bitstream.bytes, icc_bitstream.size)) {
        if (WebPMuxSetChunk(mux, "ICCP", &icc_bitstream, 1) != WEBP_MUX_OK) {
          fprintf(stderr, "Error setting image ICC\n");
        }
      }
      else {
        fprintf(stderr, "Invalid ICC profile. Writing PNG file without ICC.\n");
      }
      free((void*)icc_bitstream.bytes);
    }
  }

  // --- write EXIF metadata
  if (handle) {
    WebPData exif_bitstream;
    WebPDataInit(&exif_bitstream);
    exif_bitstream.bytes = GetExifMetaData(handle, &exif_bitstream.size);
    if (exif_bitstream.bytes) {
      if (exif_bitstream.size > 4) {
        uint32_t skip = four_bytes_to_uint32(exif_bitstream.bytes[0], exif_bitstream.bytes[1], exif_bitstream.bytes[2], exif_bitstream.bytes[3]);
        if (skip < (exif_bitstream.size - 4)) {
          skip += 4;
          uint8_t* ptr = (uint8_t*)exif_bitstream.bytes + skip;
          size_t size = exif_bitstream.size - skip;

          // libheif by default normalizes the image orientation, so that we have to set the EXIF Orientation to "Horizontal (normal)"
          modify_exif_orientation_tag_if_it_exists(ptr, (int) size, 1);
          overwrite_exif_image_size_if_it_exists(ptr, (int) size, width, height);

          if(WebPMuxSetChunk(mux, "EXIF", &exif_bitstream, 1) != WEBP_MUX_OK) {
            fprintf(stderr, "Error setting image EXIF\n");
          }
        }
      }

      free((uint8_t*)exif_bitstream.bytes);
    }
  }

  // --- write XMP metadata

  if (handle) {
    // spec: https://raw.githubusercontent.com/adobe/xmp-docs/master/XMPSpecifications/XMPSpecificationPart3.pdf
    std::vector<uint8_t> xmp = get_xmp_metadata(handle);
    if (!xmp.empty()) {
      // make sure that XMP string is always null terminated.
      if (xmp.back() != 0) {
        xmp.push_back(0);
      }

      WebPData xmp_bitstream;
      WebPDataInit(&xmp_bitstream);
      xmp_bitstream.bytes = xmp.data();
      xmp_bitstream.size = xmp.size();
      if(WebPMuxSetChunk(mux, "XMP ", &xmp_bitstream, 1) != WEBP_MUX_OK) {
        fprintf(stderr, "Error setting image XMP\n");
      }
    }
  }

  WebPData webp_data;
  WebPDataInit(&webp_data);
  if (WebPMuxAssemble(mux, &webp_data) != WEBP_MUX_OK) {
    fprintf(stderr, "Error while encoding image\n");
    WebPDataClear(&webp_data);
    return false;
  };
  std::unique_ptr<WebPData, void(*)(WebPData*)> webp_data_deleter(&webp_data, &WebPDataClear);
  if (fwrite(webp_data.bytes, 1, webp_data.size, fp) != webp_data.size) {
    fprintf(stderr, "Writing WEBP file failed\n");
    return false;
  }
  return true;
}
