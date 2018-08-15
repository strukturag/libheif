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

import (
	"fmt"
	"os"
	"runtime"
	"time"

	"github.com/strukturag/libheif/go/heif"
)

// ==================================================
//                      TEST
// ==================================================

func test_heif(filename string) {
	c, err := heif.NewContext()
	if err != nil {
		fmt.Printf("Could not create context: %s\n", err)
		return
	}

	if err := c.ReadFromFile(filename); err != nil {
		fmt.Printf("Could not read file %s: %s\n", filename, err)
		return
	}

	var nImages = c.GetNumberofTopLevelImages()
	fmt.Printf("GetNumberofTopLevelImages: %v\n", nImages)

	var IDs = c.GetListOfTopLevelImageIDs()
	fmt.Printf("List of top level image IDs %s\n", IDs)

	if pID, err := c.GetPrimaryImageID(); err != nil {
		fmt.Printf("Could not get primary image id: %s\n", err)
	} else {
		fmt.Printf("Primary image: %v\n", pID)
	}

	handle, err := c.GetPrimaryImageHandle()
	if err != nil {
		fmt.Printf("Could not get primary image: %s\n", err)
		return
	}

	fmt.Printf("image size: %v %v\n", handle.GetWidth(), handle.GetHeight())

	if image, err := handle.DecodeImage(heif.ColorspaceRGB,
		heif.Chroma444,
		nil); err != nil {
		fmt.Printf("Could not decode image: %s\n", err)
	} else if access, err := image.GetPlane(heif.ChannelR); err != nil {
		fmt.Printf("Could not get image plane: %s\n", err)
	} else {
		fmt.Printf("stride: %v\n", access.Stride)
	}
}

func main() {
	fmt.Printf("libheif version: %v\n", heif.GetVersion())
	if len(os.Args) < 2 {
		fmt.Printf("USAGE: %s <filename>\n", os.Args[0])
		return
	}

	test_heif(os.Args[1])

	runtime.GC()

	time.Sleep(time.Second)
	fmt.Println("done.")
}
