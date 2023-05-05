# libheif

[![Build Status](https://github.com/strukturag/libheif/workflows/build/badge.svg)](https://github.com/strukturag/libheif/actions) [![Build Status](https://ci.appveyor.com/api/projects/status/github/strukturag/libheif?svg=true)](https://ci.appveyor.com/project/strukturag/libheif) [![Coverity Scan Build Status](https://scan.coverity.com/projects/16641/badge.svg)](https://scan.coverity.com/projects/strukturag-libheif)

libheif is an ISO/IEC 23008-12:2017 HEIF and AVIF (AV1 Image File Format) file format decoder and encoder.
There is partial support for ISO/IEC 23008-12:2022 (2nd Edition) capabilities.

HEIF and AVIF are new image file formats employing HEVC (H.265) or AV1 image coding, respectively, for the
best compression ratios currently possible.

libheif makes use of [libde265](https://github.com/strukturag/libde265) for HEIF image decoding and x265 for encoding.
For AVIF, libaom, dav1d, svt-av1, or rav1e are used as codecs.

## Supported features

libheif has support for decoding:

* tiled images
* alpha channels
* thumbnails
* reading EXIF and XMP metadata
* reading the depth channel
* multiple images in a file
* image transformations (crop, mirror, rotate)
* overlay images
* plugin interface to add alternative codecs for additional formats (AVC, JPEG)
* decoding of files while downloading (e.g. extract image size before file has been completely downloaded)
* reading color profiles
* heix images (10 and 12 bit, chroma 4:2:2)

The encoder supports:

* lossy compression with adjustable quality
* lossless compression
* alpha channels
* thumbnails
* save multiple images to a file
* save EXIF and XMP metadata
* writing color profiles
* 10 and 12 bit images
* monochrome images

## API

The library has a C API for easy integration and wide language support.
Note that the API is still work in progress and may still change.

The decoder automatically supports both HEIF and AVIF through the same API. No changes are required to existing code to support AVIF.
The encoder can be switched between HEIF and AVIF simply by setting `heif_compression_HEVC` or `heif_compression_AV1`
to `heif_context_get_encoder_for_format()`.

Loading the primary image in an HEIF file is as easy as this:

```C
heif_context* ctx = heif_context_alloc();
heif_context_read_from_file(ctx, input_filename, nullptr);

// get a handle to the primary image
heif_image_handle* handle;
heif_context_get_primary_image_handle(ctx, &handle);

// decode the image and convert colorspace to RGB, saved as 24bit interleaved
heif_image* img;
heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);

int stride;
const uint8_t* data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);

// ... process data as needed ...

// clean up resources
heif_image_release(img);
heif_image_handle_release(handle);
heif_context_free(ctx);
```

Writing an HEIF file can be done like this:

```C
heif_context* ctx = heif_context_alloc();

// get the default encoder
heif_encoder* encoder;
heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);

// set the encoder parameters
heif_encoder_set_lossy_quality(encoder, 50);

// encode the image
heif_image* image; // code to fill in the image omitted in this example
heif_context_encode_image(ctx, image, encoder, nullptr, nullptr);

heif_encoder_release(encoder);

heif_context_write_to_file(ctx, "output.heic");

heif_context_free(ctx);
```

Get the EXIF data from an HEIF file:

```C
heif_item_id exif_id;

int n = heif_image_handle_get_list_of_metadata_block_IDs(image_handle, "Exif", &exif_id, 1);
if (n==1) {
  size_t exifSize = heif_image_handle_get_metadata_size(image_handle, exif_id);
  uint8_t* exifData = malloc(exifSize);
  struct heif_error error = heif_image_handle_get_metadata(image_handle, exif_id, exifData);
}
```

See the header file `heif.h` for the complete C API.

There is also a C++ API which is a header-only wrapper to the C API.
Hence, you can use the C++ API and still be binary compatible.
Code using the C++ API is much less verbose than using the C API directly.

There is also an experimental Go API, but this is not stable yet.

## Compiling

This library uses the CMake build system (the earlier autotools build files have been removed in v1.16.0).

Make sure that you compile and install [libde265](https://github.com/strukturag/libde265)
first, so that the configuration script will find this.
Also install x265 and its development files if you want to use HEIF encoding.

The basic build steps are as follows:

````sh
mkdir build
cd build
cmake --preset=release ..
make
````

There are CMake presets to cover the most frequent use cases.

* `release`: the preferred preset which compiles all codecs as separate plugins.
  If you do not want to distribute some of these plugins (e.g. HEIC), you can omit packaging these.
* `release-noplugins`: this is a smaller, self-contained build of libheif without using the plugin system.
  A single library is built with support for HEIC and AVIF.
* `testing`: for building and executing the unit tests. Also the internal library symbols are exposed. Do not use for distribution.
* `fuzzing`: similar to `testing`, this builds the fuzzers. The library should not distributed.

You can optionally adapt these standard configurations to your needs.
This can be done, for example, by calling `ccmake ..` from within the `build` directory.

### macOS

1. Install dependencies with Homebrew

    ```sh
    brew install cmake make pkg-config x265 libde265 libjpeg libtool
    ```

2. Configure and build project

    ```sh
    mkdir build
    cd build
    cmake --preset=release ..
    ./configure
    make
    ```

### Windows

You can build and install libheif using the [vcpkg](https://github.com/Microsoft/vcpkg/) dependency manager:

```sh
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.bat
./vcpkg integrate install
./vcpkg install libheif
```

The libheif port in vcpkg is kept up to date by Microsoft team members and community contributors. If the version is out of date, please [create an issue or pull request](https://github.com/Microsoft/vcpkg) on the vcpkg repository.

### Adding libaom encoder/decoder for AVIF

* Run the `aom.cmd` script in the `third-party` directory to download libaom and
  compile it.

When running `cmake` or `configure`, make sure that the environment variable
`PKG_CONFIG_PATH` includes the absolute path to `third-party/aom/dist/lib/pkgconfig`.

### Adding rav1e encoder for AVIF

* Install `cargo`.
* Install `cargo-c` by executing

```sh
cargo install --force cargo-c
```

* Run the `rav1e.cmd` script in the `third-party` directory to download rav1e
  and compile it.

When running `cmake`, make sure that the environment variable
`PKG_CONFIG_PATH` includes the absolute path to `third-party/rav1e/dist/lib/pkgconfig`.

### Adding dav1d decoder for AVIF

* Install [`meson`](https://mesonbuild.com/).
* Run the `dav1d.cmd` script in the `third-party` directory to download dav1d
  and compile it.

When running `cmake`, make sure that the environment variable
`PKG_CONFIG_PATH` includes the absolute path to `third-party/dav1d/dist/lib/x86_64-linux-gnu/pkgconfig`.

### Adding SVT-AV1 encoder for AVIF

You can either use the SVT-AV1 encoder libraries installed in the system or use a self-compiled current version.
If you want to compile SVT-AV1 yourself,

* Run the `svt.cmd` script in the `third-party` directory to download SVT-AV1
  and compile it.

When running `cmake` or `configure`, make sure that the environment variable
`PKG_CONFIG_PATH` includes the absolute path to `third-party/SVT-AV1/Build/linux/Release`.
You may have to replace `linux` in this path with your system's identifier.

You have to enable SVT-AV1 with CMake.

## Codec plugins

Starting with v1.14.0, each codec backend can be compiled statically into libheif or as a dynamically loaded plugin (currently Linux only).
You can choose this individually for each codec backend in the CMake settings.
Compiling a codec backend as dynamic plugin will generate a shared library that is installed in the system together with libheif.
The advantage is that only the required plugins have to be installed and libheif has fewer dependencies.

The plugins are loaded from the colon-separated (semicolon-separated on Windows) list of directories stored in the environment variable `LIBHEIF_PLUGIN_PATH`.
If this variable is empty, they are loaded from a directory specified in the CMake configuration.
You can also add plugin directories programmatically.

## Encoder benchmark

A current benchmark of the AVIF encoders (as of 14 Oct 2022) can be found on the Wiki page
[AVIF encoding benchmark](https://github.com/strukturag/libheif/wiki/AVIF-Encoder-Benchmark).

## Language bindings

* .NET Platform (C#, F#, and other languages): [libheif-sharp](https://github.com/0xC0000054/libheif-sharp)
* C++: part of libheif
* Go: part of libheif
* JavaScript: by compilation with emscripten (see below)
* NodeJS module: [libheif-js](https://www.npmjs.com/package/libheif-js)
* Python: [pyheif](https://pypi.org/project/pyheif/), [pillow_heif](https://pypi.org/project/pillow-heif/)
* Rust: [libheif-sys](https://github.com/Cykooz/libheif-sys)
* Swift: [libheif-Xcode](https://swiftpackageregistry.com/SDWebImage/libheif-Xcode)

Languages that can directly interface with C libraries (e.g., Swift, C#) should work out of the box.

## Compiling to JavaScript

libheif can also be compiled to JavaScript using
[emscripten](http://kripken.github.io/emscripten-site/).
See the `build-emscripten.sh` for further information.

## Online demo

Check out this [online demo](https://strukturag.github.io/libheif/).
This is `libheif` running in JavaScript in your browser.

## Example programs

Some example programs are provided in the `examples` directory.
The program `heif-convert` converts all images stored in an HEIF/AVIF file to JPEG or PNG.
`heif-enc` lets you convert JPEG files to HEIF/AVIF.
The program `heif-info` is a simple, minimal decoder that dumps the file structure to the console.

For example convert `example.heic` to JPEGs and one of the JPEGs back to HEIF:

```sh
cd examples/
./heif-convert example.heic example.jpeg
./heif-enc example-1.jpeg -o example.heif
```

In order to convert `example-1.jpeg` to AVIF use:

```sh
./heif-enc example-1.jpeg -A -o example.avif
```

There is also a GIMP plugin using libheif [here](https://github.com/strukturag/heif-gimp-plugin).

## HEIF/AVIF thumbnails for the Gnome desktop

The program `heif-thumbnailer` can be used as an HEIF/AVIF thumbnailer for the Gnome desktop.
The matching Gnome configuration files are in the `gnome` directory.
Place the files `heif.xml` and `avif.xml` into `/usr/share/mime/packages` and `heif.thumbnailer` into `/usr/share/thumbnailers`.
You may have to run `update-mime-database /usr/share/mime` to update the list of known MIME types.

## gdk-pixbuf loader

libheif also includes a gdk-pixbuf loader for HEIF/AVIF images. 'make install' will copy the plugin
into the system directories. However, you will still have to run `gdk-pixbuf-query-loaders --update-cache`
to update the gdk-pixbuf loader database.

## Software using libheif

* [GIMP](https://www.gimp.org/)
* [Krita](https://krita.org)
* [ImageMagick](https://imagemagick.org/)
* [GraphicsMagick](http://www.graphicsmagick.org/)
* [darktable](https://www.darktable.org)
* [digiKam 7.0.0](https://www.digikam.org/)
* [libvips](https://github.com/libvips/libvips)
* [libGD](https://libgd.github.io/)
* [Kodi HEIF image decoder plugin](https://kodi.wiki/view/Add-on:HEIF_image_decoder)
* [bimg](https://github.com/h2non/bimg)
* [GDAL](https://gdal.org/drivers/raster/heif.html)
* [OpenImageIO](https://sites.google.com/site/openimageio/)
* [XnView](https://www.xnview.com)

## Sponsors

Since I work as an independent developer, I need your support to be able to allocate time for libheif.
You can [sponsor](https://github.com/sponsors/farindk) the development using the link in the right hand column.

A big thank you goes to these major sponsors for supporting the development of libheif:

* Shopify <img src="logos/sponsors/shopify.svg" alt="shopify-logo" height="20"/>
* StrukturAG

## License

The libheif is distributed under the terms of the GNU Lesser General Public License.
The sample applications are distributed under the terms of the MIT License.

See COPYING for more details.

Copyright (c) 2017-2020 Struktur AG</br>
Copyright (c) 2017-2023 Dirk Farin</br>
Contact: Dirk Farin <dirk.farin@gmail.com>
