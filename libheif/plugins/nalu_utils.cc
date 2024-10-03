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

#include <map>
#include <memory>
#include <utility>
#include "nalu_utils.h"
#include <cstring>
#include <cstdlib>

NalUnit::NalUnit()
{
    nal_data_ptr = NULL;
    nal_unit_type = 0;
    nal_data_size = 0;
}

bool NalUnit::set_data(const unsigned char* in_data, int n)
{
    nal_data_ptr = in_data;
    nal_unit_type = bitExtracted(nal_data_ptr[0], 6, 2);
    nal_data_size = n;
    return true;
}

int NalUnit::bitExtracted(int number, int bits_count, int position_nr)
{
    return (((1 << bits_count) - 1) & (number >> (position_nr - 1)));
}

size_t NalMap::count(int nal_type)
{
    return map.count(nal_type);
}

const unsigned char* NalMap::data(int nal_type) 
{
    return map[nal_type]->data();
}

int NalMap::size(int nal_type) 
{
    return map[nal_type]->size();
}

const heif_error NalMap::parseHevcNalu(const uint8_t *cdata, size_t size)
{
    size_t ptr = 0;
    while (ptr < size)
    {
        if (4 > size - ptr)
        {
            struct heif_error err = {heif_error_Decoder_plugin_error,
                                    heif_suberror_End_of_data,
                                    "insufficient data"};
            return err;
        }

        uint32_t nal_size = (cdata[ptr] << 24) | (cdata[ptr + 1] << 16) | (cdata[ptr + 2] << 8) | (cdata[ptr + 3]);
        ptr += 4;

        if (nal_size > size - ptr)
        {
            struct heif_error err = {heif_error_Decoder_plugin_error,
                                    heif_suberror_End_of_data,
                                    "insufficient data"};
            return err;
        }

        std::unique_ptr<NalUnit> nal_unit = std::unique_ptr<NalUnit>(new NalUnit());
        nal_unit->set_data(cdata + ptr, nal_size);

        // overwrite NalMap (frees old NalUnit, if it was set)
        map[nal_unit->unit_type()] = std::move(nal_unit);

        ptr += nal_size;
    }

    return heif_error_success;
}

heif_error NalMap::buildWithStartCodesHevc(uint8_t **hevc_data, size_t *hevc_data_size, size_t additional_pad_size)
{
    int heif_idrpic_size;
    int heif_vps_size;
    int heif_sps_size;
    int heif_pps_size;
    const unsigned char* heif_vps_data;
    const unsigned char* heif_sps_data;
    const unsigned char* heif_pps_data;
    const unsigned char* heif_idrpic_data;

    if ((count(NAL_UNIT_VPS_NUT) > 0) && (count(NAL_UNIT_SPS_NUT) > 0) && (count(NAL_UNIT_PPS_NUT) > 0))
    {
        heif_vps_size = size(NAL_UNIT_VPS_NUT);
        heif_vps_data = data(NAL_UNIT_VPS_NUT);

        heif_sps_size = size(NAL_UNIT_SPS_NUT);
        heif_sps_data = data(NAL_UNIT_SPS_NUT);

        heif_pps_size = size(NAL_UNIT_PPS_NUT);
        heif_pps_data = data(NAL_UNIT_PPS_NUT);
    }
    else
    {
        struct heif_error err = { heif_error_Decoder_plugin_error,
                                    heif_suberror_End_of_data,
                                    "Unexpected end of data" };
        return err;
    }

    if ((count(NAL_UNIT_IDR_W_RADL) > 0) || (count(NAL_UNIT_IDR_N_LP) > 0))
    {
        if (count(NAL_UNIT_IDR_W_RADL) > 0)
        {
            heif_idrpic_data = data(NAL_UNIT_IDR_W_RADL);
            heif_idrpic_size = size(NAL_UNIT_IDR_W_RADL);
        }
        else
        {
            heif_idrpic_data = data(NAL_UNIT_IDR_N_LP);
            heif_idrpic_size = size(NAL_UNIT_IDR_N_LP);
        }
    }
    else
    {
        struct heif_error err = { heif_error_Decoder_plugin_error,
                                    heif_suberror_End_of_data,
                                    "Unexpected end of data" };
        return err;
    }

    const char hevc_AnnexB_StartCode[] = { 0x00, 0x00, 0x00, 0x01 };
    int hevc_AnnexB_StartCode_size = 4;

    *hevc_data_size = heif_vps_size + heif_sps_size + heif_pps_size + heif_idrpic_size + 4 * hevc_AnnexB_StartCode_size;
    *hevc_data = (uint8_t*)malloc(*hevc_data_size + additional_pad_size);

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

    map.clear();

    return heif_error_success;
}

void NalMap::clear() { map.clear(); }