/*
 * HEIF codec.
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
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

#include <inttypes.h>
#include <vector>
#include <string>
#include <memory>
#include <limits>
#include <istream>
#include <ostream>


namespace heif {

  class Error
  {
  public:
    enum ErrorCode {
      Ok,
      InvalidInput,
      NonexistingImage,
      Unsupported,
      MemoryAllocationError,
      NoSuitableDecoder
    } error_code;

    enum SubErrorCode {
      // --- InvalidInput
      Unspecified,
      ParseError,
      EndOfData,
      NoCompatibleBrandType,
      NoMetaBox,
      NoHdlrBox,
      NoPitmBox,
      NoIprpBox,
      NoIpcoBox,
      NoIpmaBox,
      NoIlocBox,
      NoIinfBox,
      NoIdatBox,
      NoPictHandler,
      NoPropertiesForItemID,
      NonexistingPropertyReferenced,
      UnsupportedImageType,
      NoInputDataInFile
    } sub_error_code;

  Error()
    : error_code(Ok)
    {
    }


  Error(SubErrorCode sc)    // TODO: hack, remove me later
    : error_code(InvalidInput),
      sub_error_code(sc)
      {
      }

  Error(ErrorCode c, SubErrorCode sc = SubErrorCode::Unspecified)
    : error_code(c),
      sub_error_code(sc)
    {
    }

    static Error OK;

    bool operator==(const Error& other) const { return error_code == other.error_code; }
    bool operator!=(const Error& other) const { return !(*this == other); }
  };


  inline std::ostream& operator<<(std::ostream& ostr, const Error& err) {
    ostr << err.error_code << "/" << err.sub_error_code;
    return ostr;
  }
}

#endif
