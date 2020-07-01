//
// Created by farindk on 01.07.20.
//

#ifndef LIBHEIF_NCLX_H
#define LIBHEIF_NCLX_H

#include <cinttypes>

namespace heif {

  struct primaries {
    primaries() = default;
    primaries(float gx,float gy, float bx,float by, float rx,float ry, float wx,float wy);

    bool defined=false;

    float greenX,greenY;
    float blueX,blueY;
    float redX,redY;
    float whiteX,whiteY;
  };

  primaries get_colour_primaries(uint16_t primaries_idx);

//  uint16_t get_transfer_characteristics() const {return m_transfer_characteristics;}
// uint16_t get_matrix_coefficients() const {return m_matrix_coefficients;}
//  bool get_full_range_flag() const {return m_full_range_flag;}
}


#endif //LIBHEIF_NCLX_H
