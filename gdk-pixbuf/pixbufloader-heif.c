/*
 * gdk-pixbuf loader module for libheif
 * Copyright (c) 2019 Oliver Giles <ohw.giles@gmail.com>
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

#define GDK_PIXBUF_ENABLE_BACKEND

#include <gdk-pixbuf/gdk-pixbuf-io.h>
#include <libheif/heif.h>
#include <math.h>


G_MODULE_EXPORT void fill_vtable(GdkPixbufModule* module);

G_MODULE_EXPORT void fill_info(GdkPixbufFormat* info);


typedef struct
{
  GdkPixbufModuleUpdatedFunc update_func;
  GdkPixbufModulePreparedFunc prepare_func;
  GdkPixbufModuleSizeFunc size_func;
  gpointer user_data;
  GByteArray* data;
} HeifPixbufCtx;


static gpointer begin_load(GdkPixbufModuleSizeFunc size_func,
                           GdkPixbufModulePreparedFunc prepare_func,
                           GdkPixbufModuleUpdatedFunc update_func,
                           gpointer user_data,
                           GError** error)
{
  HeifPixbufCtx* hpc;

  hpc = g_new0(HeifPixbufCtx, 1);
  hpc->data = g_byte_array_new();
  hpc->size_func = size_func;
  hpc->prepare_func = prepare_func;
  hpc->update_func = update_func;
  hpc->user_data = user_data;
  return hpc;
}


static void release_heif_image(guchar* pixels, gpointer data)
{
  heif_image_release((struct heif_image*) data);
}


static void free_hdr_buffer(guchar* pixels, gpointer data)
{
  g_free(pixels);
}


typedef struct {
  float to_srgb[9];
  float luma[3];
} CicpPrimaries;

static const CicpPrimaries primaries_bt2020 = {
  .to_srgb = {
     1.660227f, -0.587548f, -0.072838f,
    -0.124553f,  1.132926f, -0.008350f,
    -0.018155f, -0.100603f,  1.118998f,
  },
  .luma = { 0.2627f, 0.6780f, 0.0593f },
};


static const CicpPrimaries primaries_p3 = {
  .to_srgb = {
     1.224940f, -0.224940f,  0.000000f,
    -0.042057f,  1.042057f,  0.000000f,
    -0.019638f, -0.078636f,  1.098274f,
  },
  .luma = { 0.2290f, 0.6917f, 0.0793f },
};


typedef void (*CicpEotfFunc)(float rgb[3], const float luma[3]);


static inline float hlg_inv_oetf(float e)
{
  const float a = 0.17883277f;
  const float b = 0.28466892f;
  const float c = 0.55991073f;
  return e <= 0.5f ? (e * e) / 3.0f : (expf((e - c) / a) + b) / 12.0f;
}


static void hlg_eotf(float rgb[3], const float luma[3])
{
  rgb[0] = hlg_inv_oetf(rgb[0]);
  rgb[1] = hlg_inv_oetf(rgb[1]);
  rgb[2] = hlg_inv_oetf(rgb[2]);

  float Ys = luma[0] * rgb[0] + luma[1] * rgb[1] + luma[2] * rgb[2];
  if (Ys > 0.0f) {
    float scale = (1000.0f / 203.0f) * powf(Ys, 0.2f);
    rgb[0] *= scale;
    rgb[1] *= scale;
    rgb[2] *= scale;
  }
}


static void pq_eotf(float rgb[3], const float luma[3])
{
  const float m1 = 0.1593017578125f;
  const float m2 = 78.84375f;
  const float c1 = 0.8359375f;
  const float c2 = 18.8515625f;
  const float c3 = 18.6875f;

  (void)luma;

  for (int c = 0; c < 3; c++) {
    float Nm1 = powf(rgb[c], 1.0f / m2);
    float num = Nm1 - c1;
    if (num < 0.0f) num = 0.0f;
    float denom = c2 - c3 * Nm1;
    rgb[c] = powf(num / denom, 1.0f / m1) * (10000.0f / 203.0f);
  }
}


static inline uint8_t linear_to_srgb_u8(float v)
{
  if (v <= 0.0f) return 0;
  if (v >= 1.0f) return 255;
  v = v <= 0.0031308f ? 12.92f * v : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
  return (uint8_t)(v * 255.0f + 0.5f);
}


static uint8_t* convert_hdr_to_srgb(const struct heif_image* img, int src_bpp,
                                     int width, int height,
                                     int has_alpha, int* out_stride,
                                     CicpEotfFunc eotf,
                                     const CicpPrimaries* primaries)
{
  int src_stride;
  const uint8_t* src = heif_image_get_plane_readonly(img, heif_channel_interleaved, &src_stride);

  float max_val = (float)((1 << src_bpp) - 1);

  int channels = has_alpha ? 4 : 3;
  int dst_stride = width * channels;
  uint8_t* dst = (uint8_t*) g_malloc(dst_stride * height);

  const float* m = primaries->to_srgb;

  for (int y = 0; y < height; y++) {
    const uint16_t* sp = (const uint16_t*)(src + y * src_stride);
    uint8_t* dp = dst + y * dst_stride;

    for (int x = 0; x < width; x++) {
      float rgb[3];
      rgb[0] = sp[x * channels + 0] / max_val;
      rgb[1] = sp[x * channels + 1] / max_val;
      rgb[2] = sp[x * channels + 2] / max_val;

      eotf(rgb, primaries->luma);

      dp[x * channels + 0] = linear_to_srgb_u8(m[0] * rgb[0] + m[1] * rgb[1] + m[2] * rgb[2]);
      dp[x * channels + 1] = linear_to_srgb_u8(m[3] * rgb[0] + m[4] * rgb[1] + m[5] * rgb[2]);
      dp[x * channels + 2] = linear_to_srgb_u8(m[6] * rgb[0] + m[7] * rgb[1] + m[8] * rgb[2]);

      if (has_alpha) {
        uint16_t a_val = sp[x * channels + 3];
        float a_norm = a_val / max_val;
        if (a_norm > 1.0f) a_norm = 1.0f;
        dp[x * channels + 3] = (uint8_t)(a_norm * 255.0f + 0.5f);
      }
    }
  }

  *out_stride = dst_stride;
  return dst;
}


static gboolean stop_load(gpointer context, GError** error)
{
  HeifPixbufCtx* hpc;
  struct heif_error err;
  struct heif_context* hc = NULL;
  struct heif_image_handle* hdl = NULL;
  struct heif_image* img = NULL;
  int width, height, stride;
  int requested_width, requested_height;
  const uint8_t* data;
  GdkPixbuf* pixbuf;
  gboolean result;

  result = FALSE;
  hpc = (HeifPixbufCtx*) context;

  err = heif_init(NULL);
  if (err.code != heif_error_Ok) {
    g_warning("%s", err.message);
    goto cleanup;
  }

  hc = heif_context_alloc();
  if (!hc) {
    g_warning("cannot allocate heif_context");
    goto cleanup;
  }

  err = heif_context_read_from_memory_without_copy(hc, hpc->data->data, hpc->data->len, NULL);
  if (err.code != heif_error_Ok) {
    g_warning("%s", err.message);
    goto cleanup;
  }

  err = heif_context_get_primary_image_handle(hc, &hdl);
  if (err.code != heif_error_Ok) {
    g_warning("%s", err.message);
    goto cleanup;
  }

  int has_alpha = heif_image_handle_has_alpha_channel(hdl);

  const CicpPrimaries* primaries = NULL;
  CicpEotfFunc eotf = NULL;
  struct heif_color_profile_nclx* nclx = NULL;
  heif_image_handle_get_nclx_color_profile(hdl, &nclx);
  if (nclx) {
    int tc = nclx->transfer_characteristics;
    int cp = nclx->color_primaries;

    if (tc == 18)
      eotf = hlg_eotf;
    else if (tc == 16)
      eotf = pq_eotf;

    if (cp == 9)
      primaries = &primaries_bt2020;
    else if (cp == 12)
      primaries = &primaries_p3;

    heif_nclx_color_profile_free(nclx);
  }

  heif_chroma chroma;
  if (eotf && primaries) {
    chroma = has_alpha ? heif_chroma_interleaved_RRGGBBAA_LE : heif_chroma_interleaved_RRGGBB_LE;
  } else {
    chroma = has_alpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB;
  }

  err = heif_decode_image(hdl, &img, heif_colorspace_RGB, chroma, NULL);
  if (err.code != heif_error_Ok) {
    g_warning("%s", err.message);
    goto cleanup;
  }

  width = heif_image_get_width(img, heif_channel_interleaved);
  height = heif_image_get_height(img, heif_channel_interleaved);
  requested_width = width;
  requested_height = height;

  if (hpc->size_func) {
    (*hpc->size_func)(&requested_width, &requested_height, hpc->user_data);
  }

  if (requested_width > 0 && requested_height > 0 && (width != requested_width || height != requested_height)) {
    struct heif_image* resized;
    heif_image_scale_image(img, &resized, requested_width, requested_height, NULL);
    heif_image_release(img);
    width = requested_width;
    height = requested_height;
    img = resized;
  }

  if (eotf && primaries) {
    int src_bpp = heif_image_handle_get_luma_bits_per_pixel(hdl);
    int converted_stride;
    uint8_t* converted = convert_hdr_to_srgb(img, src_bpp, width, height, has_alpha, &converted_stride, eotf, primaries);
    heif_image_release(img);
    img = NULL;

    pixbuf = gdk_pixbuf_new_from_data(converted, GDK_COLORSPACE_RGB, has_alpha, 8,
                                      width, height, converted_stride,
                                      free_hdr_buffer, NULL);
  } else {
    data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);

    pixbuf = gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, has_alpha, 8, width, height, stride, release_heif_image,
                                      img);

    size_t profile_size = heif_image_handle_get_raw_color_profile_size(hdl);
    if(profile_size) {
      guchar *profile_data = (guchar *)g_malloc0(profile_size);

      err = heif_image_handle_get_raw_color_profile(hdl, profile_data);
      if (err.code == heif_error_Ok) {
        gchar *profile_base64 = g_base64_encode(profile_data, profile_size);
        gdk_pixbuf_set_option(pixbuf, "icc-profile", profile_base64);
        g_free(profile_base64);
      }
      else {
        // Having no ICC profile is perfectly fine. Do not show any warning because of that.
      }

      g_free(profile_data);
    }
  }
  
  if (hpc->prepare_func) {
    (*hpc->prepare_func)(pixbuf, NULL, hpc->user_data);
  }

  if (hpc->update_func != NULL) {
    (*hpc->update_func)(pixbuf, 0, 0, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf), hpc->user_data);
  }

  g_clear_object(&pixbuf);

  result = TRUE;

  cleanup:
  if (img) {
    // Do not free the image here when we pass it to gdk-pixbuf, as its memory will still be used by gdk-pixbuf.

    if (!result) {
      heif_image_release(img);
    }
  }

  if (hdl) {
    heif_image_handle_release(hdl);
  }

  if (hc) {
    heif_context_free(hc);
  }

  g_byte_array_free(hpc->data, TRUE);
  g_free(hpc);

  heif_deinit();

  return result;
}


static gboolean load_increment(gpointer context, const guchar* buf, guint size, GError** error)
{
  HeifPixbufCtx* ctx = (HeifPixbufCtx*) context;
  g_byte_array_append(ctx->data, buf, size);
  return TRUE;
}


void fill_vtable(GdkPixbufModule* module)
{
  module->begin_load = begin_load;
  module->stop_load = stop_load;
  module->load_increment = load_increment;
}


void fill_info(GdkPixbufFormat* info)
{
  static GdkPixbufModulePattern signature[] = {
      {"    ftyp", "xxxx    ", 100},
      {NULL, NULL,             0}
  };

  static gchar* mime_types[] = {
      "image/heif",
      "image/heic",
      "image/avif",
      NULL
  };

  static gchar* extensions[] = {
      "heif",
      "heic",
      "avif",
      NULL
  };

  info->name = "heif/avif";
  info->signature = signature;
  info->domain = "pixbufloader-heif";
  info->description = "HEIF/AVIF Image";
  info->mime_types = mime_types;
  info->extensions = extensions;
  info->flags = GDK_PIXBUF_FORMAT_THREADSAFE;
  info->disabled = FALSE;
  info->license = "LGPL3";
}
