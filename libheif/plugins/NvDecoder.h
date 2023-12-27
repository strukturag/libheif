/*
 * This copyright notice applies to this header file only:
 *
 * Copyright (c) 2010-2023 NVIDIA Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the software, and to permit persons to whom the
 * software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <assert.h>
#include <cstdint>
#include <vector>
#include "libheif/heif.h"
#include "nvcuvid.h"

struct nvdec_context
{
    std::vector<uint8_t> data;
    int strict;
    cudaVideoCodec eCodec = cudaVideoCodec_NumCodecs;
    CUcontext cuContext = NULL;
    CUvideoctxlock ctxLock;
    CUstream cuvidStream = 0;
    CUvideoparser hParser = NULL;
    CUvideodecoder hDecoder = NULL;
};


/**
* @brief Base class for decoder interface.
*/
class NvDecoder {

public:
    /**
    *  @brief This function is used to initialize the decoder session.
    *  Application must call this function to initialize the decoder, before
    *  starting to decode any frames.
    */
    NvDecoder(nvdec_context *ctx);
    ~NvDecoder();

    /**
    *  @brief  This function is used to get the output frame width.
    *  NV12/P016 output format width is 2 byte aligned because of U and V interleave
    */
    int GetWidth() { assert(m_nWidth); return (m_eOutputFormat == cudaVideoSurfaceFormat_NV12 || m_eOutputFormat == cudaVideoSurfaceFormat_P016) 
                                                ? (m_nWidth + 1) & ~1 : m_nWidth; }

    /**
    *  @brief  This function is used to get the actual decode width
    */
    int GetDecodeWidth() { assert(m_nWidth); return m_nWidth; }

    /**
    *  @brief  This function is used to get the output frame height (Luma height).
    */
    int GetHeight() { assert(m_nLumaHeight); return m_nLumaHeight; }

    /**
    *  @brief  This function is used to get the current chroma height.
    */
    int GetChromaHeight() { assert(m_nChromaHeight); return m_nChromaHeight; }

    /**
    *  @brief  This function is used to get the number of chroma planes.
    */
    int GetNumChromaPlanes() { assert(m_nNumChromaPlanes); return m_nNumChromaPlanes; }
    
    /**
    *   @brief  This function is used to get the current frame size based on pixel format.
    */
    int GetFrameSize() { return GetWidth() * (m_nLumaHeight + (m_nChromaHeight * m_nNumChromaPlanes)) * m_nBPP; }

    /**
    *   @brief  This function is used to get the current frame Luma plane size.
    */
    int GetLumaPlaneSize() { return GetWidth() * m_nLumaHeight * m_nBPP; }

    /**
    *   @brief  This function is used to get the current frame chroma plane size.
    */
    int GetChromaPlaneSize() { return GetWidth() *  (m_nChromaHeight * m_nNumChromaPlanes) * m_nBPP; }

    /**
    *   @brief  This function is used to get the bit depth associated with the pixel format.
    */
    int GetBitDepth() { return m_nBitDepthMinus8 + 8; }

    /**
    *   @brief  This function is used to get the bytes used per pixel.
    */
    int GetBPP() { return m_nBPP; }

    /**
    *   @brief  This function decodes a frame and returns the number of frames that are available for
    *   display. All frames that are available for display should be read before making a subsequent decode call.
    *   @param  pData - pointer to the data buffer that is to be decoded
    *   @param  nSize - size of the data buffer in bytes
    */
    int Decode(const uint8_t *pData, size_t nSize);

    /**
    *   @brief  This function returns a decoded frame. This function should be called in a loop for
    *   fetching all the frames that are available for display.
    */
    uint8_t* GetFrame();

    /**
    *   @brief  This function allows app to set operating point for AV1 SVC clips
    *   @param  opPoint - operating point of an AV1 scalable bitstream
    *   @param  bDispAllLayers - Output all decoded frames of an AV1 scalable bitstream
    */
    void SetOperatingPoint(const uint32_t opPoint, const bool bDispAllLayers) { m_nOperatingPoint = opPoint; m_bDispAllLayers = bDispAllLayers; }

    heif_error initVideoParser();
private:

    /**
    *   @brief  Callback function to be registered for getting a callback when decoding of sequence starts
    */
    static int CUDAAPI HandleVideoSequenceProc(void *pUserData, CUVIDEOFORMAT *pVideoFormat) { return ((NvDecoder *)pUserData)->HandleVideoSequence(pVideoFormat); }

    /**
    *   @brief  Callback function to be registered for getting a callback when a decoded frame is ready to be decoded
    */
    static int CUDAAPI HandlePictureDecodeProc(void *pUserData, CUVIDPICPARAMS *pPicParams) { return ((NvDecoder *)pUserData)->HandlePictureDecode(pPicParams); }

    /**
    *   @brief  Callback function to be registered for getting a callback to get operating point when AV1 SVC sequence header start.
    */
    static int CUDAAPI HandleOperatingPointProc(void *pUserData, CUVIDOPERATINGPOINTINFO *pOPInfo) { return ((NvDecoder *)pUserData)->GetOperatingPoint(pOPInfo); }

    /**
    *   @brief  This function gets called when a sequence is ready to be decoded. The function also gets called
        when there is format change
    */
    int HandleVideoSequence(CUVIDEOFORMAT *pVideoFormat);

    /**
    *   @brief  This function gets called when a picture is ready to be decoded. cuvidDecodePicture is called from this function
    *   to decode the picture
    */
    int HandlePictureDecode(CUVIDPICPARAMS *pPicParams);

    /**
    *   @brief  This function gets called after a picture is decoded and available for display. Frames are fetched and stored in 
        internal buffer
    */
    int HandlePictureDisplay(CUVIDPARSERDISPINFO *pDispInfo);

    /**
    *   @brief  This function gets called when AV1 sequence encounter more than one operating points
    */
    int GetOperatingPoint(CUVIDOPERATINGPOINTINFO *pOPInfo);

private:
    // dimension of the output
    unsigned int m_nWidth = 0, m_nLumaHeight = 0, m_nChromaHeight = 0;
    unsigned int m_nNumChromaPlanes = 0;
    // height of the mapped surface 
    int m_nSurfaceHeight = 0;
    cudaVideoSurfaceFormat m_eOutputFormat = cudaVideoSurfaceFormat_NV12;
    int m_nBitDepthMinus8 = 0;
    int m_nBPP = 1;
    uint8_t * dstFrame;

    unsigned int m_nOperatingPoint = 0;
    bool  m_bDispAllLayers = false;
    nvdec_context *m_ctx;
};
