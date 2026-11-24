/*
 * Android MediaCodec hardware HEVC decoder plugin for libheif.
 *
 * Decodes HEVC-coded HEIF items through the NDK AMediaCodec API, so that
 * image decoding is offloaded to the device's hardware video decoder.
 *
 * Data flow:
 *   - libheif pushes a stream of 4-byte length-prefixed NAL units
 *     (hvcC parameter sets followed by the item's coded slice data).
 *   - The plugin converts this to Annex-B and feeds one access unit to
 *     AMediaCodec ("video/hevc"), configured with the parameter sets as csd-0.
 *   - Decoded frames are read either through an AImageReader surface
 *     (YUV_420_888, always supported by decoders) or, as a fallback, from
 *     codec output buffers (device-dependent color formats).
 *   - The resulting YUV 4:2:0 8-bit picture is copied into a heif_image.
 *
 * The MediaCodec instances are pooled and reused across images (important for
 * grid items, where every tile is a separate image). If hardware decoding is
 * unavailable or fails, the plugin transparently falls back to the software
 * libde265 decoder.
 *
 * Environment variables:
 *   HEIF_HW_DECODER=0   disable hardware decoding (pure software path)
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

#include "libheif/heif.h"
#include "libheif/heif_plugin.h"
#include "decoder_mediacodec.h"

#include "../codecs/hevc_boxes.h"
#include "../bitstream.h"

#if HAVE_LIBDE265
#include "decoder_libde265.h"
#endif

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>

#include <unistd.h>

#include <dlfcn.h>
#include <android/hardware_buffer.h>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>


static const int MEDIACODEC_PLUGIN_PRIORITY = 150;  // preferred over libde265 (100)

static const char kSuccess[] = "Success";

static const int HEVC_NAL_VPS = 32;
static const int HEVC_NAL_SPS = 33;
static const int HEVC_NAL_PPS = 34;

// Android color formats (subset of OMX_COLOR_FORMATTYPE / Codec2 equivalents).
static const int32_t COLOR_FormatYUV420Planar = 0x13;               // I420
static const int32_t COLOR_FormatYUV420PackedPlanar = 0x14;
static const int32_t COLOR_FormatYUV420Flexible = 0x7F420888;
static const int32_t COLOR_YV12 = 0x32315659;
// Semi-planar formats handled as raw values in copy_buffer_frame:
// 0x15 (NV12), 0x27 (PackedSemiPlanar), 0x7FA30C00 (QCOM NV12).

static heif_error err_ok()
{
  return {heif_error_Ok, heif_suberror_Unspecified, kSuccess};
}

static bool hw_debug()
{
  static std::atomic<int> dbg{0};
  int v = dbg.load();
  if (v == 0) {
    v = (getenv("HEIF_HW_DEBUG") ? 1 : -1);
    dbg.store(v);
  }
  return (v == 1);
}

#define HW_DBG(...) do { if (hw_debug()) fprintf(stderr, __VA_ARGS__); } while (0)

static bool hw_prefer_buffer_mode();

static heif_error err_plugin(const char* msg)
{
  return {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, msg};
}


// ======================================================================
//                       NAL stream handling

struct NalUnit
{
  const uint8_t* data;
  size_t size;
  int type;
};

// Split a 4-byte length-prefixed NAL stream (hvcC / hvc1 sample format).
static bool split_length_prefixed_nals(const uint8_t* data, size_t size, std::vector<NalUnit>& out)
{
  size_t ptr = 0;
  while (ptr + 4 <= size) {
    uint32_t nal_size = (uint32_t(data[ptr]) << 24) | (uint32_t(data[ptr + 1]) << 16) |
                        (uint32_t(data[ptr + 2]) << 8) | uint32_t(data[ptr + 3]);
    ptr += 4;

    if (nal_size == 0 || nal_size > size - ptr) {
      return false;
    }

    NalUnit nal;
    nal.data = data + ptr;
    nal.size = nal_size;
    nal.type = (nal.data[0] >> 1) & 0x3F;
    out.push_back(nal);

    ptr += nal_size;
  }

  return (ptr == size && !out.empty());
}

static void append_annexb(std::vector<uint8_t>& dst, const NalUnit& nal)
{
  dst.push_back(0);
  dst.push_back(0);
  dst.push_back(0);
  dst.push_back(1);
  dst.insert(dst.end(), nal.data, nal.data + nal.size);
}


// ======================================================================
//                       hardware codec pool
//
// MediaCodec instances are expensive to create, but HEIF grid images decode
// one tile per decoder instance. We therefore keep a small pool of running
// codec instances that is reused across tiles with identical configuration.

struct McCodecEntry
{
  AMediaCodec* codec = nullptr;
  AImageReader* reader = nullptr;   // only in surface mode
  ANativeWindow* window = nullptr;  // only in surface mode
  bool surface_mode = true;
  int width = 0;
  int height = 0;
  std::vector<uint8_t> csd;         // parameter sets used for configuration
  bool busy = false;
};

static std::mutex g_pool_mutex;
static std::condition_variable g_pool_cv;
static std::vector<std::unique_ptr<McCodecEntry> > g_pool;
static std::atomic<bool> g_hw_broken{false};  // give up on hw; use software
static int g_hw_fail_count = 0;               // consecutive decode failures
static const size_t kMaxPoolEntries = 4;

static void destroy_entry(std::unique_ptr<McCodecEntry>& e)
{
  if (!e) {
    return;
  }
  if (e->codec) {
    AMediaCodec_stop(e->codec);
    AMediaCodec_delete(e->codec);
    e->codec = nullptr;
  }
  if (e->window) {
    ANativeWindow_release(e->window);
    e->window = nullptr;
  }
  if (e->reader) {
    AImageReader_delete(e->reader);
    e->reader = nullptr;
  }
}

// Tries surface mode first (decoder output to a surface is mandatory support),
// then falls back to direct output buffers.
static bool try_create_codec(McCodecEntry* e, int w, int h, const std::vector<uint8_t>& csd, bool surface_mode)
{
  e->surface_mode = surface_mode;

  if (surface_mode) {
    if (AImageReader_newWithUsage(w, h, AIMAGE_FORMAT_YUV_420_888,
                                 AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
                                 4, &e->reader) != AMEDIA_OK) {
      return false;
    }
    if (AImageReader_getWindow(e->reader, &e->window) != AMEDIA_OK) {
      AImageReader_delete(e->reader);
      e->reader = nullptr;
      return false;
    }
  }

  e->codec = AMediaCodec_createDecoderByType("video/hevc");
  if (!e->codec) {
    if (e->window) {
      ANativeWindow_release(e->window);
      e->window = nullptr;
    }
    if (e->reader) {
      AImageReader_delete(e->reader);
      e->reader = nullptr;
    }
    return false;
  }

  AMediaFormat* fmt = AMediaFormat_new();
  AMediaFormat_setString(fmt, "mime", "video/hevc");
  AMediaFormat_setInt32(fmt, "width", w);
  AMediaFormat_setInt32(fmt, "height", h);
  AMediaFormat_setBuffer(fmt, "csd-0", csd.data(), csd.size());

  media_status_t status = AMediaCodec_configure(e->codec, fmt,
                                                surface_mode ? e->window : nullptr,
                                                nullptr, 0);
  AMediaFormat_delete(fmt);
  HW_DBG("[mc] create codec %s: configure=%d\n", surface_mode ? "(surface)" : "(buffer)", status);
  if (status != AMEDIA_OK ||
      AMediaCodec_start(e->codec) != AMEDIA_OK) {
    AMediaCodec_stop(e->codec);
    AMediaCodec_delete(e->codec);
    e->codec = nullptr;
    if (e->window) {
      ANativeWindow_release(e->window);
      e->window = nullptr;
    }
    if (e->reader) {
      AImageReader_delete(e->reader);
      e->reader = nullptr;
    }
    return false;
  }

  return true;
}

static McCodecEntry* acquire_entry(int w, int h, const std::vector<uint8_t>& csd)
{
  for (;;) {
    std::unique_ptr<McCodecEntry> to_destroy;
    {
      std::unique_lock<std::mutex> lock(g_pool_mutex);

      if (g_hw_broken) {
        return nullptr;
      }

      for (auto& e : g_pool) {
        if (!e->busy && e->width == w && e->height == h && e->csd == csd) {
          e->busy = true;
          return e.get();
        }
      }

      // Evict an idle entry with a different configuration to make room.
      // The actual destruction runs outside the lock (it may block in the
      // codec's stop()).
      if (g_pool.size() >= kMaxPoolEntries) {
        for (auto it = g_pool.begin(); it != g_pool.end(); ++it) {
          if (!(*it)->busy) {
            to_destroy = std::move(*it);
            g_pool.erase(it);
            break;
          }
        }
        if (!to_destroy) {
          g_pool_cv.wait(lock);
          continue;
        }
        // fall through to destroy outside the lock, then retry
      }
      else {
        std::unique_ptr<McCodecEntry> e(new McCodecEntry());
        e->width = w;
        e->height = h;
        e->csd = csd;
        e->busy = true;

        if (!try_create_codec(e.get(), w, h, csd, hw_prefer_buffer_mode() ? false : true) &&
            !try_create_codec(e.get(), w, h, csd, hw_prefer_buffer_mode() ? true : false)) {
          // The device cannot provide a HEVC hardware decoder at all.
          g_hw_broken = true;
          g_pool_cv.notify_all();
          return nullptr;
        }

        McCodecEntry* result = e.get();
        g_pool.push_back(std::move(e));
        return result;
      }
    }

    destroy_entry(to_destroy);
  }
}

static void release_entry(McCodecEntry* entry, bool success)
{
  std::unique_ptr<McCodecEntry> to_destroy;

  if (success) {
    // The entry stays marked busy during the flush so no other thread can
    // grab it; flush() is quick in normal operation.
    AMediaCodec_flush(entry->codec);
    {
      std::lock_guard<std::mutex> lock(g_pool_mutex);
      entry->busy = false;
      g_hw_fail_count = 0;
    }
    g_pool_cv.notify_all();
  }
  else {
    {
      std::lock_guard<std::mutex> lock(g_pool_mutex);
      for (auto it = g_pool.begin(); it != g_pool.end(); ++it) {
        if (it->get() == entry) {
          to_destroy = std::move(*it);
          g_pool.erase(it);
          break;
        }
      }
      // A failing bitstream (or broken codec) would fail for every grid tile;
      // stop trying hardware after a few consecutive failures.
      if (++g_hw_fail_count >= 3) {
        g_hw_broken = true;
      }
    }
    g_pool_cv.notify_all();
    destroy_entry(to_destroy);  // outside the lock; may block in stop()
  }
}

// One-time check whether the platform has a "video/hevc" decoder.
static bool probe_hw_codec()
{
  // Native executables have no binder thread pool, so the Codec2 bufferpool
  // (android.hardware.media.bufferpool2.IClientManager) calls from the vendor
  // media HAL into this process block forever and every decoded work item is
  // dropped as "corrupted" on the AIDL boundary. Start the pool explicitly.
  // ABinderProcess_startThreadPool is exported by libbinder_ndk.so on all
  // supported devices but has no NDK header/stub, so resolve it via dlsym.
  {
    static std::once_flag once;
    std::call_once(once, []() {
      void* h = dlopen("libbinder_ndk.so", RTLD_NOW);
      if (h) {
        using StartFn = void (*)();
        StartFn start = reinterpret_cast<StartFn>(dlsym(h, "ABinderProcess_startThreadPool"));
        if (start) {
          start();
          HW_DBG("[mc] binder thread pool started\n");
        }
      }
    });
  }

  static std::atomic<int> available{0};
  int state = available.load();
  if (state == 0) {
    AMediaCodec* codec = AMediaCodec_createDecoderByType("video/hevc");
    state = (codec ? 1 : -1);
    if (codec) {
      AMediaCodec_delete(codec);
    }
    available.store(state);
  }
  return (state == 1);
}

static bool hw_disabled_by_env()
{
  const char* env = getenv("HEIF_HW_DECODER");
  return (env && env[0] == '0');
}

static bool hw_prefer_buffer_mode()
{
  const char* env = getenv("HEIF_HW_MODE");
  // Buffer mode is the default: surface output on many devices lands in
  // vendor UBWC-compressed buffers that cannot be copied on the CPU.
  // Set HEIF_HW_MODE=surface to prefer surface output.
  return (env == nullptr || strcmp(env, "surface") != 0);
}


// ======================================================================
//                     SPS VUI color description

// Defaults follow H.265 "unspecified" values, matching what libde265 reports
// when the VUI is absent, so the hardware path produces the same nclx
// interpretation as the software fallback.
struct SpsColorInfo
{
  uint8_t primaries = 2;
  uint8_t transfer = 2;
  uint8_t matrix = 2;
  bool full_range = false;
};

// Parse just enough of the HEVC SPS VUI to extract the color description.
// Returns false if the bitstream cannot be parsed (defaults are kept).
static bool parse_sps_vui_color_info(const uint8_t* sps, size_t size, SpsColorInfo* out)
{
  std::vector<uint8_t> rbsp = remove_start_code_emulation(sps, size);
  BitReader reader(rbsp.data(), rbsp.size());

  // BitReader::skip_bits() cannot handle more than 32 bits at once (its
  // 64-bit window shift would be undefined behavior), so skip in chunks.
  auto skip_safe = [&reader](int n) {
    while (n >= 32) {
      reader.skip_bits(32);
      n -= 32;
    }
    if (n > 0) {
      reader.skip_bits(n);
    }
  };

  reader.skip_bits(2 * 8);              // NAL header
  reader.skip_bits(4);                  // sps_video_parameter_set_id
  uint32_t max_sub_layers_minus1 = reader.get_bits8(3);
  reader.skip_bits(1);                  // sps_temporal_id_nesting_flag
  if (max_sub_layers_minus1 > 6 || reader.get_bits_remaining() < 0) {
    return false;
  }

  // --- profile_tier_level (general part: 2+1+5+32+48+8 bits) ---
  skip_safe(2 + 1 + 5 + 32 + 48 + 8);
  std::vector<uint32_t> layer_profile_present(max_sub_layers_minus1);
  std::vector<uint32_t> layer_level_present(max_sub_layers_minus1);
  for (uint32_t i = 0; i < max_sub_layers_minus1; i++) {
    layer_profile_present[i] = reader.get_bits8(1);
    layer_level_present[i] = reader.get_bits8(1);
  }
  if (max_sub_layers_minus1 > 0) {
    skip_safe(2 * (8 - max_sub_layers_minus1));   // reserved_zero_2bits
  }
  for (uint32_t i = 0; i < max_sub_layers_minus1; i++) {
    if (layer_profile_present[i]) {
      skip_safe(88);
    }
    if (layer_level_present[i]) {
      reader.skip_bits(8);
    }
  }

  // --- SPS continued ---
  uint32_t value;
  if (!reader.get_uvlc(&value) || value > 15) return false;    // sps_seq_parameter_set_id
  if (!reader.get_uvlc(&value) || value > 3) return false;     // chroma_format_idc
  if (value == 3) {
    reader.skip_bits(1);                                       // separate_colour_plane_flag
  }
  if (!reader.get_uvlc(&value)) return false;                  // pic_width_in_luma_samples
  if (!reader.get_uvlc(&value)) return false;                  // pic_height_in_luma_samples
  if (reader.get_bits8(1)) {                                   // conformance_window_flag
    for (int i = 0; i < 4; i++) {
      if (!reader.get_uvlc(&value)) return false;
    }
  }
  if (!reader.get_uvlc(&value)) return false;                  // bit_depth_luma_minus8
  if (!reader.get_uvlc(&value)) return false;                  // bit_depth_chroma_minus8

  uint32_t log2_max_poc_lsb_minus4;
  if (!reader.get_uvlc(&log2_max_poc_lsb_minus4) || log2_max_poc_lsb_minus4 > 12) return false;

  uint32_t ordering_present = reader.get_bits8(1);
  uint32_t first_layer = (ordering_present ? 0 : max_sub_layers_minus1);
  for (uint32_t i = first_layer; i <= max_sub_layers_minus1; i++) {
    if (!reader.get_uvlc(&value)) return false;                // max_dec_pic_buffering
    if (!reader.get_uvlc(&value)) return false;                // max_num_reorder_pics
    if (!reader.get_uvlc(&value)) return false;                // max_latency_increase
  }

  for (int i = 0; i < 6; i++) {
    if (!reader.get_uvlc(&value)) return false;                // log2 min/diff coding & transform, hierarchy depths
  }

  if (reader.get_bits8(1)) {                                   // scaling_list_enabled_flag
    if (reader.get_bits8(1)) {
      return false;                                            // scaling_list_data(): too complex, skip
    }
  }
  reader.skip_bits(2);                                         // amp_enabled, sao_enabled
  if (reader.get_bits8(1)) {                                   // pcm_enabled_flag
    reader.skip_bits(8);                                       // pcm sample bit depths (4+4)
    if (!reader.get_uvlc(&value)) return false;
    if (!reader.get_uvlc(&value)) return false;
    reader.skip_bits(1);                                       // pcm_loop_filter_disabled_flag
  }

  // --- short_term_ref_pic_set ---
  uint32_t num_st_rps;
  if (!reader.get_uvlc(&num_st_rps) || num_st_rps > 64) return false;
  std::vector<uint32_t> num_delta_pocs(num_st_rps, 0);
  for (uint32_t i = 0; i < num_st_rps; i++) {
    uint32_t total = 0;
    uint32_t inter_pred = (i != 0 ? reader.get_bits8(1) : 0);
    if (inter_pred) {
      reader.skip_bits(1);                                     // delta_rps_sign
      if (!reader.get_uvlc(&value)) return false;              // abs_delta_rps_minus1
      uint32_t ref_count = (i > 0 ? num_delta_pocs[i - 1] : 0);
      for (uint32_t j = 0; j <= ref_count; j++) {
        uint32_t used = reader.get_bits8(1);
        uint32_t use_delta = 1;
        if (!used) {
          use_delta = reader.get_bits8(1);
        }
        if (used || use_delta) {
          total++;
        }
      }
    }
    else {
      uint32_t neg, pos;
      if (!reader.get_uvlc(&neg) || !reader.get_uvlc(&pos)) return false;
      if (neg + pos > 64) return false;
      for (uint32_t j = 0; j < neg + pos; j++) {
        if (!reader.get_uvlc(&value)) return false;            // delta_poc_sx_minus1
        reader.skip_bits(1);                                   // used_by_curr_pic_sx_flag
      }
      total = neg + pos;
    }
    num_delta_pocs[i] = total;
  }

  if (reader.get_bits8(1)) {                                   // long_term_ref_pics_present_flag
    uint32_t num_lt;
    if (!reader.get_uvlc(&num_lt) || num_lt > 64) return false;
    for (uint32_t i = 0; i < num_lt; i++) {
      reader.skip_bits((int) log2_max_poc_lsb_minus4 + 4 + 1); // lt_ref_pic_poc_lsb_sps + used flag
    }
  }

  reader.skip_bits(2);                                         // sps_temporal_mvp, strong_intra_smoothing

  if (!reader.get_bits8(1)) {                                  // vui_parameters_present_flag
    return true;                                               // no VUI -> keep defaults
  }

  // --- VUI (read up to the color description, then stop) ---
  if (reader.get_bits8(1)) {                                   // aspect_ratio_info_present_flag
    uint32_t idc = reader.get_bits8(8);
    if (idc == 255) {
      reader.skip_bits(32);                                    // sar_width, sar_height
    }
  }
  if (reader.get_bits8(1)) {                                   // overscan_info_present_flag
    reader.skip_bits(1);                                       // overscan_appropriate_flag
  }
  if (reader.get_bits8(1)) {                                   // video_signal_type_present_flag
    reader.skip_bits(3);                                       // video_format
    out->full_range = (reader.get_bits8(1) != 0);              // video_full_range_flag
    if (reader.get_bits8(1)) {                                 // colour_description_present_flag
      out->primaries = reader.get_bits8(8);
      out->transfer = reader.get_bits8(8);
      out->matrix = reader.get_bits8(8);
    }
  }

  return (reader.get_bits_remaining() >= 0);
}


static heif_error make_heif_yuv420(int w, int h,
                                   const uint8_t* y, int y_row, int y_pix,
                                   const uint8_t* u, int u_row, int u_pix,
                                   const uint8_t* v, int v_row, int v_pix,
                                   const SpsColorInfo& color,
                                   const heif_security_limits* limits,
                                   heif_image** out_img)
{
  heif_error err = heif_image_create(w, h, heif_colorspace_YCbCr, heif_chroma_420, out_img);
  if (err.code != heif_error_Ok) {
    return err;
  }

  int cw = (w + 1) / 2;
  int ch = (h + 1) / 2;

  struct Plane
  {
    heif_channel channel;
    const uint8_t* src;
    int src_row;
    int src_pix;
    int pw, ph;
  } planes[3] = {
      {heif_channel_Y, y, y_row, y_pix, w, h},
      {heif_channel_Cb, u, u_row, u_pix, cw, ch},
      {heif_channel_Cr, v, v_row, v_pix, cw, ch}
  };

  for (const Plane& p : planes) {
    err = heif_image_add_plane_safe(*out_img, p.channel, p.pw, p.ph, 8, limits);
    if (err.code != heif_error_Ok) {
      heif_image_release(*out_img);
      *out_img = nullptr;
      return err;
    }

    size_t dst_stride;
    uint8_t* dst = heif_image_get_plane2(*out_img, p.channel, &dst_stride);
    if (dst == nullptr) {
      heif_image_release(*out_img);
      *out_img = nullptr;
      return err_plugin("heif_image_get_plane2 returned NULL");
    }
    HW_DBG("[mc] copy ch=%d %dx%d src=%p rs=%d ps=%d dst=%p ds=%zu\n",
           (int) p.channel, p.pw, p.ph, (const void*) p.src, p.src_row, p.src_pix,
           (void*) dst, dst_stride);

    for (int yy = 0; yy < p.ph; yy++) {
      const uint8_t* src = p.src + static_cast<size_t>(yy) * p.src_row;
      uint8_t* dst_row = dst + static_cast<size_t>(yy) * dst_stride;
      if (p.src_pix == 1) {
        memcpy(dst_row, src, static_cast<size_t>(p.pw));
      }
      else {
        for (int xx = 0; xx < p.pw; xx++) {
          dst_row[xx] = src[xx * p.src_pix];
        }
      }
    }
  }

  // Color description from the SPS VUI, mirroring what the software decoder
  // (libde265) reports for the same stream.
  heif_color_profile_nclx* nclx = heif_nclx_color_profile_alloc();
  if (nclx) {
    heif_nclx_color_profile_set_color_primaries(nclx, color.primaries);
    heif_nclx_color_profile_set_transfer_characteristics(nclx, color.transfer);
    heif_nclx_color_profile_set_matrix_coefficients(nclx, color.matrix);
    nclx->full_range_flag = color.full_range;
    heif_image_set_nclx_color_profile(*out_img, nclx);
    heif_nclx_color_profile_free(nclx);
  }

  return err_ok();
}


// ======================================================================
//                       frame extraction

static heif_error copy_surface_frame(McCodecEntry* e, size_t out_idx,
                                     int w, int h, const SpsColorInfo& color,
                                     const heif_security_limits* limits,
                                     heif_image** out_img)
{
  if (AMediaCodec_releaseOutputBuffer(e->codec, out_idx, true) != AMEDIA_OK) {
    return err_plugin("failed to render decoded frame to surface");
  }

  // Poll for the rendered frame. acquireNextImage() would block forever, so
  // use acquireLatestImage() (non-blocking below maxImages) with a time budget.
  AImage* image = nullptr;
  for (int i = 0; i < 300; i++) {
    if (AImageReader_acquireLatestImage(e->reader, &image) == AMEDIA_OK && image) {
      break;
    }
    usleep(5000);
  }
  if (!image) {
    return err_plugin("no decoded frame received from AImageReader");
  }
  HW_DBG("[mc] surface frame acquired\n");

  AImageCropRect crop;
  media_status_t status = AImage_getCropRect(image, &crop);
  if (status != AMEDIA_OK) {
    crop.left = 0;
    crop.top = 0;
    crop.right = w;
    crop.bottom = h;
  }

  uint8_t* planes[3] = {nullptr, nullptr, nullptr};
  int plane_len[3] = {0, 0, 0};
  int row_stride[3] = {0, 0, 0};
  int pix_stride[3] = {1, 1, 1};

  heif_error err = err_plugin("cannot access decoded image planes");
  bool ok = true;
  for (int p = 0; p < 3; p++) {
    if (AImage_getPlaneData(image, p, &planes[p], &plane_len[p]) != AMEDIA_OK ||
        AImage_getPlaneRowStride(image, p, &row_stride[p]) != AMEDIA_OK ||
        AImage_getPlanePixelStride(image, p, &pix_stride[p]) != AMEDIA_OK) {
      ok = false;
      break;
    }
  }

  // Some gralloc implementations (e.g. Qualcomm NV12-style buffers) expose
  // only the Y plane through AImage. Lock the underlying AHardwareBuffer
  // instead to get the true multi-plane layout. AHardwareBuffer_lockPlanes
  // is API 29+, so load it dynamically.
  bool ahb_locked = false;
  AHardwareBuffer* ahb = nullptr;
  bool chroma_ok = (planes[1] != nullptr && planes[2] != nullptr &&
                    row_stride[1] > 0 && row_stride[2] > 0 &&
                    pix_stride[1] > 0 && pix_stride[2] > 0);
  if (ok && !chroma_ok) {
    using LockPlanesFn = int (*)(AHardwareBuffer*, uint64_t, int32_t, const ARect*, AHardwareBuffer_Planes*);
    static LockPlanesFn lock_planes = nullptr;
    static std::once_flag ahb_once;
    std::call_once(ahb_once, [] {
      void* h = dlopen("libandroid.so", RTLD_NOW);
      if (h) {
        lock_planes = reinterpret_cast<LockPlanesFn>(dlsym(h, "AHardwareBuffer_lockPlanes"));
      }
      HW_DBG("[mc] lockPlanes dlsym: %p\n", (void*) lock_planes);
    });

    media_status_t gst = AImage_getHardwareBuffer(image, &ahb);
    int32_t num_planes = -1;
    AImage_getNumberOfPlanes(image, &num_planes);
    HW_DBG("[mc] getHardwareBuffer=%d ahb=%p numPlanes=%d\n", (int) gst, (void*) ahb, (int) num_planes);

    if (gst == AMEDIA_OK && ahb != nullptr) {
      AHardwareBuffer_Desc desc;
      memset(&desc, 0, sizeof(desc));
      AHardwareBuffer_describe(ahb, &desc);
      HW_DBG("[mc] ahb desc: fmt=0x%x %ux%u stride=%u layers=%u usage=0x%llx\n",
             desc.format, desc.width, desc.height, desc.stride, desc.layers,
             (unsigned long long) desc.usage);

      bool locked = false;
      if (lock_planes) {
        AHardwareBuffer_Planes pl;
        memset(&pl, 0, sizeof(pl));
        int rc = lock_planes(ahb, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr, &pl);
        HW_DBG("[mc] lockPlanes rc=%d count=%u\n", rc, pl.planeCount);
        if (rc == 0 && pl.planeCount >= 3) {
          for (int p = 0; p < 3; p++) {
            planes[p] = static_cast<uint8_t*>(pl.planes[p].data);
            row_stride[p] = (int) pl.planes[p].rowStride;
            pix_stride[p] = (int) pl.planes[p].pixelStride;
            plane_len[p] = 0x3fffffff;  // full plane is mapped and valid
          }
          locked = true;
        }
      }

      if (!locked) {
        // Fall back to a single-pointer lock and assume the common NV12 layout:
        // Y = stride * height, interleaved UV follows.
        void* base = nullptr;
        int rc = AHardwareBuffer_lock(ahb, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr, &base);
        HW_DBG("[mc] ahb lock rc=%d base=%p\n", rc, base);
        if (rc == 0 && base != nullptr) {
          planes[0] = static_cast<uint8_t*>(base);
          row_stride[0] = (int) desc.stride;
          pix_stride[0] = 1;
          planes[1] = planes[0] + (size_t) desc.stride * desc.height;
          planes[2] = planes[1] + 1;
          row_stride[1] = row_stride[2] = (int) desc.stride;
          pix_stride[1] = pix_stride[2] = 2;
          for (int p = 0; p < 3; p++) {
            plane_len[p] = 0x3fffffff;
          }
          locked = true;
        }
      }

      if (locked) {
        ahb_locked = true;
        HW_DBG("[mc] ahb planes: %p/%p/%p rs=%d/%d/%d ps=%d/%d/%d\n",
               (void*) planes[0], (void*) planes[1], (void*) planes[2],
               row_stride[0], row_stride[1], row_stride[2],
               pix_stride[0], pix_stride[1], pix_stride[2]);
      }
    }
  }

  HW_DBG("[mc] img crop=%d,%d-%d,%d planes=%p/%p/%p len=%d/%d/%d rs=%d/%d/%d ps=%d/%d/%d ok=%d\n",
         crop.left, crop.top, crop.right, crop.bottom,
         (void*) planes[0], (void*) planes[1], (void*) planes[2],
         plane_len[0], plane_len[1], plane_len[2],
         row_stride[0], row_stride[1], row_stride[2],
         pix_stride[0], pix_stride[1], pix_stride[2], (int) ok);

  if (ok) {
    for (int p = 0; p < 3 && ok; p++) {
      if (planes[p] == nullptr || row_stride[p] <= 0 || pix_stride[p] <= 0) {
        ok = false;
      }
    }
    // Make sure the planned copy stays inside the mapped planes.
    auto fits = [](size_t len, size_t off, int rows, int row, int cols, int pix) {
      if (rows <= 0 || cols <= 0 || row <= 0 || pix <= 0) {
        return false;
      }
      size_t need = off + static_cast<size_t>(rows - 1) * static_cast<size_t>(row) +
                    static_cast<size_t>(cols) * static_cast<size_t>(pix);
      return need <= len;
    };
    const uint8_t* y = planes[0] + static_cast<size_t>(crop.top) * row_stride[0] + crop.left * pix_stride[0];
    const uint8_t* u = planes[1] + static_cast<size_t>(crop.top / 2) * row_stride[1] + (crop.left / 2) * pix_stride[1];
    const uint8_t* v = planes[2] + static_cast<size_t>(crop.top / 2) * row_stride[2] + (crop.left / 2) * pix_stride[2];
    if (!ok ||
        !fits(plane_len[0], (size_t) (y - planes[0]), h, row_stride[0], w, pix_stride[0]) ||
        !fits(plane_len[1], (size_t) (u - planes[1]), (h + 1) / 2, row_stride[1], (w + 1) / 2, pix_stride[1]) ||
        !fits(plane_len[2], (size_t) (v - planes[2]), (h + 1) / 2, row_stride[2], (w + 1) / 2, pix_stride[2])) {
      HW_DBG("[mc] invalid plane layout or bounds exceeded, aborting copy\n");
      err = err_plugin("decoded frame plane layout invalid or too small");
    }
    else {
      const char* dump_path = getenv("HEIF_HW_DUMP");
      if (dump_path != nullptr) {
        FILE* f = fopen(dump_path, "wb");
        if (f) {
          int cw = (w + 1) / 2, chh = (h + 1) / 2;
          auto dump_plane = [&](const uint8_t* src, int srow, int spix, int pw, int ph) {
            for (int yy = 0; yy < ph; yy++) {
              const uint8_t* s = src + static_cast<size_t>(yy) * srow;
              if (spix == 1) {
                fwrite(s, 1, (size_t) pw, f);
              }
              else {
                for (int xx = 0; xx < pw; xx++) {
                  fputc(s[xx * spix], f);
                }
              }
            }
          };
          dump_plane(y, row_stride[0], pix_stride[0], w, h);
          dump_plane(u, row_stride[1], pix_stride[1], cw, chh);
          dump_plane(v, row_stride[2], pix_stride[2], cw, chh);
          fclose(f);
          HW_DBG("[mc] dumped yuv420p to %s\n", dump_path);
        }
      }

      err = make_heif_yuv420(w, h,
                             y, row_stride[0], pix_stride[0],
                             u, row_stride[1], pix_stride[1],
                             v, row_stride[2], pix_stride[2],
                             color, limits, out_img);
    }
  }

  if (ahb_locked) {
    AHardwareBuffer_unlock(ahb, nullptr);
  }
  AImage_delete(image);
  return err;
}

static heif_error copy_buffer_frame(McCodecEntry* e, size_t out_idx,
                                    const AMediaCodecBufferInfo& info,
                                    int w, int h, const SpsColorInfo& color,
                                    int32_t color_format, int32_t stride, int32_t slice_height,
                                    int32_t crop_l, int32_t crop_t,
                                    const heif_security_limits* limits,
                                    heif_image** out_img)
{
  size_t buf_size = 0;
  uint8_t* buf = AMediaCodec_getOutputBuffer(e->codec, out_idx, &buf_size);
  if (!buf || info.size <= 0) {
    AMediaCodec_releaseOutputBuffer(e->codec, out_idx, false);
    return err_plugin("cannot access decoded output buffer");
  }

  const uint8_t* base = buf + info.offset;

  int y_stride = (stride > 0 ? stride : w);
  int y_rows = (slice_height > 0 ? slice_height : h);

  bool planar = (color_format == COLOR_FormatYUV420Planar ||
                 color_format == COLOR_FormatYUV420PackedPlanar ||
                 color_format == COLOR_YV12);
  bool yv12 = (color_format == COLOR_YV12);

  const uint8_t* y = base + static_cast<size_t>(crop_t) * y_stride + crop_l;
  const uint8_t* u;
  const uint8_t* v;
  int u_row, v_row, u_pix, v_pix;

  if (planar) {
    int c_stride = (stride > 0 ? stride / 2 : (w + 1) / 2);
    int c_rows = (y_rows + 1) / 2;
    const uint8_t* plane1 = base + static_cast<size_t>(y_stride) * y_rows;
    const uint8_t* plane2 = plane1 + static_cast<size_t>(c_stride) * c_rows;

    u = (yv12 ? plane2 : plane1) + static_cast<size_t>(crop_t / 2) * c_stride + crop_l / 2;
    v = (yv12 ? plane1 : plane2) + static_cast<size_t>(crop_t / 2) * c_stride + crop_l / 2;
    u_row = v_row = c_stride;
    u_pix = v_pix = 1;
  }
  else {
    // semi-planar NV12 (including QCOM and flexible variants)
    int c_stride = y_stride;
    const uint8_t* uv = base + static_cast<size_t>(y_stride) * y_rows;
    u = uv + static_cast<size_t>(crop_t / 2) * c_stride + (crop_l & ~1);
    v = u + 1;
    u_row = v_row = c_stride;
    u_pix = v_pix = 2;
  }

  heif_error err = make_heif_yuv420(w, h,
                                    y, y_stride, 1,
                                    u, u_row, u_pix,
                                    v, v_row, v_pix,
                                    color, limits, out_img);

  AMediaCodec_releaseOutputBuffer(e->codec, out_idx, false);
  return err;
}


// ======================================================================
//                       decode one access unit

static heif_error decode_with_entry(McCodecEntry* e, const std::vector<uint8_t>& au,
                                    int w, int h, const SpsColorInfo& color,
                                    const heif_security_limits* limits,
                                    heif_image** out_img)
{
  // Drop stale frames that might still sit in the ImageReader queue.
  if (e->surface_mode) {
    AImage* stale = nullptr;
    while (AImageReader_acquireLatestImage(e->reader, &stale) == AMEDIA_OK && stale) {
      AImage_delete(stale);
      stale = nullptr;
    }
  }

  // --- feed the access unit

  ssize_t in_idx = AMediaCodec_dequeueInputBuffer(e->codec, 5000000);
  if (in_idx < 0) {
    return err_plugin("no input buffer available from MediaCodec");
  }

  size_t in_size = 0;
  uint8_t* in_buf = AMediaCodec_getInputBuffer(e->codec, static_cast<size_t>(in_idx), &in_size);
  if (!in_buf || in_size < au.size()) {
    return err_plugin("MediaCodec input buffer too small");
  }
  memcpy(in_buf, au.data(), au.size());

  // Attach EOS to the picture buffer itself: reliable even when the codec
  // does not hand out a second input buffer for a separate EOS packet.
  if (AMediaCodec_queueInputBuffer(e->codec, static_cast<size_t>(in_idx), 0,
                                   au.size(), 0,
                                   AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != AMEDIA_OK) {
    return err_plugin("failed to queue input buffer");
  }
  HW_DBG("[mc] queued AU: %zu bytes in slot %zd (EOS flag)\n", au.size(), in_idx);

  // --- collect the decoded frame

  int32_t color_format = COLOR_FormatYUV420Flexible;
  int32_t stride = 0;
  int32_t slice_height = 0;
  int32_t crop_l = 0, crop_t = 0;

  for (int tries = 0; tries < 400; tries++) {
    AMediaCodecBufferInfo info;
    ssize_t out_idx = AMediaCodec_dequeueOutputBuffer(e->codec, &info, 5000);

    if (out_idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
      if (tries % 50 == 0) {
        HW_DBG("[mc] out: TRY_AGAIN (%d)\n", tries);
      }
      continue;
    }
    if (out_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
      AMediaFormat* ofmt = AMediaCodec_getOutputFormat(e->codec);
      if (ofmt) {
        AMediaFormat_getInt32(ofmt, "color-format", &color_format);
        AMediaFormat_getInt32(ofmt, "stride", &stride);
        AMediaFormat_getInt32(ofmt, "slice-height", &slice_height);
        AMediaFormat_getInt32(ofmt, "crop-left", &crop_l);
        AMediaFormat_getInt32(ofmt, "crop-top", &crop_t);
        HW_DBG("[mc] out format: color=%d stride=%d slice=%d crop=%d,%d\n",
               color_format, stride, slice_height, crop_l, crop_t);
        AMediaFormat_delete(ofmt);
      }
      continue;
    }
    if (out_idx < 0) {
      HW_DBG("[mc] out: idx=%zd\n", out_idx);
      continue;
    }
    HW_DBG("[mc] out buffer %zd size=%d flags=0x%x\n", out_idx, info.size, info.flags);

    if (e->surface_mode) {
      // Surface output buffers carry no accessible data (info.size == 0);
      // the frame itself goes to the surface and must be rendered.
      if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) {
        AMediaCodec_releaseOutputBuffer(e->codec, static_cast<size_t>(out_idx), false);
        continue;
      }
      return copy_surface_frame(e, static_cast<size_t>(out_idx), w, h, color, limits, out_img);
    }
    else {
      if (info.size <= 0) {
        AMediaCodec_releaseOutputBuffer(e->codec, static_cast<size_t>(out_idx), false);
        continue;
      }
      return copy_buffer_frame(e, static_cast<size_t>(out_idx), info, w, h, color,
                               color_format, stride, slice_height, crop_l, crop_t,
                               limits, out_img);
    }
  }

  return err_plugin("timeout while waiting for decoded frame");
}


// ======================================================================
//                       software fallback

static heif_error sw_fallback_decode(const uint8_t* data, size_t size, uintptr_t user_data,
                                     int num_threads, bool strict_decoding,
                                     heif_image** out_img, uintptr_t* out_user_data,
                                     const heif_security_limits* limits)
{
#if HAVE_LIBDE265
  const heif_decoder_plugin* sw = get_decoder_plugin_libde265();
  if (!sw) {
    return err_plugin("no software decoder available");
  }

  heif_decoder_plugin_options options{};
  options.format = heif_compression_HEVC;
  options.strict_decoding = strict_decoding ? 1 : 0;
  options.num_threads = num_threads;
  options.limits = limits;

  void* swd = nullptr;
  heif_error err = sw->new_decoder2(&swd, &options);
  if (err.code != heif_error_Ok) {
    return err;
  }

  err = sw->push_data2(swd, data, size, user_data);
  if (err.code == heif_error_Ok) {
    err = sw->flush_data(swd);
  }
  if (err.code == heif_error_Ok) {
    err = sw->decode_next_image2(swd, out_img, out_user_data, limits);
  }

  sw->free_decoder(swd);
  return err;
#else
  (void) data; (void) size; (void) user_data;
  (void) num_threads; (void) strict_decoding;
  (void) out_img; (void) out_user_data; (void) limits;
  return err_plugin("hardware decoding failed and no software fallback was compiled in");
#endif
}


// ======================================================================
//                       plugin interface

struct mc_decoder
{
  std::vector<uint8_t> pending;
  uintptr_t pending_user_data = 0;
  bool strict_decoding = false;
  int num_threads = 0;
  const heif_security_limits* limits = nullptr;
};

static const char* mc_plugin_name()
{
  return "Android MediaCodec HEVC decoder";
}

static int mc_does_support_format(heif_compression_format format)
{
  if (format != heif_compression_HEVC) {
    return 0;
  }
  if (hw_disabled_by_env() || !probe_hw_codec()) {
    return 0;
  }
  return MEDIACODEC_PLUGIN_PRIORITY;
}

static int mc_does_support_format2(const heif_decoder_plugin_compressed_format_description* format)
{
  return mc_does_support_format(format->format);
}

static heif_error mc_new_decoder2(void** dec, const heif_decoder_plugin_options* options)
{
  mc_decoder* decoder = new mc_decoder();
  if (options) {
    decoder->strict_decoding = (options->strict_decoding != 0);
    decoder->num_threads = options->num_threads;
    decoder->limits = options->limits;
  }
  *dec = decoder;
  return err_ok();
}

static heif_error mc_new_decoder(void** dec)
{
  heif_decoder_plugin_options options{};
  options.format = heif_compression_HEVC;
  options.strict_decoding = 0;
  options.num_threads = 0;
  options.limits = nullptr;
  return mc_new_decoder2(dec, &options);
}

static void mc_free_decoder(void* decoder_raw)
{
  delete static_cast<mc_decoder*>(decoder_raw);
}

static void mc_set_strict_decoding(void* decoder_raw, int flag)
{
  static_cast<mc_decoder*>(decoder_raw)->strict_decoding = (flag != 0);
}

static heif_error mc_push_data2(void* decoder_raw, const void* data, size_t size, uintptr_t user_data)
{
  mc_decoder* decoder = static_cast<mc_decoder*>(decoder_raw);
  const uint8_t* cdata = static_cast<const uint8_t*>(data);
  decoder->pending.insert(decoder->pending.end(), cdata, cdata + size);
  decoder->pending_user_data = user_data;
  return err_ok();
}

static heif_error mc_push_data(void* decoder_raw, const void* data, size_t size)
{
  return mc_push_data2(decoder_raw, data, size, 0);
}

static heif_error mc_flush_data(void* decoder_raw)
{
  (void) decoder_raw;
  return err_ok();
}

static heif_error mc_decode_next_image2(void* decoder_raw, heif_image** out_img,
                                        uintptr_t* out_user_data,
                                        const heif_security_limits* limits)
{
  mc_decoder* decoder = static_cast<mc_decoder*>(decoder_raw);
  *out_img = nullptr;

  if (decoder->pending.empty()) {
    return err_plugin("no compressed data pending in MediaCodec decoder");
  }

  const heif_security_limits* eff_limits =
      (limits ? limits : (decoder->limits ? decoder->limits : heif_get_global_security_limits()));

  std::vector<NalUnit> nals;
  bool hw_done = false;

  if (split_length_prefixed_nals(decoder->pending.data(), decoder->pending.size(), nals) &&
      !hw_disabled_by_env() && !g_hw_broken) {

    const NalUnit* sps = nullptr;
    for (const NalUnit& nal : nals) {
      HW_DBG("[mc] NAL type=%d size=%zu\n", nal.type, nal.size);
      if (nal.type == HEVC_NAL_SPS) {
        sps = &nal;
      }
    }

    if (sps && probe_hw_codec()) {
      HEVCDecoderConfigurationRecord config{};
      uint32_t width = 0, height = 0;
      Error sps_err = parse_sps_for_hvcC_configuration(sps->data, sps->size, &config, &width, &height);

      SpsColorInfo color;
      bool vui_ok = parse_sps_vui_color_info(sps->data, sps->size, &color);

      HW_DBG("[mc] %zu NALs, SPS %ux%u chroma=%d depth=%d/%d, err=%d, vui=%d "
             "primaries=%d transfer=%d matrix=%d full=%d\n",
             nals.size(), width, height,
             (int) config.chroma_format, (int) config.bit_depth_luma,
             (int) config.bit_depth_chroma, (sps_err ? 1 : 0), (vui_ok ? 1 : 0),
             (int) color.primaries, (int) color.transfer, (int) color.matrix,
             (int) color.full_range);

      // Hardware path currently handles 8-bit 4:2:0 pictures; anything else
      // goes to the software decoder.
      if (!sps_err && width > 0 && height > 0 &&
          config.chroma_format == 1 &&
          config.bit_depth_luma <= 8 && config.bit_depth_chroma <= 8) {

        std::vector<uint8_t> csd, au;
        const char* nalfmt = getenv("HEIF_HW_NALFMT");
        bool raw_input = (nalfmt && strcmp(nalfmt, "raw") == 0);
        bool no_cfg = (nalfmt && strcmp(nalfmt, "annexb-nocfg") == 0);
        bool no_sei = (nalfmt && strcmp(nalfmt, "annexb-nosei") == 0);

        for (const NalUnit& nal : nals) {
          if (nal.type == HEVC_NAL_VPS || nal.type == HEVC_NAL_SPS || nal.type == HEVC_NAL_PPS) {
            append_annexb(csd, nal);
            if (no_cfg) {
              continue;
            }
          }
          if (no_sei && nal.type >= 35 && nal.type <= 40) {
            continue;
          }
          if (raw_input) {
            au.insert(au.end(), nal.data - 4, nal.data + nal.size);  // keep 4-byte length prefix
          }
          else {
            append_annexb(au, nal);
          }
        }
        HW_DBG("[mc] nalfmt=%s csd=%zu au=%zu\n", nalfmt ? nalfmt : "annexb", csd.size(), au.size());

        if (!csd.empty()) {
          HW_DBG("[mc] csd=%zu au=%zu, acquiring codec for %dx%d\n",
                 csd.size(), au.size(), (int) width, (int) height);
          McCodecEntry* entry = acquire_entry((int) width, (int) height, csd);
          HW_DBG("[mc] entry=%p\n", (void*) entry);
          if (entry) {
            heif_error err = decode_with_entry(entry, au, (int) width, (int) height,
                                               color, eff_limits, out_img);
            HW_DBG("[mc] hw decode result: %d (%s)\n", err.code, err.message ? err.message : "");
            release_entry(entry, err.code == heif_error_Ok);
            if (err.code == heif_error_Ok) {
              hw_done = true;
            }
          }
        }
      }
    }
  }

  if (hw_done) {
    if (out_user_data) {
      *out_user_data = decoder->pending_user_data;
    }
    decoder->pending.clear();
    return err_ok();
  }

  heif_error err = sw_fallback_decode(decoder->pending.data(), decoder->pending.size(),
                                      decoder->pending_user_data,
                                      decoder->num_threads, decoder->strict_decoding,
                                      out_img, out_user_data, eff_limits);
  decoder->pending.clear();
  return err;
}

static heif_error mc_decode_next_image(void* decoder_raw, heif_image** out_img,
                                       const heif_security_limits* limits)
{
  return mc_decode_next_image2(decoder_raw, out_img, nullptr, limits);
}

static heif_error mc_decode_image(void* decoder_raw, heif_image** out_img)
{
  return mc_decode_next_image2(decoder_raw, out_img, nullptr, heif_get_global_security_limits());
}


static const heif_decoder_plugin decoder_mediacodec
    {
        5,
        mc_plugin_name,
        nullptr,
        nullptr,
        mc_does_support_format,
        mc_new_decoder,
        mc_free_decoder,
        mc_push_data,
        mc_decode_image,
        mc_set_strict_decoding,
        "mediacodec",
        mc_decode_next_image,
        /* minimum_required_libheif_version */ LIBHEIF_MAKE_VERSION(1, 21, 0),
        mc_does_support_format2,
        mc_new_decoder2,
        mc_push_data2,
        mc_flush_data,
        mc_decode_next_image2
    };

const heif_decoder_plugin* get_decoder_plugin_mediacodec()
{
  return &decoder_mediacodec;
}
