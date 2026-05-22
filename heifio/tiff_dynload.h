/*
  libheif example application.

  MIT License

  Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#ifndef LIBHEIF_TIFF_DYNLOAD_H
#define LIBHEIF_TIFF_DYNLOAD_H

// We still compile against the installed libtiff headers (for the types, the tag constants and the
// exact function signatures), but on POSIX systems the reader (decoder_tiff.cc) does not link
// libtiff. Instead it calls libtiff through the function-pointer table below, which is filled at
// runtime via dlopen()/dlsym(). This way 'heif-enc' has no hard libtiff dependency: TIFF input
// simply becomes available whenever libtiff is installed on the user's system.

extern "C" {
#include <tiff.h>
#include <tiffio.h>
}

namespace heifio_tiff {

// Function pointers for every libtiff function used by the TIFF reader. The member types are taken
// directly from the installed libtiff declarations, so they always match the local headers
// (including the variadic TIFFGetField/TIFFSetField).
struct TiffFns
{
  decltype(&::TIFFOpen)                TIFFOpen;
  decltype(&::TIFFClose)               TIFFClose;
  decltype(&::TIFFGetField)            TIFFGetField;
  decltype(&::TIFFGetFieldDefaulted)   TIFFGetFieldDefaulted;
  decltype(&::TIFFSetField)            TIFFSetField;
  decltype(&::TIFFSetWarningHandler)   TIFFSetWarningHandler;
  decltype(&::TIFFReadScanline)        TIFFReadScanline;
  decltype(&::TIFFReadEncodedStrip)    TIFFReadEncodedStrip;
  decltype(&::TIFFReadEncodedTile)     TIFFReadEncodedTile;
  decltype(&::TIFFWriteScanline)       TIFFWriteScanline;     // used by the TIFF writer (heif-dec)
  decltype(&::TIFFIsTiled)             TIFFIsTiled;
  decltype(&::TIFFIsByteSwapped)       TIFFIsByteSwapped;
  decltype(&::TIFFSwabShort)           TIFFSwabShort;
  decltype(&::TIFFSwabLong)            TIFFSwabLong;
  decltype(&::TIFFScanlineSize)        TIFFScanlineSize;
  decltype(&::TIFFStripSize)           TIFFStripSize;
  decltype(&::TIFFTileSize)            TIFFTileSize;
  decltype(&::TIFFNumberOfStrips)      TIFFNumberOfStrips;
  decltype(&::TIFFNumberOfDirectories) TIFFNumberOfDirectories;
  decltype(&::TIFFSetDirectory)        TIFFSetDirectory;
  decltype(&::TIFFComputeTile)         TIFFComputeTile;
  decltype(&::TIFFDataWidth)           TIFFDataWidth;
  decltype(&::TIFFGetSeekProc)         TIFFGetSeekProc;
  decltype(&::TIFFGetReadProc)         TIFFGetReadProc;
  decltype(&::TIFFClientdata)          TIFFClientdata;
  decltype(&::_TIFFmalloc)             _TIFFmalloc;
  decltype(&::_TIFFfree)               _TIFFfree;
};

// Returns the libtiff function table, or nullptr if libtiff could not be loaded at runtime.
// The table is loaded lazily on first call and cached.
const TiffFns* tiff_fns();

// Human-readable reason for the load failure (valid when tiff_fns() returns nullptr).
const char* tiff_load_error();

} // namespace heifio_tiff

#endif // LIBHEIF_TIFF_DYNLOAD_H
