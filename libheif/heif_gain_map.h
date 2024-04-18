/*
 * HEIF codec.
 * Copyright (c) 2017 Dirk Farin <dirk.farin@gmail.com>
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

#ifndef LIBHEIF_HEIF_GAIN_MAP_H
#define LIBHEIF_HEIF_GAIN_MAP_H

#include "error.h"
#include <vector>

// Gain map metadata, for tone mapping between SDR and HDR.
struct heif_gain_map_metadata {
  uint32_t gainMapMinN[3];
  uint32_t gainMapMinD[3];
  uint32_t gainMapMaxN[3];
  uint32_t gainMapMaxD[3];
  uint32_t gainMapGammaN[3];
  uint32_t gainMapGammaD[3];

  uint32_t baseOffsetN[3];
  uint32_t baseOffsetD[3];
  uint32_t alternateOffsetN[3];
  uint32_t alternateOffsetD[3];

  uint32_t baseHdrHeadroomN;
  uint32_t baseHdrHeadroomD;
  uint32_t alternateHdrHeadroomN;
  uint32_t alternateHdrHeadroomD;

  bool backwardDirection;
  bool useBaseColorSpace;

  static Error prepare_gain_map_metadata(const heif_gain_map_metadata* gain_map_metadata,
                                         std::vector<uint8_t> &data);

  static Error parse_gain_map_metadata(const std::vector<uint8_t> &data,
                                       heif_gain_map_metadata* gain_map_metadata);

  void dump() const {
    printf("GAIN MAP METADATA: \n");
    printf("min numerator:                       %d, %d, %d\n", gainMapMinN[0], gainMapMinN[1], gainMapMinN[2]);
    printf("min denominator:                     %d, %d, %d\n", gainMapMinD[0], gainMapMinD[1], gainMapMinD[2]);
    printf("max numerator:                       %d, %d, %d\n", gainMapMaxN[0], gainMapMaxN[1], gainMapMaxN[2]);
    printf("max denominator:                     %d, %d, %d\n", gainMapMaxD[0], gainMapMaxD[1], gainMapMaxD[2]);
    printf("gamma numerator:                     %d, %d, %d\n", gainMapGammaN[0], gainMapGammaN[1], gainMapGammaN[2]);
    printf("gamma denominator:                   %d, %d, %d\n", gainMapGammaD[0], gainMapGammaD[1], gainMapGammaD[2]);
    printf("SDR offset numerator:                %d, %d, %d\n", baseOffsetN[0], baseOffsetN[1], baseOffsetN[2]);
    printf("SDR offset denominator:              %d, %d, %d\n", baseOffsetD[0], baseOffsetD[1], baseOffsetD[2]);
    printf("HDR offset numerator:                %d, %d, %d\n", alternateOffsetN[0], alternateOffsetN[1], alternateOffsetN[2]);
    printf("HDR offset denominator:              %d, %d, %d\n", alternateOffsetD[0], alternateOffsetD[1], alternateOffsetD[2]);
    printf("base HDR head room numerator:        %d\n",         baseHdrHeadroomN);
    printf("base HDR head room denominator:      %d\n",         baseHdrHeadroomD);
    printf("alternate HDR head room numerator:   %d\n",         alternateHdrHeadroomN);
    printf("alternate HDR head room denominator: %d\n",         alternateHdrHeadroomD);
    printf("backwardDirection:                   %s\n",         backwardDirection ? "true" : "false");
    printf("use base color space:                %s\n",         useBaseColorSpace ? "true" : "false");
  }
};

#endif
