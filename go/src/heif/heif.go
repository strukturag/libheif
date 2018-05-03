/*
 * GO interface to libheif
 * Copyright (c) 2018 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of heif, an example application using libheif.
 *
 * heif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * heif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with heif.  If not, see <http://www.gnu.org/licenses/>.
 */

package heif

/*
#cgo CPPFLAGS: -I/home/domain/farindk/sys/include/libheif
#cgo pkg-config: libheif
#include <stdlib.h>
#include <heif.h>
*/
import "C"

import (
        "runtime"
        "unsafe"
)


func HeifGetVersion() string {
    return C.GoString(C.heif_get_version())
}



// --- HeifError

type HeifError struct {
        Code int
        Subcode int
        Message string
}

func NewHeifError(cerror C.struct_heif_error) HeifError {
        var error HeifError
        error.Code = int(cerror.code)
        error.Subcode = int(cerror.subcode)
        error.Message = C.GoString(cerror.message)
        return error
}



// --- HeifContext

type HeifContext struct {
        context *C.struct_heif_context;
}


func NewHeifContext() *HeifContext {
        ctx := new(HeifContext)

        ctx.context = C.heif_context_alloc()
        runtime.SetFinalizer(ctx, free_heif_context)

        return ctx
}


func free_heif_context(c *HeifContext) {
        C.heif_context_free(c.context)
        c.context = nil;
        runtime.SetFinalizer(c, nil)
}


func (c *HeifContext) ReadFromFile(filename string) HeifError {
        var c_filename = C.CString(filename)
        defer C.free(unsafe.Pointer(c_filename))

        var err = C.heif_context_read_from_file(c.context, c_filename, nil)
        return NewHeifError(err);
}


func (c *HeifContext) GetNumberofTopLevelImages() int {
        return int(C.heif_context_get_number_of_top_level_images(c.context))
}


func (c *HeifContext) IsTopLevelImageID(ID int) bool {
        return C.heif_context_is_top_level_image_ID(c.context, C.heif_item_id(ID)) != 0
}


func (c *HeifContext) GetListOfTopLevelImageIDs() []int {
        var nToplevel = int(C.heif_context_get_number_of_top_level_images(c.context))
        var origIDs = make([]C.heif_item_id, nToplevel)

        C.heif_context_get_list_of_top_level_image_IDs(c.context, &origIDs[0], C.int(nToplevel))

        var IDs = make([]int, nToplevel)
        for i:=0; i<nToplevel; i++ {
          IDs[i] = int(origIDs[i])
        }

        return IDs
}


func (c *HeifContext) GetPrimaryImageID() (int, HeifError) {
        var c_id C.heif_item_id
        var err = C.heif_context_get_primary_image_ID(c.context, &c_id)

        return int(c_id), NewHeifError(err)
}



// --- HeifImageHandle

type HeifImageHandle struct {
        handle *C.struct_heif_image_handle
}

func free_heif_image_handle(c *HeifImageHandle) {
        C.heif_image_handle_release(c.handle)
        c.handle = nil;
        runtime.SetFinalizer(c, nil)
}

func (c *HeifContext) GetPrimaryImageHandle() (*HeifImageHandle, HeifError) {
        var handle HeifImageHandle
        var err = C.heif_context_get_primary_image_handle(c.context, &handle.handle)
        runtime.SetFinalizer(&handle, free_heif_image_handle)
        return &handle, NewHeifError(err)
}

func (c *HeifContext) GetImageHandle(id int) (*HeifImageHandle, HeifError) {
        var handle HeifImageHandle
        var error = C.heif_context_get_image_handle(c.context, C.heif_item_id(id), &handle.handle)
        runtime.SetFinalizer(&handle, free_heif_image_handle)
        return &handle, NewHeifError(error)
}


func (h *HeifImageHandle) IsPrimaryImage() bool {
        return C.heif_image_handle_is_primary_image(h.handle) != 0
}


func (h *HeifImageHandle) GetWidth() int {
        return int(C.heif_image_handle_get_width(h.handle))
}

func (h *HeifImageHandle) GetHeight() int {
        return int(C.heif_image_handle_get_height(h.handle))
}

func (h *HeifImageHandle) HasAlphaChannel() bool {
        return C.heif_image_handle_has_alpha_channel(h.handle) != 0
}


func (h *HeifImageHandle) HasDepthImage() bool {
        return C.heif_image_handle_has_depth_image(h.handle) != 0
}


func (h *HeifImageHandle) GetNumberOfDepthImages() int {
        return int(C.heif_image_handle_get_number_of_depth_images(h.handle))
}

func (h *HeifImageHandle) GetListOfDepthImageIDs() []int {
        var num = int(C.heif_image_handle_get_number_of_depth_images(h.handle))
        var origIDs = make([]C.heif_item_id, num)

        C.heif_image_handle_get_list_of_depth_image_IDs(h.handle, &origIDs[0], C.int(num))

        var IDs = make([]int, num)
        for i:=0; i<num; i++ {
          IDs[i] = int(origIDs[i])
        }
        return IDs
}

func (h *HeifImageHandle) GetDepthImageHandle(depth_image_id int) (*HeifImageHandle, HeifError) {
        var handle HeifImageHandle
        var error = C.heif_image_handle_get_depth_image_handle(h.handle, C.heif_item_id(depth_image_id), &handle.handle)
        runtime.SetFinalizer(&handle, free_heif_image_handle)
        return &handle, NewHeifError(error)
}


func (h *HeifImageHandle) GetNumberOfThumbnails() int {
        return int(C.heif_image_handle_get_number_of_thumbnails(h.handle))
}

func (h *HeifImageHandle) GetListOfThumbnailIDs() []int {
        var num = int(C.heif_image_handle_get_number_of_thumbnails(h.handle))
        var origIDs = make([]C.heif_item_id, num)

        C.heif_image_handle_get_list_of_thumbnail_IDs(h.handle, &origIDs[0], C.int(num))

        var IDs = make([]int, num)
        for i:=0; i<num; i++ {
          IDs[i] = int(origIDs[i])
        }
        return IDs
}

func (h *HeifImageHandle) GetThumbnail(thumbnail_id int) (*HeifImageHandle, HeifError) {
        var handle HeifImageHandle
        var error = C.heif_image_handle_get_thumbnail(h.handle, C.heif_item_id(thumbnail_id), &handle.handle)
        runtime.SetFinalizer(&handle, free_heif_image_handle)
        return &handle, NewHeifError(error)
}


// TODO: EXIF metadata


// --- HeifImage

type HeifDecodingOptions struct {
        options *C.struct_heif_decoding_options
}

func NewHeifDecodingOptions() *HeifDecodingOptions {
        var options HeifDecodingOptions;
        options.options = C.heif_decoding_options_alloc();

        runtime.SetFinalizer(&options, free_heif_decoding_options)
        return &options
}

func free_heif_decoding_options(options *HeifDecodingOptions) {
        C.heif_decoding_options_free(options.options)
        options.options = nil;
        runtime.SetFinalizer(options, nil)
}

type HeifImage struct {
        image *C.struct_heif_image
}

func free_heif_image(image *HeifImage) {
        C.heif_image_release(image.image)
        image.image = nil;
        runtime.SetFinalizer(image, nil)
}

func (h *HeifImageHandle) DecodeImage(colorspace C.enum_heif_colorspace, chroma C.enum_heif_chroma, options *HeifDecodingOptions) (*HeifImage, HeifError) {
        var image HeifImage;

        var opt *C.struct_heif_decoding_options;
        if (options != nil) {
                opt = options.options;
        }

        var err = C.heif_decode_image(h.handle, &image.image, colorspace, chroma, opt)
        runtime.SetFinalizer(&image, free_heif_image)
        return &image, NewHeifError(err)
}


func (img *HeifImage) GetColorspace() C.enum_heif_colorspace {
        return C.heif_image_get_colorspace(img.image);
}

func (img *HeifImage) GetChromaFormat() C.enum_heif_chroma {
        return C.heif_image_get_chroma_format(img.image);
}

func (img *HeifImage) GetWidth(channel C.enum_heif_channel) int {
        return int(C.heif_image_get_width(img.image, channel))
}

func (img *HeifImage) GetHeight(channel C.enum_heif_channel) int {
        return int(C.heif_image_get_height(img.image, channel))
}

func (img *HeifImage) GetBitsPerPixel(channel C.enum_heif_channel) int {
        return int(C.heif_image_get_bits_per_pixel(img.image, channel))
}

type HeifImageAccess struct {
        Plane unsafe.Pointer
        Stride int

        image *HeifImage // need this reference to make sure the image is not GC'ed while we access it
}

func (img* HeifImage) GetPlane(channel C.enum_heif_channel) HeifImageAccess {
        var access HeifImageAccess

        var stride C.int;
        var plane = C.heif_image_get_plane(img.image, channel, &stride)

        access.Plane = unsafe.Pointer(plane)
        access.Stride = int(stride)
        access.image = img
        return access
}

func (img* HeifImage) ScaleImage(width int, height int) (*HeifImage, HeifError) {
        var scaled_image HeifImage;
        var err = C.heif_image_scale_image(img.image, &scaled_image.image, C.int(width), C.int(height), nil)
        runtime.SetFinalizer(&scaled_image, free_heif_image)
        return &scaled_image, NewHeifError(err)
}
