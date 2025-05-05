/*
 * HEIF codec.
 * Copyright (c) 2017 Dirk Farin <dirk.farin@gmail.com>
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

#ifndef LIBHEIF_ERROR_H
#define LIBHEIF_ERROR_H

#include <cinttypes>
#include <cstddef>

#include <vector>
#include <string>
#include <memory>
#include <limits>
#include <istream>
#include <ostream>
#include <sstream>

#include "libheif/heif.h"
#include <cassert>


class ErrorBuffer
{
public:
  ErrorBuffer() = default;

  void set_success()
  {
    m_error_message = c_success;
  }

  void set_error(const std::string& err)
  {
    m_buffer = err;
    m_error_message = m_buffer.c_str();
  }

  const char* get_error() const
  {
    return m_error_message;
  }

private:
  constexpr static const char* c_success = "Success";
  std::string m_buffer;
  const char* m_error_message = c_success;
};


class Error
{
public:
  enum heif_error_code error_code = heif_error_Ok;
  enum heif_suberror_code sub_error_code = heif_suberror_Unspecified;
  std::string message;

  Error();

  Error(heif_error_code c,
        heif_suberror_code sc = heif_suberror_Unspecified,
        const std::string& msg = "");

  static Error from_heif_error(const heif_error&);

  static const Error Ok;

  static const Error InternalError;

  static const char kSuccess[];

  bool operator==(const Error& other) const { return error_code == other.error_code; }

  bool operator!=(const Error& other) const { return !(*this == other); }

  Error operator||(const Error& other) const {
    if (error_code != heif_error_Ok) {
      return *this;
    }
    else {
      return other;
    }
  }

  operator bool() const { return error_code != heif_error_Ok; }

  static const char* get_error_string(heif_error_code err);

  static const char* get_error_string(heif_suberror_code err);

  heif_error error_struct(ErrorBuffer* error_buffer) const;
};


inline std::ostream& operator<<(std::ostream& ostr, const Error& err)
{
  ostr << err.error_code << "/" << err.sub_error_code;
  return ostr;
}


template <typename T> class Result
{
public:
  Result() = default;

  Result(const T& v) : value(v), error(Error::Ok) {}

  Result(const Error& e) : error(e) {}

  operator bool() const { return error.error_code == heif_error_Ok; }

  T& operator*()
  {
    assert(error.error_code == heif_error_Ok);
    return value;
  }

  T value{};
  Error error;
};

#endif
