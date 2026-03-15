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


static void free_hlg_buffer(guchar* pixels, gpointer data)
{
  g_free(pixels);
}


static uint8_t* convert_hlg_to_srgb(const struct heif_image* img, int src_bpp,
                                     int width, int height,
                                     int has_alpha, int* out_stride)
{
  int src_stride;
  const uint8_t* src = heif_image_get_plane_readonly(img, heif_channel_interleaved, &src_stride);
  if (!src)
    return NULL;

  float max_val = (float)((1 << src_bpp) - 1);

  int src_channels = has_alpha ? 4 : 3;
  int dst_channels = has_alpha ? 4 : 3;
  int dst_stride = width * dst_channels;
  uint8_t* dst = (uint8_t*) g_malloc(dst_stride * height);

  /* HLG OETF constants (BT.2100) */
  const float hlg_a = 0.17883277f;
  const float hlg_b = 0.28466892f;
  const float hlg_c = 0.55991073f;

  /* BT.2020 -> BT.709 matrix (linear light) */
  const float m[9] = {
     1.660227f, -0.587548f, -0.072838f,
    -0.124553f,  1.132926f, -0.008350f,
    -0.018155f, -0.100603f,  1.118998f,
  };

  for (int y = 0; y < height; y++) {
    const uint16_t* sp = (const uint16_t*)(src + y * src_stride);
    uint8_t* dp = dst + y * dst_stride;

    for (int x = 0; x < width; x++) {
      float rgb[3];

      /* Read 16-bit native and normalize to [0, 1] */
      for (int c = 0; c < 3; c++) {
        uint16_t val = sp[x * src_channels + c];
        float E_prime = val / max_val;

        /* Inverse HLG OETF (BT.2100 Table 5) -> scene linear */
        if (E_prime <= 0.5f)
          rgb[c] = (E_prime * E_prime) / 3.0f;
        else
          rgb[c] = (expf((E_prime - hlg_c) / hlg_a) + hlg_b) / 12.0f;
      }

      /* OOTF: scene linear -> display linear (BT.2100), 1000 nit target */
      float Ys = 0.2627f * rgb[0] + 0.6780f * rgb[1] + 0.0593f * rgb[2];
      if (Ys > 0.0f) {
        float scale = (1000.0f / 203.0f) * powf(Ys, 0.2f);
        rgb[0] *= scale;
        rgb[1] *= scale;
        rgb[2] *= scale;
      }

      /* BT.2020 -> BT.709 gamut mapping */
      float r = m[0] * rgb[0] + m[1] * rgb[1] + m[2] * rgb[2];
      float g = m[3] * rgb[0] + m[4] * rgb[1] + m[5] * rgb[2];
      float b = m[6] * rgb[0] + m[7] * rgb[1] + m[8] * rgb[2];

      if (r < 0.0f) r = 0.0f; else if (r > 1.0f) r = 1.0f;
      if (g < 0.0f) g = 0.0f; else if (g > 1.0f) g = 1.0f;
      if (b < 0.0f) b = 0.0f; else if (b > 1.0f) b = 1.0f;

      /* sRGB OETF */
      r = r <= 0.0031308f ? 12.92f * r : 1.055f * powf(r, 1.0f / 2.4f) - 0.055f;
      g = g <= 0.0031308f ? 12.92f * g : 1.055f * powf(g, 1.0f / 2.4f) - 0.055f;
      b = b <= 0.0031308f ? 12.92f * b : 1.055f * powf(b, 1.0f / 2.4f) - 0.055f;

      /* Quantize to 8-bit */
      dp[x * dst_channels + 0] = (uint8_t)(r * 255.0f + 0.5f);
      dp[x * dst_channels + 1] = (uint8_t)(g * 255.0f + 0.5f);
      dp[x * dst_channels + 2] = (uint8_t)(b * 255.0f + 0.5f);

      if (has_alpha) {
        uint16_t a_val = sp[x * src_channels + 3];
        float a_norm = a_val / max_val;
        if (a_norm > 1.0f) a_norm = 1.0f;
        dp[x * dst_channels + 3] = (uint8_t)(a_norm * 255.0f + 0.5f);
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

  int is_hlg = 0;
  struct heif_color_profile_nclx* nclx = NULL;
  heif_image_handle_get_nclx_color_profile(hdl, &nclx);
  if (nclx) {
    is_hlg = nclx->transfer_characteristics == 18;
    heif_nclx_color_profile_free(nclx);
  }

  heif_chroma chroma;
  if (is_hlg) {
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

  if (is_hlg) {
    int src_bpp = heif_image_handle_get_luma_bits_per_pixel(hdl);
    int converted_stride;
    uint8_t* converted = convert_hlg_to_srgb(img, src_bpp, width, height, has_alpha, &converted_stride);
    heif_image_release(img);
    img = NULL;

    if (!converted) {
      g_warning("HLG to sRGB conversion failed");
      goto cleanup;
    }

    pixbuf = gdk_pixbuf_new_from_data(converted, GDK_COLORSPACE_RGB, has_alpha, 8,
                                      width, height, converted_stride,
                                      free_hlg_buffer, NULL);
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
