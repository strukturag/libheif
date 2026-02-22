/*
 * libheif OMAF (ISO/IEC 23090-2)
 *
 * Copyright (c) 2026 Brad Hards <bradh@frogmouth.net>
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

#include "omaf_boxes.h"

#include <memory>
#include <string>


Error Box_prfr::parse(BitstreamRange& range, const heif_security_limits* limits)
{
  parse_full_box_header(range);

  if (get_version() > 0) {
    return unsupported_version_error("prfr");
  }

  uint8_t projection_type = (range.read8() & 0x1F);
  switch (projection_type)
  {
  case 0x00:
    m_projection = heif_image_projection::equirectangular;
    break;
  case 0x01:
    m_projection = heif_image_projection::cube_map;
    break;
  default:
    m_projection = heif_image_projection::unknown_other;
    break;
  }
  return range.get_error();
}

std::string Box_prfr::dump(Indent& indent) const
{
  std::ostringstream sstr;
  sstr << Box::dump(indent);
  sstr << indent << "projection_type: " << m_projection << "\n";
  return sstr.str();
}

Error Box_prfr::write(StreamWriter& writer) const
{
  size_t box_start = reserve_box_header_space(writer);
  switch (m_projection) {
    case heif_image_projection::equirectangular:
      writer.write8(0x00);
      break;
    case heif_image_projection::cube_map:
      writer.write8(0x01);
      break;
    default:
      return {
        heif_error_Invalid_input,
        heif_suberror_Unspecified,
        "Unsupported image projection value."
    };
  }
  prepend_header(writer, box_start);
  return Error::Ok;
}

Error Box_prfr::set_image_projection(heif_image_projection projection)
{
  if ((projection == heif_image_projection::equirectangular) || (projection == heif_image_projection::cube_map)) {
    m_projection = projection;
    return Error::Ok;
  } else {
    return {
      heif_error_Invalid_input,
      heif_suberror_Unspecified,
      "Unsupported image projection value."
    };
  }
}
