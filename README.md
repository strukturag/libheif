# libheif

libheif is a ISO/IEC 23008-12:2017 HEIF file format decoder.

Currently it supports extracting various elements from the file structure that
can then be passed to a HEVC decoder like
[libde265](https://github.com/strukturag/libde265) to get the image contents.


## API

The API is experimental and is expected to change. See the `examples` folder
for sample code using libheif.


## Compiling to JavaScript

libheif can be compiled to JavaScript using
[emscripten](http://kripken.github.io/emscripten-site/).
See the `build-emscripten.sh` for further information.
