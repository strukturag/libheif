/*
 * HEIF JPEG codec.
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
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

#include "jpeg.h"
#include <string>
#include "security_limits.h"

std::string Box_jpgC::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);

  sstr << indent << "num bytes: " << m_data.size() << "\n";

  return sstr.str();
}


Error Box_jpgC::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);

  writer.write(m_data);

  prepend_header(writer, box_start);

  return Error::Ok;
}


Error Box_jpgC::parse(BitstreamRange& range)
{
  if (!has_fixed_box_size()) {
    return Error{heif_error_Unsupported_feature, heif_suberror_Unspecified, "jpgC with unspecified size are not supported"};
  }

  size_t nBytes = range.get_remaining_bytes();
  if (nBytes > MAX_MEMORY_BLOCK_SIZE) {
    return Error{heif_error_Invalid_input, heif_suberror_Unspecified, "jpgC block exceeds maximum size"};
  }

  m_data.resize(nBytes);
  range.read(m_data.data(), nBytes);
  return range.get_error();
}
