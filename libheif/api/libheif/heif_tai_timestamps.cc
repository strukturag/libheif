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

#include <libheif/heif_tai_timestamps.h>

const uint64_t heif_tai_clock_info_time_uncertainty_unknown = 0xFFFFFFFFFFFFFFFF;
const int32_t heif_tai_clock_info_clock_drift_rate_unknown = 0x7FFFFFFF;
const int8_t heif_tai_clock_info_clock_type_unknown = 0;
const int8_t heif_tai_clock_info_clock_type_not_synchronized_to_atomic_source = 1;
const int8_t heif_tai_clock_info_clock_type_synchronized_to_atomic_source = 2;

const uint64_t heif_tai_timestamp_unknown = 0xFFFFFFFFFFFFFFFF;
