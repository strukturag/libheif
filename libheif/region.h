/*
 * HEIF codec.
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
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

#ifndef LIBHEIF_REGION_H
#define LIBHEIF_REGION_H

#include <cstdint>
#include <vector>
#include <memory>
#include "heif_image.h"
#include "libheif/heif.h"


namespace heif
{
  class RegionGeometry;


  class RegionItem
  {
  public:
    Error parse(const std::vector<uint8_t>& data);

    long unsigned int get_number_of_regions()
    { return mRegions.size(); }

    std::vector<std::shared_ptr<RegionGeometry>> get_regions()
    { return mRegions; }

    uint32_t item_id;
    uint32_t reference_width;
    uint32_t reference_height;

  private:

    std::vector<std::shared_ptr<RegionGeometry>> mRegions;
  };


  class RegionGeometry
  {
  public:
    virtual ~RegionGeometry() = default;
    virtual heif_region_type getRegionType() = 0;

  protected:
    uint32_t parse_unsigned(const std::vector<uint8_t>& data, int field_size, unsigned int *dataOffset);
    int32_t parse_signed(const std::vector<uint8_t>& data, int field_size, unsigned int *dataOffset);
  };

  class RegionGeometry_Point : public RegionGeometry
  {
  public:
    Error parse(const std::vector<uint8_t>& data, int field_size, unsigned int *dataOffset);

    heif_region_type getRegionType() override
    { return heif_region_type_point; }

    int32_t x,y;
  };

  class RegionGeometry_Rectangle : public RegionGeometry
  {
  public:
    Error parse(const std::vector<uint8_t>& data, int field_size, unsigned int *dataOffset);

    heif_region_type getRegionType() override
    { return heif_region_type_rectangle; }

    int32_t x,y;
    uint32_t width,height;
  };

  class RegionGeometry_Ellipse : public RegionGeometry
  {
  public:
    int32_t x,y;
    uint32_t radius_x, radius_y;
  };

  class RegionGeometry_Polygon : public RegionGeometry
  {
  public:
    struct Point
    {
      int32_t x, y;
    };

    bool closed = true;
    std::vector<Point> points;
  };

  class RegionGeometry_Mask : public RegionGeometry
  {
  public:
    int32_t x,y;
    uint32_t width, height;

    // The mask may be decoded lazily on-the-fly.
    std::shared_ptr<heif::HeifPixelImage> get_mask() const { return {}; } // TODO

  private:
    enum class EncodingMethod {
      Inline, Referenced
    } mEncodingMethod;

    std::shared_ptr<heif::HeifPixelImage> mCachedMask;
  };
}

#endif //LIBHEIF_REGION_H
