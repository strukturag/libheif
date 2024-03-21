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

#include "gain_map_metadata.h"

Error streamWriteU8(std::vector<uint8_t> &data, uint8_t value) {
  data.push_back(value);
  return Error::Ok;
}

Error streamWriteU32(std::vector<uint8_t> &data, uint32_t value) {
  data.push_back((value >> 24) & 0xff);
  data.push_back((value >> 16) & 0xff);
  data.push_back((value >> 8) & 0xff);
  data.push_back(value & 0xff);
  return Error::Ok;
}

Error streamReadU8(const std::vector<uint8_t> &data, uint8_t &value, size_t &pos) {
  if (pos >= data.size()) {
    return Error(heif_error_Invalid_input, heif_suberror_End_of_data);
  }
  value = data[pos++];
  return Error::Ok;
}

Error streamReadU32(const std::vector<uint8_t> &data, uint32_t &value, size_t &pos) {
  if (pos >= data.size() - 3) {
    return Error(heif_error_Invalid_input, heif_suberror_End_of_data);
  }
  value = (data[pos]     << 24  |
           data[pos + 1] << 16  |
           data[pos + 2] << 8   |
           data[pos + 3]);
  pos += 4;
  return Error::Ok;
}

Error GainMapMetadata::prepare_gain_map_metadata(const GainMapMetadata* metadata,
                                                 std::vector<uint8_t> &data) {
  if (metadata == nullptr) {
    return Error(heif_error_Usage_error, heif_suberror_Null_pointer_argument);
  }
  const uint8_t version = 0;
  streamWriteU8(data, version);

    uint8_t flags = 0u;
    // Always write three channels for now for simplicity.
    // TODO(maryla): the draft says that this specifies the count of channels of the
    // gain map. But tone mapping is done in RGB space so there are always three
    // channels, even if the gain map is grayscale. Should this be revised?
    const bool allChannelsIdentical =
        metadata->gainMapMinN[0] == metadata->gainMapMinN[1] && metadata->gainMapMinN[0] == metadata->gainMapMinN[2] &&
        metadata->gainMapMinD[0] == metadata->gainMapMinD[1] && metadata->gainMapMinD[0] == metadata->gainMapMinD[2] &&
        metadata->gainMapMaxN[0] == metadata->gainMapMaxN[1] && metadata->gainMapMaxN[0] == metadata->gainMapMaxN[2] &&
        metadata->gainMapMaxD[0] == metadata->gainMapMaxD[1] && metadata->gainMapMaxD[0] == metadata->gainMapMaxD[2] &&
        metadata->gainMapGammaN[0] == metadata->gainMapGammaN[1] && metadata->gainMapGammaN[0] == metadata->gainMapGammaN[2] &&
        metadata->gainMapGammaD[0] == metadata->gainMapGammaD[1] && metadata->gainMapGammaD[0] == metadata->gainMapGammaD[2] &&
        metadata->baseOffsetN[0] == metadata->baseOffsetN[1] && metadata->baseOffsetN[0] == metadata->baseOffsetN[2] &&
        metadata->baseOffsetD[0] == metadata->baseOffsetD[1] && metadata->baseOffsetD[0] == metadata->baseOffsetD[2] &&
        metadata->alternateOffsetN[0] == metadata->alternateOffsetN[1] &&
        metadata->alternateOffsetN[0] == metadata->alternateOffsetN[2] &&
        metadata->alternateOffsetD[0] == metadata->alternateOffsetD[1] &&
        metadata->alternateOffsetD[0] == metadata->alternateOffsetD[2];
    const uint8_t channelCount = allChannelsIdentical ? 1u : 3u;

    if (channelCount == 3) {
        flags |= 1;
    }
    if (metadata->useBaseColorSpace) {
        flags |= 2;
    }
    if (metadata->backwardDirection) {
        flags |= 4;
    }
    const uint32_t denom = metadata->baseHdrHeadroomD;
    bool useCommonDenominator = metadata->baseHdrHeadroomD == denom && metadata->alternateHdrHeadroomD == denom;
    for (int c = 0; c < channelCount; ++c) {
        useCommonDenominator = useCommonDenominator && metadata->gainMapMinD[c] == denom && metadata->gainMapMaxD[c] == denom &&
                               metadata->gainMapGammaD[c] == denom && metadata->baseOffsetD[c] == denom &&
                               metadata->alternateOffsetD[c] == denom;
    }
    if (useCommonDenominator) {
        flags |= 8;
    }
    streamWriteU8(data, flags);

    if (useCommonDenominator) {
        streamWriteU32(data, denom);
        streamWriteU32(data, metadata->baseHdrHeadroomN);
        streamWriteU32(data, metadata->alternateHdrHeadroomN);
        for (int c = 0; c < channelCount; ++c) {
            streamWriteU32(data, (uint32_t)metadata->gainMapMinN[c]);
            streamWriteU32(data, (uint32_t)metadata->gainMapMaxN[c]);
            streamWriteU32(data, metadata->gainMapGammaN[c]);
            streamWriteU32(data, (uint32_t)metadata->baseOffsetN[c]);
            streamWriteU32(data, (uint32_t)metadata->alternateOffsetN[c]);
        }
    } else {
        streamWriteU32(data, metadata->baseHdrHeadroomN);
        streamWriteU32(data, metadata->baseHdrHeadroomD);
        streamWriteU32(data, metadata->alternateHdrHeadroomN);
        streamWriteU32(data, metadata->alternateHdrHeadroomD);
        for (int c = 0; c < channelCount; ++c) {
            streamWriteU32(data, (uint32_t)metadata->gainMapMinN[c]);
            streamWriteU32(data, metadata->gainMapMinD[c]);
            streamWriteU32(data, (uint32_t)metadata->gainMapMaxN[c]);
            streamWriteU32(data, metadata->gainMapMaxD[c]);
            streamWriteU32(data, metadata->gainMapGammaN[c]);
            streamWriteU32(data, metadata->gainMapGammaD[c]);
            streamWriteU32(data, (uint32_t)metadata->baseOffsetN[c]);
            streamWriteU32(data, metadata->baseOffsetD[c]);
            streamWriteU32(data, (uint32_t)metadata->alternateOffsetN[c]);
            streamWriteU32(data, metadata->alternateOffsetD[c]);
        }
    }

  return Error::Ok;
}

Error GainMapMetadata::parse_gain_map_metadata(const std::vector<uint8_t> &data,
                                               GainMapMetadata* metadata) {
  if (metadata == nullptr) {
    return Error(heif_error_Usage_error, heif_suberror_Null_pointer_argument);
  }
  size_t pos = 0;

  uint8_t version = 0xff;
  streamReadU8(data, version, pos);
  if (version != 0) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Unsupported_data_version,
                 "Box[tmap] has unsupported version");
  }

  uint8_t flags = 0xff;
  streamReadU8(data, flags, pos);
  uint8_t channelCount = (flags & 1) * 2 + 1;
  if (!(channelCount == 1 || channelCount == 3)) {
    return Error(heif_error_Invalid_input,
                 heif_suberror_Unsupported_data_version,
                 "Gain map image must have either 1 or 3 channels");
  }
  metadata->useBaseColorSpace = (flags & 2) != 0;
  metadata->backwardDirection = (flags & 4) != 0;
  const bool useCommonDenominator = (flags & 8) != 0;

  if (useCommonDenominator) {
    uint32_t commonDenominator;
    streamReadU32(data, commonDenominator, pos);

    streamReadU32(data, metadata->baseHdrHeadroomN, pos);
    metadata->baseHdrHeadroomD = commonDenominator;
    streamReadU32(data, metadata->alternateHdrHeadroomN, pos);
    metadata->alternateHdrHeadroomD = commonDenominator;

    for (int c = 0; c < channelCount; ++c) {
      streamReadU32(data, metadata->gainMapMinN[c], pos);
      metadata->gainMapMinD[c] = commonDenominator;
      streamReadU32(data, metadata->gainMapMaxN[c], pos);
      metadata->gainMapMaxD[c] = commonDenominator;
      streamReadU32(data, metadata->gainMapGammaN[c], pos);
      metadata->gainMapGammaD[c] = commonDenominator;
      streamReadU32(data, metadata->baseOffsetN[c], pos);
      metadata->baseOffsetD[c] = commonDenominator;
      streamReadU32(data, metadata->alternateOffsetN[c], pos);
      metadata->alternateOffsetD[c] = commonDenominator;
    }
  } else {
    streamReadU32(data, metadata->baseHdrHeadroomN, pos);
    streamReadU32(data, metadata->baseHdrHeadroomD, pos);
    streamReadU32(data, metadata->alternateHdrHeadroomN, pos);
    streamReadU32(data, metadata->alternateHdrHeadroomD, pos);
    for (int c = 0; c < channelCount; ++c) {
      streamReadU32(data, metadata->gainMapMinN[c], pos);
      streamReadU32(data, metadata->gainMapMinD[c], pos);
      streamReadU32(data, metadata->gainMapMaxN[c], pos);
      streamReadU32(data, metadata->gainMapMaxD[c], pos);
      streamReadU32(data, metadata->gainMapGammaN[c], pos);
      streamReadU32(data, metadata->gainMapGammaD[c], pos);
      streamReadU32(data, metadata->baseOffsetN[c], pos);
      streamReadU32(data, metadata->baseOffsetD[c], pos);
      streamReadU32(data, metadata->alternateOffsetN[c], pos);
      streamReadU32(data, metadata->alternateOffsetD[c], pos);
    }
  }
    // Fill the remaining values by copying those from the first channel.
    for (int c = channelCount; c < 3; ++c) {
      metadata->gainMapMinN[c] = metadata->gainMapMinN[0];
      metadata->gainMapMinD[c] = metadata->gainMapMinD[0];
      metadata->gainMapMaxN[c] = metadata->gainMapMaxN[0];
      metadata->gainMapMaxD[c] = metadata->gainMapMaxD[0];
      metadata->gainMapGammaN[c] = metadata->gainMapGammaN[0];
      metadata->gainMapGammaD[c] = metadata->gainMapGammaD[0];
      metadata->baseOffsetN[c] = metadata->baseOffsetN[0];
      metadata->baseOffsetD[c] = metadata->baseOffsetD[0];
      metadata->alternateOffsetN[c] = metadata->alternateOffsetN[0];
      metadata->alternateOffsetD[c] = metadata->alternateOffsetD[0];
    }

  return Error::Ok;
}