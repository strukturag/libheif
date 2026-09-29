/*
 * HEIF codec.
 * Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>
 *
 * This file is part of libheif.
 *
 * libheif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libheif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libheif.  If not, see <http://www.gnu.org/licenses/>.
 */

// This header is included first into every translation unit that contains code of the HEVC
// reference software HM: into the HM source files themselves, which libheif compiles through
// the generated wrappers (hm_source_wrapper.cc.in), and into encoder_hm_variant.cc.
//
// HM is written as a command line program, and a process can only contain one version of
// it. Without changing the HM sources, this header makes it usable as a library, and in
// several versions ("variants") at once:
//
// - HM prints statistics for every picture. printf(), and fprintf() to stdout and stderr,
//   are turned into functions that collect the text in a buffer. The plugin uses the text
//   for its error messages.
// - HM calls exit() when it meets a configuration that it does not support. exit() is turned
//   into a function that throws heif_hm_exit, which is caught in encoder_hm_variant.cc.
// - All code of HM is put into the namespace HEIF_HM_NAMESPACE, which is different for each
//   variant. The namespace is opened around the HM source file by the file that includes
//   this header.
//
// For the namespace, all headers of the system that HM uses are included here, outside of
// the namespace. They have include guards, so that they are not read again when HM includes
// them inside of the namespace. A header that is missing in the list below fails to compile
// inside of the namespace, it cannot go unnoticed.

#ifndef LIBHEIF_HM_AS_LIBRARY_H
#define LIBHEIF_HM_AS_LIBRARY_H

#if !defined(HEIF_HM_NAMESPACE)
#error "HEIF_HM_NAMESPACE has to be defined as the name of the namespace of this HM variant"
#endif

// --- the system headers that HM uses

#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <memory.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef __APPLE__
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cinttypes>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <istream>
#include <limits>
#include <list>
#include <map>
#include <numeric>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>


// --- no output, no exit

struct heif_hm_exit
{
  int code;
};

// What HM has printed since the buffer was cleared. HM prints a lot, and an error message
// is the last thing it prints, so only the end of the text is kept.
// All variants of HM share this buffer.
inline std::string& heif_hm_messages()
{
  static thread_local std::string messages;
  return messages;
}

inline void heif_hm_append_message(const char* format, va_list args)
{
  const size_t max_size = 4000;

  char buffer[1000];
  int n = vsnprintf(buffer, sizeof(buffer), format, args);
  if (n <= 0) {
    return;
  }

  std::string& messages = heif_hm_messages();
  messages.append(buffer, static_cast<size_t>(n) < sizeof(buffer) ? static_cast<size_t>(n) : sizeof(buffer) - 1);
  if (messages.size() > 2 * max_size) {
    messages.erase(0, messages.size() - max_size);
  }
}

inline int heif_hm_printf(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  heif_hm_append_message(format, args);
  va_end(args);
  return 0;
}

inline int heif_hm_fprintf(FILE* stream, const char* format, ...)
{
  int result = 0;

  va_list args;
  va_start(args, format);
  if (stream == stdout || stream == stderr) {
    heif_hm_append_message(format, args);
  }
  else {
    result = vfprintf(stream, format, args);
  }
  va_end(args);

  return result;
}

[[noreturn]] inline void heif_hm_exit_by_exception(int code)
{
  throw heif_hm_exit{code};
}

#define printf heif_hm_printf
#define fprintf heif_hm_fprintf
#define exit heif_hm_exit_by_exception


// --- one namespace for each variant of HM

// HM calls some of its functions with an explicit global scope, like ::getComponentScaleX().
// A name that is looked up in the global namespace is also searched in the namespaces that
// the global namespace uses, so these calls find the functions in the namespace of HM.
namespace HEIF_HM_NAMESPACE {
}
using namespace HEIF_HM_NAMESPACE;

// The MD5 functions of HM have C linkage, which knows no namespaces.
#define HEIF_HM_CONCATENATE2(a, b) a ## b
#define HEIF_HM_CONCATENATE(a, b) HEIF_HM_CONCATENATE2(a, b)

#define MD5Init HEIF_HM_CONCATENATE(HEIF_HM_NAMESPACE, _MD5Init)
#define MD5Update HEIF_HM_CONCATENATE(HEIF_HM_NAMESPACE, _MD5Update)
#define MD5Final HEIF_HM_CONCATENATE(HEIF_HM_NAMESPACE, _MD5Final)

#endif
