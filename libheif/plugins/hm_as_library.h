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

// This header is force-included into every source file of the HEVC reference software HM
// when it is built for libheif (third-party/hm.cmd). HM is written as a command line
// program: its encoder library prints statistics for every picture and calls exit() when it
// meets a configuration that it does not support. Neither is acceptable inside a library.
// Without changing the HM sources, this header
//
// - turns printf(), and fprintf() to stdout and stderr, into functions that collect the text
//   in a buffer. The libheif plugin uses it for its error messages.
// - turns exit() into a function that throws heif_hm_exit, which the libheif plugin catches
//   and reports as an error.
//
// The headers that declare these functions are included first. They have include guards,
// so that they are not read again after the names have been redefined.
//
// The libheif plugin includes this header too, with LIBHEIF_HM_PLUGIN defined: it has to
// know heif_hm_exit and the message buffer, but keeps the functions of the C library.

#ifndef LIBHEIF_HM_AS_LIBRARY_H
#define LIBHEIF_HM_AS_LIBRARY_H

#ifdef __cplusplus

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>

struct heif_hm_exit
{
  int code;
};

// What HM has printed since the buffer was cleared. HM prints a lot, and an error message
// is the last thing it prints, so only the end of the text is kept.
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

#if !defined(LIBHEIF_HM_PLUGIN)
#define printf heif_hm_printf
#define fprintf heif_hm_fprintf
#define exit heif_hm_exit_by_exception
#endif

#endif
#endif
