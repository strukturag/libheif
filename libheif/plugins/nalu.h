/*
 * HEIF codec NALU support functionality.
 *
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

#ifndef LIBHEIF_PLUGIN_NALU_H
#define LIBHEIF_PLUGIN_NALU_H

#include <map>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "libheif/heif.h"

class NalUnit
{
public:

    virtual ~NalUnit() {};

    int size() const
    {
        return nal_data_size;
    }

    int unit_type() const
    {
        return nal_unit_type;
    }

    const unsigned char *data() const
    {
        return nal_data_ptr;
    }

protected:
    virtual bool set_data(const unsigned char *in_data, int n) = 0;
    int bitExtracted(int number, int bits_count, int position_nr);

    const unsigned char *nal_data_ptr;

    int nal_unit_type;

    int nal_data_size;

};

class NalUnitHEVC: public NalUnit
{
public:
    NalUnitHEVC(const unsigned char *in_data, int n);

    ~NalUnitHEVC() {};

    /**
     * Coded slice segment of an IDR picture.
     *
     * See ISO/IEC 23008-2:2020 Table 7-1 and Section 7.3.
     */
    static const int IDR_W_RADL;

    /**
     * Coded slice segment of an IDR picture.
     *
     * See ISO/IEC 23008-2:2020 Table 7-1 and Section 7.3.
     */
    static const int IDR_N_LP;

    /**
     * Video Parameter Set (VPS) NAL Unit Type.
     *
     * See ISO/IEC 23008-2:2020 Table 7-1 and Section 7.3.2.1.
     */
    static const int VPS_NUT;

    /**
     * Sequence Parameter Set (SPS) NAL Unit Type.
     *
     * See ISO/IEC 23008-2:2020 Table 7-1 and Section 7.3.2.2.
     */
    static const int SPS_NUT;

    /**
     * Picture Parameter Set (VPS) NAL Unit Type.
     *
     * See ISO/IEC 23008-2:2020 Table 7-1 and Section 7.3.2.3.
     */
    static const int PPS_NUT;

protected:
    bool set_data(const unsigned char *in_data, int n);
};

class NalUnitAVC: public NalUnit
{
public:
    NalUnitAVC(const unsigned char *in_data, int n);

    ~NalUnitAVC() {};

protected:
    bool set_data(const unsigned char *in_data, int n);
};

class NalUnitMap
{
public:
    heif_error parseNALU_HEVC(const uint8_t* data, size_t data_len);

    heif_error parseNALU_AVC(const uint8_t* data, size_t data_len);

    bool IDR_is_valid();

    bool NUTs_are_valid();

    void clear();

    void buildWithStartCodesHEVC(uint8_t** hevc_data, size_t *hevc_data_size);

    void buildWithStartCodesAVC(uint8_t** avc_data, size_t *avc_data_size);

private:

    const size_t size(int unitType) const;

    const unsigned char* data(int unit_type) const;
    
    std::map<int, const NalUnit*> m_map;
};

#endif
