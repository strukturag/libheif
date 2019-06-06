# libheif

[![Build Status](https://travis-ci.org/strukturag/libheif.svg?branch=master)](https://travis-ci.org/strukturag/libheif) [![Build Status](https://ci.appveyor.com/api/projects/status/github/strukturag/libheif?svg=true)](https://ci.appveyor.com/project/strukturag/libheif) [![Coverity Scan Build Status](https://scan.coverity.com/projects/16641/badge.svg)](https://scan.coverity.com/projects/strukturag-libheif)


libheif is an ISO/IEC 23008-12:2017 HEIF file format decoder and encoder.

HEIF is a new image file format employing HEVC (h.265) image coding for the
best compression ratios currently possible.

libheif makes use of [libde265](https://github.com/strukturag/libde265) for
the actual image decoding and x265 for encoding. Alternative codecs for, e.g., AVC and JPEG can be
provided as plugins. There is experimental code for an AV1 plugin (for AVIF format support) in the 'avif' branch.


## Supported features

libheif has support for decoding
* tiled images
* alpha channels
* thumbnails
* reading EXIF and XMP metadata
* reading the depth channel
* multiple images in an HEIF file
* image transformations (crop, mirror, rotate)
* overlay images
* plugin interface to add decoders for additional formats (AV1, AVC, JPEG)
* decoding of files while downloading (e.g. extract image size before file has been completely downloaded)
* reading color profiles
* 10 and 12 bit images

The encoder supports:
* lossy compression with adjustable quality
* lossless compression
* alpha channels
* thumbnails
* save multiple images to an HEIF file
* save EXIF and XMP metadata
* writing color profiles
* 10 and 12 bit images

## API

The library has a C API for easy integration and wide language support.
Note that the API is still work in progress and may still change.

Loading the primary image in an HEIF file is as easy as this:

```C
heif_context* ctx = heif_context_alloc();
heif_context_read_from_file(ctx, input_filename, nullptr);

// get a handle to the primary image
heif_image_handle* handle;
heif_context_get_primary_image_handle(ctx, &handle);

// decode the image and convert colorspace to RGB, saved as 24bit interleaved
heif_image* img;
heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_24bit, nullptr);

int stride;
const uint8_t* data = heif_pixel_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
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
heif_context_encode_image(ctx, nullptr, image, encoder);

heif_encoder_release(encoder);

heif_context_write_to_file(context, "output.heic");
```

See the header file `heif.h` for the complete C API.

There is also a C++ API which is a header-only wrapper to the C API.
Hence, you can use the C++ API and still be binary compatible.
Code using the C++ API is much less verbose than using the C API directly.

There is also an experimental Go API, but this is not stable yet.


## Compiling

This library uses a standard autoconf/automake build system.
After downloading, run `./autogen.sh` to build the configuration scripts,
then call `./configure` and `make`.
Make sure that you compile and install [libde265](https://github.com/strukturag/libde265)
first, so that the configuration script will find this.
Preferably, download the `frame-parallel` branch of libde265, as this uses a
more recent API than version in the `master` branch.
Also install x265 and its development files if you want to use HEIF encoding.


## Compiling to JavaScript

libheif can also be compiled to JavaScript using
[emscripten](http://kripken.github.io/emscripten-site/).
See the `build-emscripten.sh` for further information.


## Online demo

Check out this [online demo](https://strukturag.github.io/libheif/).
This is `libheif` running in JavaScript in your browser.


## Example programs

Some example programs are provided in the `examples` directory.
The program `heif-convert` converts all images stored in an HEIF file to JPEG or PNG.
`heif-enc` lets you convert JPEG files to HEIF.
The program `heif-info` is a simple, minimal decoder that dumps the file structure to the console.

There is also a GIMP plugin using libheif [here](https://github.com/strukturag/heif-gimp-plugin).


## HEIF thumbnails for the Gnome desktop

The program `heif-thumbnailer` can be used as a HEIF thumbnailer for the Gnome desktop.
The matching Gnome configuration files are in the `gnome` directory.
Place the file `heif.xml` into `/usr/share/mime/packages` and `heif.thumbnailer` into `/usr/share/thumbnailers`.
You may have to run `update-mime-database /usr/share/mime` to update the list of known MIME types.


## License

The libheif is distributed under the terms of the GNU Lesser General Public License.
The sample applications are distributed under the terms of the MIT License.

See COPYING for more details.

Copyright (c) 2017-2019 Struktur AG Contact: Dirk Farin farin@struktur.de
