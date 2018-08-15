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
#cgo pkg-config: libheif
#include <stdlib.h>
#include <libheif/heif.h>
*/
import "C"

import (
	"fmt"
	"image"
	"image/color"
	"io"
	"io/ioutil"
	"runtime"
	"unsafe"
)

func GetVersion() string {
	return C.GoString(C.heif_get_version())
}

type Compression int

const (
	CompressionUndefined = C.heif_compression_undefined
	CompressionHEVC      = C.heif_compression_HEVC
	CompressionAVC       = C.heif_compression_AVC
	CompressionJPEG      = C.heif_compression_JPEG
)

type Chroma int

const (
	ChromaUndefined       = C.heif_chroma_undefined
	ChromaMonochrome      = C.heif_chroma_monochrome
	Chroma420             = C.heif_chroma_420
	Chroma422             = C.heif_chroma_422
	Chroma444             = C.heif_chroma_444
	ChromaInterleavedRGB  = C.heif_chroma_interleaved_RGB
	ChromaInterleavedRGBA = C.heif_chroma_interleaved_RGBA
)

type Colorspace int

const (
	ColorspaceUndefined  = C.heif_colorspace_undefined
	ColorspaceYCbCr      = C.heif_colorspace_YCbCr
	ColorspaceRGB        = C.heif_colorspace_RGB
	ColorspaceMonochrome = C.heif_colorspace_monochrome
)

type Channel int

const (
	ChannelY           = C.heif_channel_Y
	ChannelCb          = C.heif_channel_Cb
	ChannelCr          = C.heif_channel_Cr
	ChannelR           = C.heif_channel_R
	ChannelG           = C.heif_channel_G
	ChannelB           = C.heif_channel_B
	ChannelAlpha       = C.heif_channel_Alpha
	ChannelInterleaved = C.heif_channel_interleaved
)

type ProgressStep int

const (
	ProgressStepTotal    = C.heif_progress_step_total
	ProgressStepLoadTile = C.heif_progress_step_load_tile
)

// --- HeifError

type HeifError struct {
	Code    int
	Subcode int
	Message string
}

func (e *HeifError) Error() string {
	return e.Message
}

func convertHeifError(cerror C.struct_heif_error) error {
	if cerror.code == C.heif_error_Ok {
		return nil
	}

	return &HeifError{
		Code:    int(cerror.code),
		Subcode: int(cerror.subcode),
		Message: C.GoString(cerror.message),
	}
}

func convertItemIds(ids []C.heif_item_id, count int) []int {
	result := make([]int, count)
	for i := 0; i < count; i++ {
		result[i] = int(ids[i])
	}
	return result
}

// --- Context

type Context struct {
	context *C.struct_heif_context
}

func NewContext() (*Context, error) {
	ctx := &Context{
		context: C.heif_context_alloc(),
	}
	if ctx.context == nil {
		return nil, fmt.Errorf("Could not allocate context")
	}

	runtime.SetFinalizer(ctx, free_heif_context)
	return ctx, nil
}

func free_heif_context(c *Context) {
	C.heif_context_free(c.context)
	c.context = nil
	runtime.SetFinalizer(c, nil)
}

func (c *Context) ReadFromFile(filename string) error {
	c_filename := C.CString(filename)
	defer C.free(unsafe.Pointer(c_filename))

	err := C.heif_context_read_from_file(c.context, c_filename, nil)
	return convertHeifError(err)
}

func (c *Context) ReadFromMemory(data []byte) error {
	// TODO: Use reader API internally.
	err := C.heif_context_read_from_memory(c.context, unsafe.Pointer(&data[0]), C.size_t(len(data)), nil)
	return convertHeifError(err)
}

func (c *Context) GetNumberofTopLevelImages() int {
	return int(C.heif_context_get_number_of_top_level_images(c.context))
}

func (c *Context) IsTopLevelImageID(ID int) bool {
	return C.heif_context_is_top_level_image_ID(c.context, C.heif_item_id(ID)) != 0
}

func (c *Context) GetListOfTopLevelImageIDs() []int {
	nToplevel := int(C.heif_context_get_number_of_top_level_images(c.context))
	if nToplevel == 0 {
		return []int{}
	}

	origIDs := make([]C.heif_item_id, nToplevel)
	C.heif_context_get_list_of_top_level_image_IDs(c.context, &origIDs[0], C.int(nToplevel))
	return convertItemIds(origIDs, nToplevel)
}

func (c *Context) GetPrimaryImageID() (int, error) {
	var c_id C.heif_item_id
	err := C.heif_context_get_primary_image_ID(c.context, &c_id)
	if err := convertHeifError(err); err != nil {
		return 0, err
	}

	return int(c_id), nil
}

// --- ImageHandle

type ImageHandle struct {
	handle *C.struct_heif_image_handle
}

func free_heif_image_handle(c *ImageHandle) {
	C.heif_image_handle_release(c.handle)
	c.handle = nil
	runtime.SetFinalizer(c, nil)
}

func (c *Context) GetPrimaryImageHandle() (*ImageHandle, error) {
	var handle ImageHandle
	err := C.heif_context_get_primary_image_handle(c.context, &handle.handle)
	if err := convertHeifError(err); err != nil {
		return nil, err
	}
	runtime.SetFinalizer(&handle, free_heif_image_handle)
	return &handle, convertHeifError(err)
}

func (c *Context) GetImageHandle(id int) (*ImageHandle, error) {
	var handle ImageHandle
	var err = C.heif_context_get_image_handle(c.context, C.heif_item_id(id), &handle.handle)
	if err := convertHeifError(err); err != nil {
		return nil, err
	}
	runtime.SetFinalizer(&handle, free_heif_image_handle)
	return &handle, nil
}

func (h *ImageHandle) IsPrimaryImage() bool {
	return C.heif_image_handle_is_primary_image(h.handle) != 0
}

func (h *ImageHandle) GetWidth() int {
	return int(C.heif_image_handle_get_width(h.handle))
}

func (h *ImageHandle) GetHeight() int {
	return int(C.heif_image_handle_get_height(h.handle))
}

func (h *ImageHandle) HasAlphaChannel() bool {
	return C.heif_image_handle_has_alpha_channel(h.handle) != 0
}

func (h *ImageHandle) HasDepthImage() bool {
	return C.heif_image_handle_has_depth_image(h.handle) != 0
}

func (h *ImageHandle) GetNumberOfDepthImages() int {
	return int(C.heif_image_handle_get_number_of_depth_images(h.handle))
}

func (h *ImageHandle) GetListOfDepthImageIDs() []int {
	num := int(C.heif_image_handle_get_number_of_depth_images(h.handle))
	if num == 0 {
		return []int{}
	}

	origIDs := make([]C.heif_item_id, num)
	C.heif_image_handle_get_list_of_depth_image_IDs(h.handle, &origIDs[0], C.int(num))
	return convertItemIds(origIDs, num)
}

func (h *ImageHandle) GetDepthImageHandle(depth_image_id int) (*ImageHandle, error) {
	var handle ImageHandle
	err := C.heif_image_handle_get_depth_image_handle(h.handle, C.heif_item_id(depth_image_id), &handle.handle)
	if err := convertHeifError(err); err != nil {
		return nil, err
	}

	runtime.SetFinalizer(&handle, free_heif_image_handle)
	return &handle, nil
}

func (h *ImageHandle) GetNumberOfThumbnails() int {
	return int(C.heif_image_handle_get_number_of_thumbnails(h.handle))
}

func (h *ImageHandle) GetListOfThumbnailIDs() []int {
	num := int(C.heif_image_handle_get_number_of_thumbnails(h.handle))
	if num == 0 {
		return []int{}
	}

	origIDs := make([]C.heif_item_id, num)
	C.heif_image_handle_get_list_of_thumbnail_IDs(h.handle, &origIDs[0], C.int(num))
	return convertItemIds(origIDs, num)
}

func (h *ImageHandle) GetThumbnail(thumbnail_id int) (*ImageHandle, error) {
	var handle ImageHandle
	var error = C.heif_image_handle_get_thumbnail(h.handle, C.heif_item_id(thumbnail_id), &handle.handle)
	runtime.SetFinalizer(&handle, free_heif_image_handle)
	return &handle, convertHeifError(error)
}

// TODO: EXIF metadata

// --- Image

type DecodingOptions struct {
	options *C.struct_heif_decoding_options
}

func NewDecodingOptions() (*DecodingOptions, error) {
	options := &DecodingOptions{
		options: C.heif_decoding_options_alloc(),
	}
	if options.options == nil {
		return nil, fmt.Errorf("Could not allocate decoding options")
	}

	runtime.SetFinalizer(options, free_heif_decoding_options)
	return options, nil
}

func free_heif_decoding_options(options *DecodingOptions) {
	C.heif_decoding_options_free(options.options)
	options.options = nil
	runtime.SetFinalizer(options, nil)
}

type Image struct {
	image *C.struct_heif_image
}

func free_heif_image(image *Image) {
	C.heif_image_release(image.image)
	image.image = nil
	runtime.SetFinalizer(image, nil)
}

func (h *ImageHandle) DecodeImage(colorspace Colorspace, chroma Chroma, options *DecodingOptions) (*Image, error) {
	var image Image

	var opt *C.struct_heif_decoding_options
	if options != nil {
		opt = options.options
	}

	err := C.heif_decode_image(h.handle, &image.image, uint32(colorspace), uint32(chroma), opt)
	if err := convertHeifError(err); err != nil {
		return nil, err
	}

	runtime.SetFinalizer(&image, free_heif_image)
	return &image, nil
}

func (img *Image) GetColorspace() Colorspace {
	return Colorspace(C.heif_image_get_colorspace(img.image))
}

func (img *Image) GetChromaFormat() Chroma {
	return Chroma(C.heif_image_get_chroma_format(img.image))
}

func (img *Image) GetWidth(channel Channel) int {
	return int(C.heif_image_get_width(img.image, uint32(channel)))
}

func (img *Image) GetHeight(channel Channel) int {
	return int(C.heif_image_get_height(img.image, uint32(channel)))
}

func (img *Image) GetBitsPerPixel(channel Channel) int {
	return int(C.heif_image_get_bits_per_pixel(img.image, uint32(channel)))
}

func (img *Image) GetImage() (image.Image, error) {
	var i image.Image
	switch img.GetColorspace() {
	case ColorspaceYCbCr:
		var subsample image.YCbCrSubsampleRatio
		switch img.GetChromaFormat() {
		case Chroma420:
			subsample = image.YCbCrSubsampleRatio420
		case Chroma422:
			subsample = image.YCbCrSubsampleRatio422
		case Chroma444:
			subsample = image.YCbCrSubsampleRatio444
		default:
			return nil, fmt.Errorf("Unsupported YCbCr chrome format: %v", img.GetChromaFormat())
		}
		y, err := img.GetPlane(ChannelY)
		if err != nil {
			return nil, err
		}
		cb, err := img.GetPlane(ChannelCb)
		if err != nil {
			return nil, err
		}
		cr, err := img.GetPlane(ChannelCr)
		if err != nil {
			return nil, err
		}
		i = &image.YCbCr{
			Y:              y.Plane,
			Cb:             cb.Plane,
			Cr:             cr.Plane,
			YStride:        y.Stride,
			CStride:        cb.Stride,
			SubsampleRatio: subsample,
			Rect: image.Rectangle{
				Min: image.Point{
					X: 0,
					Y: 0,
				},
				Max: image.Point{
					X: img.GetWidth(ChannelY),
					Y: img.GetHeight(ChannelY),
				},
			},
		}
	case ColorspaceRGB:
		switch img.GetChromaFormat() {
		case ChromaInterleavedRGBA:
			rgba, err := img.GetPlane(ChannelInterleaved)
			if err != nil {
				return nil, err
			}
			i = &image.RGBA{
				Pix:    rgba.Plane,
				Stride: rgba.Stride,
				Rect: image.Rectangle{
					Min: image.Point{
						X: 0,
						Y: 0,
					},
					Max: image.Point{
						X: img.GetWidth(ChannelInterleaved),
						Y: img.GetHeight(ChannelInterleaved),
					},
				},
			}
		default:
			return nil, fmt.Errorf("Unsupported RGB chroma format: %v", img.GetChromaFormat())
		}
	default:
		return nil, fmt.Errorf("Unsupported colorspace: %v", img.GetColorspace())
	}

	return i, nil
}

type ImageAccess struct {
	Plane  []byte
	Stride int

	image *Image // need this reference to make sure the image is not GC'ed while we access it
}

func (img *Image) GetPlane(channel Channel) (*ImageAccess, error) {
	height := C.heif_image_get_height(img.image, uint32(channel))
	if height == -1 {
		return nil, fmt.Errorf("No such channel")
	}

	var stride C.int
	plane := C.heif_image_get_plane(img.image, uint32(channel), &stride)
	if plane == nil {
		return nil, fmt.Errorf("No such channel")
	}

	access := &ImageAccess{
		Plane:  C.GoBytes(unsafe.Pointer(plane), stride*height),
		Stride: int(stride),
		image:  img,
	}
	return access, nil
}

func (img *Image) ScaleImage(width int, height int) (*Image, error) {
	var scaled_image Image
	err := C.heif_image_scale_image(img.image, &scaled_image.image, C.int(width), C.int(height), nil)
	if err := convertHeifError(err); err != nil {
		return nil, err
	}

	runtime.SetFinalizer(&scaled_image, free_heif_image)
	return &scaled_image, nil
}

// --- High-level decoding API, always decodes primary image (if present).

func decodePrimaryImageFromReader(r io.Reader) (*ImageHandle, error) {
	ctx, err := NewContext()
	if err != nil {
		return nil, err
	}

	data, err := ioutil.ReadAll(r)
	if err != nil {
		return nil, err
	}

	if err := ctx.ReadFromMemory(data); err != nil {
		return nil, err
	}

	handle, err := ctx.GetPrimaryImageHandle()
	if err != nil {
		return nil, err
	}

	return handle, nil
}

func decodeImage(r io.Reader) (image.Image, error) {
	handle, err := decodePrimaryImageFromReader(r)
	if err != nil {
		return nil, err
	}

	img, err := handle.DecodeImage(ColorspaceUndefined, ChromaUndefined, nil)
	if err != nil {
		return nil, err
	}

	return img.GetImage()
}

func decodeConfig(r io.Reader) (image.Config, error) {
	var config image.Config
	handle, err := decodePrimaryImageFromReader(r)
	if err != nil {
		return config, err
	}

	config = image.Config{
		ColorModel: color.YCbCrModel,
		Width:      handle.GetWidth(),
		Height:     handle.GetHeight(),
	}
	return config, nil
}

func init() {
	// Assume .heic images always start with "\x00\x00\x00\x1cftyp".
	image.RegisterFormat("heif", "\x00\x00\x00\x1c\x66\x74\x79\x70", decodeImage, decodeConfig)
}
