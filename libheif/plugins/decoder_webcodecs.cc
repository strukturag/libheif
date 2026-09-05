/*
 * HEIF codec.
 * Copyright (c) 2025 Dirk Farin <dirk.farin@gmail.com>
 *
 * This file is part of libheif.
 *
 * libheif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libheif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libheif.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "decoder_webcodecs.h"
#include "libheif/heif_plugin.h"
#include "codecs/hevc_boxes.h"
#include "bitstream.h"
#include "nalu_utils.h"

#include <algorithm>
#include <assert.h>
#include <cstring>
#include <emscripten/emscripten.h>
#include <cstdio>
#include <emscripten/bind.h>
#include <limits>
#include <memory>
#include <queue>
#include <string>
#include <vector>
#include <utility>


struct NALUnit {
  std::vector<uint8_t> data;
};

struct webcodecs_decoder
{
  std::queue<NALUnit> data_queue;
};

static const char kEmptyString[] = "";
static const char kSuccess[] = "Success";

static const int WEBCODECS_PLUGIN_PRIORITY = 80;

#define MAX_PLUGIN_NAME_LENGTH 80

static char plugin_name[MAX_PLUGIN_NAME_LENGTH];

/** 
 * Decodes a HEVC frame using the browser's WebCodecs API. This implementation
 * prefers hardware decoding when available.
 *
 * As of this writing, most HEIC images will be decoded directly into the NV12
 * pixel format. For images returned in NV12 or planar YUV format (I420, I422,
 * I444), the format will be preserved when returning the data to C++.
 *
 * Any other image format returned by the WebCodecs API will be converted to
 * RGBA before being returned to C++ to ensure that the result can be
 * properly interpreted by the plugin.
 * 
 * Note that the WebCodecs API don't support converting into NV12 format in
 * cases where the native pixel format is something else. That's why RGBA is
 * used as a fallback format, b/c the browser can always convert to it.
 */
EM_JS(emscripten::EM_VAL, decode_with_browser_hevc, (const char *codec_ptr, uintptr_t hvcc_record_ptr, size_t hvcc_record_size, uintptr_t data_ptr, size_t data_size), {
  return Asyncify.handleSleep((callback) => {
    const codec = UTF8ToString(codec_ptr);
    const data = HEAPU8.subarray(data_ptr, data_ptr + data_size);
    const description = HEAPU8.subarray(hvcc_record_ptr, hvcc_record_ptr + hvcc_record_size);
    let returnedError = false;

    function returnError(err) {
      if (!returnedError) {
        returnedError = true;

        console.error(err);
        callback({'error': err.stack});
      }
    }

    function handleEmptyFormat(decoded) {
      // Use the visible rectangle, not the coded rectangle. The coded rectangle
      // may include non-visible padding (HEVC conformance window) that is not
      // part of the image.
      const width = decoded.visibleRect.width;
      const height = decoded.visibleRect.height;
      const canvas = new OffscreenCanvas(width, height);
      const context = canvas.getContext('2d');
      context.drawImage(decoded, 0, 0, width, height);
      const imageData = context.getImageData(0, 0, width, height);
      const data = imageData.data;
      const format = 'RGBA';
      const planes = [{offset: 0, stride: width * 4}];
      callback(Emval.toHandle({
        'buffer': data,
        'format': format,
        'planes': planes,
        'width': width,
        'height': height,
      }));

      decoded.close();
    }

    if (typeof VideoDecoder === 'undefined') {
      returnError(new Error('VideoDecoder API is not available'));

      return;
    }

    const decoder = new VideoDecoder({
      output: (decoded) => {
        // For 10-bit color images, the format is observed to be null. In this
        // case the VideoFrame.copyTo API doesn't work, however, it does work
        // to draw the VideoFrame to a Canvas and then extract the image bytes.
        // Drawing to a canvas is slower than copyTo, so only use it when
        // necessary.
        if (!decoded.format) {
          handleEmptyFormat(decoded);
          return;
        }

        const nativeFormats = ['NV12', 'I420', 'I422', 'I444'];
        const format = nativeFormats.includes(decoded.format) ? decoded.format : 'RGBA';
        const fullRange = decoded.colorSpace ? decoded.colorSpace.fullRange : false;

        // Always operate on the visible rectangle. allocationSize() and
        // copyTo() default to it anyway, but pass it explicitly so that the
        // buffer size, the plane layout and the dimensions reported to C++
        // are guaranteed to describe the same rectangle. The coded rectangle
        // (codedWidth x codedHeight) can be larger because of the HEVC
        // conformance window, which is set by the file being decoded. It must
        // never be used as the geometry of the copied buffer.
        const rect = decoded.visibleRect;
        const width = rect.width;
        const height = rect.height;
        const formatOptions = nativeFormats.includes(format) ?
          {'rect': rect} :
          {'rect': rect, 'format': format, 'colorSpace': 'srgb'};
        const bufferSize = nativeFormats.includes(format) ?
          decoded.allocationSize(formatOptions) :
          width * height * 4;

        const buffer = new Uint8Array(bufferSize);

        Promise.resolve().then(
          () => decoded.copyTo(buffer, formatOptions)
        ).then((planes) => {
          callback(Emval.toHandle({
            'buffer': buffer,
            'format': format,
            'planes': planes,
            'width': width,
            'height': height,
            'fullRange': fullRange,
          }));

          decoded.close();
        }).catch((e) => {
          returnError(e);
        });
      },
      error: (e) => {
        returnError(e);
      }
    });

    try {
      decoder.configure({
        codec,
        hardwareAcceleration: 'prefer-hardware',
        optimizeForLatency: true,
        description,
      });

      const chunk = new EncodedVideoChunk({
        timestamp: 0,
        type: 'key',
        data: data,
      });

      decoder.decode(chunk);
      decoder.flush();
    } catch (e) {
      returnError(e);
    }
  });
});


static std::vector<uint8_t> remove_start_code_emulation2(const uint8_t* sps, size_t size)
{
  std::vector<uint8_t> out_data;

  for (size_t i = 0; i < size; i++) {
    if (i + 2 < size &&
        sps[i] == 0 &&
        sps[i + 1] == 0 &&
        sps[i + 2] == 3) {
      out_data.push_back(0);
      out_data.push_back(0);
      i += 2;
    }
    else {
      out_data.push_back(sps[i]);
    }
  }

  return out_data;
}


// Parses the SPS and fills 'config'. On return, 'width' and 'height' hold the
// size of the visible image (after applying the conformance window), while
// 'coded_width' and 'coded_height' hold the size of the coded picture.
Error parse_sps_for_hvcC_configuration2(const uint8_t* sps, size_t size,
                                       HEVCDecoderConfigurationRecord* config,
                                       uint32_t* width, uint32_t* height,
                                       uint32_t* coded_width, uint32_t* coded_height)
{
  // remove start-code emulation bytes from SPS header stream

  std::vector<uint8_t> sps_no_emul = remove_start_code_emulation2(sps, size);

  sps = sps_no_emul.data();
  size = sps_no_emul.size();


  BitReader reader(sps, size);

  // skip NAL header
  reader.skip_bits(2 * 8);

  // skip VPS ID
  reader.skip_bits(4);

  uint8_t nMaxSubLayersMinus1 = reader.get_bits8(3);

  config->temporal_id_nested = reader.get_bits8(1);

  // --- profile_tier_level ---

  config->general_profile_space = reader.get_bits8(2);
  config->general_tier_flag = reader.get_bits8(1);
  config->general_profile_idc = reader.get_bits8(5);
  config->general_profile_compatibility_flags = reader.get_bits32(32);

  reader.skip_bits(16); // skip reserved bits
  reader.skip_bits(16); // skip reserved bits
  reader.skip_bits(16); // skip reserved bits

  config->general_level_idc = reader.get_bits8(8);

  std::vector<bool> layer_profile_present(nMaxSubLayersMinus1);
  std::vector<bool> layer_level_present(nMaxSubLayersMinus1);

  for (int i = 0; i < nMaxSubLayersMinus1; i++) {
    layer_profile_present[i] = reader.get_bits(1);
    layer_level_present[i] = reader.get_bits(1);
  }

  if (nMaxSubLayersMinus1 > 0) {
    for (int i = nMaxSubLayersMinus1; i < 8; i++) {
      reader.skip_bits(2);
    }
  }

  for (int i = 0; i < nMaxSubLayersMinus1; i++) {
    if (layer_profile_present[i]) {
      reader.skip_bits(2 + 1 + 5);
      reader.skip_bits(32);
      reader.skip_bits(16);
    }

    if (layer_level_present[i]) {
      reader.skip_bits(8);
    }
  }


  // --- SPS continued ---

  Error invalidUVLC{
    heif_error_Invalid_input,
    heif_suberror_Invalid_parameter_value,
    "Invalid variable length code in HEVC SPS header"
  };

  uint32_t dummy, value;
  if (!reader.get_uvlc(&dummy) || // skip seq_parameter_seq_id
      !reader.get_uvlc(&value)) {
    return invalidUVLC;
  }
  if (value > 3) {
    // chroma_format_idc is in the range 0..3 (H.265 section 7.4.3.2.1). The
    // value is later cast to heif_chroma, so it must not be left unchecked.
    return Error{heif_error_Invalid_input,
                 heif_suberror_Invalid_parameter_value,
                 "SPS chroma_format_idc out of range"};
  }
  config->chroma_format = (uint8_t) value;

  if (config->chroma_format == 3) {
    reader.skip_bits(1);
  }

  if (!reader.get_uvlc(width) ||
      !reader.get_uvlc(height)) {
    return invalidUVLC;
  }

  *coded_width = *width;
  *coded_height = *height;

  bool conformance_window = reader.get_bits(1);
  if (conformance_window) {
    uint32_t left, right, top, bottom;
    if (!reader.get_uvlc(&left) ||
        !reader.get_uvlc(&right) ||
        !reader.get_uvlc(&top) ||
        !reader.get_uvlc(&bottom)) {
      return invalidUVLC;
    }

    //printf("conformance borders: %u %u %u %u\n",left,right,top,bottom);

    uint32_t subH = 1, subV = 1;
    if (config->chroma_format == 1) {
      subV = 2;
      subH = 2;
    }
    if (config->chroma_format == 2) { subH = 2; }

    const uint64_t crop_w = (uint64_t)subH * ((uint64_t)left + (uint64_t)right);
    const uint64_t crop_h = (uint64_t)subV * ((uint64_t)top + (uint64_t)bottom);
    if (crop_w > *width || crop_h > *height) {
      return Error{heif_error_Invalid_input,
                   heif_suberror_Invalid_parameter_value,
                   "SPS conformance window exceeds image dimensions"};
    }
    *width  -= (uint32_t)crop_w;
    *height -= (uint32_t)crop_h;
  }

  if (!reader.get_uvlc(&value)) {
    return invalidUVLC;
  }
  if (value > 8) {
    return Error{heif_error_Invalid_input,
                 heif_suberror_Invalid_parameter_value,
                 "SPS bit_depth_luma_minus8 out of range"};
  }
  config->bit_depth_luma = (uint8_t) (value + 8);

  if (!reader.get_uvlc(&value)) {
    return invalidUVLC;
  }
  if (value > 8) {
    return Error{heif_error_Invalid_input,
                 heif_suberror_Invalid_parameter_value,
                 "SPS bit_depth_chroma_minus8 out of range"};
  }
  config->bit_depth_chroma = (uint8_t) (value + 8);



  // --- init static configuration fields ---

  config->configuration_version = 1;
  config->min_spatial_segmentation_idc = 0; // TODO: get this value from the VUI, 0 should be safe
  config->parallelism_type = 0; // TODO, 0 should be safe
  config->avg_frame_rate = 0; // makes no sense for HEIF
  config->constant_frame_rate = 0; // makes no sense for HEIF
  config->num_temporal_layers = 1; // makes no sense for HEIF

  return Error::Ok;
}


static const char* webcodecs_plugin_name()
{
  strcpy(plugin_name, "Webcodecs HEVC decoder");

  const char* webcodecs_version = "1";

  if (strlen(webcodecs_version) + 10 < MAX_PLUGIN_NAME_LENGTH) {
    strcat(plugin_name, ", version ");
    strcat(plugin_name, webcodecs_version);
  }

  return plugin_name;
}


static void webcodecs_init_plugin()
{

}


static void webcodecs_deinit_plugin()
{

}


static int webcodecs_does_support_format(enum heif_compression_format format)
{
  if (format == heif_compression_HEVC) {
    return WEBCODECS_PLUGIN_PRIORITY;
  }
  else {
    return 0;
  }
}


static struct heif_error webcodecs_new_decoder(void** dec)
{
  struct webcodecs_decoder* decoder = new webcodecs_decoder();
  struct heif_error err = {heif_error_Ok, heif_suberror_Unspecified, kSuccess};

  *dec = decoder;
  return err;
}


static void webcodecs_free_decoder(void* decoder_raw)
{
  struct webcodecs_decoder* decoder = (struct webcodecs_decoder*) decoder_raw;

  delete decoder;
}


static struct heif_error webcodecs_push_data(void* decoder_raw, const void* data, size_t size)
{
  struct webcodecs_decoder* decoder = (struct webcodecs_decoder*) decoder_raw;

  const uint8_t* cdata = (const uint8_t*) data;

  size_t ptr = 0;
  while (ptr < size) {
    if (4 > size - ptr) {
      struct heif_error err = {heif_error_Decoder_plugin_error,
                               heif_suberror_End_of_data,
                               kEmptyString};
      return err;
    }

    uint32_t nal_size = static_cast<uint32_t>((cdata[ptr] << 24) | (cdata[ptr + 1] << 16) | (cdata[ptr + 2] << 8) | (cdata[ptr + 3]));
    ptr += 4;

    if (nal_size > size - ptr) {
      struct heif_error err = {heif_error_Decoder_plugin_error,
                               heif_suberror_End_of_data,
                               kEmptyString};
      return err;
    }

    NALUnit nal_unit;
    nal_unit.data.assign(cdata + ptr, cdata + ptr + nal_size);
    decoder->data_queue.push(std::move(nal_unit));
    ptr += nal_size;
  }

  struct heif_error err = {heif_error_Ok, heif_suberror_Unspecified, kSuccess};
  return err;
}


static void normalize_luma_range(uint8_t* dst, int stride, int width, int height) {
  // Luma data coming from the browser's VideoDecoder API may be using a
  // limited range (16-235) instead of the full range (0-255). If this is the
  // case, we need to normalize the data to the full range.
  for (int y = 0; y < height; y++) {
    uint8_t* p = dst + y * stride;
    for (int x = 0; x < width; x++) {
      float v = (static_cast<float>(p[x]) - 16.0f) * 255.0f / 219.0f;
      p[x] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, v + 0.5f)));
    }
  }
}

static void normalize_chroma_range(uint8_t* dst, int stride, int width, int height) {
  // Chroma data coming from the browser's VideoDecoder API may be using a
  // limited range (16-240) instead of the full range (0-255). If this is the
  // case, we need to normalize the data to the full range.
  for (int y = 0; y < height; y++) {
    uint8_t* p = dst + y * stride;
    for (int x = 0; x < width; x++) {
      float v = (static_cast<float>(p[x]) - 16.0f) * 255.0f / 224.0f;
      p[x] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, v + 0.5f)));
    }
  }
}

// Returns the size of the chroma planes for the given chroma format. Odd luma
// sizes are rounded up, matching the convention used throughout libheif.
static void get_chroma_plane_size(heif_chroma chroma, int width, int height,
                                  int* chroma_w, int* chroma_h)
{
  *chroma_w = width;
  *chroma_h = height;
  if (chroma == heif_chroma_420 || chroma == heif_chroma_monochrome) {
    *chroma_w = (width + 1) / 2;
    *chroma_h = (height + 1) / 2;
  }
  else if (chroma == heif_chroma_422) {
    *chroma_w = (width + 1) / 2;
  }
}


// Checks that a plane consisting of 'rows' rows of 'row_bytes' bytes each,
// starting at 'offset' and with 'stride' bytes between consecutive rows, lies
// entirely within a buffer of 'buffer_size' bytes.
//
// The plane layout is reported by the browser and the image geometry
// ultimately comes from the file being decoded, so none of these values may be
// trusted. Every copy loop that reads from the browser's buffer must be
// preceded by this check.
static bool plane_fits_in_buffer(size_t buffer_size,
                                 int offset, int stride,
                                 int rows, int row_bytes)
{
  if (offset < 0 || stride < 0 || rows <= 0 || row_bytes <= 0) {
    return false;
  }

  if (stride < row_bytes) {
    return false;
  }

  const uint64_t end = static_cast<uint64_t>(offset) +
                       static_cast<uint64_t>(rows - 1) * static_cast<uint64_t>(stride) +
                       static_cast<uint64_t>(row_bytes);
  return end <= buffer_size;
}


static const heif_error kPlaneOutOfBoundsError = {
    heif_error_Decoder_plugin_error,
    heif_suberror_Unspecified,
    "Decoding failed: plane layout reported by the browser exceeds the decoded buffer"};


// The caller must have verified with plane_fits_in_buffer() that each source
// plane covers 'height' rows of 'width' bytes (luma) and the corresponding
// chroma plane size (see get_chroma_plane_size()).
static struct heif_error convert_planar_yuv_to_heif_image(
    const uint8_t* y_src, int y_src_stride,
    const uint8_t* u_src, int u_src_stride,
    const uint8_t* v_src, int v_src_stride,
    int width, int height,
    struct heif_image** out_img,
    heif_chroma chroma,
    bool is_full_range,
    const heif_security_limits* limits) {
  heif_error err;
  bool is_mono = chroma == heif_chroma_monochrome;

  int chroma_w, chroma_h;
  get_chroma_plane_size(chroma, width, height, &chroma_w, &chroma_h);

  err = heif_image_create(
      width, height,
      is_mono ? heif_colorspace_monochrome
              : heif_colorspace_YCbCr,
      is_mono ? heif_chroma_monochrome : chroma,
      out_img);
  if (err.code) {
    return err;
  }

  err = heif_image_add_plane_safe(
      *out_img, heif_channel_Y, width, height, 8, limits);
  if (err.code) {
    heif_image_release(*out_img);
    return err;
  }

  int y_stride;
  uint8_t* y_dst = heif_image_get_plane(
      *out_img, heif_channel_Y, &y_stride);
  for (int i = 0; i < height; ++i) {
    memcpy(y_dst + i * y_stride,
           y_src + i * y_src_stride,
           width);
  }

  if (!is_full_range) {
    normalize_luma_range(y_dst, y_stride, width, height);
  }

  if (!is_mono) {
    err = heif_image_add_plane_safe(
        *out_img, heif_channel_Cb,
        chroma_w, chroma_h, 8, limits);
    if (err.code) {
      heif_image_release(*out_img);
      return err;
    }

    err = heif_image_add_plane_safe(
        *out_img, heif_channel_Cr,
        chroma_w, chroma_h, 8, limits);
    if (err.code) {
      heif_image_release(*out_img);
      return err;
    }

    int cb_stride;
    uint8_t* cb_dst = heif_image_get_plane(
        *out_img, heif_channel_Cb, &cb_stride);
    for (int i = 0; i < chroma_h; ++i) {
      memcpy(cb_dst + i * cb_stride,
             u_src + i * u_src_stride,
             chroma_w);
    }

    int cr_stride;
    uint8_t* cr_dst = heif_image_get_plane(
        *out_img, heif_channel_Cr, &cr_stride);
    for (int i = 0; i < chroma_h; ++i) {
      memcpy(cr_dst + i * cr_stride,
             v_src + i * v_src_stride,
             chroma_w);
    }

    if (!is_full_range) {
      normalize_chroma_range(
          cb_dst, cb_stride, chroma_w, chroma_h);
      normalize_chroma_range(
          cr_dst, cr_stride, chroma_w, chroma_h);
    }
  }

  return {heif_error_Ok, heif_suberror_Unspecified, kSuccess};
}

static struct heif_error convert_nv12_to_heif_image(
    const std::unique_ptr<uint8_t[]>& buffer, size_t buffer_size,
    int width, int height,
    int y_offset, int y_src_stride,
    int uv_offset, int uv_src_stride,
    struct heif_image** out_img,
    bool is_mono,
    bool is_full_range,
    const heif_security_limits* limits) {
  if (!plane_fits_in_buffer(buffer_size, y_offset, y_src_stride, height, width)) {
    return kPlaneOutOfBoundsError;
  }

  if (is_mono) {
    return convert_planar_yuv_to_heif_image(
        buffer.get() + y_offset, y_src_stride,
        nullptr, 0, nullptr, 0,
        width, height, out_img,
        heif_chroma_monochrome, is_full_range, limits);
  }

  int chroma_w, chroma_h;
  get_chroma_plane_size(heif_chroma_420, width, height, &chroma_w, &chroma_h);

  // The interleaved UV plane has chroma_w Cb/Cr sample pairs per row.
  if (!plane_fits_in_buffer(buffer_size, uv_offset, uv_src_stride, chroma_h, chroma_w * 2)) {
    return kPlaneOutOfBoundsError;
  }

  std::vector<uint8_t> u_buf(static_cast<size_t>(chroma_w) * chroma_h);
  std::vector<uint8_t> v_buf(static_cast<size_t>(chroma_w) * chroma_h);

  for (int i = 0; i < chroma_h; ++i) {
    const uint8_t* uv_row =
        buffer.get() + uv_offset + i * uv_src_stride;
    for (int j = 0; j < chroma_w; ++j) {
      u_buf[i * chroma_w + j] = uv_row[j * 2];
      v_buf[i * chroma_w + j] = uv_row[j * 2 + 1];
    }
  }

  return convert_planar_yuv_to_heif_image(
      buffer.get() + y_offset, y_src_stride,
      u_buf.data(), chroma_w,
      v_buf.data(), chroma_w,
      width, height, out_img,
      heif_chroma_420, is_full_range, limits);
}

/** 
 * Generates a HEVC codec string as defined in ISO/IEC 14496-15 specification,
 * Annex E.3.
 */
static std::string get_hevc_codec_string(const HEVCDecoderConfigurationRecord& config) {
  std::string codec_string = "hvc1.";

  // Profile IDC
  codec_string += std::to_string(config.general_profile_idc);
  codec_string += ".";

  // Profile Compatibility Flags
  uint32_t profile_compatibility_flags = config.general_profile_compatibility_flags;
  char buffer[9];
  snprintf(buffer, sizeof(buffer), "%X", profile_compatibility_flags);
  codec_string += buffer;
  codec_string += ".";

  // Tier and Level
  codec_string += (config.general_tier_flag ? "H" : "L");
  codec_string += std::to_string(config.general_level_idc);
  codec_string += ".";

  // Constraint Indicator Flags
  uint64_t constraint_flags = 0;
  for (int i = 0; i < 48; ++i) {
    if (config.general_constraint_indicator_flags[i]) {
      constraint_flags |= (1ULL << (47 - i));
    }
  }
  snprintf(buffer, sizeof(buffer), "%06X",
           (unsigned int)(constraint_flags >> 24));
  codec_string += buffer;

  return codec_string;
}




static void get_nal_units(struct webcodecs_decoder* decoder,
                          NALUnit& vps_nal_unit,
                          NALUnit& sps_nal_unit,
                          NALUnit& pps_nal_unit,
                          NALUnit& data_unit) {
  // This code parses the NAL units to find the VPS, SPS, PPS, and data NAL
  // units. It handles cases where the NAL units are not in the expected order
  // and where there are extra NAL units that should be ignored. The last seen
  // VPS, SPS, PPS, and VCL data are used.
  while (!decoder->data_queue.empty()) {
    NALUnit nal_unit = decoder->data_queue.front();
    decoder->data_queue.pop();

    if (nal_unit.data.empty()) {
      continue;
    }

    const uint8_t nal_type = (nal_unit.data[0] >> 1) & 0x3F;

    if (nal_type == HEVC_NAL_UNIT_VPS_NUT) {
      vps_nal_unit = nal_unit;
    } else if (nal_type == HEVC_NAL_UNIT_SPS_NUT) {
      sps_nal_unit = nal_unit;
    } else if (nal_type == HEVC_NAL_UNIT_PPS_NUT) {
      pps_nal_unit = nal_unit;
    } else if (nal_type <= HEVC_NAL_UNIT_MAX_VCL) {
      // Assume the plugin will only receive one VCL NAL unit.
      data_unit = nal_unit;
    }
  }
}


static struct heif_error webcodecs_decode_image_with_limits(void* decoder_raw,
                                                              struct heif_image** out_img,
                                                              const heif_security_limits* limits)
{
  struct webcodecs_decoder* decoder = (struct webcodecs_decoder*) decoder_raw;
  *out_img = nullptr;

  NALUnit vps_nal_unit;
  NALUnit sps_nal_unit;
  NALUnit pps_nal_unit;
  NALUnit data_unit;

  get_nal_units(decoder, vps_nal_unit, sps_nal_unit, pps_nal_unit, data_unit);

  if (vps_nal_unit.data.empty() || sps_nal_unit.data.empty() || pps_nal_unit.data.empty() || data_unit.data.empty()) {
    return {heif_error_Decoder_plugin_error,
            heif_suberror_End_of_data,
            "Missing required NAL units (VPS, SPS, PPS, or data)"};
  }

  HEVCDecoderConfigurationRecord config;
  uint32_t w, h;              // visible size after applying the conformance window
  uint32_t coded_w, coded_h;  // size of the coded picture
  Error err = parse_sps_for_hvcC_configuration2(sps_nal_unit.data.data(), sps_nal_unit.data.size(),
                                                &config, &w, &h, &coded_w, &coded_h);
  if (err != Error::Ok) {
    return {heif_error_Decoder_plugin_error,
            heif_suberror_Unspecified,
            "Failed to parse SPS"};
  }

  // Reject coded picture sizes beyond the security limits before handing the
  // bitstream to the browser. libheif checks the SPS stored in the hvcC box,
  // but this plugin honours the last SPS in the NAL stream, which may differ.
  if (coded_w == 0 || coded_h == 0 || w == 0 || h == 0) {
    return {heif_error_Decoder_plugin_error,
            heif_suberror_Invalid_image_size,
            "SPS declares a zero-sized image"};
  }

  if (limits && limits->max_image_size_pixels > 0) {
    const auto max_dim = static_cast<uint32_t>(std::numeric_limits<int>::max());
    if (coded_w > max_dim || coded_h > max_dim ||
        coded_w > limits->max_image_size_pixels / coded_h) {
      return {heif_error_Memory_allocation_error,
              heif_suberror_Security_limit_exceeded,
              "SPS coded picture size exceeds the maximum image size"};
    }
  }

  config.m_nal_array.push_back(HEVCDecoderConfigurationRecord::NalArray{0, HEVC_NAL_UNIT_VPS_NUT, {vps_nal_unit.data}});
  config.m_nal_array.push_back(HEVCDecoderConfigurationRecord::NalArray{0, HEVC_NAL_UNIT_SPS_NUT, {sps_nal_unit.data}});
  config.m_nal_array.push_back(HEVCDecoderConfigurationRecord::NalArray{0, HEVC_NAL_UNIT_PPS_NUT, {pps_nal_unit.data}});

  StreamWriter writer;
  config.write(writer);
  std::vector<uint8_t> hvcc_record = writer.get_data();

  // The WebCodecs API expects the NAL unit to be prefixed with its size (4 bytes, big-endian).
  uint32_t nal_size = static_cast<uint32_t>(data_unit.data.size());
  std::vector<uint8_t> data_with_size(4 + nal_size);
  // Write length in Big Endian
  data_with_size[0] = (nal_size >> 24) & 0xFF;
  data_with_size[1] = (nal_size >> 16) & 0xFF;
  data_with_size[2] = (nal_size >> 8) & 0xFF;
  data_with_size[3] = nal_size & 0xFF;
  // Append NAL payload
  memcpy(data_with_size.data() + 4, data_unit.data.data(), nal_size);

  std::string codec_string = get_hevc_codec_string(config);

  emscripten::val result = emscripten::val::take_ownership(
    decode_with_browser_hevc(
      codec_string.c_str(),
      (uintptr_t)hvcc_record.data(),
      hvcc_record.size(),
      (uintptr_t)data_with_size.data(),
      data_with_size.size()
    )
  );

  if (!result["error"].isUndefined()) {
    static char error_message[256];
    std::string error_str = result["error"].as<std::string>();
    snprintf(error_message, sizeof(error_message), "%s", error_str.c_str());
    return {heif_error_Decoder_plugin_error,
            heif_suberror_Unspecified,
            error_message};
  }

  if (result.isUndefined()) {
    return {heif_error_Decoder_plugin_error,
            heif_suberror_Unspecified,
            "Decoding failed: decode_with_browser_hevc returned undefined"};
  }

  emscripten::val js_array = result["buffer"];
  if (js_array.isUndefined()) {
    return {heif_error_Decoder_plugin_error,
            heif_suberror_Unspecified,
            "Decoding failed: result.buffer is undefined"};
  }

  const size_t len = js_array["length"].as<size_t>();
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[len]);
  emscripten::val memory_view(emscripten::typed_memory_view(len, buffer.get()));
  memory_view.call<void>("set", js_array);

  // These are the dimensions of the visible rectangle that the JavaScript side
  // copied into 'buffer'. They are only used together with the plane layout
  // after plane_fits_in_buffer() has confirmed that the layout stays within
  // 'len' bytes.
  if (result["width"].isUndefined() || result["height"].isUndefined()) {
    return {heif_error_Decoder_plugin_error,
            heif_suberror_Unspecified,
            "Decoding failed: result.width or result.height is undefined"};
  }

  // The upper bound keeps the per-row byte counts computed below (up to four
  // bytes per pixel) and the rounded-up chroma sizes representable as int.
  const int max_dim = std::numeric_limits<int>::max() / 4;
  const int width = result["width"].as<int>();
  const int height = result["height"].as<int>();
  if (width <= 0 || height <= 0 || width > max_dim || height > max_dim) {
    return {heif_error_Decoder_plugin_error,
            heif_suberror_Invalid_image_size,
            "Decoding failed: invalid image size reported by the browser"};
  }

  std::string format = result["format"].as<std::string>();

  emscripten::val planes = result["planes"];
  if (planes.isUndefined() || !planes.isArray()) {
    return {heif_error_Decoder_plugin_error,
            heif_suberror_Unspecified,
            "Decoding failed: result.planes is undefined or not an array"};
  }

  bool is_full_range = !result["fullRange"].isUndefined() && result["fullRange"].as<bool>();

  // Most HEIC images in the browser will be decoded natively in NV12 pixel
  // format. Using the bytes directly helps retain the original image fidelity.
  if (format == "NV12") {
    bool is_mono = config.chroma_format == 0;
    if (!is_mono && planes["length"].as<size_t>() < 2) {
      return {heif_error_Decoder_plugin_error,
              heif_suberror_Unspecified,
              "Decoding failed: NV12 format requires at least 2 planes"};
    } else if (is_mono && planes["length"].as<size_t>() < 1) {
      return {heif_error_Decoder_plugin_error,
              heif_suberror_Unspecified,
              "Decoding failed: NV12 monochrome format requires at least 1 plane"};
    }

    emscripten::val y_plane = planes[0];
    if (y_plane.isUndefined()) {
      return {heif_error_Decoder_plugin_error,
              heif_suberror_Unspecified,
              "Decoding failed: result.planes[0] is undefined"};
    }

    const int y_offset = y_plane["offset"].as<int>();
    const int y_src_stride = y_plane["stride"].as<int>();
    int uv_offset = 0;
    int uv_src_stride = 0;

    if (!is_mono) {
      emscripten::val uv_plane = planes[1];
      if (uv_plane.isUndefined()) {
        return {heif_error_Decoder_plugin_error,
                heif_suberror_Unspecified,
                "Decoding failed: result.planes[1] is undefined"};
      }

      uv_offset = uv_plane["offset"].as<int>();
      uv_src_stride = uv_plane["stride"].as<int>();
    }

    return convert_nv12_to_heif_image(buffer, len, width, height,
                                      y_offset, y_src_stride, uv_offset, uv_src_stride,
                                      out_img, is_mono, is_full_range, limits);
  } else if (format == "I420" || format == "I422" || format == "I444") {
    if (planes["length"].as<size_t>() < 3) {
      return {heif_error_Decoder_plugin_error,
              heif_suberror_Unspecified,
              "Decoding failed: planar YUV format requires 3 planes"};
    }

    emscripten::val y_plane = planes[0];
    emscripten::val u_plane = planes[1];
    emscripten::val v_plane = planes[2];
    if (y_plane.isUndefined() || u_plane.isUndefined() || v_plane.isUndefined()) {
      return {heif_error_Decoder_plugin_error,
              heif_suberror_Unspecified,
              "Decoding failed: one or more YUV planes are undefined"};
    }

    heif_chroma chroma = heif_chroma_420;
    if (format == "I422") {
      chroma = heif_chroma_422;
    } else if (format == "I444") {
      chroma = heif_chroma_444;
    }

    const int y_offset = y_plane["offset"].as<int>();
    const int y_src_stride = y_plane["stride"].as<int>();
    const int u_offset = u_plane["offset"].as<int>();
    const int u_src_stride = u_plane["stride"].as<int>();
    const int v_offset = v_plane["offset"].as<int>();
    const int v_src_stride = v_plane["stride"].as<int>();

    int chroma_w, chroma_h;
    get_chroma_plane_size(chroma, width, height, &chroma_w, &chroma_h);

    if (!plane_fits_in_buffer(len, y_offset, y_src_stride, height, width) ||
        !plane_fits_in_buffer(len, u_offset, u_src_stride, chroma_h, chroma_w) ||
        !plane_fits_in_buffer(len, v_offset, v_src_stride, chroma_h, chroma_w)) {
      return kPlaneOutOfBoundsError;
    }

    return convert_planar_yuv_to_heif_image(
        buffer.get() + y_offset, y_src_stride,
        buffer.get() + u_offset, u_src_stride,
        buffer.get() + v_offset, v_src_stride,
        width, height,
        out_img, chroma, is_full_range, limits);
  } else if (format == "RGBA") {
    if (planes["length"].as<size_t>() < 1) {
      return {heif_error_Decoder_plugin_error,
              heif_suberror_Unspecified,
              "Decoding failed: RGBA format requires at least 1 plane"};
    }

    emscripten::val rgba_plane = planes[0];
    if (rgba_plane.isUndefined()) {
      return {heif_error_Decoder_plugin_error,
              heif_suberror_Unspecified,
              "Decoding failed: result.planes[0] is undefined"};
    }

    const int rgba_offset = rgba_plane["offset"].as<int>();
    const int rgba_src_stride = rgba_plane["stride"].as<int>();

    if (!plane_fits_in_buffer(len, rgba_offset, rgba_src_stride, height, width * 4)) {
      return kPlaneOutOfBoundsError;
    }

    heif_error err;
    err = heif_image_create(width,
                            height,
                            heif_colorspace_RGB,
                            heif_chroma_interleaved_RGBA,
                            out_img);
    if (err.code) {
      return err;
    }

    err = heif_image_add_plane_safe(*out_img, heif_channel_interleaved, width, height, 8, limits);
    if (err.code) {
      heif_image_release(*out_img);
      return err;
    }

    int stride;
    uint8_t* dst = heif_image_get_plane(*out_img, heif_channel_interleaved, &stride);

    for (int i = 0; i < height; ++i) {
      memcpy(dst + i * stride,
             buffer.get() + rgba_offset + i * rgba_src_stride,
             width * 4);
    }

    return {heif_error_Ok, heif_suberror_Unspecified, kSuccess};
  } else {
    return {heif_error_Decoder_plugin_error,
            heif_suberror_Unsupported_color_conversion,
            "Decoding failed: unsupported pixel format"};
  }
}


void webcodecs_set_strict_decoding(void* decoder_raw, int flag)
{
}


static int webcodecs_does_support_format2(const heif_decoder_plugin_compressed_format_description* format)
{
  return webcodecs_does_support_format(format->format);
}


static struct heif_error webcodecs_new_decoder2(void** dec, const heif_decoder_plugin_options* options)
{
  return webcodecs_new_decoder(dec);
}


static struct heif_error webcodecs_push_data2(void* decoder_raw, const void* data, size_t size, uintptr_t user_data)
{
  return webcodecs_push_data(decoder_raw, data, size);
}


static struct heif_error webcodecs_flush_data(void* decoder_raw)
{
  return {heif_error_Ok, heif_suberror_Unspecified, kSuccess};
}


static struct heif_error webcodecs_decode_next_image(void* decoder_raw,
                                                     struct heif_image** out_img,
                                                     const heif_security_limits* limits)
{
  return webcodecs_decode_image_with_limits(decoder_raw, out_img, limits);
}


static struct heif_error webcodecs_decode_next_image2(void* decoder_raw,
                                                      struct heif_image** out_img,
                                                      uintptr_t* out_user_data,
                                                      const heif_security_limits* limits)
{
  if (out_user_data) {
    *out_user_data = 0;
  }
  return webcodecs_decode_image_with_limits(decoder_raw, out_img, limits);
}


static struct heif_error webcodecs_decode_image(void* decoder_raw,
                                                struct heif_image** out_img)
{
  auto* limits = heif_get_global_security_limits();
  return webcodecs_decode_image_with_limits(decoder_raw, out_img, limits);
}


static const struct heif_decoder_plugin decoder_webcodecs
    {
        5,
        webcodecs_plugin_name,
        webcodecs_init_plugin,
        webcodecs_deinit_plugin,
        webcodecs_does_support_format,
        webcodecs_new_decoder,
        webcodecs_free_decoder,
        webcodecs_push_data,
        webcodecs_decode_image,
        webcodecs_set_strict_decoding,
        "webcodecs",
        webcodecs_decode_next_image,
        0,
        webcodecs_does_support_format2,
        webcodecs_new_decoder2,
        webcodecs_push_data2,
        webcodecs_flush_data,
        webcodecs_decode_next_image2
    };



const struct heif_decoder_plugin* get_decoder_plugin_webcodecs()
{
  return &decoder_webcodecs;
}

#if PLUGIN_WEBCODECS
heif_plugin_info plugin_info {
  1,
  heif_plugin_type_decoder,
  &decoder_webcodecs
};
#endif
