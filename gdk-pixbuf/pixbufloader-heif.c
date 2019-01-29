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

#include <gdk-pixbuf/gdk-pixbuf-io.h>
#include <libheif/heif.h>

G_MODULE_EXPORT void fill_vtable (GdkPixbufModule* module);
G_MODULE_EXPORT void fill_info (GdkPixbufFormat* info);

typedef struct {
	GdkPixbufModuleUpdatedFunc  update_func;
	GdkPixbufModulePreparedFunc prepare_func;
	GdkPixbufModuleSizeFunc     size_func;
	gpointer                    user_data;
	GByteArray*                 data;
} HeifPixbufCtx;

static gpointer begin_load(GdkPixbufModuleSizeFunc size_func, GdkPixbufModulePreparedFunc prepare_func, GdkPixbufModuleUpdatedFunc update_func, gpointer user_data, GError** error)
{
	HeifPixbufCtx* hpc;

	hpc               = g_new0(HeifPixbufCtx, 1);
	hpc->data         = g_byte_array_new();
	hpc->size_func    = size_func;
	hpc->prepare_func = prepare_func;
	hpc->update_func  = update_func;
	hpc->user_data    = user_data;
	return hpc;
}

static void cleanup_heif_context(guchar* pixels, gpointer data)
{
	heif_context_free((struct heif_context*) data);
}

static gboolean stop_load(gpointer context, GError** error)
{
	HeifPixbufCtx* hpc;
	struct heif_error err;
	struct heif_context* hc;
	struct heif_image_handle* hdl;
	struct heif_image* img;
	int width, height, stride;
	int requested_width, requested_height;
	const uint8_t* data;
	GdkPixbuf* pixbuf;
	gboolean result;

	result = FALSE;
	hpc = (HeifPixbufCtx*) context;

	hc = heif_context_alloc();

	err = heif_context_read_from_memory_without_copy(hc, hpc->data->data, hpc->data->len, NULL);
	if(err.code != heif_error_Ok) {
		g_warning("%s", err.message);
		goto cleanup;
	}

	err = heif_context_get_primary_image_handle(hc, &hdl);
	if(err.code != heif_error_Ok) {
		g_warning("%s", err.message);
		goto cleanup;
	}

	err = heif_decode_image(hdl, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, NULL);
	if(err.code != heif_error_Ok) {
		g_warning("%s", err.message);
		goto cleanup;
	}

	width = heif_image_get_width(img, heif_channel_interleaved);
	height = heif_image_get_height(img, heif_channel_interleaved);
	requested_width = 0;
	requested_height = 0;
	if(hpc->size_func)
		(*hpc->size_func)(&requested_width, &requested_height, hpc->user_data);

	if(requested_width == 0 || requested_height == 0) {
		width = heif_image_get_width(img, heif_channel_interleaved);
		height = heif_image_get_height(img, heif_channel_interleaved);
	} else {
		struct heif_image* resized;
		heif_image_scale_image(img, &resized, requested_width, requested_height, NULL);
		heif_image_release(img);
		img = resized;
	}

	data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);

	pixbuf = gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, TRUE, 8, width, height, stride, cleanup_heif_context, hc);

	if(hpc->prepare_func)
		(*hpc->prepare_func)(pixbuf, NULL, hpc->user_data);

	if(hpc->update_func != NULL)
		(*hpc->update_func)(pixbuf, 0, 0, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf), hpc->user_data);

	result = TRUE;

cleanup:
	if(!result)
		heif_context_free(hc);

	g_byte_array_free(hpc->data, TRUE);
	g_free(hpc);

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
	module->begin_load     = begin_load;
	module->stop_load      = stop_load;
	module->load_increment = load_increment;
}

void fill_info(GdkPixbufFormat* info)
{
	static GdkPixbufModulePattern signature[] = {
	    { "    ftyp", "xxxx    ", 100 },
	    { NULL, NULL, 0 }
	};

	static gchar *mime_types[] = {
	    "image/heif",
	    "image/heic",
	    NULL
	};

	static gchar *extensions[] = {
	    "heif",
	    "heic",
	    NULL
	};

	info->name        = "heif";
	info->signature   = signature;
	info->domain      = "pixbufloader-heif";
	info->description = "HEIF Image";
	info->mime_types  = mime_types;
	info->extensions  = extensions;
	info->flags       = GDK_PIXBUF_FORMAT_THREADSAFE;
	info->disabled    = FALSE;
	info->license     = "GPLv3";
}
