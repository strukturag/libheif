//
// Created by farindk on 01.07.20.
//

#include "nclx.h"


heif::primaries::primaries(float gx,float gy, float bx,float by, float rx,float ry, float wx,float wy)
{
  defined = true;
  redX = rx;
  redY = ry;
  greenX = gx;
  greenY = gy;
  blueX = bx;
  blueY = by;
  whiteX = wx;
  whiteY = wy;
}


heif::primaries heif::get_colour_primaries(uint16_t primaries_idx)
{
  switch (primaries_idx) {
    case 1:
      return {0.300f, 0.600f, 0.150f, 0.060f, 0.640f, 0.330f, 0.3127f, 0.3290f};
    case 4:
      return {0.21f, 0.71f, 0.14f, 0.08f, 0.67f, 0.33f, 0.310f, 0.316f};
    case 5:
      return {0.29f, 0.60f, 0.15f, 0.06f, 0.64f, 0.33f, 0.3127f, 0.3290f};
    case 6:
    case 7:
      return {0.310f, 0.595f, 0.155f, 0.070f, 0.630f, 0.340f, 0.3127f, 0.3290f};
    case 8:
      return {0.243f, 0.692f, 0.145f, 0.049f, 0.681f, 0.319f, 0.310f, 0.316f};
    case 9:
      return {0.170f, 0.797f, 0.131f, 0.046f, 0.708f, 0.292f, 0.3127f, 0.3290f};
    case 10:
      return {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.333333f, 0.33333f};
    case 11:
      return {0.265f, 0.690f, 0.150f, 0.060f, 0.680f, 0.320f, 0.314f, 0.351f};
    case 12:
      return {0.265f, 0.690f, 0.150f, 0.060f, 0.680f, 0.320f, 0.3127f, 0.3290f};
    case 22:
      return {0.295f, 0.605f, 0.155f, 0.077f, 0.630f, 0.340f, 0.3127f, 0.3290f};
    default:
      return {};
  }
}
