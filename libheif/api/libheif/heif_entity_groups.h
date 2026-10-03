/*
 * HEIF codec.
 * Copyright (c) 2017-2025 Dirk Farin <dirk.farin@gmail.com>
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

#ifndef LIBHEIF_HEIF_ENTITY_GROUPS_H
#define LIBHEIF_HEIF_ENTITY_GROUPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "libheif/heif_library.h"

// Entity group type FourCCs.
// For the complete list of registered entity group types, see https://mp4ra.org/registered-types/entity-groups

/**
 * Alternatives.
 *
 * Alternative versions of the same content, listed in order of preference.
 * Only one of them should be displayed.
 *
 * See ISO/IEC 14496-12:2026 Section 8.15.3.1.
 */
#define heif_entity_group_altr   heif_fourcc('a','l','t','r')

/**
 * Multi-resolution pyramid.
 *
 * The same image at different resolutions, listed from lowest to highest resolution.
 *
 * See ISO/IEC 23008-12:2025/Amd. 1:2025 Section 6.8.12.
 */
#define heif_entity_group_pymd   heif_fourcc('p','y','m','d')

/**
 * Timeline equivalence.
 *
 * Relates untimed image items to an equivalent position in the timeline of a track.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.1.
 */
#define heif_entity_group_eqiv   heif_fourcc('e','q','i','v')

/**
 * Image burst.
 *
 * A series of burst-captured images in temporally increasing order.
 * The images can be stored as image items or as a single image sequence track (the group's only entity).
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.2.
 */
#define heif_entity_group_brst   heif_fourcc('b','r','s','t')

/**
 * Time-synchronized capture.
 *
 * Images captured simultaneously, stored either as image items or as image sequence tracks, but not a mixture of both.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.3.
 */
#define heif_entity_group_tsyn   heif_fourcc('t','s','y','n')

/**
 * Stereo pair.
 *
 * A stereoscopic pair of two image items: first the left view, then the right view.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.5.
 */
#define heif_entity_group_ster   heif_fourcc('s','t','e','r')

/**
 * Stereo pair with monoscopic fallback.
 *
 * Three image items: left view, right view, and a monoscopic fallback,
 * which may be the same as the left or right view.
 *
 * See ISO/IEC 23008-12:2025/Amd. 1:2025 Section 6.8.11.
 */
#define heif_entity_group_stem   heif_fourcc('s','t','e','m')

/**
 * Auto-exposure bracketing.
 *
 * Images of the same scene taken with varying exposure settings.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.6.2.
 */
#define heif_entity_group_aebr   heif_fourcc('a','e','b','r')

/**
 * White balance bracketing.
 *
 * Images of the same scene taken with varying white balance settings.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.6.3.
 */
#define heif_entity_group_wbbr   heif_fourcc('w','b','b','r')

/**
 * Focus bracketing.
 *
 * Images of the same scene taken with varying focus settings.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.6.4.
 */
#define heif_entity_group_fobr   heif_fourcc('f','o','b','r')

/**
 * Flash exposure bracketing.
 *
 * Images of the same scene taken with varying flash exposure settings.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.6.5.
 */
#define heif_entity_group_afbr   heif_fourcc('a','f','b','r')

/**
 * Depth of field bracketing.
 *
 * Images of the same scene taken with varying aperture settings.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.6.6.
 */
#define heif_entity_group_dobr   heif_fourcc('d','o','b','r')

/**
 * Album collection.
 *
 * A user-defined album of images.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.7.1.
 */
#define heif_entity_group_albc   heif_fourcc('a','l','b','c')

/**
 * Favorites collection.
 *
 * A user-defined collection of favorite images.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.7.2.
 */
#define heif_entity_group_favc   heif_fourcc('f','a','v','c')

/**
 * Panorama.
 *
 * Images captured to be composed into a panorama, listed in panorama order.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.8.
 */
#define heif_entity_group_pano   heif_fourcc('p','a','n','o')

/**
 * Slideshow.
 *
 * Images intended to form a slideshow, listed in display order.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.9.
 */
#define heif_entity_group_slid   heif_fourcc('s','l','i','d')

/**
 * Progressive rendering.
 *
 * The same image at different quality levels, listed from lowest to highest quality.
 * These images should also be members of an 'altr' entity group.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.10.
 */
#define heif_entity_group_prgr   heif_fourcc('p','r','g','r')

// ------------------------- entity groups ------------------------

typedef uint32_t heif_entity_group_id;

typedef struct heif_entity_group
{
  heif_entity_group_id entity_group_id;
  // one of the heif_entity_group_* FourCC constants defined above
  uint32_t entity_group_type;
  heif_item_id* entities;
  uint32_t num_entities;
} heif_entity_group;

// `type_filter` is the wanted FourCC of the entity group type.
// Use 0 for `type_filter` or `item_filter` to disable the filter.
// Returns an array of heif_entity_group structs with *out_num_groups entries.
LIBHEIF_API
heif_entity_group* heif_context_get_entity_groups(const heif_context*,
                                                  uint32_t type_filter,
                                                  heif_item_id item_filter,
                                                  int* out_num_groups);

// Release an array of entity groups returned by heif_context_get_entity_groups().
LIBHEIF_API
void heif_entity_groups_release(heif_entity_group*, int num_groups);

// Create an alternative ('altr') entity group containing the supplied item IDs,
// preserving their order. Track entities are not currently supported. An item may
// belong to only one alternative entity group.
// `out_group_id` may be NULL.
LIBHEIF_API
heif_error heif_context_add_alternative_entity_group(heif_context* ctx,
                                                     const heif_item_id* item_ids,
                                                     uint32_t num_items,
                                                     heif_entity_group_id* out_group_id);

// Create a stereo-pair ('ster') entity group. The first image is the left view and
// the second image is the right view. `out_group_id` may be NULL.
LIBHEIF_API
heif_error heif_context_add_stereo_pair_entity_group(heif_context* ctx,
                                                     heif_item_id left_image_id,
                                                     heif_item_id right_image_id,
                                                     heif_entity_group_id* out_group_id);

// Create a stereo-pair-with-monoscopic-fallback ('stem') entity group. The first
// image is the left view, the second image is the right view, and the third image
// is the monoscopic fallback. The fallback may be identical to either stereo view.
// `out_group_id` may be NULL.
LIBHEIF_API
heif_error heif_context_add_stereo_pair_with_monoscopic_fallback_entity_group(
    heif_context* ctx,
    heif_item_id left_image_id,
    heif_item_id right_image_id,
    heif_item_id monoscopic_image_id,
    heif_entity_group_id* out_group_id);


#ifdef __cplusplus
}
#endif

#endif
