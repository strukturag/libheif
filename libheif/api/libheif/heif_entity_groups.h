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

// Entity group type FourCC; For complete list of entity group types see https://mp4ra.org/registered-types/entity-groups
/**
 * Alternative image.
 *
 * The entity group consists of multiple alternative versions, the application should display only one of them. The most preferred image comes first.
 *
 * See ISO/IEC 14496-12:2026 Section 8.15.3.1.
 */
#define heif_entity_group_altr   heif_fourcc('a','l','t','r')

/**
 * Multi-resolution pyramid.
 *
 * The entity group consists of multiple resolution versions. Low resolution image comes first.
 * 
 * See ISO/IEC 23008-12:2025/Amd. 1:2025 Section 6.8.12.
 */
#define heif_entity_group_pymd   heif_fourcc('p','y','m','d')

/**
 * Equivalent entity group
 * 
 * The entity group associates an untimed image with a position in the timeline of a sequence.
 * 
 * See ISO/IEC 23008-12:2025 Section 6.8.1
 */
#define heif_entity_group_eqiv   heif_fourcc('e','q','i','v')

/**
 * Burst image entity group
 *
 * The entity group consists of burst images. The images can be stored as an image sequence or a set of image items. Only one entity in the entity group if it is an image sequence.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.2
 */
#define heif_entity_group_brst   heif_fourcc('b','r','s','t')

/**
 * Time-synchronized capture entity group
 *
 * The entity group consists of images taken at synchronized time. The images can be stored as an image sequence or a set of image items, but not both.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.3
 */
#define heif_entity_group_tsyn   heif_fourcc('t','s','y','n')

/**
 * Stereo pair.
 *
 * The entity group consists of a stereoscopic pair of image items. The first image is left image, the second image is right image.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.5.
 */
#define heif_entity_group_ster   heif_fourcc('s','t','e','r')

/**
 * Stereo pair with mono fallback.
 *
 * The entity group consists of a stereoscopic pair of image items and a monoscopic fallback.
 * The first image is left image, the second image is right image, the third image is monoscopic image. The monoscopic fallback can be the same as left or right image.
 *
 * See ISO/IEC 23008-12:2025/Amd. 1:2025 Section 6.8.11.
 */
#define heif_entity_group_stem   heif_fourcc('s','t','e','m')

/**
 * Auto-exposure bracket
 *
 * The entity group consists of image items taken at different exposure settings.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.6.2.
 */
#define heif_entity_group_aebr   heif_fourcc('a','e','b','r')

/**
 * White bracket
 *
 * The entity group consists of image items taken at different white balance settings.
 *
  * See ISO/IEC 23008-12:2025 Section 6.8.6.3.
 */
#define heif_entity_group_wbbr   heif_fourcc('w','b','b','r')

/**
 * Focus bracket
 *
 * The entity group consists of image items taken at different focus settings.
 *
  * See ISO/IEC 23008-12:2025 Section 6.8.6.4.
 */
#define heif_entity_group_fobr   heif_fourcc('f','o','b','r')

/**
 * Flash exposure bracket
 *
 * The entity group consists of image items taken at different flash exposure settings.
 *
  * See ISO/IEC 23008-12:2025 Section 6.8.6.5.
 */
#define heif_entity_group_afbr   heif_fourcc('a','f','b','r')

/**
 * Depth of field bracket
 *
 * The entity group consists of image items taken at different aperture settings.
 *
  * See ISO/IEC 23008-12:2025 Section 6.8.6.6.
 */
#define heif_entity_group_dobr   heif_fourcc('d','o','b','r')

/**
 * Album collection
 *
 * The entity group consists of entities that form an album collection.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.7.1.
 */
#define heif_entity_group_albc   heif_fourcc('a','l','b','c')

/**
 * Favorites collection
 *
 * The entity group consists of entities that form an collection of favorites images.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.7.2.
 */
#define heif_entity_group_favc   heif_fourcc('f','a','v','c')

/**
 * Panorama
 *
 * The entity group consists of images captured in order to create a panorama.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.8.
 */
#define heif_entity_group_pano   heif_fourcc('p','a','n','o')

/**
 * Slideshow
 *
 * The entity group consists of images intended to form a slideshow.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.9.
 */
#define heif_entity_group_slid   heif_fourcc('s','l','i','d')

/**
 * Progressive rendering
 *
 * The entity group consists of images of different quality. Low quality image comes first. These images should also be members of an 'altr' entity group.
 *
 * See ISO/IEC 23008-12:2025 Section 6.8.10.
 */
#define heif_entity_group_prgr   heif_fourcc('p','r','g','r')

// ------------------------- entity groups ------------------------

typedef uint32_t heif_entity_group_id;

typedef struct heif_entity_group
{
  heif_entity_group_id entity_group_id;
  // this is a FourCC constant defined above
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


#ifdef __cplusplus
}
#endif

#endif
