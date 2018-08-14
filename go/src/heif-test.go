/*
 * Simple GO interface test program
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
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

package main

/*
#cgo pkg-config: libheif
#include <stdlib.h>
#include <libheif/heif.h>
*/
import "C"

import (
	"fmt"
	"os"
	"runtime"
	"time"
)

import . "heif"

// ==================================================
//                      TEST
// ==================================================

func test_heif(filename string) {
	var c *HeifContext = NewHeifContext()

	var err = c.ReadFromFile(filename)
	fmt.Printf("%s\n", err.Message)
	if err.Code != 0 {
		return
	}

	var nImages = c.GetNumberofTopLevelImages()
	fmt.Printf("GetNumberofTopLevelImages: %v\n", nImages)

	var IDs = c.GetListOfTopLevelImageIDs()
	fmt.Printf("List of top level image IDs %s\n", IDs)

	var pID int
	pID, err = c.GetPrimaryImageID()

	fmt.Printf("Primary image: %v\n", pID)

	var handle *HeifImageHandle
	handle, _ = c.GetPrimaryImageHandle()

	fmt.Printf("image size: %v %v\n", handle.GetWidth(), handle.GetHeight())

	var image, _ = handle.DecodeImage(C.heif_colorspace_RGB,
		C.heif_chroma_444,
		nil)

	var access = image.GetPlane(C.heif_channel_R)

	fmt.Printf("stride: %v\n", access.Stride)
}

func main() {
	fmt.Printf("libheif version: %v\n", HeifGetVersion())

	test_heif(os.Args[1])

	runtime.GC()

	time.Sleep(time.Second)
	fmt.Println("done.")
}
