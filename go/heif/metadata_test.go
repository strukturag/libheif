package heif

import (
	"fmt"
	"os"
	"path"
	"testing"
)

// exists checks if the given path exists
func exists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

func TestExifCount(t *testing.T) {
	ctx, err := NewContext()
	if err != nil {
		t.Fatalf("Can't create context: %s", err)
	}

	imageFilename := path.Join("..", "..", "examples", "mont.heic")

	if !exists(imageFilename) {
		// skip the test, since the image is not available in the repositry, for copyright reasons
		// the test image can be downloaded from:
		// https://dynamicwallpaper.club/wallpaper/la4wfuwtkg
		return
	}

	if err := ctx.ReadFromFile(imageFilename); err != nil {
		t.Fatalf("Can't read from %s: %s", imageFilename, err)
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
}
