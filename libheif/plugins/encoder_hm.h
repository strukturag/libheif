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

#ifndef LIBHEIF_ENCODER_HM_H
#define LIBHEIF_ENCODER_HM_H

#include "common_utils.h"

/* Encoder plugin that uses the HEVC reference software HM. This plugin is EXPERIMENTAL.

   HM is slow, and it is not written as a library, but it implements all of HEVC.
   We use it for what the other HEVC encoders cannot do: bit depths of 9, 11 and
   13 to 15 bits, and the coding tools of the range extensions.

   HM encodes one image at a time in the whole process, because it keeps part of
   its state in global variables. The plugin serializes the calls.

   Image sizes: the HEVC picture is padded to a multiple of the minimum coding block
   size, and the conformance window removes the padding again. With chroma 4:2:0 and
   4:2:2, the conformance window can only remove an even number of samples, so an
   image with an odd size is encoded one sample larger and libheif adds a 'clap'
   property.
 */

const struct heif_encoder_plugin* get_encoder_plugin_hm();

#if PLUGIN_HM
extern "C" {
MAYBE_UNUSED LIBHEIF_API extern heif_plugin_info plugin_info;
}
#endif

#endif
