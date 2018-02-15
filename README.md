# libheif

[![Build Status](https://travis-ci.org/strukturag/libheif.svg?branch=master)](https://travis-ci.org/strukturag/libheif) [![Build Status](https://ci.appveyor.com/api/projects/status/github/strukturag/libheif?svg=true)](https://ci.appveyor.com/project/strukturag/libheif)

libheif is an ISO/IEC 23008-12:2017 HEIF file format decoder (encoder to come).

HEIF is a new image file format employing HEVC (h.265) image coding for the
best compression ratios currently possible.

libheif makes use of [libde265](https://github.com/strukturag/libde265) for
the actual image decoding. Alternative codecs for, e.g., AVC and JPEG can be
provided as plugins.


## Supported features

libheif has support for
* tiled images
* alpha channels
* thumbnails
* reading EXIF data
* reading the depth channel
* multiple images in a HEIF file
* image transformations (crop, mirror, rotate)
* overlay images
* plugin interface to add decoders for additional formats (AVC, JPEG)


## API

The library has a C API for easy integration and wide language support.
Note that the API is still work in progress and may still change.

Loading the primary image in a HEIF file is as easy as this:

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
heif_context_new_heic(ctx);

// get the default encoder
heif_encoder* encoder;
heif_context_get_encoders(ctx, heif_compression_HEVC, nullptr, &encoder, 1);

// set the encoder parameters
heif_encoder_init(encoder);
heif_encoder_set_lossy_quality(encoder, 50);

// encode the image
heif_image* image; // code to fill in the image omitted in this example
heif_context_encode_image(ctx, nullptr, image, encoder);

heif_encoder_deinit(encoder);

heif_context_write_to_file(context, "output.heic");
```

See the header file `heif.h` for the complete C API.


## Compiling

This library uses a standard autoconf/automake build system.
After downloading, run `./autogen.sh` to build the configuration scripts,
then call `./configure` and `make`.
Make sure that you compile and install [libde265](https://github.com/strukturag/libde265)
first, so that the configuration script will find this.
Preferably, download the `frame-parallel` branch of libde265, as this uses a
more recent API than version in the `master` branch.


## Compiling to JavaScript

libheif can also be compiled to JavaScript using
[emscripten](http://kripken.github.io/emscripten-site/).
See the `build-emscripten.sh` for further information.


## Online demo

Check out this [online demo](https://strukturag.github.io/libheif/).
This is `libheif` running in JavaScript in your browser.


## Example programs

Two example programs are provided in the `examples` directory.
The program `heif-convert` converts all images stored in an HEIF file to JPEG or PNG.
The program `heif-info` is a simple, minimal decoder that dumps the file structure to the console.

There is also a GIMP plugin using libheif [here](https://github.com/strukturag/heif-gimp-plugin).


## License

The libheif is distributed under the terms of the GNU Lesser General Public License.
The sample applications are distributed under the terms of the GNU General Public License.

See COPYING for more details.

Copyright (c) 2017-2018 Struktur AG Contact: Dirk Farin farin@struktur.de
