/*
 * HEIF codec.
 * Copyright (c) 2025 Dirk Farin <dirk.farin@gmail.com>
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

#ifndef LIBHEIF_HEIF_TAI_TIMESTAMPS_H
#define LIBHEIF_HEIF_TAI_TIMESTAMPS_H

#include <libheif/heif.h>

LIBHEIF_API extern const uint64_t heif_tai_clock_info_time_uncertainty_unknown;
LIBHEIF_API extern const int32_t heif_tai_clock_info_clock_drift_rate_unknown;
LIBHEIF_API extern const int8_t heif_tai_clock_info_clock_type_unknown;
LIBHEIF_API extern const int8_t heif_tai_clock_info_clock_type_not_synchronized_to_atomic_source;
LIBHEIF_API extern const int8_t heif_tai_clock_info_clock_type_synchronized_to_atomic_source;

struct heif_tai_clock_info
{
  uint8_t version;

  // version 1

  // standard deviation for timestamp generation process
  uint64_t time_uncertainty;

  // receptor clock resolution in nanoseconds
  uint32_t clock_resolution;

  // clock drift rate in picoseconds/second when synchronization is stopped
  int32_t clock_drift_rate;

  // whether clock is synchronized to an atomic source
  uint8_t clock_type;
};


LIBHEIF_API extern const uint64_t heif_tai_timestamp_unknown;

struct heif_tai_timestamp_packet
{
  uint8_t version;

  // version 1

  uint64_t tai_timestamp;
  uint8_t synchronization_state;         // bool
  uint8_t timestamp_generation_failure;  // bool
  uint8_t timestamp_is_modified;         // bool
};



#endif //LIBHEIF_HEIF_TAI_TIMESTAMPS_H
