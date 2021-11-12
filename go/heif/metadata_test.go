package heif

import (
	"fmt"
	"path"
	"testing"
)

func TestExifCount(t *testing.T) {
	ctx, err := NewContext()
	if err != nil {
		t.Fatalf("Can't create context: %s", err)
	}

	filename := path.Join("..", "..", "examples", "mont.heic")
	if err := ctx.ReadFromFile(filename); err != nil {
		t.Fatalf("Can't read from %s: %s", filename, err)
	}

	if count := ctx.GetNumberOfTopLevelImages(); count != 16 {
		t.Errorf("Expected %d top level images, got %d", 16, count)
	}
	if ids := ctx.GetListOfTopLevelImageIDs(); len(ids) != 16 {
		t.Errorf("Expected %d top level image ids, got %+v", 16, ids)
	}
	if _, err := ctx.GetPrimaryImageID(); err != nil {
		t.Errorf("Expected a primary image, got %s", err)
	}
	handle, err := ctx.GetPrimaryImageHandle()
	if err != nil {
		t.Errorf("Could not get primary image handle: %s", err)
	}
	if !handle.IsPrimaryImage() {
		t.Error("Expected primary image")
	}

	exifCount := handle.ExifCount()
	fmt.Println("Exif count", exifCount)

	exifIDs := handle.ExifIDs()
	fmt.Println("Exif IDs", exifIDs)

	metadataCount := handle.MetadataCount()
	fmt.Println("Metadata count", metadataCount)

	metadataIDs := handle.MetadataIDs()
	fmt.Println("Metadata IDs", metadataIDs)

	metadataID := metadataIDs[0]

	times, err := handle.ImageTimes(metadataID)
	if err != nil {
		t.Fail()
	}
	fmt.Println(times)

	thumbnail := false

	handle.GetWidth()
	handle.GetHeight()
	handle.HasAlphaChannel()
	handle.HasDepthImage()
	count := handle.GetNumberOfDepthImages()
	if ids := handle.GetListOfDepthImageIDs(); len(ids) != count {
		t.Errorf("Expected %d depth image ids, got %d", count, len(ids))
	}
	if !thumbnail {
		count = handle.GetNumberOfThumbnails()
		ids := handle.GetListOfThumbnailIDs()
		if len(ids) != count {
			t.Errorf("Expected %d thumbnail image ids, got %d", count, len(ids))
		}
		for _, id := range ids {
			if thumb, err := handle.GetThumbnail(id); err != nil {
				t.Errorf("Could not get thumbnail %d: %s", id, err)
			} else {
				CheckHeifImage(t, thumb, true)
			}
		}
	}

	if img, err := handle.DecodeImage(ColorspaceUndefined, ChromaUndefined, nil); err != nil {
		t.Errorf("Could not decode image: %s", err)
	} else {
		img.GetColorspace()
		img.GetChromaFormat()
	}

	decodeTests := []decodeTest{
		decodeTest{ColorspaceYCbCr, Chroma420},
		decodeTest{ColorspaceYCbCr, Chroma422},
		decodeTest{ColorspaceYCbCr, Chroma444},
		decodeTest{ColorspaceRGB, Chroma444},
		decodeTest{ColorspaceRGB, ChromaInterleavedRGB},
		decodeTest{ColorspaceRGB, ChromaInterleavedRGBA},
		decodeTest{ColorspaceRGB, ChromaInterleavedRRGGBB_BE},
		decodeTest{ColorspaceRGB, ChromaInterleavedRRGGBBAA_BE},
	}
	for _, test := range decodeTests {
		if img, err := handle.DecodeImage(test.colorspace, test.chroma, nil); err != nil {
			t.Errorf("Could not decode image with %v / %v: %s", test.colorspace, test.chroma, err)
		} else {
			img.GetColorspace()
			img.GetChromaFormat()

			if _, err := img.GetImage(); err != nil {
				t.Errorf("Could not get image with %v /%v: %s", test.colorspace, test.chroma, err)
				continue
			}
		}
	}

}
