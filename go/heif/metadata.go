package heif

/*
 * GO interface to libheif
 * Copyright (c) 2021 Alexander F. Rødseth <xyproto@archlinux.org>
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

/*
#cgo pkg-config: libheif
#include <libheif/heif.h>
#include <stdlib.h>
#include <string.h>
*/
import "C"

import (
	"bytes"
	"encoding/base64"
	"errors"
	"math"
	"time"
	"unsafe"

	"github.com/antchfx/xmlquery"
	"github.com/antchfx/xpath"
	"howett.net/plist"
)

type (
	// MetadataID contains an uint that represents the ID of a block of metadata
	MetadataID uint

	// TimeTable maps from image index to a time.Time that contains the hour and minute for a dynamic wallpaper image
	TimeTable []time.Time
)

var (
	errH24 = errors.New("unusual apple_desktop:h24 metadata")
)

func (h *ImageHandle) MetadataCount() int {
	n := int(C.heif_image_handle_get_number_of_metadata_blocks(h.handle, nil))
	keepAlive(h)
	return n
}

func (h *ImageHandle) MetadataIDs() []MetadataID {
	nMeta := h.MetadataCount()
	if nMeta == 0 {
		return []MetadataID{}
	}
	meta := make([]C.uint, nMeta)

	C.heif_image_handle_get_list_of_metadata_block_IDs(h.handle, nil, &meta[0], C.int(nMeta))
	keepAlive(h)
	metaDataIDs := make([]MetadataID, nMeta)
	for i := 0; i < nMeta; i++ {
		metaDataIDs[i] = MetadataID(meta[i])
	}
	return metaDataIDs
}

func (h *ImageHandle) Metadata(mID MetadataID) []byte {
	nMeta := h.MetadataCount()
	if nMeta == 0 {
		return []byte{}
	}

	nData := C.heif_image_handle_get_metadata_size(h.handle, C.uint(mID))
	keepAlive(h)

	data := C.malloc(C.sizeof_char * nData)
	defer C.free(unsafe.Pointer(data))

	C.heif_image_handle_get_metadata(h.handle, C.uint(mID), data)
	keepAlive(h)

	return C.GoBytes(data, C.int(nData))
}

func (h *ImageHandle) ExifCount() int {
	filter := C.CString("Exif")
	defer C.free(unsafe.Pointer(filter))
	n := int(C.heif_image_handle_get_number_of_metadata_blocks(h.handle, filter))
	keepAlive(h)
	return n
}

func (h *ImageHandle) ExifIDs() []MetadataID {
	nMeta := h.ExifCount()
	if nMeta == 0 {
		return []MetadataID{}
	}
	filter := C.CString("Exif")
	defer C.free(unsafe.Pointer(filter))
	meta := make([]C.uint, nMeta)
	C.heif_image_handle_get_list_of_metadata_block_IDs(h.handle, filter, &meta[0], C.int(nMeta))
	keepAlive(h)
	metaDataIDs := make([]MetadataID, nMeta)
	for i := 0; i < nMeta; i++ {
		metaDataIDs[i] = MetadataID(meta[i])
	}
	return metaDataIDs
}

// MetadataMap takes a metadata ID and an XPath expression
// Within the metadata there may be XML, within the XML there may be a Base64 encoded string,
// and within the Base64 encoded string there may be a propery list in the Apple plist format
// and within that plist there may be a timestamp or solar position,
// encoded in the form of a property list. Oh joy.
func (h *ImageHandle) MetadataMap(mID MetadataID, xs string) (map[string]interface{}, error) {
	xmlData := bytes.ReplaceAll(h.Metadata(mID), []byte{0}, []byte{})
	expr, err := xpath.Compile(xs)
	if err != nil {
		return nil, err
	}
	doc, err := xmlquery.Parse(bytes.NewReader(xmlData))
	if err != nil {
		return nil, err
	}
	base64string, ok := expr.Evaluate(xmlquery.CreateXPathNavigator(doc)).(string)
	if !ok {
		return nil, errors.New("could not find string at " + xs)
	}
	b64decoded, err := base64.StdEncoding.DecodeString(base64string)
	if err != nil {
		return nil, err
	}
	decodedMap := make(map[string]interface{})
	_, err = plist.Unmarshal(b64decoded, &decodedMap)
	if err != nil {
		return nil, err
	}
	return decodedMap, nil
}

func (h *ImageHandle) AppleTimesMap(mID MetadataID) (map[string]interface{}, error) {
	return h.MetadataMap(mID, "string(//x:xmpmeta/rdf:RDF/rdf:Description/@apple_desktop:h24)")
}

func (h *ImageHandle) AppleSolarMap(mID MetadataID) (map[string]interface{}, error) {
	return h.MetadataMap(mID, "string(//x:xmpmeta/rdf:RDF/rdf:Description/@apple_desktop:solar)")
}

// ImageTimes returns the image times for a dynamic wallpaper.
func (h *ImageHandle) ImageTimes(mID MetadataID) (TimeTable, error) {
	m, err := h.AppleTimesMap(mID)
	if err != nil {
		return nil, err
	}

	// TODO: m["ap"] is ignored for now. It should have an "l" and "d" key but it's unclear what those might be.
	// Perhaps longitude and latitude?

	tiMapList, found := m["ti"]
	if !found {
		return nil, err
	}
	tii, ok := tiMapList.([]interface{})
	if !ok {
		return nil, errH24
	}
	imageTimes := make(TimeTable, len(tii))
	for _, tiMap := range tii {
		tiMapMap, ok := tiMap.(map[string]interface{})
		if !ok {
			return nil, errH24
		}
		iDuck, ok := tiMapMap["i"]
		if !ok {
			return nil, errH24
		}
		tDuck, ok := tiMapMap["t"]
		if !ok {
			return nil, errH24
		}
		iValue, ok := iDuck.(uint64)
		if !ok {
			return nil, errH24
		}
		tValue, ok := tDuck.(float64)
		if !ok {
			return nil, errH24
		}
		imageIndex := iValue
		imageHourFloat := tValue * 24.0
		imageHourInt := int(math.Floor(imageHourFloat))
		imageMinuteFloat := (imageHourFloat - float64(imageHourInt)) * 60.0
		imageMinuteInt := int(math.Floor(imageMinuteFloat))

		// Generate a time.Time that contains the correct hour and minute
		now := time.Now()
		timeWithHourAndMinute := time.Date(now.Year(), now.Month(), now.Day(), imageHourInt, imageMinuteInt, 0, 0, now.Location())

		// For the found imageIndex, set the time.Time
		imageTimes[imageIndex] = timeWithHourAndMinute
	}
	return imageTimes, nil
}

// TODO: Also create a function that pulls out contents from the results of AppleSolarMap
