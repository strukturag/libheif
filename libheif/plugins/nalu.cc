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

#include "nalu.h"

NalUnitHEVC::NalUnitHEVC(const unsigned char* in_data, int n)
{
    set_data(in_data, n);
}

bool NalUnitHEVC::set_data(const unsigned char* in_data, int n)
{
    nal_data_ptr = in_data;
    nal_unit_type = bitExtracted(nal_data_ptr[0], 6, 2);
    nal_data_size = n;
    return true;
}

NalUnitAVC::NalUnitAVC(const unsigned char* in_data, int n)
{
    set_data(in_data, n);
}

bool NalUnitAVC::set_data(const unsigned char* in_data, int n)
{
    nal_data_ptr = in_data;
    nal_unit_type = bitExtracted(nal_data_ptr[0], 5, 1);
    nal_data_size = n;
    return true;
}

int NalUnit::bitExtracted(int number, int bits_count, int position_nr)
{
    return (((1 << bits_count) - 1) & (number >> (position_nr - 1)));
}

const int NalUnitHEVC::IDR_W_RADL = 19;

const int NalUnitHEVC::IDR_N_LP = 20;

const int NalUnitHEVC::VPS_NUT = 32;

const int NalUnitHEVC::SPS_NUT = 33;

const int NalUnitHEVC::PPS_NUT = 34;

heif_error NalUnitMap::parseNALU_AVC(const uint8_t* data, size_t data_len)
{
    struct heif_error err = { heif_error_Decoder_plugin_error,
                              heif_suberror_End_of_data,
                              "this isn't implemented yet" };
    return err;
}

heif_error NalUnitMap::parseNALU_HEVC(const uint8_t* data, size_t data_len)
{
    size_t ptr = 0;
    while (ptr < data_len) {
        if (4 > data_len - ptr) {
            struct heif_error err = { heif_error_Decoder_plugin_error,
                                    heif_suberror_End_of_data,
                                    "insufficient data" };
            return err;
        }

        uint32_t nal_size = (data[ptr] << 24) | (data[ptr + 1] << 16) | (data[ptr + 2] << 8) | (data[ptr + 3]);
        ptr += 4;

        if (nal_size > data_len - ptr) {
            struct heif_error err = { heif_error_Decoder_plugin_error,
                                    heif_suberror_End_of_data,
                                    "insufficient data" };
            return err;
        }

        NalUnit* nal_unit = new NalUnitHEVC(data + ptr, nal_size);
        m_map[nal_unit->unit_type()] = nal_unit;

        ptr += nal_size;
    }
    return heif_error_success;
}

bool NalUnitMap::IDR_is_valid()
{
    return ((m_map.count(NalUnitHEVC::IDR_W_RADL) > 0) || (m_map.count(NalUnitHEVC::IDR_N_LP) > 0));
}

bool NalUnitMap::NUTs_are_valid()
{
    return ((m_map.count(NalUnitHEVC::VPS_NUT) > 0)
        && (m_map.count(NalUnitHEVC::SPS_NUT) > 0)
        && (m_map.count(NalUnitHEVC::PPS_NUT) > 0));
}

void NalUnitMap::clear()
{
    for (auto current = m_map.begin(); current != m_map.end(); ++current) {
            delete current->second;
        }
    m_map.clear();
}

void NalUnitMap::buildWithStartCodesHEVC(uint8_t** hevc_data, size_t *hevc_data_size)
{

    size_t heif_vps_size = size(NalUnitHEVC::VPS_NUT);
    const unsigned char* heif_vps_data = data(NalUnitHEVC::VPS_NUT);

    size_t heif_sps_size = size(NalUnitHEVC::SPS_NUT);
    const unsigned char* heif_sps_data = data(NalUnitHEVC::SPS_NUT);

    size_t heif_pps_size = size(NalUnitHEVC::PPS_NUT);
    const unsigned char* heif_pps_data = data(NalUnitHEVC::PPS_NUT);

    size_t heif_idrpic_size;
    const unsigned char* heif_idrpic_data;

    if (m_map.count(NalUnitHEVC::IDR_W_RADL) > 0) {
        heif_idrpic_data = data(NalUnitHEVC::IDR_W_RADL);
        heif_idrpic_size = size(NalUnitHEVC::IDR_W_RADL);
    } else {
        heif_idrpic_data = data(NalUnitHEVC::IDR_N_LP);
        heif_idrpic_size = size(NalUnitHEVC::IDR_N_LP);
    }

    const char hevc_AnnexB_StartCode[] = { 0x00, 0x00, 0x00, 0x01 };
    int hevc_AnnexB_StartCode_size = 4;

    *hevc_data_size = heif_vps_size + heif_sps_size + heif_pps_size + heif_idrpic_size + 4 * hevc_AnnexB_StartCode_size;
    *hevc_data = (uint8_t*)malloc(*hevc_data_size);

    //Copy hevc pps data
    uint8_t* hevc_data_ptr = *hevc_data;
    memcpy(hevc_data_ptr, hevc_AnnexB_StartCode, hevc_AnnexB_StartCode_size);
    hevc_data_ptr += hevc_AnnexB_StartCode_size;
    memcpy(hevc_data_ptr, heif_vps_data, heif_vps_size);
    hevc_data_ptr += heif_vps_size;

    //Copy hevc sps data
    memcpy(hevc_data_ptr, hevc_AnnexB_StartCode, hevc_AnnexB_StartCode_size);
    hevc_data_ptr += hevc_AnnexB_StartCode_size;
    memcpy(hevc_data_ptr, heif_sps_data, heif_sps_size);
    hevc_data_ptr += heif_sps_size;

    //Copy hevc pps data
    memcpy(hevc_data_ptr, hevc_AnnexB_StartCode, hevc_AnnexB_StartCode_size);
    hevc_data_ptr += hevc_AnnexB_StartCode_size;
    memcpy(hevc_data_ptr, heif_pps_data, heif_pps_size);
    hevc_data_ptr += heif_pps_size;

    //Copy hevc idrpic data
    memcpy(hevc_data_ptr, hevc_AnnexB_StartCode, hevc_AnnexB_StartCode_size);
    hevc_data_ptr += hevc_AnnexB_StartCode_size;
    memcpy(hevc_data_ptr, heif_idrpic_data, heif_idrpic_size);
}

void NalUnitMap::buildWithStartCodesAVC(uint8_t** avc_data, size_t *avc_data_size)
{
    size_t idr_size = size(5);
    const unsigned char* idr_data = data(5);

    size_t sei_size = size(6);
    const unsigned char* sei_data = data(6);

    const char avc_AnnexB_StartCode[] = { 0x00, 0x00, 0x00, 0x01 };
    int avc_AnnexB_StartCode_size = 4;

    *avc_data_size = idr_size + sei_size + 2 * avc_AnnexB_StartCode_size;
    *avc_data = (uint8_t*)malloc(*avc_data_size);

    uint8_t* avc_data_ptr = *avc_data;
    memcpy(avc_data_ptr, avc_AnnexB_StartCode, avc_AnnexB_StartCode_size);
    avc_data_ptr += avc_AnnexB_StartCode_size;
    memcpy(avc_data_ptr, idr_data, idr_size);
    avc_data_ptr += idr_size;

    memcpy(avc_data_ptr, avc_AnnexB_StartCode, avc_AnnexB_StartCode_size);
    avc_data_ptr += avc_AnnexB_StartCode_size;
    memcpy(avc_data_ptr, sei_data, sei_size);
}

const size_t NalUnitMap::size(int unitType) const
{
    return m_map.at(unitType)->size();
}

const unsigned char* NalUnitMap::data(int unit_type) const
{
    return m_map.at(unit_type)->data();
}