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

/*
 *
 */
struct heif_tai_clock_info {
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

LIBHEIF_API extern const uint64_t heif_tai_clock_info_time_uncertainty_unknown;
LIBHEIF_API extern const int32_t heif_tai_clock_info_clock_drift_rate_unknown;
LIBHEIF_API extern const int8_t heif_tai_clock_info_clock_type_unknown;
LIBHEIF_API extern const int8_t heif_tai_clock_info_clock_type_not_synchronized_to_atomic_source;
LIBHEIF_API extern const int8_t heif_tai_clock_info_clock_type_synchronized_to_atomic_source;


struct heif_tai_timestamp_packet {
  uint8_t version;

  // version 1

  // number of nanoseconds since TAI epoch (1958-01-01T00:00:00.0)
  uint64_t tai_timestamp;

  // whether the remote and receiptor clocks are in sync
  uint8_t synchronization_state;         // bool

  // whether the receptor clock failed to generate a timestamp
  uint8_t timestamp_generation_failure;  // bool

  // whether the original clock value has been modified
  uint8_t timestamp_is_modified;         // bool
};

LIBHEIF_API
void heif_tai_clock_info_release(struct heif_tai_clock_info* clock_info);


/**
 * Creates a new clock info property if it doesn't exist yet.
 *
 * @param out_optional_propertyId Output parameter for the property ID of the tai_clock_info. This parameter may be nullptr if the info is not required.
 */
LIBHEIF_API
struct heif_error heif_item_set_property_tai_clock_info(struct heif_context* ctx,
                                                        heif_item_id itemId,
                                                        const struct heif_tai_clock_info* clock,
                                                        heif_property_id* out_optional_propertyId);

/** This function allocates a new heif_tai_clock_info and returns it through out_clock.
 *
 * @param out_clock This parameter must not be nullptr. The object returned through this parameter must
 *                  be released with heif_tai_clock_info_release().
 */
LIBHEIF_API
struct heif_error heif_item_get_property_tai_clock_info(const struct heif_context* ctx,
                                                        heif_item_id itemId,
                                                        struct heif_tai_clock_info** out_clock);


/**
 * Creates a new TAI timestamp property if it doesn't exist yet.
 *
 * @param out_optional_propertyId Output parameter for the property ID of the TAI timestamp. This parameter may be nullptr if the info is not required.
 */
LIBHEIF_API
struct heif_error heif_item_set_property_tai_timestamp(struct heif_context* ctx,
                                                       heif_item_id itemId,
                                                       struct heif_tai_timestamp_packet* timestamp,
                                                       heif_property_id* out_optional_propertyId);

LIBHEIF_API
struct heif_error heif_item_get_property_tai_timestamp(const struct heif_context* ctx,
                                                       heif_item_id itemId,
                                                       struct heif_tai_timestamp_packet** out_timestamp);


#endif //LIBHEIF_HEIF_TAI_TIMESTAMPS_H
