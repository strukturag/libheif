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

#ifndef LIBHEIF_HEIFIO_HELPER_FUNCS_H
#define LIBHEIF_HEIFIO_HELPER_FUNCS_H

#include <cinttypes>

// These small helpers are also defined in the libheif-internal "common_utils.h", but they are
// duplicated here so that heifio does not depend on any non-public libheif header. This lets the
// command-line tools be built against an installed system libheif (see WITH_LIBHEIF_FROM_SYSTEM).

constexpr uint32_t four_bytes_to_uint32(uint8_t msb, uint8_t b, uint8_t c, uint8_t lsb)
{
  return (static_cast<uint32_t>(msb << 24) |
          static_cast<uint32_t>(b << 16) |
          static_cast<uint32_t>(c << 8) |
          static_cast<uint32_t>(lsb));
}

constexpr uint16_t two_bytes_to_uint16(uint8_t msb, uint8_t lsb)
{
  return (static_cast<uint16_t>(msb << 8) |
          static_cast<uint16_t>(lsb));
}

inline uint16_t clip_int_u16(int32_t x, uint16_t maxi)
{
  if (x < 0) return 0;
  if (x > maxi) return maxi;
  return static_cast<uint16_t>(x);
}

#endif //LIBHEIF_HEIFIO_HELPER_FUNCS_H
