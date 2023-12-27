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

// for std::cout and friends
#include <iostream>

// for ceil()
#include <cmath>

// for memset, memcpy
#include <cstring>

// TODO: remove this once we dump the errorLog
#include <sstream>

#include "libheif/heif_plugin.h"

#include "NvDecoder.h"

/**
* @brief Exception class for error reporting from the decode API.
*/
class NVDECException : public std::exception
{
public:
    NVDECException(const std::string& errorStr, const CUresult errorCode)
        : m_errorString(errorStr), m_errorCode(errorCode) {}

    virtual ~NVDECException() throw() {}
    virtual const char* what() const throw() { return m_errorString.c_str(); }
    CUresult  getErrorCode() const { return m_errorCode; }
    const std::string& getErrorString() const { return m_errorString; }
    static NVDECException makeNVDECException(const std::string& errorStr, const CUresult errorCode,
        const std::string& functionName, const std::string& fileName, int lineNo);
private:
    std::string m_errorString;
    CUresult m_errorCode;
};

inline NVDECException NVDECException::makeNVDECException(const std::string& errorStr, const CUresult errorCode, const std::string& functionName,
    const std::string& fileName, int lineNo)
{
    std::ostringstream errorLog;
    errorLog << functionName << " : " << errorStr << " at " << fileName << ":" << lineNo << std::endl;
    NVDECException exception(errorLog.str(), errorCode);
    return exception;
}

#define NVDEC_THROW_ERROR( errorStr, errorCode )                                                         \
    do                                                                                                   \
    {                                                                                                    \
        throw NVDECException::makeNVDECException(errorStr, errorCode, __FUNCTION__, __FILE__, __LINE__); \
    } while (0)


#define NVDEC_API_CALL( cuvidAPI )                                                                                 \
    do                                                                                                             \
    {                                                                                                              \
        CUresult errorCode = cuvidAPI;                                                                             \
        if( errorCode != CUDA_SUCCESS)                                                                             \
        {                                                                                                          \
            std::ostringstream errorLog;                                                                           \
            errorLog << #cuvidAPI << " returned error " << errorCode;                                              \
            throw NVDECException::makeNVDECException(errorLog.str(), errorCode, __FUNCTION__, __FILE__, __LINE__); \
        }                                                                                                          \
    } while (0)


#define CUDA_DRVAPI_CALL( call )                                                                                                 \
    do                                                                                                                           \
    {                                                                                                                            \
        CUresult err__ = call;                                                                                                   \
        if (err__ != CUDA_SUCCESS)                                                                                               \
        {                                                                                                                        \
            const char *szErrName = NULL;                                                                                        \
            cuGetErrorName(err__, &szErrName);                                                                                   \
            std::ostringstream errorLog;                                                                                         \
            errorLog << "CUDA driver API error " << szErrName ;                                                                  \
            throw NVDECException::makeNVDECException(errorLog.str(), err__, __FUNCTION__, __FILE__, __LINE__);                   \
        }                                                                                                                        \
    }                                                                                                                            \
    while (0)


#ifdef __cuda_cuda_h__
inline bool check(CUresult e, int iLine, const char *szFile) {
    if (e != CUDA_SUCCESS) {
        const char *szErrName = NULL;
        cuGetErrorName(e, &szErrName);
        // LOG(FATAL) << "CUDA driver API error " << szErrName << " at line " << iLine << " in file " << szFile;
        std::cout << "CUDA driver API error " << szErrName << " at line " << iLine << " in file " << szFile << std::endl;
        return false;
    }
    return true;
}
#endif


#define ck(call) check(call, __LINE__, __FILE__)


/**
* @brief Template class to facilitate color space conversion
*/
template<typename T>
class YuvConverter {
public:
    YuvConverter(int nWidth, int nHeight) : nWidth(nWidth), nHeight(nHeight) {
        pQuad = new T[((nWidth + 1) / 2) * ((nHeight + 1) / 2)];
    }
    ~YuvConverter() {
        delete[] pQuad;
    }
    void PlanarToUVInterleaved(T *pFrame, int nPitch = 0) {
        if (nPitch == 0) {
            nPitch = nWidth;
        }

        // sizes of source surface plane
        int nSizePlaneY = nPitch * nHeight;
        int nSizePlaneU = ((nPitch + 1) / 2) * ((nHeight + 1) / 2);
        int nSizePlaneV = nSizePlaneU;

        T *puv = pFrame + nSizePlaneY;
        if (nPitch == nWidth) {
            memcpy(pQuad, puv, nSizePlaneU * sizeof(T));
        } else {
            for (int i = 0; i < (nHeight + 1) / 2; i++) {
                memcpy(pQuad + ((nWidth + 1) / 2) * i, puv + ((nPitch + 1) / 2) * i, ((nWidth + 1) / 2) * sizeof(T));
            }
        }
        T *pv = puv + nSizePlaneU;
        for (int y = 0; y < (nHeight + 1) / 2; y++) {
            for (int x = 0; x < (nWidth + 1) / 2; x++) {
                puv[y * nPitch + x * 2] = pQuad[y * ((nWidth + 1) / 2) + x];
                puv[y * nPitch + x * 2 + 1] = pv[y * ((nPitch + 1) / 2) + x];
            }
        }
    }
    void UVInterleavedToPlanar(T *pFrame, int nPitch = 0) {
        if (nPitch == 0) {
            nPitch = nWidth;
        }

        // sizes of source surface plane
        int nSizePlaneY = nPitch * nHeight;
        int nSizePlaneU = ((nPitch + 1) / 2) * ((nHeight + 1) / 2);
        int nSizePlaneV = nSizePlaneU;

        T *puv = pFrame + nSizePlaneY,
            *pu = puv, 
            *pv = puv + nSizePlaneU;

        // split chroma from interleave to planar
        for (int y = 0; y < (nHeight + 1) / 2; y++) {
            for (int x = 0; x < (nWidth + 1) / 2; x++) {
                pu[y * ((nPitch + 1) / 2) + x] = puv[y * nPitch + x * 2];
                pQuad[y * ((nWidth + 1) / 2) + x] = puv[y * nPitch + x * 2 + 1];
            }
        }
        if (nPitch == nWidth) {
            memcpy(pv, pQuad, nSizePlaneV * sizeof(T));
        } else {
            for (int i = 0; i < (nHeight + 1) / 2; i++) {
                memcpy(pv + ((nPitch + 1) / 2) * i, pQuad + ((nWidth + 1) / 2) * i, ((nWidth + 1) / 2) * sizeof(T));
            }
        }
    }

private:
    T *pQuad;
    int nWidth, nHeight;
};


void ConvertSemiplanarToPlanar(uint8_t *pHostFrame, int nWidth, int nHeight, int nBitDepth) {
    if (nBitDepth == 8) {
        // nv12->iyuv
        YuvConverter<uint8_t> converter8(nWidth, nHeight);
        converter8.UVInterleavedToPlanar(pHostFrame);
    } else {
        // p016->yuv420p16
        YuvConverter<uint16_t> converter16(nWidth, nHeight);
        converter16.UVInterleavedToPlanar((uint16_t *)pHostFrame);
    }
}

static float GetChromaHeightFactor(cudaVideoSurfaceFormat eSurfaceFormat)
{
    float factor = 0.5;
    switch (eSurfaceFormat)
    {
    case cudaVideoSurfaceFormat_NV12:
    case cudaVideoSurfaceFormat_P016:
        factor = 0.5;
        break;
    case cudaVideoSurfaceFormat_YUV444:
    case cudaVideoSurfaceFormat_YUV444_16Bit:
        factor = 1.0;
        break;
    }

    return factor;
}

static int GetChromaPlaneCount(cudaVideoSurfaceFormat eSurfaceFormat)
{
    int numPlane = 1;
    switch (eSurfaceFormat)
    {
    case cudaVideoSurfaceFormat_NV12:
    case cudaVideoSurfaceFormat_P016:
        numPlane = 1;
        break;
    case cudaVideoSurfaceFormat_YUV444:
    case cudaVideoSurfaceFormat_YUV444_16Bit:
        numPlane = 2;
        break;
    }

    return numPlane;
}


/* Called when the parser encounters sequence header for AV1 SVC content
*  return value interpretation:
*      < 0 : fail, >=0: succeeded (bit 0-9: currOperatingPoint, bit 10-10: bDispAllLayer, bit 11-30: reserved, must be set 0)
*/
int NvDecoder::GetOperatingPoint(CUVIDOPERATINGPOINTINFO *pOPInfo)
{
    if (pOPInfo->codec == cudaVideoCodec_AV1)
    {
        if (pOPInfo->av1.operating_points_cnt > 1)
        {
            // clip has SVC enabled
            if (m_nOperatingPoint >= pOPInfo->av1.operating_points_cnt)
                m_nOperatingPoint = 0;

            printf("AV1 SVC clip: operating point count %d  ", pOPInfo->av1.operating_points_cnt);
            printf("Selected operating point: %d, IDC 0x%x bOutputAllLayers %d\n", m_nOperatingPoint, pOPInfo->av1.operating_points_idc[m_nOperatingPoint], m_bDispAllLayers);
            return (m_nOperatingPoint | (m_bDispAllLayers << 10));
        }
    }
    return -1;
}

/* Return value from HandleVideoSequence() are interpreted as   :
*  0: fail, 1: succeeded, > 1: override dpb size of parser (set by CUVIDPARSERPARAMS::ulMaxNumDecodeSurfaces while creating parser)
*/
int NvDecoder::HandleVideoSequence(CUVIDEOFORMAT *pVideoFormat)
{
    int nDecodeSurface = pVideoFormat->min_num_decode_surfaces;

    CUVIDDECODECAPS decodecaps;
    memset(&decodecaps, 0, sizeof(decodecaps));

    decodecaps.eCodecType = pVideoFormat->codec;
    decodecaps.eChromaFormat = pVideoFormat->chroma_format;
    decodecaps.nBitDepthMinus8 = pVideoFormat->bit_depth_luma_minus8;

    CUDA_DRVAPI_CALL(cuCtxPushCurrent(m_ctx->cuContext));
    NVDEC_API_CALL(cuvidGetDecoderCaps(&decodecaps));
    CUDA_DRVAPI_CALL(cuCtxPopCurrent(NULL));

    if(!decodecaps.bIsSupported){
        NVDEC_THROW_ERROR("Codec not supported on this GPU", CUDA_ERROR_NOT_SUPPORTED);
        return nDecodeSurface;
    }

    if ((pVideoFormat->coded_width > decodecaps.nMaxWidth) ||
        (pVideoFormat->coded_height > decodecaps.nMaxHeight)){

        std::ostringstream errorString;
        errorString << std::endl
                    << "Resolution          : " << pVideoFormat->coded_width << "x" << pVideoFormat->coded_height << std::endl
                    << "Max Supported (wxh) : " << decodecaps.nMaxWidth << "x" << decodecaps.nMaxHeight << std::endl
                    << "Resolution not supported on this GPU";

        const std::string cErr = errorString.str();
        NVDEC_THROW_ERROR(cErr, CUDA_ERROR_NOT_SUPPORTED);
        return nDecodeSurface;
    }

    if ((pVideoFormat->coded_width>>4)*(pVideoFormat->coded_height>>4) > decodecaps.nMaxMBCount){

        std::ostringstream errorString;
        errorString << std::endl
                    << "MBCount             : " << (pVideoFormat->coded_width >> 4)*(pVideoFormat->coded_height >> 4) << std::endl
                    << "Max Supported mbcnt : " << decodecaps.nMaxMBCount << std::endl
                    << "MBCount not supported on this GPU";

        const std::string cErr = errorString.str();
        NVDEC_THROW_ERROR(cErr, CUDA_ERROR_NOT_SUPPORTED);
        return nDecodeSurface;
    }

    m_ctx->eCodec = pVideoFormat->codec;
    cudaVideoChromaFormat eChromaFormat = pVideoFormat->chroma_format;
    m_nBitDepthMinus8 = pVideoFormat->bit_depth_luma_minus8;
    m_nBPP = m_nBitDepthMinus8 > 0 ? 2 : 1;

    // Set the output surface format same as chroma format
    if ((eChromaFormat == cudaVideoChromaFormat_420) || (eChromaFormat == cudaVideoChromaFormat_Monochrome)) {
        m_eOutputFormat = pVideoFormat->bit_depth_luma_minus8 ? cudaVideoSurfaceFormat_P016 : cudaVideoSurfaceFormat_NV12;
    } else if (eChromaFormat == cudaVideoChromaFormat_444) {
        m_eOutputFormat = pVideoFormat->bit_depth_luma_minus8 ? cudaVideoSurfaceFormat_YUV444_16Bit : cudaVideoSurfaceFormat_YUV444;
    } else if (eChromaFormat == cudaVideoChromaFormat_422) {
        m_eOutputFormat = cudaVideoSurfaceFormat_NV12;  // no 4:2:2 output format supported yet so make 420 default
    }

    // Check if output format supported. If not, check falback options
    if (!(decodecaps.nOutputFormatMask & (1 << m_eOutputFormat)))
    {
        if (decodecaps.nOutputFormatMask & (1 << cudaVideoSurfaceFormat_NV12))
            m_eOutputFormat = cudaVideoSurfaceFormat_NV12;
        else if (decodecaps.nOutputFormatMask & (1 << cudaVideoSurfaceFormat_P016))
            m_eOutputFormat = cudaVideoSurfaceFormat_P016;
        else if (decodecaps.nOutputFormatMask & (1 << cudaVideoSurfaceFormat_YUV444))
            m_eOutputFormat = cudaVideoSurfaceFormat_YUV444;
        else if (decodecaps.nOutputFormatMask & (1 << cudaVideoSurfaceFormat_YUV444_16Bit))
            m_eOutputFormat = cudaVideoSurfaceFormat_YUV444_16Bit;
        else 
            NVDEC_THROW_ERROR("No supported output format found", CUDA_ERROR_NOT_SUPPORTED);
    }

    CUVIDDECODECREATEINFO videoDecodeCreateInfo = { 0 };
    videoDecodeCreateInfo.CodecType = pVideoFormat->codec;
    videoDecodeCreateInfo.ChromaFormat = pVideoFormat->chroma_format;
    videoDecodeCreateInfo.OutputFormat = m_eOutputFormat;
    videoDecodeCreateInfo.bitDepthMinus8 = pVideoFormat->bit_depth_luma_minus8;
    if (pVideoFormat->progressive_sequence)
        videoDecodeCreateInfo.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;
    else
        videoDecodeCreateInfo.DeinterlaceMode = cudaVideoDeinterlaceMode_Adaptive;
    videoDecodeCreateInfo.ulNumOutputSurfaces = 2;
    // With PreferCUVID, JPEG is still decoded by CUDA while video is decoded by NVDEC hardware
    videoDecodeCreateInfo.ulCreationFlags = cudaVideoCreate_PreferCUVID;
    videoDecodeCreateInfo.ulNumDecodeSurfaces = nDecodeSurface;
    videoDecodeCreateInfo.vidLock = m_ctx->ctxLock;
    videoDecodeCreateInfo.ulWidth = pVideoFormat->coded_width;
    videoDecodeCreateInfo.ulHeight = pVideoFormat->coded_height;

    unsigned int maxHeight = 0;
    unsigned int maxWidth = 0;
    // AV1 has max width/height of sequence in sequence header
    if (pVideoFormat->codec == cudaVideoCodec_AV1 && pVideoFormat->seqhdr_data_length > 0)
    {
        CUVIDEOFORMATEX *vidFormatEx = (CUVIDEOFORMATEX *)pVideoFormat;
        maxWidth = vidFormatEx->av1.max_width;
        maxHeight = vidFormatEx->av1.max_height;
    }
    if (maxWidth < pVideoFormat->coded_width) {
        maxWidth = pVideoFormat->coded_width;
    }
    if (maxHeight < pVideoFormat->coded_height) {
        maxHeight = pVideoFormat->coded_height;
    }
    videoDecodeCreateInfo.ulMaxWidth = maxWidth;
    videoDecodeCreateInfo.ulMaxHeight = maxHeight;

    m_nWidth = pVideoFormat->display_area.right - pVideoFormat->display_area.left;
    m_nLumaHeight = pVideoFormat->display_area.bottom - pVideoFormat->display_area.top;
    videoDecodeCreateInfo.ulTargetWidth = pVideoFormat->coded_width;
    videoDecodeCreateInfo.ulTargetHeight = pVideoFormat->coded_height;

    m_nChromaHeight = (int)(ceil((float)m_nLumaHeight * GetChromaHeightFactor(m_eOutputFormat)));
    m_nNumChromaPlanes = GetChromaPlaneCount(m_eOutputFormat);
    m_nSurfaceHeight = (int) videoDecodeCreateInfo.ulTargetHeight;

    CUDA_DRVAPI_CALL(cuCtxPushCurrent(m_ctx->cuContext));
    NVDEC_API_CALL(cuvidCreateDecoder(&(m_ctx->hDecoder), &videoDecodeCreateInfo));
    CUDA_DRVAPI_CALL(cuCtxPopCurrent(NULL));
    return nDecodeSurface;
}


/* Return value from HandlePictureDecode() are interpreted as:
*  0: fail, >=1: succeeded
*/
int NvDecoder::HandlePictureDecode(CUVIDPICPARAMS *pPicParams) {
    if (!(m_ctx->hDecoder))
    {
        NVDEC_THROW_ERROR("Decoder not initialized.", CUDA_ERROR_NOT_INITIALIZED);
        return false;
    }
    CUDA_DRVAPI_CALL(cuCtxPushCurrent(m_ctx->cuContext));
    NVDEC_API_CALL(cuvidDecodePicture(m_ctx->hDecoder, pPicParams));
    if ((!pPicParams->field_pic_flag) || (pPicParams->second_field))
    {
        CUVIDPARSERDISPINFO dispInfo;
        memset(&dispInfo, 0, sizeof(dispInfo));
        dispInfo.picture_index = pPicParams->CurrPicIdx;
        dispInfo.progressive_frame = !pPicParams->field_pic_flag;
        dispInfo.top_field_first = pPicParams->bottom_field_flag ^ 1;
        HandlePictureDisplay(&dispInfo);
    }
    CUDA_DRVAPI_CALL(cuCtxPopCurrent(NULL));
    return 1;
}

/* Return value from HandlePictureDisplay() are interpreted as:
*  0: fail, >=1: succeeded
*/
int NvDecoder::HandlePictureDisplay(CUVIDPARSERDISPINFO *pDispInfo) {
    CUVIDPROCPARAMS videoProcessingParameters = {};
    videoProcessingParameters.progressive_frame = pDispInfo->progressive_frame;
    videoProcessingParameters.second_field = pDispInfo->repeat_first_field + 1;
    videoProcessingParameters.top_field_first = pDispInfo->top_field_first;
    videoProcessingParameters.unpaired_field = pDispInfo->repeat_first_field < 0;
    videoProcessingParameters.output_stream = m_ctx->cuvidStream;

    CUdeviceptr dpSrcFrame = 0;
    unsigned int nSrcPitch = 0;
    CUDA_DRVAPI_CALL(cuCtxPushCurrent(m_ctx->cuContext));
    NVDEC_API_CALL(cuvidMapVideoFrame(m_ctx->hDecoder, pDispInfo->picture_index, &dpSrcFrame,
        &nSrcPitch, &videoProcessingParameters));

    CUVIDGETDECODESTATUS DecodeStatus;
    memset(&DecodeStatus, 0, sizeof(DecodeStatus));
    CUresult result = cuvidGetDecodeStatus(m_ctx->hDecoder, pDispInfo->picture_index, &DecodeStatus);
    if (result == CUDA_SUCCESS && (DecodeStatus.decodeStatus == cuvidDecodeStatus_Error || DecodeStatus.decodeStatus == cuvidDecodeStatus_Error_Concealed))
    {
        printf("Decode Error occurred for picture.\n");
    }

    dstFrame = new uint8_t[GetFrameSize()];
    
    // Copy luma plane
    CUDA_MEMCPY2D m = { 0 };
    m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    m.srcDevice = dpSrcFrame;
    m.srcPitch = nSrcPitch;
    m.dstMemoryType = CU_MEMORYTYPE_HOST;
    m.dstDevice = (CUdeviceptr)(m.dstHost = dstFrame);
    m.dstPitch = GetWidth() * m_nBPP;
    m.WidthInBytes = GetWidth() * m_nBPP;
    m.Height = m_nLumaHeight;
    CUDA_DRVAPI_CALL(cuMemcpy2DAsync(&m, m_ctx->cuvidStream));

    // Copy chroma plane
    // NVDEC output has luma height aligned by 2. Adjust chroma offset by aligning height
    m.srcDevice = (CUdeviceptr)((uint8_t *)dpSrcFrame + m.srcPitch * ((m_nSurfaceHeight + 1) & ~1));
    m.dstDevice = (CUdeviceptr)(m.dstHost = dstFrame + m.dstPitch * m_nLumaHeight);
    m.Height = m_nChromaHeight;
    CUDA_DRVAPI_CALL(cuMemcpy2DAsync(&m, m_ctx->cuvidStream));

    if (m_nNumChromaPlanes == 2)
    {
        m.srcDevice = (CUdeviceptr)((uint8_t *)dpSrcFrame + m.srcPitch * ((m_nSurfaceHeight + 1) & ~1) * 2);
        m.dstDevice = (CUdeviceptr)(m.dstHost = dstFrame + m.dstPitch * m_nLumaHeight * 2);
        m.Height = m_nChromaHeight;
        CUDA_DRVAPI_CALL(cuMemcpy2DAsync(&m, m_ctx->cuvidStream));
    }
    CUDA_DRVAPI_CALL(cuStreamSynchronize(m_ctx->cuvidStream));
    CUDA_DRVAPI_CALL(cuCtxPopCurrent(NULL));

    NVDEC_API_CALL(cuvidUnmapVideoFrame(m_ctx->hDecoder, dpSrcFrame));
    return 1;
}

NvDecoder::NvDecoder(nvdec_context * ctx) : m_ctx(ctx)
{
}

heif_error NvDecoder::initVideoParser()
{
    CUVIDPARSERPARAMS videoParserParameters = {};
    videoParserParameters.CodecType = m_ctx->eCodec;
    videoParserParameters.ulMaxNumDecodeSurfaces = 1;
    videoParserParameters.ulClockRate = 1000;
    videoParserParameters.ulMaxDisplayDelay = 0;
    videoParserParameters.pUserData = this; // TODO: make this ctx once all the members are gone
    videoParserParameters.pfnSequenceCallback = HandleVideoSequenceProc;
    videoParserParameters.pfnDecodePicture = HandlePictureDecodeProc;
    videoParserParameters.pfnDisplayPicture = NULL;
    videoParserParameters.pfnGetOperatingPoint = HandleOperatingPointProc;
    videoParserParameters.pfnGetSEIMsg = NULL;
    CUresult errorCode = cuvidCreateVideoParser(&(m_ctx->hParser), &videoParserParameters);
    if (errorCode != CUDA_SUCCESS) {
        struct heif_error err = {heif_error_Decoder_plugin_error,
                                 heif_suberror_Plugin_loading_error,
                                 "could not create CUVID video parser"};
        return err;
    }
    return heif_error_ok;
}

NvDecoder::~NvDecoder() {

    if (m_ctx->hParser) {
        cuvidDestroyVideoParser(m_ctx->hParser);
    }
    cuCtxPushCurrent(m_ctx->cuContext);
    if (m_ctx->hDecoder) {
        cuvidDestroyDecoder(m_ctx->hDecoder);
    }

    delete dstFrame;

    cuCtxPopCurrent(NULL);

    cuvidCtxLockDestroy(m_ctx->ctxLock);
}

int NvDecoder::Decode(const uint8_t *pData, size_t nSize)
{
    CUVIDSOURCEDATAPACKET packet = { 0 };
    packet.payload = pData;
    packet.payload_size = nSize;
    packet.flags = CUVID_PKT_ENDOFSTREAM;
    packet.timestamp = 0;
    NVDEC_API_CALL(cuvidParseVideoData(m_ctx->hParser, &packet));

    return 1;
}

uint8_t* NvDecoder::GetFrame()
{
    // convert result to heif pixel image
    ConvertSemiplanarToPlanar(dstFrame, GetWidth(), GetHeight(), m_nBitDepthMinus8 + 8);

    return dstFrame;
}

