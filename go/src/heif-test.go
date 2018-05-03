package main


/*
#cgo CPPFLAGS: -I/home/domain/farindk/sys/include/libheif
#cgo pkg-config: libheif
#include <stdlib.h>
#include <heif.h>
*/
import "C"

import (
        "fmt"
        "runtime"
        "time"
        "os"
)

import . "heif"


// ==================================================
//                      TEST
// ==================================================

func test_heif(filename string) {
    var c *HeifContext = NewHeifContext()

    var err = c.ReadFromFile(filename)
    fmt.Printf("%s\n", err.Message)
    if (err.Code != 0) {
        return
    }

    var nImages = c.GetNumberofTopLevelImages()
    fmt.Printf("GetNumberofTopLevelImages: %v\n", nImages)

    var IDs = c.GetListOfTopLevelImageIDs()
    fmt.Printf("List of top level image IDs %s\n", IDs);

    var pID int;
    pID, err = c.GetPrimaryImageID()

    fmt.Printf("Primary image: %v\n", pID);

    var handle *HeifImageHandle
    handle, _ = c.GetPrimaryImageHandle();

    fmt.Printf("image size: %v %v\n", handle.GetWidth(), handle.GetHeight())

    var image, _ = handle.DecodeImage(C.heif_colorspace_RGB,
                                      C.heif_chroma_444,
                                      nil)

    var access = image.GetPlane(C.heif_channel_R)

    fmt.Printf("stride: %v\n",access.Stride)
}


func main() {
    fmt.Printf("libheif version: %v\n", HeifGetVersion())

    test_heif(os.Args[1])

    runtime.GC()

    time.Sleep(time.Second)
    fmt.Println("done.")
}
