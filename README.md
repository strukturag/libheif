# libheif

[![Build Status](https://travis-ci.org/strukturag/libheif.svg?branch=master)](https://travis-ci.org/strukturag/libheif)

libheif is an ISO/IEC 23008-12:2017 HEIF file format decoder (encoder to come).

HEIF is a new image file format employing HEVC (h.265) image coding for the
best compression ratios currently possible.

Libheif makes use of [libde265](https://github.com/strukturag/libde265) for
the actual image decoding. Alternative codecs for, e.g., AVC and JPEG can be
provided as plugins.


## API

The library has a C API for easy integration and wide language support.
Note that the API is still work in progress and may still change.

Loading the primary image in a HEIF file is as easy as this:

```
heif_context* ctx = heif_context_alloc();
heif_context_read_from_file(ctx, input_filename);

// get a handle to the primary image
heif_image_handle* handle;
heif_context_get_primary_image_handle(ctx, &handle);

// decode the image and convert colorspace to RGB, saved as 24bit interleaved
heif_image* img;
heif_decode_image(handle, heif_colorspace_RGB, heif_chroma_interleaved_24bit, &img);

int stride;
const uint8_t* data = heif_pixel_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
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

There is also a GIMP plugin using libHEIF [here](https://github.com/strukturag/heif-gimp-plugin).


## License

The libheif is distributed under the terms of the GNU Lesser General Public License.
The sample applications are distributed under the terms of the GNU General Public License.

See COPYING for more details.

Copyright (c) 2017-2018 Struktur AG Contact: Dirk Farin farin@struktur.de
