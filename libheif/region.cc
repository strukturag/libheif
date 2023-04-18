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

#include "region.h"
#include "error.h"
#include "heif_file.h"
#include "box.h"
#include <utility>


Error RegionItem::parse(const std::vector<uint8_t>& data)
{
  if (data.size() < 8) {
    return Error(heif_error_Invalid_input, heif_suberror_Invalid_region_data,
                 "Less than 8 bytes of data");
  }

  uint8_t version = data[0];
  (void) version; // version is unused

  uint8_t flags = data[1];
  int field_size = ((flags & 1) ? 32 : 16);

  unsigned int dataOffset;
  if (field_size == 32) {
    if (data.size() < 12) {
      return Error(heif_error_Invalid_input, heif_suberror_Invalid_region_data,
                   "Region data incomplete");
    }
    reference_width =
        ((data[2] << 24) | (data[3] << 16) | (data[4] << 8) | (data[5]));

    reference_height =
        ((data[6] << 24) | (data[7] << 16) | (data[8] << 8) | (data[9]));
    dataOffset = 10;
  }
  else {
    reference_width = ((data[2] << 8) | (data[3]));
    reference_height = ((data[4] << 8) | (data[5]));
    dataOffset = 6;
  }

  uint8_t region_count = data[dataOffset];
  dataOffset += 1;
  for (int i = 0; i < region_count; i++) {
    if (data.size() <= dataOffset) {
      return Error(heif_error_Invalid_input, heif_suberror_Invalid_region_data,
                   "Region data incomplete");
    }

    uint8_t geometry_type = data[dataOffset];
    dataOffset += 1;

    std::shared_ptr<RegionGeometry> region;

    if (geometry_type == 0) {
      region = std::make_shared<RegionGeometry_Point>();
    }
    else if (geometry_type == 1) {
      region = std::make_shared<RegionGeometry_Rectangle>();
    }
    else if (geometry_type == 2) {
      region = std::make_shared<RegionGeometry_Ellipse>();
    }
    else if (geometry_type == 3) {
      auto polygon = std::make_shared<RegionGeometry_Polygon>();
      polygon->closed = true;
      region = polygon;
    }
    else if (geometry_type == 6) {
      auto polygon = std::make_shared<RegionGeometry_Polygon>();
      polygon->closed = false;
      region = polygon;
    }
    else {
      //     // TODO: this isn't going to work - we can only exit here.
      //   std::cout << "ignoring unsupported region geometry type: "
      //             << (int)geometry_type << std::endl;

      continue;
    }

    Error error = region->parse(data, field_size, &dataOffset);
    if (error) {
      return error;
    }

    mRegions.push_back(region);
  }
  return Error::Ok;
}

uint32_t RegionGeometry::parse_unsigned(const std::vector<uint8_t>& data,
                                        int field_size,
                                        unsigned int* dataOffset)
{
  uint32_t x;
  if (field_size == 32) {
    x = ((data[*dataOffset] << 24) | (data[*dataOffset + 1] << 16) |
         (data[*dataOffset + 2] << 8) | (data[*dataOffset + 3]));
    *dataOffset = *dataOffset + 4;
  }
  else {
    x = ((data[*dataOffset] << 8) | (data[*dataOffset + 1]));
    *dataOffset = *dataOffset + 2;
  }
  return x;
}

int32_t RegionGeometry::parse_signed(const std::vector<uint8_t>& data,
                                     int field_size,
                                     unsigned int* dataOffset)
{
  // TODO: fix this for negative values
  int32_t x;
  if (field_size == 32) {
    x = ((data[*dataOffset] << 24) | (data[*dataOffset + 1] << 16) |
         (data[*dataOffset + 2] << 8) | (data[*dataOffset + 3]));
    *dataOffset = *dataOffset + 4;
  }
  else {
    x = ((data[*dataOffset] << 8) | (data[*dataOffset + 1]));
    *dataOffset = *dataOffset + 2;
  }
  return x;
}

Error RegionGeometry_Point::parse(const std::vector<uint8_t>& data,
                                  int field_size,
                                  unsigned int* dataOffset)
{
  unsigned int bytesRequired = (field_size / 8) * 2;
  if (data.size() - *dataOffset < bytesRequired) {
    return Error(heif_error_Invalid_input, heif_suberror_Invalid_region_data,
                 "Insufficient data remaining for point region");
  }
  x = parse_signed(data, field_size, dataOffset);
  y = parse_signed(data, field_size, dataOffset);

  return Error::Ok;
}

Error RegionGeometry_Rectangle::parse(const std::vector<uint8_t>& data,
                                      int field_size,
                                      unsigned int* dataOffset)
{
  unsigned int bytesRequired = (field_size / 8) * 4;
  if (data.size() - *dataOffset < bytesRequired) {
    return Error(heif_error_Invalid_input, heif_suberror_Invalid_region_data,
                 "Insufficient data remaining for rectangle region");
  }
  x = parse_signed(data, field_size, dataOffset);
  y = parse_signed(data, field_size, dataOffset);
  width = parse_unsigned(data, field_size, dataOffset);
  height = parse_unsigned(data, field_size, dataOffset);
  return Error::Ok;
}


Error RegionGeometry_Ellipse::parse(const std::vector<uint8_t>& data,
                                    int field_size,
                                    unsigned int* dataOffset)
{
  unsigned int bytesRequired = (field_size / 8) * 4;
  if (data.size() - *dataOffset < bytesRequired) {
    return Error(heif_error_Invalid_input, heif_suberror_Invalid_region_data,
                 "Insufficient data remaining for ellipse region");
  }
  x = parse_signed(data, field_size, dataOffset);
  y = parse_signed(data, field_size, dataOffset);
  radius_x = parse_unsigned(data, field_size, dataOffset);
  radius_y = parse_unsigned(data, field_size, dataOffset);
  return Error::Ok;
}


Error RegionGeometry_Polygon::parse(const std::vector<uint8_t>& data,
                                    int field_size,
                                    unsigned int* dataOffset)
{
  unsigned int bytesRequired1 = (field_size / 8) * 1;
  if (data.size() - *dataOffset < bytesRequired1) {
    return Error(heif_error_Invalid_input, heif_suberror_Invalid_region_data,
                 "Insufficient data remaining for polygon");
  }

  uint32_t numPoints = parse_unsigned(data, field_size, dataOffset);
  unsigned int bytesRequired2 = (field_size / 8) * numPoints * 2;
  if (data.size() - *dataOffset < bytesRequired2) {
    return Error(heif_error_Invalid_input, heif_suberror_Invalid_region_data,
                 "Insufficient data remaining for polygon");
  }

  for (uint32_t i = 0; i < numPoints; i++) {
    Point p;
    p.x = parse_signed(data, field_size, dataOffset);
    p.y = parse_signed(data, field_size, dataOffset);
    points.push_back(p);
  }

  return Error::Ok;
}


RegionCoordinateTransform RegionCoordinateTransform::create(std::shared_ptr<heif::HeifFile> file,
                                                            heif_item_id item_id,
                                                            int reference_width, int reference_height)
{
  std::vector<heif::Box_ipco::Property> properties;

  Error err = file->get_properties(item_id, properties);
  if (err) {
    return {};
  }

  int image_width = 0, image_height = 0;

  for (auto& property : properties) {
    if (property.property->get_short_type() == fourcc("ispe")) {
      auto ispe = std::dynamic_pointer_cast<heif::Box_ispe>(property.property);
      image_width = ispe->get_width();
      image_height = ispe->get_height();
      break;
    }
  }

  if (image_width == 0 || image_height == 0) {
    return {};
  }

  RegionCoordinateTransform transform;
  transform.a = image_width / (double)reference_width;
  transform.d = image_height / (double)reference_height;

  for (auto& property : properties) {
    switch (property.property->get_short_type()) {
      case fourcc("imir"): {
        auto imir = std::dynamic_pointer_cast<heif::Box_imir>(property.property);
        if (imir->get_mirror_direction() == heif_transform_mirror_direction_horizontal) {
          transform.a = -transform.a;
          transform.b = -transform.b;
          transform.tx = image_width - 1 - transform.tx;
        }
        else {
          transform.c = -transform.c;
          transform.d = -transform.d;
          transform.ty = image_height - 1 - transform.ty;
        }
        break;
      }
      case fourcc("irot"): {
        auto irot = std::dynamic_pointer_cast<heif::Box_irot>(property.property);
        RegionCoordinateTransform tmp;
        switch (irot->get_rotation()) {
          case 90:
            tmp.a = transform.c;
            tmp.b = transform.d;
            tmp.c = -transform.a;
            tmp.d = -transform.b;
            tmp.tx = transform.ty;
            tmp.ty = -transform.tx + image_width - 1;
            transform = tmp;
            std::swap(image_width, image_height);
            break;
          case 180:
            transform.a = -transform.a;
            transform.b = -transform.b;
            transform.tx = image_width - 1 - transform.tx;
            transform.c = -transform.c;
            transform.d = -transform.d;
            transform.ty = image_height - 1 - transform.ty;
            break;
          case 270:
            tmp.a = -transform.c;
            tmp.b = -transform.d;
            tmp.c = transform.a;
            tmp.d = transform.b;
            tmp.tx = -transform.ty + image_height - 1;
            tmp.ty = transform.tx;
            transform = tmp;
            std::swap(image_width, image_height);
            break;
          default:
            break;
        }
        break;
      }
      case fourcc("clap"): {
        auto clap = std::dynamic_pointer_cast<heif::Box_clap>(property.property);
        int left = clap->left_rounded(image_width);
        int top = clap->top_rounded(image_height);
        transform.tx -= left;
        transform.ty -= top;
        image_width = clap->get_width_rounded();
        image_height = clap->get_height_rounded();
        break;
      }
      default:
        break;
    }
  }

  return transform;
}


RegionCoordinateTransform::Point RegionCoordinateTransform::transform_point(Point p)
{
  Point newp;
  newp.x = p.x * a + p.x * b + tx;
  newp.y = p.x * c + p.y * d + ty;
  return newp;
}


RegionCoordinateTransform::Extent RegionCoordinateTransform::transform_extent(Extent e)
{
  Extent newe;
  newe.x = e.x * a + e.y * b;
  newe.y = e.x * c + e.y * d;
  return newe;
}
