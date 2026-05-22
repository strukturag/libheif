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

#include "tiff_dynload.h"

// When TIFF_VIA_DLOPEN is defined (and we are on POSIX), libtiff is loaded at runtime via dlopen()
// and the reader has no link dependency on it. Otherwise (the default, and always on Windows) the
// table simply points at the directly-linked libtiff functions.
#if defined(TIFF_VIA_DLOPEN) && !defined(_WIN32)
#define HEIFIO_TIFF_DLOPEN 1
#else
#define HEIFIO_TIFF_DLOPEN 0
#endif

#if HEIFIO_TIFF_DLOPEN
#include <dlfcn.h>
#include <mutex>
#endif

namespace heifio_tiff {

#if HEIFIO_TIFF_DLOPEN

namespace {

const char* kLibraryCandidates[] = {
#if defined(__APPLE__)
    "libtiff.6.dylib",
    "libtiff.5.dylib",
    "libtiff.dylib",
#else
    "libtiff.so.6",
    "libtiff.so.5",
    "libtiff.so",
#endif
};

TiffFns g_fns;
bool g_loaded = false;
const char* g_error = "TIFF support requires libtiff, but it could not be loaded at runtime.";

// Load one symbol; on failure record the error and return false.
template <typename FnPtr>
bool load_symbol(void* handle, FnPtr& dst, const char* name)
{
  dst = reinterpret_cast<FnPtr>(dlsym(handle, name));
  if (!dst) {
    g_error = "libtiff was loaded but a required symbol is missing.";
    return false;
  }
  return true;
}

bool load_libtiff()
{
  void* h = nullptr;
  for (const char* name : kLibraryCandidates) {
    h = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (h) {
      break;
    }
  }
  if (!h) {
    g_error = "TIFF support requires libtiff to be installed, but it could not be loaded at runtime.";
    return false;
  }

  bool ok = true;
  ok &= load_symbol(h, g_fns.TIFFOpen, "TIFFOpen");
  ok &= load_symbol(h, g_fns.TIFFClose, "TIFFClose");
  ok &= load_symbol(h, g_fns.TIFFGetField, "TIFFGetField");
  ok &= load_symbol(h, g_fns.TIFFGetFieldDefaulted, "TIFFGetFieldDefaulted");
  ok &= load_symbol(h, g_fns.TIFFSetField, "TIFFSetField");
  ok &= load_symbol(h, g_fns.TIFFSetWarningHandler, "TIFFSetWarningHandler");
  ok &= load_symbol(h, g_fns.TIFFReadScanline, "TIFFReadScanline");
  ok &= load_symbol(h, g_fns.TIFFReadEncodedStrip, "TIFFReadEncodedStrip");
  ok &= load_symbol(h, g_fns.TIFFReadEncodedTile, "TIFFReadEncodedTile");
  ok &= load_symbol(h, g_fns.TIFFIsTiled, "TIFFIsTiled");
  ok &= load_symbol(h, g_fns.TIFFIsByteSwapped, "TIFFIsByteSwapped");
  ok &= load_symbol(h, g_fns.TIFFSwabShort, "TIFFSwabShort");
  ok &= load_symbol(h, g_fns.TIFFSwabLong, "TIFFSwabLong");
  ok &= load_symbol(h, g_fns.TIFFScanlineSize, "TIFFScanlineSize");
  ok &= load_symbol(h, g_fns.TIFFStripSize, "TIFFStripSize");
  ok &= load_symbol(h, g_fns.TIFFTileSize, "TIFFTileSize");
  ok &= load_symbol(h, g_fns.TIFFNumberOfStrips, "TIFFNumberOfStrips");
  ok &= load_symbol(h, g_fns.TIFFNumberOfDirectories, "TIFFNumberOfDirectories");
  ok &= load_symbol(h, g_fns.TIFFSetDirectory, "TIFFSetDirectory");
  ok &= load_symbol(h, g_fns.TIFFComputeTile, "TIFFComputeTile");
  ok &= load_symbol(h, g_fns.TIFFDataWidth, "TIFFDataWidth");
  ok &= load_symbol(h, g_fns.TIFFGetSeekProc, "TIFFGetSeekProc");
  ok &= load_symbol(h, g_fns.TIFFGetReadProc, "TIFFGetReadProc");
  ok &= load_symbol(h, g_fns.TIFFClientdata, "TIFFClientdata");
  ok &= load_symbol(h, g_fns._TIFFmalloc, "_TIFFmalloc");
  ok &= load_symbol(h, g_fns._TIFFfree, "_TIFFfree");

  if (!ok) {
    dlclose(h);
    return false;
  }

  // Keep the handle open for the lifetime of the process (intentionally not dlclose'd).
  return true;
}

} // namespace

const TiffFns* tiff_fns()
{
  static std::once_flag once;
  std::call_once(once, []() { g_loaded = load_libtiff(); });
  return g_loaded ? &g_fns : nullptr;
}

const char* tiff_load_error()
{
  return g_error;
}

#else // direct-linked libtiff (the default, and always on Windows)

// libtiff is linked directly; the table just points at the real functions so that the call sites
// in decoder_tiff.cc work unchanged regardless of the loading mode.
namespace {

const TiffFns g_fns = {
    &::TIFFOpen,
    &::TIFFClose,
    &::TIFFGetField,
    &::TIFFGetFieldDefaulted,
    &::TIFFSetField,
    &::TIFFSetWarningHandler,
    &::TIFFReadScanline,
    &::TIFFReadEncodedStrip,
    &::TIFFReadEncodedTile,
    &::TIFFIsTiled,
    &::TIFFIsByteSwapped,
    &::TIFFSwabShort,
    &::TIFFSwabLong,
    &::TIFFScanlineSize,
    &::TIFFStripSize,
    &::TIFFTileSize,
    &::TIFFNumberOfStrips,
    &::TIFFNumberOfDirectories,
    &::TIFFSetDirectory,
    &::TIFFComputeTile,
    &::TIFFDataWidth,
    &::TIFFGetSeekProc,
    &::TIFFGetReadProc,
    &::TIFFClientdata,
    &::_TIFFmalloc,
    &::_TIFFfree,
};

} // namespace

const TiffFns* tiff_fns()
{
  return &g_fns;
}

const char* tiff_load_error()
{
  return "";
}

#endif // HEIFIO_TIFF_DLOPEN

} // namespace heifio_tiff
